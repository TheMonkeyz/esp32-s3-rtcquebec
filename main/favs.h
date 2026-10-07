#pragma once
// The favourites: stop + route + direction, in the order the pages show them. NVS namespace "favs": "n" (u8) and one
// string per favourite, "f0".."f7" = "<stop>/<route>/<dir>" (typed keys, not a blob: a blob that changes size drops
// the settings on an update).
#include <stdbool.h>
#include "rtc_api.h"

#define FAVS_MAX 8

int favs_load(rtc_fav_t *out);                    // how many (0..FAVS_MAX); invalid entries are skipped
bool favs_save(const rtc_fav_t *favs, int n);
