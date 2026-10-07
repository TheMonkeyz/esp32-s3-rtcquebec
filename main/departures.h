#pragma once
// Next departures of the favourites, fetched from RTC's website API in one task (departures.c). Polling rules
// (docs/ARCHITECTURE.md, Data sources): the favourite on view every DEPS_SHOWN_S, the others at most every
// DEPS_HIDDEN_S, one request at a time, never while offline.
#include <stdbool.h>
#include <stdint.h>
#include "rtc_api.h"
#include "favs.h"

#define DEPS_SHOWN_S 30
#define DEPS_HIDDEN_S (5 * 60)

typedef enum {
    DEP_WAITING,            // not fetched yet
    DEP_OK,                 // board holds the last reply
    DEP_NOT_FOUND,          // RTC answered 404: this route doesn't serve this stop in this direction
} dep_state_t;

typedef struct {
    rtc_fav_t fav;
    dep_state_t state;
    rtc_board_t board;      // the last good reply (DEP_OK)
    time_t fetched;         // when it came (UTC), 0 = never
    bool failing;           // the last try failed (network, server): board is older than it should be
} dep_entry_t;

// changed(i): favourite i has new data or a new state; called from the fetch task (take the display lock)
void deps_start(void (*changed)(int i));
void deps_set_favs(const rtc_fav_t *favs, int n);   // replaces the list; the new ones are fetched soon
void deps_show(int i);                              // the favourite on view (-1: none); fetched now if due
bool deps_get(int i, dep_entry_t *out);

// For the settings page: one request run by the fetch task, waiting up to 20 s.
// 1 = found, 0 = RTC says no such route / stop on that route, -1 = couldn't ask (offline, timeout, bad reply).
int deps_lookup_route(const char *route, rtc_route_t *out);
int deps_check_fav(const rtc_fav_t *f, rtc_board_t *out);

// Today's service date as RTC wants it (yyyymmdd, local time); false while the clock isn't set
bool deps_date(char *out, int n);
