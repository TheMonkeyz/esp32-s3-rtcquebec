// RTC Québec display: espforge's start-up order and Wi-Fi setup paths, the favourite stops (settings page routes)
// and their departures (departures.c, shown by ui.c).
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "board.h"
#include "diag.h"
#include "testcon.h"
#include "net.h"
#include "web.h"
#include "ota.h"
#include "app_text.h"
#include "ui.h"
#include "favs.h"
#include "departures.h"
#include "presence.h"

static const char *TAG = "app";
#define BOOT_BTN GPIO_NUM_0

extern const uint8_t page_start[] asm("_binary_index_html_start");
extern const uint8_t page_end[]   asm("_binary_index_html_end");

// Holding BOOT while pressing RESET enters download mode, so BOOT is read about a second after start-up:
// held for ~1 s then = forget the saved Wi-Fi
static bool boot_button_held(void)
{
    gpio_config_t c = { .pin_bit_mask = 1ULL << BOOT_BTN, .mode = GPIO_MODE_INPUT, .pull_up_en = 1 };
    gpio_config(&c);
    for (int i = 0; i < 20; i++) {
        if (gpio_get_level(BOOT_BTN)) return false;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return true;
}

// cJSON's trees in PSRAM: thousands of small nodes went to internal RAM, the scarce one
static void *json_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

// POST /api/settings {"lang":"en"|"fr"}: the app's own settings (docs/PROTOCOL.md §4)
static esp_err_t settings_post(httpd_req_t *req)
{
    cJSON *j = web_read_json(req);
    const char *lang = cJSON_GetStringValue(cJSON_GetObjectItem(j, "lang"));
    bool ok = lang && !strcmp(i18n_code(i18n_from_code(lang)), lang);   // one of the app's languages
    if (ok && i18n_from_code(lang) != i18n_lang()) {
        i18n_set(i18n_from_code(lang));
        ok = i18n_save();
        ui_texts_changed();
        ESP_LOGI(TAG, "language %s", lang);
    }
    cJSON_Delete(j);
    if (!ok) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad settings");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}


/* ---------- favourite stops (settings page) ---------- */

static cJSON *fav_json(const rtc_fav_t *f)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "stop", f->stop);
    cJSON_AddStringToObject(o, "route", f->route);
    cJSON_AddStringToObject(o, "dir", f->dir);
    return o;
}

// GET /api/favs: {"max":8,"favs":[{"stop","route","dir","stop_name","direction"}]} (names once fetched)
static esp_err_t favs_get(httpd_req_t *req)
{
    cJSON *j = cJSON_CreateObject(), *a = cJSON_AddArrayToObject(j, "favs");
    cJSON_AddNumberToObject(j, "max", FAVS_MAX);
    dep_entry_t e;
    for (int i = 0; deps_get(i, &e); i++) {
        cJSON *o = fav_json(&e.fav);
        if (e.state == DEP_OK) {
            cJSON_AddStringToObject(o, "stop_name", e.board.stop_name);
            cJSON_AddStringToObject(o, "direction", e.board.direction);
        }
        cJSON_AddItemToArray(a, o);
    }
    return web_send_json(req, j);
}

// POST /api/route {"route":"800"}: its two directions, asked from RTC.
// {"ok":true,"route","name","dirs":[{"code","name"},...]}, or {"ok":false,"why":"no_route"|"rtc"} (always 200: a
// refusal is an answer, like POST /api/favs's).
static esp_err_t route_post(httpd_req_t *req)
{
    cJSON *in = web_read_json(req);
    char route[8] = "";
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItem(in, "route"));
    if (s) strlcpy(route, s, sizeof(route));
    cJSON_Delete(in);
    rtc_route_t *r = heap_caps_calloc(1, sizeof(*r), MALLOC_CAP_SPIRAM);
    if (!r) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "memory");
    int got = route[0] ? deps_lookup_route(route, r) : 0;
    esp_err_t ret;
    if (got == 1) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddBoolToObject(j, "ok", true);
        cJSON *a = cJSON_AddArrayToObject(j, "dirs");
        cJSON_AddStringToObject(j, "route", r->route);
        cJSON_AddStringToObject(j, "name", r->name);
        for (int k = 0; k < 2; k++) {
            cJSON *d = cJSON_CreateObject();
            cJSON_AddStringToObject(d, "code", r->dir_code[k]);
            cJSON_AddStringToObject(d, "name", r->dir_name[k]);
            cJSON_AddItemToArray(a, d);
        }
        ret = web_send_json(req, j);
    } else {
        ret = httpd_resp_sendstr(req, got == 0 ? "{\"ok\":false,\"why\":\"no_route\"}" : "{\"ok\":false,\"why\":\"rtc\"}");
    }
    free(r);
    return ret;
}

