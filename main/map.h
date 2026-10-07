#pragma once
// The map screen's street map: OpenStreetMap tiles around a place, dimmed for the AMOLED, drawn into a MAP_SIZE square
// RGB565 picture (PSRAM). The last MAP_SLOTS pictures are kept, so reopening a stop's map downloads nothing. One task
// downloads the tiles (OSM's tile usage policy: an identifying User-Agent, few requests, kept while in use).
#include <stdbool.h>
#include <stdint.h>

#define MAP_SIZE 466                   // the round display's width and height
#define MAP_ZOOM 15                    // the first view: ~3.3 m a pixel in Québec City, ~1.5 km across
#define MAP_ZOOM_MIN 13                // ~6 km across
#define MAP_ZOOM_MAX 17                // ~370 m across
#define MAP_SLOTS 4                    // ~434 KB of PSRAM each: a stop at two zooms, and another

typedef enum { MAP_LOADING, MAP_READY, MAP_FAILED } map_state_t;

typedef struct {
    const uint16_t *px;                // MAP_SIZE x MAP_SIZE RGB565 (dark until tiles arrive)
    int zoom;
    double ox, oy;                     // its top-left corner in world pixels at that zoom (geo.h)
    map_state_t state;
    int tiles, done;                   // tiles it needs, tiles drawn
} map_view_t;

// updated(): a tile was drawn or the view is done; called from the map task (take the display lock)
void map_init(void (*updated)(void));
// The picture centred on (lat, lon) at zoom: a kept one, or a new one that starts downloading. Call from any task.
void map_show(double lat, double lon, int zoom, map_view_t *out);
// What the last map_show()'s picture is now (state, tiles done)
void map_status(map_view_t *out);
