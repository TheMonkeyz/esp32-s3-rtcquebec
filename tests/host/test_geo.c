// geo.c: tile maths checked against OpenStreetMap's own formulas (wiki "Slippy map tilenames")
#include <math.h>
#include "check.h"
#include "geo.h"

int main(void)
{
    double x, y;
    // St-Dominique (stop 1025): 46.815845, -71.217659 at zoom 15 -> tile 9901, 11549 (the OSM wiki formula, in Python)
    geo_world_px(46.815845, -71.217659, 15, &x, &y);
    CHECK((int)(x / GEO_TILE) == 9901 && (int)(y / GEO_TILE) == 11549, "tile %d/%d", (int)(x / GEO_TILE), (int)(y / GEO_TILE));
    geo_world_px(0, 0, 0, &x, &y);
    CHECK(fabs(x - 128) < 1e-9 && fabs(y - 128) < 1e-9, "0,0 at zoom 0 is the centre: %f %f", x, y);
    geo_world_px(0, -180, 1, &x, &y);
    CHECK(fabs(x) < 1e-9 && fabs(y - 256) < 1e-9, "west edge: %f %f", x, y);

    int tx0, ty0, tx1, ty1;
    geo_tiles(1000.0, 300.0, 466, 466, &tx0, &ty0, &tx1, &ty1);   // x 1000..1465 -> tiles 3..5; y 300..765 -> 1..2
    CHECK(tx0 == 3 && tx1 == 5 && ty0 == 1 && ty1 == 2, "%d-%d %d-%d", tx0, tx1, ty0, ty1);
    geo_tiles(512.0, 512.0, 256, 256, &tx0, &ty0, &tx1, &ty1);    // exactly one tile
    CHECK(tx0 == 2 && tx1 == 2 && ty0 == 2 && ty1 == 2, "%d-%d %d-%d", tx0, tx1, ty0, ty1);

    // St-Dominique 1025 to 1105 (across Charest): a few tens of metres
    double d = geo_distance_m(46.815845, -71.217659, 46.81590, -71.21800);
    CHECK(d > 20 && d < 40, "%f m", d);
    CHECK(fabs(geo_distance_m(46.8, -71.2, 46.8, -71.2)) < 1e-6, "zero");

    double px = 300, py = 400;
    CHECK(geo_clamp_circle(&px, &py, 100) && fabs(px - 60) < 1e-9 && fabs(py - 80) < 1e-9, "%f %f", px, py);
    px = 30; py = 40;
    CHECK(!geo_clamp_circle(&px, &py, 100) && px == 30 && py == 40, "inside stays");
    return check_done("geo");
}