// POST /api/favs {"favs":[{"stop","route","dir"},...]}: the whole list, in page order. A favourite not in the current
// list is checked with RTC first. {"ok":true} or {"ok":false,"bad":<index>,"why":"invalid"|"not_served"|"rtc"}.
static esp_err_t favs_post(httpd_req_t *req)
{
    cJSON *in = web_read_json(req);
    const cJSON *arr = cJSON_GetObjectItem(in, "favs");
    rtc_fav_t f[FAVS_MAX], cur[FAVS_MAX];
    int n = 0, ncur = 0, bad = -1;
    const char *why = "invalid";
    dep_entry_t e;
    while (ncur < FAVS_MAX && deps_get(ncur, &e)) cur[ncur++] = e.fav;
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) > FAVS_MAX) bad = 0;
    const cJSON *o;
    cJSON_ArrayForEach(o, arr) {
        if (bad >= 0 || n == FAVS_MAX) break;
        rtc_fav_t x = {0};
        const char *s;
        if ((s = cJSON_GetStringValue(cJSON_GetObjectItem(o, "stop")))) strlcpy(x.stop, s, sizeof(x.stop));
        if ((s = cJSON_GetStringValue(cJSON_GetObjectItem(o, "route")))) strlcpy(x.route, s, sizeof(x.route));
        if ((s = cJSON_GetStringValue(cJSON_GetObjectItem(o, "dir")))) strlcpy(x.dir, s, sizeof(x.dir));
        if (!rtc_fav_valid(&x)) { bad = n; break; }
        f[n++] = x;
    }
    cJSON_Delete(in);
    for (int i = 0; i < n && bad < 0; i++) {
        bool known = false;
        for (int k = 0; k < ncur && !known; k++) known = !memcmp(&cur[k], &f[i], sizeof(rtc_fav_t));
        if (known) continue;
        rtc_board_t *b = heap_caps_malloc(sizeof(*b), MALLOC_CAP_SPIRAM);
        int r = b ? deps_check_fav(&f[i], b) : -1;
        free(b);
        if (r != 1) { bad = i; why = r == 0 ? "not_served" : "rtc"; }
    }
    if (bad < 0 && !favs_save(f, n)) { bad = 0; why = "save"; }
    if (bad >= 0) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddBoolToObject(j, "ok", false);
        cJSON_AddNumberToObject(j, "bad", bad);
        cJSON_AddStringToObject(j, "why", why);
        ESP_LOGW(TAG, "favourites refused: #%d %s", bad, why);
        return web_send_json(req, j);
    }
    deps_set_favs(f, n);
    ui_favs_changed();
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* ---------- screen dimming (presence.c) ---------- */

static float num_or(const cJSON *j, const char *key, float def)
{
    const cJSON *v = cJSON_GetObjectItem(j, key);
    return cJSON_IsNumber(v) ? (float)v->valuedouble : def;
}

// weather_amoled's shape (plus "ok"): the settings, then the live state (the page polls it while open)
static esp_err_t presence_send(httpd_req_t *req, bool ok)
{
    presence_cfg_t c;
    presence_status_t st;
    presence_get_config(&c);
    presence_get_status(&st);
    static const char *names[] = {"active", "dim", "off"};
    cJSON *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "ok", ok);
    cJSON_AddBoolToObject(j, "enabled", c.enabled);
    cJSON_AddNumberToObject(j, "margin_db", c.margin_db);
    cJSON_AddNumberToObject(j, "wake_s", c.wake_s);
    cJSON_AddNumberToObject(j, "dim_s", c.dim_s);
    cJSON_AddNumberToObject(j, "off_s", c.off_s);
    cJSON_AddNumberToObject(j, "bright_pct", c.bright_pct);
    cJSON_AddNumberToObject(j, "dim_pct", c.dim_pct);
    cJSON_AddNumberToObject(j, "baseline_db", c.baseline_db);
    cJSON_AddNumberToObject(j, "level_db", st.level_db);
    cJSON_AddNumberToObject(j, "threshold_db", st.threshold_db);
    cJSON_AddStringToObject(j, "state", names[st.state]);
    cJSON_AddNumberToObject(j, "wake_progress", st.wake_progress);
    cJSON_AddNumberToObject(j, "quiet_s", st.quiet_s);
    cJSON_AddBoolToObject(j, "calibrating", st.calibrating);
    cJSON_AddNumberToObject(j, "calib_left_s", st.calib_left_s);
    cJSON_AddBoolToObject(j, "mic_ok", st.mic_ok);
    cJSON_AddNumberToObject(j, "brightness", st.brightness);
    cJSON_AddBoolToObject(j, "imu_ok", st.imu_ok);
    cJSON_AddNumberToObject(j, "motion_g", st.motion_g);
    cJSON_AddBoolToObject(j, "motion_wake", presence_motion_wake());
    cJSON_AddNumberToObject(j, "motion_thr", st.motion_thr);
    return web_send_json(req, j);
}

