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
#include "esp_attr.h"
#include "http_once.h"
#include "svc.h"
#include "net.h"

static const char *TAG = "deps";
#define RX_CAP 4096                     // a reply is ~1.1 KB (5 departures)
#define NOTICES_CAP (128 * 1024)        // a route's notices: ~13 KB for one notice naming 15 routes; 6 at most
#define GAP_MS 2000                     // between two requests

static SemaphoreHandle_t mu;            // entries, n, shown, job
static dep_entry_t ent[FAVS_MAX];
static int64_t tried[FAVS_MAX];         // esp_timer µs of the last try, 0 = never
static int n_ent, shown = -1;
static TaskHandle_t task;
static void (*on_changed)(int i);
static int svc_rtc = -1;

// The notices of each distinct favourite route (under mu)
typedef struct {
    char route[8];
    int64_t tried;                      // esp_timer µs, 0 = never
    time_t fetched;                     // last good reply (UTC), 0 = never
    bool failing;
    int n;
    rtc_notice_t nt[RTC_NOTICES_MAX];
} route_alerts_t;
static EXT_RAM_BSS_ATTR route_alerts_t ra[FAVS_MAX];
static int n_ra;

// The buses of the favourite whose map is open (under mu)
static int track = -1;
static rtc_fav_t track_fav;
static int64_t bus_tried;
static time_t bus_fetched;
static bool bus_failing;
static int n_bus;
static rtc_bus_t bus[RTC_BUSES_MAX];

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
        .buffer_size_tx = 2048,         // the notices' query is ~1.3 KB (the default 512 can't send it)
    };
    int64_t t0 = esp_timer_get_time();
    int status;
    esp_err_t err = http_once(&cfg, &status);
    if (err == ESP_OK && status == 200 && rtc_reply_none(buf)) {
        svc_ok(svc_rtc, t0);                                  // "null": nothing by that number, as a 404
        status = 404;
    } else if (err == ESP_OK && status == 200) {
        if (parse(buf, out)) svc_ok(svc_rtc, t0);
        else { svc_fail_why(svc_rtc, SVC_WHY_BAD_REPLY, t0); status = -1; }
    } else if (err == ESP_OK && status == 404) {
        svc_ok(svc_rtc, t0);                                  // a real answer: "no such stop / route"
    } else {
        svc_http(svc_rtc, err, status, t0);
        status = 0;
    }
    const char *what = !strncmp(url, RTC_API, strlen(RTC_API)) ? url + strlen(RTC_API) : "notices";
    ESP_LOGI(TAG, "GET %s: %d, %d B in %d ms", what, status, rx.len, (int)((esp_timer_get_time() - t0) / 1000));
    return status;
}

static bool parse_board(const char *s, void *out) { return rtc_parse_board(s, out); }

typedef struct { rtc_bus_t *b; int n; } buses_t;
static bool parse_buses(const char *s, void *out)
{
    buses_t *x = out;
    x->n = rtc_parse_buses(s, x->b, RTC_BUSES_MAX);
    return x->n >= 0;
}

typedef struct { rtc_notice_t *nt; int n; } notices_t;
static bool parse_notices(const char *s, void *out)
{
    notices_t *x = out;
    x->n = rtc_parse_notices(s, x->nt, RTC_NOTICES_MAX);
    return x->n >= 0;
}

// Local time with its offset, as the website's query has it: "2026-10-07T00:52:00-04:00"
static bool now_iso(char *out, int n)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year < 120 || n < 26) return false;
    rtc_iso_local(now, &tm, out, n);
    return true;
}

