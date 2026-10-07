// Street map pictures from OpenStreetMap tiles (see map.h). The tile download and PNG decoding follow weather_amoled's
// radar.c: one keep-alive connection, png_rows() a row at a time (~50 KB, not the whole image), the same dimming.
#include "map.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "png_rows.h"
#include "svc.h"
#include "net.h"
#include "geo.h"

static const char *TAG = "map";
#define BODY_CAP (192 * 1024)          // an OSM tile is 5-60 KB
#define DARK 0x10A3                    // rgb565(18, 20, 24): where no tile is (yet)

typedef struct {
    uint16_t *px;
    int zoom;
    double ox, oy;
    map_state_t state;
    int tiles, done;
    uint32_t used;                     // LRU stamp
    bool valid;
} slot_t;

static slot_t slots[MAP_SLOTS];
static int cur = -1;                   // the slot map_show() returned last
static uint32_t stamp;
static SemaphoreHandle_t mu;
static TaskHandle_t task;
static void (*on_updated)(void);
static int svc_osm = -1;

static inline uint16_t rgb565(int r, int g, int b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }

// OSM's standard style is bright: desaturated half-way and dimmed for a dark AMOLED (weather_amoled's dim_map)
static uint16_t dim_map(uint8_t r, uint8_t g, uint8_t b)
{
    int l = (77 * r + 150 * g + 29 * b) >> 8;
    return rgb565(((l + r) / 2) * 55 / 100, ((l + g) / 2) * 55 / 100, ((l + b) / 2) * 55 / 100);
}

typedef struct { uint16_t *dst; int ox, oy; } tile_t;   // a 256 px tile into the picture at (ox, oy)

static bool tile_row(unsigned y, const uint8_t *rgba, unsigned w, void *user)
{
    const tile_t *t = user;
    if ((y & 63) == 63) vTaskDelay(1);
    int sy = t->oy + (int)y;
    if (sy < 0) return true;
    if (sy >= MAP_SIZE) return false;                                   // below the picture: done
    for (unsigned x = 0; x < w; x++) {
        int sx = t->ox + (int)x;
        if (sx >= 0 && sx < MAP_SIZE) t->dst[sy * MAP_SIZE + sx] = dim_map(rgba[x * 4], rgba[x * 4 + 1], rgba[x * 4 + 2]);
    }
    return true;
}

typedef struct { uint8_t *buf; int len, cap; bool over; } body_t;

static esp_err_t on_http(esp_http_client_event_t *e)
{
    body_t *b = e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA) {
        if (b->len + e->data_len <= b->cap) { memcpy(b->buf + b->len, e->data, e->data_len); b->len += e->data_len; }
        else b->over = true;
    }
    return ESP_OK;
}

// One tile on the kept connection (*hc made on first use, dropped after an error)
static bool fetch(esp_http_client_handle_t *hc, const char *url, body_t *b)
{
    b->len = 0;
    b->over = false;
    if (!*hc) {
        esp_http_client_config_t c = {
            .url = url, .event_handler = on_http, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 10000,
            .user_agent = svc_user_agent(), .buffer_size = 4096, .keep_alive_enable = true,
        };
        if (!(*hc = esp_http_client_init(&c))) return false;
    } else {
        esp_http_client_set_url(*hc, url);
    }
    esp_http_client_set_user_data(*hc, b);
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = esp_http_client_perform(*hc);
    int st = esp_http_client_get_status_code(*hc);
    svc_http(svc_osm, err, st, t0);
    if (err != ESP_OK) { esp_http_client_cleanup(*hc); *hc = NULL; }
    bool ok = err == ESP_OK && st == 200 && b->len > 0 && !b->over;
    if (!ok) ESP_LOGW(TAG, "GET %s: %s, %d%s", url, esp_err_to_name(err), st, b->over ? " (too big)" : "");
    return ok;
}

// Draws every tile of slot s; true when all arrived
static bool load(int s, body_t *b)
{
    slot_t *sl = &slots[s];
    int tx0, ty0, tx1, ty1;
    geo_tiles(sl->ox, sl->oy, MAP_SIZE, MAP_SIZE, &tx0, &ty0, &tx1, &ty1);
    int ok = 0;
    char url[96];
    esp_http_client_handle_t hc = NULL;
    for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++) {
            snprintf(url, sizeof(url), "https://tile.openstreetmap.org/%d/%d/%d.png", sl->zoom, tx, ty);
            bool got = false;
            for (int a = 0; a < 2 && !got; a++) got = fetch(&hc, url, b);
            tile_t t = { sl->px, tx * GEO_TILE - (int)sl->ox, ty * GEO_TILE - (int)sl->oy };
            if (got && png_rows(b->buf, b->len, tile_row, &t, NULL, NULL)) ok++;
            xSemaphoreTake(mu, portMAX_DELAY);
            sl->done = ok;
            xSemaphoreGive(mu);
            if (on_updated) on_updated();
        }
    if (hc) esp_http_client_cleanup(hc);
    ESP_LOGI(TAG, "zoom %d at %.0f,%.0f: %d/%d tiles", sl->zoom, sl->ox, sl->oy, ok, sl->tiles);
    return ok == sl->tiles;
}