// GET /api/presence: the screen dimming settings and its state
static esp_err_t presence_get(httpd_req_t *req) { return presence_send(req, true); }

// POST /api/presence with any of "enabled", "margin_db", "wake_s", "dim_s", "off_s", "bright_pct", "dim_pct",
// "motion_wake", "motion_thr": saved, then GET's answer ("ok":false: not saved)
static esp_err_t presence_post(httpd_req_t *req)
{
    cJSON *j = web_read_json(req);
    if (!j) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
    presence_cfg_t c;
    presence_status_t ps;
    presence_get_config(&c);
    presence_get_status(&ps);
    const cJSON *en = cJSON_GetObjectItem(j, "enabled");
    if (cJSON_IsBool(en)) c.enabled = cJSON_IsTrue(en);
    c.margin_db = num_or(j, "margin_db", c.margin_db);
    c.wake_s = num_or(j, "wake_s", c.wake_s);
    c.dim_s = num_or(j, "dim_s", c.dim_s);
    c.off_s = num_or(j, "off_s", c.off_s);
    c.bright_pct = (int)num_or(j, "bright_pct", c.bright_pct);
    c.dim_pct = (int)num_or(j, "dim_pct", c.dim_pct);
    const cJSON *mw = cJSON_GetObjectItem(j, "motion_wake");
    bool saved = true;
    if (cJSON_IsBool(mw) || cJSON_IsNumber(cJSON_GetObjectItem(j, "motion_thr")))
        saved = presence_set_motion(cJSON_IsBool(mw) ? cJSON_IsTrue(mw) : presence_motion_wake(),
                                    num_or(j, "motion_thr", ps.motion_thr));
    cJSON_Delete(j);
    saved = presence_set_config(&c) && saved;
    return presence_send(req, saved);
}

// POST /api/calibrate {"seconds":5}: measure the room's background noise (keep quiet meanwhile). GET's answer with
// "calibrating":true, or {"ok":false,"why":"no_mic"|"busy"} (200: a refusal is an answer)
static esp_err_t calibrate_post(httpd_req_t *req)
{
    cJSON *j = web_read_json(req);
    int secs = j ? (int)num_or(j, "seconds", 5) : 5;
    cJSON_Delete(j);
    presence_status_t st;
    presence_get_status(&st);
    if (!st.mic_ok) return httpd_resp_sendstr(req, "{\"ok\":false,\"why\":\"no_mic\"}");
    if (!presence_calibrate(secs)) return httpd_resp_sendstr(req, "{\"ok\":false,\"why\":\"busy\"}");
    return presence_send(req, true);
}

// The stops may be changed from the setup network too: its password on the display is the same proof as the key in
// the settings QR code (a phone that joined it through Wi-Fi setup got 403 on "Find directions", 2026-10-06)
static const web_route_t app_routes[] = {
    { "/api/settings", HTTP_POST, settings_post, .keyed = true },
    { "/api/favs",     HTTP_GET,  favs_get, .keyed = false },
    { "/api/favs",     HTTP_POST, favs_post, .keyed = true },
    { "/api/route",    HTTP_POST, route_post, .keyed = true },
    { "/api/presence", HTTP_GET,  presence_get, .keyed = false },
    { "/api/presence", HTTP_POST, presence_post, .keyed = true },
    { "/api/calibrate", HTTP_POST, calibrate_post, .keyed = true },
};

