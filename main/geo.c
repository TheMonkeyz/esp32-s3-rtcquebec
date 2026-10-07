// Web Mercator maths (see geo.h)
#include "geo.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void geo_world_px(double lat, double lon, int z, double *x, double *y)
{
    double n = GEO_TILE * (double)(1L << z);
    double r = lat * M_PI / 180;
    *x = (lon + 180) / 360 * n;
    *y = (1 - log(tan(r) + 1 / cos(r)) / M_PI) / 2 * n;
}

void geo_tiles(double ox, double oy, int w, int h, int *tx0, int *ty0, int *tx1, int *ty1)
{
    *tx0 = (int)floor(ox / GEO_TILE);
    *ty0 = (int)floor(oy / GEO_TILE);
    *tx1 = (int)floor((ox + w - 1) / GEO_TILE);
    *ty1 = (int)floor((oy + h - 1) / GEO_TILE);
}

double geo_distance_m(double lat1, double lon1, double lat2, double lon2)
{
    double p1 = lat1 * M_PI / 180, p2 = lat2 * M_PI / 180;
    double dp = p2 - p1, dl = (lon2 - lon1) * M_PI / 180;
    double a = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    return 6371000 * 2 * atan2(sqrt(a), sqrt(1 - a));
}

bool geo_clamp_circle(double *px, double *py, double r)
{
    double d = sqrt(*px * *px + *py * *py);
    if (d <= r) return false;
    *px = *px * r / d;
    *py = *py * r / d;
    return true;
}
