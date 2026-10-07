// Next departures of the favourites (see departures.h). One task makes every request to RTC, the settings page's
// lookups included, so there is never more than one at a time and the polling rules hold in one place.
#include "departures.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "http_once.h"
#include "svc.h"
#include "net.h"

static const char *TAG = "deps";
#define RX_CAP 4096                     // a reply is ~1.1 KB (5 departures)
#define GAP_MS 2000                     // between two requests

static SemaphoreHandle_t mu;            // entries, n, shown, job
static dep_entry_t ent[FAVS_MAX];
static int64_t tried[FAVS_MAX];         // esp_timer µs of the last try, 0 = never
static int n_ent, shown = -1;
static TaskHandle_t task;
static void (*on_changed)(int i);
static int svc_rtc = -1;

// A lookup for the settings page, run by the task (one at a time: job_mu)
typedef struct {
    bool route;                         // a route's directions, else a favourite's board
    char route_no[8];
    rtc_fav_t fav;
    rtc_route_t *route_out;
    rtc_board_t *board_out;
    int result;
} job_t;
static SemaphoreHandle_t job_mu, job_done;
static job_t *job;

bool deps_date(char *out, int n)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year < 120) return false;                       // SNTP hasn't set the clock
    snprintf(out, n, "%04d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return true;
}

/* ---------- one request ---------- */

typedef struct { char *buf; int len, cap; } rx_t;

static esp_err_t http_evt(esp_http_client_event_t *e)
{
    rx_t *rx = e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA && rx->len + e->data_len < rx->cap) {
        memcpy(rx->buf + rx->len, e->data, e->data_len);
        rx->len += e->data_len;
        rx->buf[rx->len] = 0;
    }
    return ESP_OK;
}

// GET url into buf; returns the HTTP status (0 = no answer). Reports to the service health list.
static int get(const char *url, char *buf, int cap, bool (*parse)(const char *, void *), void *out)
{
    rx_t rx = { .buf = buf, .cap = cap };
    buf[0] = 0;
    esp_http_client_config_t cfg = {
        .url = url, .event_handler = http_evt, .user_data = &rx, .user_agent = svc_user_agent(),
        .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 10000,
    };
    int64_t t0 = esp_timer_get_time();
    int status;
    esp_err_t err = http_once(&cfg, &status);
    if (err == ESP_OK && status == 200) {
        if (parse(buf, out)) svc_ok(svc_rtc, t0);
        else { svc_fail_why(svc_rtc, SVC_WHY_BAD_REPLY, t0); status = -1; }
    } else if (err == ESP_OK && status == 404) {
        svc_ok(svc_rtc, t0);                                  // a real answer: "no such stop / route"
    } else {
        svc_http(svc_rtc, err, status, t0);
        status = 0;
    }
    ESP_LOGI(TAG, "GET %s: %d in %d ms", url + strlen(RTC_API), status, (int)((esp_timer_get_time() - t0) / 1000));
    return status;
}

static bool parse_board(const char *s, void *out) { return rtc_parse_board(s, out); }
static bool parse_route(const char *s, void *out) { return rtc_parse_route(s, out); }

// 1 found, 0 404, -1 failed
static int ask_board(const rtc_fav_t *f, rtc_board_t *out, char *buf)
{
    char date[12], url[200];
    if (!deps_date(date, sizeof(date)) || !rtc_board_url(url, sizeof(url), f, date)) return -1;
    int st = get(url, buf, RX_CAP, parse_board, out);
    return st == 200 ? 1 : st == 404 ? 0 : -1;
}

static int ask_route(const char *route, rtc_route_t *out, char *buf)
{
    char date[12], url[160];
    if (!deps_date(date, sizeof(date)) || !rtc_route_url(url, sizeof(url), route, date)) return -1;
    int st = get(url, buf, RX_CAP, parse_route, out);
    return st == 200 ? 1 : st == 404 ? 0 : -1;
}

/* ---------- the task ---------- */

// The favourite to fetch now, -1 if none is due. The one on view first.
static int due(int64_t now)
{
    if (shown >= 0 && shown < n_ent && (!tried[shown] || now - tried[shown] >= DEPS_SHOWN_S * 1000000LL)) return shown;
    for (int i = 0; i < n_ent; i++)
        if (!tried[i] || now - tried[i] >= DEPS_HIDDEN_S * 1000000LL) return i;
    return -1;
}

