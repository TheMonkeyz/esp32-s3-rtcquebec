#pragma once
// Web Mercator ("slippy map") maths for the map screen: OpenStreetMap's tiles are 256 px squares, 2^z of them across
// the world at zoom z. Pure C: tests/host/test_geo.c.
#include <stdbool.h>

#define GEO_TILE 256

// World pixel coordinates of a place at zoom z (x to the east, y to the south)
void geo_world_px(double lat, double lon, int z, double *x, double *y);

// The tiles covering a w x h window whose top-left corner is (ox, oy) in world pixels: tx0..tx1, ty0..ty1
void geo_tiles(double ox, double oy, int w, int h, int *tx0, int *ty0, int *tx1, int *ty1);

// Distance in metres between two places (haversine)
double geo_distance_m(double lat1, double lon1, double lat2, double lon2);

// A point (px, py) relative to a circle's centre, pulled in to radius r when it lies outside: true if it was outside
bool geo_clamp_circle(double *px, double *py, double r);