// 1 fetched, -1 failed; nt / n filled on success
static int ask_notices(const char *route, rtc_notice_t *nt, int *n)
{
    char iso[32];
    char *url = malloc(2048), *buf = heap_caps_malloc(NOTICES_CAP, MALLOC_CAP_SPIRAM);
    int r = -1;
    if (url && buf && now_iso(iso, sizeof(iso)) && rtc_notices_url(url, 2048, route, iso)) {
        notices_t x = { .nt = nt };
        if (get(url, buf, NOTICES_CAP, parse_notices, &x) == 200) { *n = x.n; r = 1; }
    }
    free(url);
    free(buf);
    return r;
}
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

        // The map's buses first: someone is looking at them
        xSemaphoreTake(mu, portMAX_DELAY);
        bool buses = track >= 0 && (!bus_tried || esp_timer_get_time() - bus_tried >= DEPS_BUSES_S * 1000000LL);
        rtc_fav_t tf = track_fav;
        if (buses) bus_tried = esp_timer_get_time();
        xSemaphoreGive(mu);
        if (buses) {
            static rtc_bus_t got[RTC_BUSES_MAX];
            char url[160];
            buses_t x = { .b = got };
            int st = rtc_buses_url(url, sizeof(url), tf.route, tf.dir) ? get(url, buf, RX_CAP, parse_buses, &x) : -1;
            xSemaphoreTake(mu, portMAX_DELAY);
            bool same = track >= 0 && !memcmp(&track_fav, &tf, sizeof(tf));
            if (same) {
                bus_failing = st != 200;
                if (st == 200) { n_bus = x.n; memcpy(bus, got, x.n * sizeof(got[0])); bus_fetched = time(NULL); }
            }
            xSemaphoreGive(mu);
            if (same && on_changed) on_changed(DEPS_CHANGED_BUSES);
            vTaskDelay(pdMS_TO_TICKS(GAP_MS));
            continue;
        }

        xSemaphoreTake(mu, portMAX_DELAY);
        int i = due(esp_timer_get_time());
        rtc_fav_t f = i >= 0 ? ent[i].fav : (rtc_fav_t){0};
        if (i >= 0) tried[i] = esp_timer_get_time();
        int a = -1;                                           // else a route's notices, when due
        char route[8] = "";
        for (int k = 0; k < n_ra && i < 0 && a < 0; k++)
            if (!ra[k].tried || esp_timer_get_time() - ra[k].tried >= DEPS_ALERTS_S * 1000000LL) a = k;
        if (a >= 0) { ra[a].tried = esp_timer_get_time(); strlcpy(route, ra[a].route, sizeof(route)); }
        xSemaphoreGive(mu);
        if (a >= 0) {
            static EXT_RAM_BSS_ATTR rtc_notice_t got[RTC_NOTICES_MAX];
            int n = 0, r = ask_notices(route, got, &n);
            xSemaphoreTake(mu, portMAX_DELAY);
            bool same = a < n_ra && !strcmp(ra[a].route, route);
            if (same) {
                ra[a].failing = r < 0;
                if (r == 1) { ra[a].n = n; memcpy(ra[a].nt, got, n * sizeof(got[0])); ra[a].fetched = time(NULL); }
            }
            xSemaphoreGive(mu);
            if (same && r == 1) ESP_LOGI(TAG, "route %s: %d notice(s)", route, n);
            if (same && on_changed) on_changed(DEPS_CHANGED_ALERTS);
            vTaskDelay(pdMS_TO_TICKS(GAP_MS));
            continue;
        }
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
    if (track >= 0) {                                         // the map's favourite: where it is now, or gone
        int t = -1;
        for (int i = 0; i < n_ent && t < 0; i++) if (!memcmp(&ent[i].fav, &track_fav, sizeof(rtc_fav_t))) t = i;
        track = t;
    }
    // The distinct routes; a route kept keeps its notices
    static EXT_RAM_BSS_ATTR route_alerts_t old_ra[FAVS_MAX];
    int old_n_ra = n_ra;
    memcpy(old_ra, ra, sizeof(ra));
    n_ra = 0;
    for (int i = 0; i < n_ent; i++) {
        bool seen = false;
        for (int k = 0; k < n_ra && !seen; k++) seen = !strcmp(ra[k].route, ent[i].fav.route);
        if (seen) continue;
        route_alerts_t *r = &ra[n_ra++];
        int k = 0;
        while (k < old_n_ra && strcmp(old_ra[k].route, ent[i].fav.route)) k++;
        if (k < old_n_ra) *r = old_ra[k];
        else { memset(r, 0, sizeof(*r)); strlcpy(r->route, ent[i].fav.route, sizeof(r->route)); }
    }
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

