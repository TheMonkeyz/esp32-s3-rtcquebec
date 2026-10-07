// The display in the browser: LVGL and the firmware's own code, unchanged: main.c's app_main (run as a task, as on the
// display), the screens (ui.c with forge_lvgl's pager and slide.c), the departures (departures.c, rtc_api.c), the
// favourites (favs.c) and the settings page's routes (main.c, forge_ota's ota_web.c, served by emu_web.c). The hardware
// is replaced by the page (emu_display.c, emu_touch.c, emu_http.c, emu_nvs.c, emu_stubs.c): the "Wi-Fi" is up at
// once, so app_main goes from its start-up straight to the first stop. This loop is the display's LVGL task and its
// scheduler.
#include <stdio.h>
#include <string.h>
#include <emscripten.h>
#include "lvgl.h"
#include "nvs.h"
#include "favs.h"
#include "freertos/task.h"

void app_main(void);                               // main.c
void emu_web_poll(void);                           // emu_web.c: the settings page's requests

// Two stops for a new visitor (no NVS "favs" yet), so the display has something to show and a swipe somewhere to go:
// route 800 both ways, at St-Dominique toward Colline Parlementaire and toward Terminus Chute-Montmorency. Added
// once: favs.c writes "n" whenever the list is saved, so a visitor who removed them keeps none.
static const rtc_fav_t demo[] = {
    { .stop = "1025", .route = "800", .dir = "0" },
    { .stop = "1105", .route = "800", .dir = "1" },
};

static void demo_favs(void)
{
    nvs_handle_t h;
    uint8_t n;
    if (nvs_open("favs", NVS_READWRITE, &h) != ESP_OK) return;
    bool saved_once = nvs_get_u8(h, "n", &n) == ESP_OK;
    nvs_close(h);
    if (!saved_once) favs_save(demo, sizeof(demo) / sizeof(demo[0]));
}

// ?stop=1025&route=800&dir=0 in the page's address: that favourite comes first, so the display opens on it (added if
// new, in place of the last one when the list is full; kept, as if chosen on the settings page). The departures task
// checks it like any other: a route that doesn't stop there says so on its page.
EM_JS(int, js_param, (const char *key, char *out, int n), {
    const v = new URLSearchParams(location.search).get(UTF8ToString(key));
    if (!v) return 0;
    stringToUTF8(v, out, n);
    return 1;
});

static void fav_from_address(void)
{
    rtc_fav_t f = {0}, list[FAVS_MAX];
    if (!js_param("stop", f.stop, sizeof(f.stop)) || !js_param("route", f.route, sizeof(f.route)) ||
        !js_param("dir", f.dir, sizeof(f.dir)) || !rtc_fav_valid(&f)) return;
    int n = favs_load(list), at = -1;
    for (int i = 0; i < n && at < 0; i++) if (!memcmp(&list[i], &f, sizeof(f))) at = i;
    if (at == 0) return;
    if (at < 0) at = n < FAVS_MAX ? n++ : n - 1;
    memmove(&list[1], &list[0], at * sizeof(f));
    list[0] = f;
    favs_save(list, n);
}

static void app_task(void *arg)
{
    app_main();                                    // never returns: the work is in its tasks and timers
}

int main(void)
{
    demo_favs();
    fav_from_address();
    xTaskCreate(app_task, "main", 8192, NULL, 1, NULL);
    for (;;) {
        emu_tasks_run();                           // app_main (at first), "deps", "setup_radio": until their next wait
        emu_web_poll();                            // the settings page's requests, as the display's web server
        uint32_t wait = 15;
        if (lv_is_initialized() && lv_display_get_default()) wait = lv_timer_handler();   // (board_init done)
        emscripten_sleep(wait > 15 ? 15 : wait < 1 ? 1 : wait);
    }
}
