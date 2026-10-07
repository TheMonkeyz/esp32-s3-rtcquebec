#pragma once
// RTC's website API (api-iv.rtcquebec.ca/api/legacy): request URLs and reply parsing. Pure C (cJSON only), so the
// host tests check it against saved replies (tests/host/test_rtc_api.c). The API is undocumented and has changed
// before: everything that knows its shape is in this file and rtc_api.c (docs/ARCHITECTURE.md, Data sources).
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define RTC_API "https://api-iv.rtcquebec.ca/api/legacy"
#define RTC_DEPS_MAX 5                    // the API gives the next 5 departures

// A favourite: a stop, a route and the route's direction there (all as the API writes them: "1025", "800", "0")
typedef struct {
    char stop[8];
    char route[8];
    char dir[4];
} rtc_fav_t;

typedef struct {
    time_t depart;                        // UTC
    bool live;                            // real time ("ntr"); else the schedule's time
    bool cancelled;                       // "annule"
} rtc_dep_t;

// BorneVirtuelle_ArretParcours: the next departures of one route at one stop
typedef struct {
    char route[8];                        // "800"
    char direction[48];                   // "Colline Parlementaire"
    char stop_name[48];                   // "St-Dominique"
    char stop_desc[64];                   // "Charest Est / Saint-Dominique"
    bool not_served;                      // "arretNonDesservi": the stop isn't served now (a detour, works)
    bool drop_off_only;                   // "descenteSeulement"
    int n;                                // departures in dep[] (0: none left today)
    rtc_dep_t dep[RTC_DEPS_MAX];
} rtc_board_t;

// Parcours_Periode: a route and its two directions
typedef struct {
    char route[8];
    char name[96];                        // "Terminus Chute-Montmorency - Colline Parlementaire"
    char dir_code[2][4];                  // principal, return
    char dir_name[2][48];
} rtc_route_t;

// date: the service day, yyyymmdd. false: the URL didn't fit.
bool rtc_board_url(char *out, size_t n, const rtc_fav_t *f, const char *date);
bool rtc_route_url(char *out, size_t n, const char *route, const char *date);

// false: not the reply expected (missing fields); out is then zeroed
bool rtc_parse_board(const char *json, rtc_board_t *out);
bool rtc_parse_route(const char *json, rtc_route_t *out);

// A 200 reply that says "nothing here": RTC answers `null` for a route number not in use (999), where an unknown
// one (4242) gets a 404. Callers treat it as "not found", not as a bad reply.
bool rtc_reply_none(const char *json);

// "2026-10-06T22:46:57-04:00" -> UTC seconds; false if it isn't that shape
bool rtc_parse_time(const char *iso, time_t *out);

// What a favourite's fields may hold: stop 1-6 digits, route 1-5 letters or digits, direction 1-3 digits
bool rtc_fav_valid(const rtc_fav_t *f);