// Does notice nt concern favourite i (its route in its direction)?
static bool concerns(const rtc_notice_t *nt, int i) { return rtc_notice_for(nt, ent[i].fav.route, ent[i].fav.dir); }

int deps_alerts(dep_alert_t *out, int max, time_t *fetched, bool *failing)
{
    if (!mu) return 0;
    xSemaphoreTake(mu, portMAX_DELAY);
    int n = 0;
    time_t oldest = n_ra ? time(NULL) : 0;
    bool fail = false;
    for (int k = 0; k < n_ra; k++) {
        if (!ra[k].fetched) oldest = 0;
        else if (oldest && ra[k].fetched < oldest) oldest = ra[k].fetched;
        fail |= ra[k].failing;
        for (int m = 0; m < ra[k].n; m++) {
            const rtc_notice_t *nt = &ra[k].nt[m];
            int dup = -1;
            for (int d = 0; d < n && dup < 0; d++) if (!strcmp(out[d].n.id, nt->id)) dup = d;
            char routes[40] = "";                             // the favourite routes it concerns
            for (int i = 0; i < n_ent; i++) {
                if (!concerns(nt, i) || strstr(routes, ent[i].fav.route)) continue;
                if (routes[0]) strlcat(routes, "  ", sizeof(routes));
                strlcat(routes, ent[i].fav.route, sizeof(routes));
            }
            if (!routes[0] || dup >= 0) continue;
            if (n == max) continue;
            out[n].n = *nt;
            strlcpy(out[n].routes, routes, sizeof(out[n].routes));
            n++;
        }
    }
    xSemaphoreGive(mu);
    // Urgent first, then newest
    for (int a = 1; a < n; a++)
        for (int b = a; b > 0; b--) {
            const rtc_notice_t *x = &out[b - 1].n, *y = &out[b].n;
            bool swap = (y->urgent && !x->urgent) || (y->urgent == x->urgent && y->start > x->start);
            if (!swap) break;
            dep_alert_t t = out[b];
            out[b] = out[b - 1];
            out[b - 1] = t;
        }
    if (fetched) *fetched = oldest;
    if (failing) *failing = fail;
    return n;
}

int deps_alerts_for(int i)
{
    if (!mu) return 0;
    xSemaphoreTake(mu, portMAX_DELAY);
    int n = 0;
    for (int k = 0; i >= 0 && i < n_ent && k < n_ra; k++)
        if (!strcmp(ra[k].route, ent[i].fav.route))
            for (int m = 0; m < ra[k].n; m++) n += concerns(&ra[k].nt[m], i);
    xSemaphoreGive(mu);
    return n;
}

void deps_track(int i)
{
    if (!mu) return;
    xSemaphoreTake(mu, portMAX_DELAY);
    bool same = i >= 0 && i < n_ent && track >= 0 && !memcmp(&track_fav, &ent[i].fav, sizeof(rtc_fav_t));
    if (!same) {                                              // another route: forget the other's buses
        n_bus = 0;
        bus_tried = 0;
        bus_fetched = 0;
        bus_failing = false;
    }
    track = i >= 0 && i < n_ent ? i : -1;
    if (track >= 0) track_fav = ent[i].fav;
    xSemaphoreGive(mu);
    if (task) xTaskNotifyGive(task);
}

int deps_buses(rtc_bus_t *out, int max, time_t *fetched, bool *failing)
{
    if (!mu) return 0;
    xSemaphoreTake(mu, portMAX_DELAY);
    int n = n_bus < max ? n_bus : max;
    memcpy(out, bus, n * sizeof(bus[0]));
    if (fetched) *fetched = bus_fetched;
    if (failing) *failing = bus_failing;
    xSemaphoreGive(mu);
    return n;
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