static void fetch_task(void *arg)
{
    char *buf = heap_caps_malloc(RX_CAP, MALLOC_CAP_SPIRAM);
    rtc_board_t *b = heap_caps_malloc(sizeof(rtc_board_t), MALLOC_CAP_SPIRAM);
    while (1) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        xSemaphoreTake(mu, portMAX_DELAY);
        job_t *jb = job;                                      // taken: run_job now waits for it whatever happens
        job = NULL;
        xSemaphoreGive(mu);
        if (jb) {                                             // the settings page waits for it
            jb->result = jb->route ? ask_route(jb->route_no, jb->route_out, buf)
                                   : ask_board(&jb->fav, jb->board_out, buf);
            xSemaphoreGive(job_done);
            vTaskDelay(pdMS_TO_TICKS(GAP_MS));
            continue;
        }
        char date[12];
        if (!net_is_connected() || !deps_date(date, sizeof(date))) continue;

        xSemaphoreTake(mu, portMAX_DELAY);
        int i = due(esp_timer_get_time());
        rtc_fav_t f = i >= 0 ? ent[i].fav : (rtc_fav_t){0};
        if (i >= 0) tried[i] = esp_timer_get_time();
        xSemaphoreGive(mu);
        if (i < 0) continue;

        int r = ask_board(&f, b, buf);
        xSemaphoreTake(mu, portMAX_DELAY);
        bool same = i < n_ent && !memcmp(&ent[i].fav, &f, sizeof(f));   // the list didn't change meanwhile
        if (same) {
            dep_entry_t *e = &ent[i];
            e->failing = r < 0;
            if (r == 1) { e->state = DEP_OK; e->board = *b; e->fetched = time(NULL); }
            else if (r == 0) e->state = DEP_NOT_FOUND;
        }
        xSemaphoreGive(mu);
        if (same && on_changed) on_changed(i);
        vTaskDelay(pdMS_TO_TICKS(GAP_MS));
    }
}

static void probe_url(char *url, size_t n)                    // the service health check (status page)
{
    char date[12];
    if (!deps_date(date, sizeof(date)) || !rtc_route_url(url, n, "800", date)) url[0] = 0;
}

void deps_start(void (*changed)(int i))
{
    on_changed = changed;
    mu = xSemaphoreCreateMutex();
    job_mu = xSemaphoreCreateMutex();
    job_done = xSemaphoreCreateBinary();
    svc_rtc = svc_add("RTC", "BorneVirtuelle", probe_url);
    // TLS needs internal RAM for its stack: 6 KB measured enough for esp_http_client + mbedTLS in forge_ota
    xTaskCreatePinnedToCore(fetch_task, "deps", 6144, NULL, 4, &task, 0);
}

// Before deps_start() there is no lock and nothing to give: a call then is ignored (it once took a NULL mutex at
// start-up and asserted, a restart loop)
void deps_set_favs(const rtc_fav_t *favs, int n)
{
    if (!mu) return;
    xSemaphoreTake(mu, portMAX_DELAY);
    dep_entry_t old[FAVS_MAX];
    int64_t old_tried[FAVS_MAX];
    int old_n = n_ent;
    memcpy(old, ent, sizeof(old));
    memcpy(old_tried, tried, sizeof(old_tried));
    n_ent = n < FAVS_MAX ? n : FAVS_MAX;
    for (int i = 0; i < n_ent; i++) {                         // a favourite kept keeps its data
        int k = -1;
        for (int j = 0; j < old_n && k < 0; j++) if (!memcmp(&old[j].fav, &favs[i], sizeof(rtc_fav_t))) k = j;
        if (k >= 0) { ent[i] = old[k]; tried[i] = old_tried[k]; }
        else { ent[i] = (dep_entry_t){ .fav = favs[i] }; tried[i] = 0; }
    }
    if (shown >= n_ent) shown = -1;
    xSemaphoreGive(mu);
    if (task) xTaskNotifyGive(task);
}

void deps_show(int i)
{
    if (!mu) return;
    xSemaphoreTake(mu, portMAX_DELAY);
    shown = i;
    xSemaphoreGive(mu);
    if (task) xTaskNotifyGive(task);
}

bool deps_get(int i, dep_entry_t *out)
{
    if (!mu) return false;
    xSemaphoreTake(mu, portMAX_DELAY);
    bool ok = i >= 0 && i < n_ent;
    if (ok) *out = ent[i];
    xSemaphoreGive(mu);
    return ok;
}

static int run_job(job_t *j)
{
    if (!task || !mu || !net_is_connected()) return -1;
    if (xSemaphoreTake(job_mu, pdMS_TO_TICKS(20000)) != pdTRUE) return -1;
    xSemaphoreTake(job_done, 0);
    xSemaphoreTake(mu, portMAX_DELAY);
    job = j;
    xSemaphoreGive(mu);
    xTaskNotifyGive(task);
    // The task may be in the middle of a favourite's request (up to ~10 s): give it 20 s to take this one. Once taken,
    // wait for the end whatever it takes: j is on this stack.
    bool done = xSemaphoreTake(job_done, pdMS_TO_TICKS(20000)) == pdTRUE;
    if (!done) {
        xSemaphoreTake(mu, portMAX_DELAY);
        bool taken = job != j;
        if (!taken) job = NULL;
        xSemaphoreGive(mu);
        done = taken && xSemaphoreTake(job_done, portMAX_DELAY) == pdTRUE;
    }
    xSemaphoreGive(job_mu);
    return done ? j->result : -1;
}

int deps_lookup_route(const char *route, rtc_route_t *out)
{
    job_t j = { .route = true, .route_out = out };
    strlcpy(j.route_no, route, sizeof(j.route_no));
    return run_job(&j);
}

int deps_check_fav(const rtc_fav_t *f, rtc_board_t *out)
{
    job_t j = { .fav = *f, .board_out = out };
    return run_job(&j);
}
