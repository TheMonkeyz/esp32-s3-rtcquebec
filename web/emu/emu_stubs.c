// What main.c and the screens ask of the rest of the firmware, answered for the browser: online (the browser's
// network) with a saved "network", so main.c goes straight to the stops as on a display that joined its Wi-Fi; no
// setup network, no Easy Connect, no updates (Restart reloads the page); no diagnostics or test console (the log is
// the browser's console).
#include <stdio.h>
#include <string.h>
#include <emscripten.h>
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_system.h"
#include "net.h"
#include "ota.h"
#include "web.h"
#include "diag.h"
#include "testcon.h"

const char *esp_err_to_name(esp_err_t e)
{
    switch (e) {
    case ESP_OK: return "ESP_OK";
    case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
    case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
    case ESP_ERR_HTTP_CONNECT: return "ESP_ERR_HTTP_CONNECT";
    default: return "ESP_FAIL";
    }
}

int64_t esp_timer_get_time(void) { return (int64_t)(emscripten_get_now() * 1000.0); }

/* ---------- network: the browser's ---------- */
void net_init(void) {}
bool net_load_creds(char *ssid, size_t sl, char *pass, size_t pl)
{
    snprintf(ssid, sl, "browser");
    if (pl) pass[0] = 0;
    return true;
}
void net_clear_creds(void) {}
void net_begin(const char *ssid, const char *pass) { (void)ssid; (void)pass; }
bool net_wait(int timeout_ms) { (void)timeout_ms; return true; }
bool net_wait_connected(int timeout_ms) { (void)timeout_ms; return true; }
bool net_test_short_setup(void) { return false; }
void net_start_portal(void) {}
const char *net_setup_ap_pass(void) { return ""; }
bool net_is_connected(void) { return EM_ASM_INT({ return navigator.onLine ? 1 : 0; }); }
bool net_get_ip(char *out, size_t n) { snprintf(out, n, "browser"); return true; }
bool net_get_ssid(char *out, size_t n) { snprintf(out, n, "browser"); return true; }
bool net_in_portal(void) { return false; }
int net_ap_clients(void) { return 0; }
void net_setup_ap_start(void) {}
void net_setup_ap_stop(void) {}
void net_setup_ap_stop_any(void) {}
bool net_setup_ap_active(void) { return false; }
bool net_dpp_start(net_dpp_uri_cb_t on_uri, net_dpp_done_cb_t on_done) { (void)on_uri; (void)on_done; return false; }
void net_dpp_stop(void) {}
bool net_dpp_active(void) { return false; }

/* ---------- updates: none in the browser (the settings page's channel picker is remembered) ---------- */
void ota_web_routes(void);                         // forge_ota's /api/update (ota_web.c)
static char channel[8] = "stable";
void ota_get_status(ota_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->state = OTA_UP_TO_DATE;
    snprintf(out->current, sizeof(out->current), "%s", EMU_VERSION);
    snprintf(out->channel, sizeof(out->channel), "%s", channel);
}
void ota_start(ota_listener_t listener)            // as forge_ota's: its routes, and the listener hears the status
{
    ota_web_routes();
    ota_status_t st;
    ota_get_status(&st);
    if (listener) listener(&st);
}
void ota_check_now(void) {}
bool ota_install(void) { return false; }
void ota_set_channel(const char *ch) { if (ch && (!strcmp(ch, "stable") || !strcmp(ch, "beta"))) snprintf(channel, sizeof(channel), "%s", ch); }
bool ota_pending_verify(void) { return false; }
void ota_get_notes(char *out, size_t size) { if (size) out[0] = 0; }
void ota_restart_when_safe(void) { esp_restart(); }

/* ---------- the web server: emu_web.c serves the settings page's requests ---------- */
void web_start(void) {}
const char *web_key(void) { return "0000000000000000"; }   // (index.html passes it to the settings page)
bool web_url(char *out, int n) { (void)out; (void)n; return false; }   // no settings QR code: the page is below

/* ---------- diagnostics and the test console: the board's serial port, not here ---------- */
void diag_start(int period_s) { (void)period_s; }
void diag_mark(const char *stage) { (void)stage; }
uint32_t diag_failed_allocs(void) { return 0; }
void diag_add_hook(diag_hook_t hook) { (void)hook; }
void testcon_add_where(testcon_where_fn_t fn) { (void)fn; }
void testcon_start(void) {}

// forge_core's NVS check (favs.c's and i18n_nvs.c's saves): emu_nvs.c never fails
bool nvs_check(esp_err_t err, const char *what) { (void)what; return err == ESP_OK; }
