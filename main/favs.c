// Favourites in NVS (see favs.h)
#include "favs.h"
#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "nvs_util.h"
#include "esp_log.h"

static const char *TAG = "favs";
#define NS "favs"

int favs_load(rtc_fav_t *out)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return 0;   // never saved
    uint8_t n = 0;
    nvs_get_u8(h, "n", &n);
    int got = 0;
    for (int i = 0; i < n && i < FAVS_MAX; i++) {
        char key[4], v[32];
        size_t len = sizeof(v);
        snprintf(key, sizeof(key), "f%d", i);
        if (nvs_get_str(h, key, v, &len) != ESP_OK) continue;
        rtc_fav_t f = {0};
        if (sscanf(v, "%7[^/]/%7[^/]/%3s", f.stop, f.route, f.dir) == 3 && rtc_fav_valid(&f)) out[got++] = f;
        else ESP_LOGW(TAG, "%s: \"%s\" skipped", key, v);
    }
    nvs_close(h);
    return got;
}

bool favs_save(const rtc_fav_t *favs, int n)
{
    if (n < 0 || n > FAVS_MAX) return false;
    nvs_handle_t h;
    bool ok = nvs_check(nvs_open(NS, NVS_READWRITE, &h), "open " NS);
    if (!ok) return false;
    for (int i = 0; i < FAVS_MAX && ok; i++) {
        char key[4], v[32];
        snprintf(key, sizeof(key), "f%d", i);
        if (i < n) {
            snprintf(v, sizeof(v), "%s/%s/%s", favs[i].stop, favs[i].route, favs[i].dir);
            ok = nvs_check(nvs_set_str(h, key, v), "favs/f");
        } else {
            esp_err_t e = nvs_erase_key(h, key);
            ok = e == ESP_ERR_NVS_NOT_FOUND || nvs_check(e, "favs/f erase");
        }
    }
    ok = ok && nvs_check(nvs_set_u8(h, "n", n), "favs/n") && nvs_check(nvs_commit(h), "favs commit");
    nvs_close(h);
    if (ok) ESP_LOGI(TAG, "%d saved", n);
    return ok;
}
