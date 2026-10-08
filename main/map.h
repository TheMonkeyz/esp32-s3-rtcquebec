#pragma once
// The map screen's street map: espforge's forge_map (OpenStreetMap tiles around a place, dimmed for the AMOLED, kept
// pictures in PSRAM) with this app's choices. Before v0.4.0 this app had its own map.c / geo.c, which forge_map grew
// from.
#include "forge_map.h"

#define MAP_SIZE 466                   // the round display's width and height
#define MAP_ZOOM 15                    // the first view: ~3.3 m a pixel in Québec City, ~1.5 km across
#define MAP_ZOOM_MIN 13                // ~6 km across
#define MAP_ZOOM_MAX 17                // ~370 m across
#define MAP_SLOTS 4                    // ~434 KB of PSRAM each: a stop at two zooms, and another