// The saved network can't be reached at start-up. Setup opens by itself for AUTO_SETUP_S, then the device just
// keeps trying the saved network; a long-press still opens setup (a setup network left open for hours is a way in
// for anyone nearby). A rule with a time window needs a test that crosses it: harness "wifi offline-boot-short".
#define AUTO_SETUP_S (15 * 60)
static void offline_setup(const char *ssid)
{
    char note[160], body[160], still[160];
    snprintf(note, sizeof(note), tr(T_CANT_REACH), ssid);
    snprintf(body, sizeof(body), tr(T_CONNECTING), ssid);
    snprintf(still, sizeof(still), tr(T_STILL_TRYING), ssid);
    int64_t until = esp_timer_get_time() + (net_test_short_setup() ? 60 : AUTO_SETUP_S) * 1000000LL;
    bool gave_up = false;
    while (1) {
        bool by_itself = esp_timer_get_time() < until;
        bool opened = by_itself && !ui_wifi_setup_open();
        if (opened) ui_wifi_setup(note);
        while (ui_wifi_setup_open() && !net_is_connected()) {
            // The window ends while it is open: it closes, unless a phone is on it (one the user opened stays)
            if (opened && esp_timer_get_time() >= until && ui_wifi_setup_close()) break;
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        if (net_is_connected()) break;
        if (!gave_up && esp_timer_get_time() >= until) {
            gave_up = true;
            ESP_LOGW(TAG, "Setup network: no longer opened by itself (long-press opens it); still trying \"%s\"", ssid);
        }
        ui_message("Wi-Fi", gave_up ? still : body);
        if (net_wait_connected(30000)) break;
        if (!gave_up) ESP_LOGW(TAG, "Still can't reach \"%s\", offering the setup network again", ssid);
    }
    ESP_LOGI(TAG, "Saved network is back");
    ui_wifi_setup_end();
    ui_message("Wi-Fi", tr(T_CONNECTED));
    for (int i = 0; i < 120 && net_ap_clients() > 0; i++) vTaskDelay(pdMS_TO_TICKS(1000));   // let a phone finish
    net_setup_ap_stop();
}

// No saved network: the setup network and its captive portal until a phone sends one (then it restarts)
static void first_setup(void)
{
    net_start_portal();
    web_start();
    ui_wifi_setup(tr(T_FIRST_SETUP));
    diag_mark("app ready");
    while (1) vTaskDelay(portMAX_DELAY);
}

void app_main(void)
{
    ESP_LOGI(TAG, "starting");
    setenv("TZ", CONFIG_APP_TZ, 1);   // the clock's time zone (menuconfig "Starter app"); SNTP sets UTC
    tzset();
    cJSON_InitHooks(&(cJSON_Hooks){ .malloc_fn = json_alloc, .free_fn = free });
    diag_mark("start");
    net_init();                 // NVS first: settings, the language, the saved network
    app_text_init();
    diag_start(60);             // "diag:" lines every 60 s (heap, tasks, display)
    board_init();               // display, LVGL, touch; registers fps/tap/swipe/screen with the test console
    diag_mark("board");
    presence_start();           // microphones and motion sensor (the board's I2C bus): dims the screen when quiet
    touch_set_press_filter(presence_touch);   // a touch on a dark screen only wakes it (no tap, swipe, long-press)
    web_set_page(page_start, page_end);
    web_add_routes(app_routes, sizeof(app_routes) / sizeof(app_routes[0]));
    ota_start(ui_ota);          // logs "ota: Running ..."; checks once Wi-Fi is up; confirms a new image after 60 s
    ui_init();
    deps_start(ui_deps_changed);   // first: the other deps_ calls use its lock. Fetches once Wi-Fi and the clock are up
    rtc_fav_t favs[FAVS_MAX];
    int nfav = favs_load(favs);
    ESP_LOGI(TAG, "%d favourite stop(s)", nfav);
    deps_set_favs(favs, nfav);
    ui_favs_changed();
    testcon_start();            // ready before Wi-Fi, so start-up itself can be tested
    ui_message("RTC Québec", tr(T_STARTING));

    if (boot_button_held()) net_clear_creds();
    char ssid[NET_SSID_MAX + 1] = {0}, pass[NET_PASS_MAX + 1] = {0};
    if (!net_load_creds(ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGI(TAG, "No saved Wi-Fi, starting setup portal");
        first_setup();
    }

    char body[160];
    snprintf(body, sizeof(body), tr(T_CONNECTING), ssid);
    ui_message("Wi-Fi", body);
    net_begin(ssid, pass);
    web_start();                // up early, so a long-press can offer the setup page right away
    if (!net_wait(30000)) {
        ESP_LOGW(TAG, "Wi-Fi connect failed, offering the setup network");
        offline_setup(ssid);
    }
    diag_mark("wifi up");
    ui_home();
    diag_mark("app ready");     // forge.json ready_line: the harness starts its tests here

    // The work is in departures.c's task and the UI's timers
    while (1) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}