static void map_task(void *arg)
{
    body_t b = { .cap = BODY_CAP };
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (int s; ; ) {                                   // the slots waiting for tiles, the one shown first
            xSemaphoreTake(mu, portMAX_DELAY);
            s = cur >= 0 && slots[cur].valid && slots[cur].state == MAP_LOADING ? cur : -1;
            xSemaphoreGive(mu);
            if (s < 0) break;
            if (!net_is_connected()) { vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
            if (!b.buf && !(b.buf = heap_caps_malloc(BODY_CAP, MALLOC_CAP_SPIRAM))) {
                ESP_LOGE(TAG, "no memory for a tile");
                xSemaphoreTake(mu, portMAX_DELAY);
                slots[s].state = MAP_FAILED;
                xSemaphoreGive(mu);
                break;
            }
            bool all = load(s, &b);
            xSemaphoreTake(mu, portMAX_DELAY);
            slots[s].state = all ? MAP_READY : MAP_FAILED;
            xSemaphoreGive(mu);
            if (on_updated) on_updated();
        }
        free(b.buf);                                        // 192 KB of PSRAM back between maps
        b.buf = NULL;
    }
}

void map_init(void (*updated)(void))
{
    on_updated = updated;
    mu = xSemaphoreCreateMutex();
    svc_osm = svc_add("OpenStreetMap", "Map tiles", NULL);
    // TLS and the PNG rows: internal RAM for the stack, the buffers in PSRAM
    xTaskCreatePinnedToCore(map_task, "map", 6144, NULL, 3, &task, 0);
}

static void fill(map_view_t *out, const slot_t *s)
{
    *out = (map_view_t){ .px = s->px, .zoom = s->zoom, .ox = s->ox, .oy = s->oy, .state = s->state, .tiles = s->tiles,
                         .done = s->done };
}

void map_show(double lat, double lon, int zoom, map_view_t *out)
{
    double x, y;
    geo_world_px(lat, lon, zoom, &x, &y);
    double ox = floor(x - MAP_SIZE / 2), oy = floor(y - MAP_SIZE / 2);
    xSemaphoreTake(mu, portMAX_DELAY);
    int s = -1;
    for (int i = 0; i < MAP_SLOTS && s < 0; i++)            // kept? (a failed one is tried again)
        if (slots[i].valid && slots[i].zoom == zoom && slots[i].ox == ox && slots[i].oy == oy) s = i;
    if (s >= 0 && slots[s].state == MAP_FAILED) slots[s].state = MAP_LOADING;
    if (s >= 0 && slots[s].state == MAP_READY) ESP_LOGI(TAG, "zoom %d at %.0f,%.0f: kept", zoom, ox, oy);
    if (s < 0) {                                            // a new picture in the least recently used slot
        s = 0;
        for (int i = 1; i < MAP_SLOTS; i++) if (!slots[i].valid || (slots[s].valid && slots[i].used < slots[s].used)) s = i;
        slot_t *sl = &slots[s];
        if (!sl->px) sl->px = heap_caps_malloc(MAP_SIZE * MAP_SIZE * 2, MALLOC_CAP_SPIRAM);
        int tx0, ty0, tx1, ty1;
        geo_tiles(ox, oy, MAP_SIZE, MAP_SIZE, &tx0, &ty0, &tx1, &ty1);
        sl->zoom = zoom;
        sl->ox = ox;
        sl->oy = oy;
        sl->tiles = (tx1 - tx0 + 1) * (ty1 - ty0 + 1);
        sl->done = 0;
        sl->valid = sl->px != NULL;
        sl->state = sl->px ? MAP_LOADING : MAP_FAILED;
        if (sl->px) for (int i = 0; i < MAP_SIZE * MAP_SIZE; i++) sl->px[i] = DARK;
        else ESP_LOGE(TAG, "no memory for a map picture");
    }
    slots[s].used = ++stamp;
    cur = s;
    fill(out, &slots[s]);
    bool start = slots[s].state == MAP_LOADING;
    xSemaphoreGive(mu);
    if (start && task) xTaskNotifyGive(task);
}

void map_status(map_view_t *out)
{
    xSemaphoreTake(mu, portMAX_DELAY);
    if (cur >= 0) fill(out, &slots[cur]);
    else *out = (map_view_t){ .state = MAP_FAILED };
    xSemaphoreGive(mu);
}
