#pragma once
// RTC's website API (api-iv.rtcquebec.ca/api/legacy): request URLs and reply parsing. Pure C (cJSON only), so the
// host tests check it against saved replies (tests/host/test_rtc_api.c). The API is undocumented and has changed
// before: everything that knows its shape is in this file and rtc_api.c (docs/ARCHITECTURE.md, Data sources).
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define RTC_API "https://api-iv.rtcquebec.ca/api/legacy"
#define RTC_NOTICES "https://www.rtcquebec.ca/en/api/notices_v2"   // the website's notices (Drupal JSON:API)
#define RTC_NOTICES_MAX 6                 // per route and request (page[limit])
#define RTC_NOTICE_ROUTES 32              // route/direction pairs kept per notice
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
    double lat, lon;                      // the stop (0, 0: not in the reply)
    bool not_served;                      // "arretNonDesservi": the stop isn't served now (a detour, works)
    bool drop_off_only;                   // "descenteSeulement"
    int n;                                // departures in dep[] (0: none left today)
    rtc_dep_t dep[RTC_DEPS_MAX];
} rtc_board_t;

// ListeAutobus_Parcours: the buses on a route in one direction, where they are (rtcquebec.ca's route map uses it as
// "getBusPositions"; RTC caches it 20 s)
#define RTC_BUSES_MAX 12
typedef struct {
    char id[12];                          // "1269" (the bus)
    double lat, lon;
    char updated[24];                     // "2026-10-07T01:02:58", local time, no offset (as RTC sends it)
} rtc_bus_t;
bool rtc_buses_url(char *out, size_t n, const char *route, const char *dir);
int rtc_parse_buses(const char *json, rtc_bus_t *out, int max);   // how many, -1 if it isn't that reply

// Parcours_Periode: a route and its two directions
typedef struct {
    char route[8];
    char name[96];                        // "Terminus Chute-Montmorency - Colline Parlementaire"
    char dir_code[2][4];                  // principal, return
    char dir_name[2][48];
} rtc_route_t;

// A notice on routes (rtcquebec.ca's "Avis sur les parcours"): works, detours, stops not served. Its texts are
// RTC's, in French only.
typedef struct {
    char id[40];                          // the notice's UUID
    char title[112];                      // "Arrêt De Ste-Hélène (1263) non desservi"
    char subtitle[112];                   // often empty
    char begin[48], end[48];              // works' start and end as RTC writes them ("10 novembre", "indéterminée")
    bool urgent;
    time_t start;                         // publication start (UTC)
    int n_routes;
    struct { char route[8]; char dir[4]; } routes[RTC_NOTICE_ROUTES];
} rtc_notice_t;

// The notices published now that name `route` (any direction), newest first. now_iso: local time with its offset,
// "2026-10-07T00:52:00-04:00".
bool rtc_notices_url(char *out, size_t n, const char *route, const char *now_iso);
// Fills out[0..max-1]; returns how many, -1 if it isn't a notices reply
int rtc_parse_notices(const char *json, rtc_notice_t *out, int max);
// The notice names this route in this direction
bool rtc_notice_for(const rtc_notice_t *n, const char *route, const char *dir);

// date: the service day, yyyymmdd. false: the URL didn't fit.
bool rtc_board_url(char *out, size_t n, const rtc_fav_t *f, const char *date);
bool rtc_route_url(char *out, size_t n, const char *route, const char *date);

// false: not the reply expected (missing fields); out is then zeroed
bool rtc_parse_board(const char *json, rtc_board_t *out);
bool rtc_parse_route(const char *json, rtc_route_t *out);

// A 200 reply that says "nothing here": RTC answers `null` for a route number not in use (999), where an unknown
// one (4242) gets a 404. Callers treat it as "not found", not as a bad reply.
bool rtc_reply_none(const char *json);

// now (UTC) and its local time -> "2026-10-07T00:52:00-04:00" (seconds dropped). The offset comes from the two, not
// from strftime's %z: Emscripten's (the emulator's) ignores TZ and wrote local time as +00:00.
void rtc_iso_local(time_t now, const struct tm *local, char *out, size_t n);

// "2026-10-06T22:46:57-04:00" -> UTC seconds; false if it isn't that shape
bool rtc_parse_time(const char *iso, time_t *out);

// What a favourite's fields may hold: stop 1-6 digits, route 1-5 letters or digits, direction 1-3 digits
bool rtc_fav_valid(const rtc_fav_t *f);
