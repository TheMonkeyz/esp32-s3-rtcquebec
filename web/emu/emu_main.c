// The display in the browser: main.c's app_main() runs unchanged as a task (espforge's web/emu/forge/emu_loop.c), the
// "Wi-Fi" is up at once, so it goes from its start-up straight to the first stop. This file is what only the browser
// needs: two demo stops for a new visitor, and a stop from the page's address.
#include <string.h>
#include "nvs.h"
#include "favs.h"
#include "emu.h"

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
static void fav_from_address(void)
{
    rtc_fav_t f = {0}, list[FAVS_MAX];
    if (!emu_param("stop", f.stop, sizeof(f.stop)) || !emu_param("route", f.route, sizeof(f.route)) ||
        !emu_param("dir", f.dir, sizeof(f.dir)) || !rtc_fav_valid(&f)) return;
    int n = favs_load(list), at = -1;
    for (int i = 0; i < n && at < 0; i++) if (!memcmp(&list[i], &f, sizeof(f))) at = i;
    if (at == 0) return;
    if (at < 0) at = n < FAVS_MAX ? n++ : n - 1;
    memmove(&list[1], &list[0], at * sizeof(f));
    list[0] = f;
    favs_save(list, n);
}

int main(void)
{
    demo_favs();
    fav_from_address();
    emu_start_app_main();
    emu_loop();
}
