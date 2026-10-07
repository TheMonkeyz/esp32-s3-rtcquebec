// rtc_api.c: RTC's replies as seen on 2026-10-06 (data/), and the shapes it must refuse
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "rtc_api.h"

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "can't open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *s = malloc(n + 1);
    s[fread(s, 1, n, f)] = 0;
    fclose(f);
    return s;
}

int main(void)
{
    time_t t;
    CHECK(rtc_parse_time("2026-10-06T22:46:57-04:00", &t) && t == 1791341217, "%ld", (long)t);  // 02:46:57 UTC
    CHECK(rtc_parse_time("2026-01-15T08:00:00-05:00", &t) && t == 1768482000, "winter offset: %ld", (long)t);
    CHECK(rtc_parse_time("2026-10-06T22:46:57+00:00", &t) && t == 1791326817, "UTC: %ld", (long)t);
    CHECK(!rtc_parse_time("2026-10-06T22:46:57", &t), "no offset");
    CHECK(!rtc_parse_time("2026-10-06T22:46:57-04:00Z", &t), "trailing text");
    CHECK(!rtc_parse_time("2026-13-06T22:46:57-04:00", &t), "month 13");
    CHECK(!rtc_parse_time(NULL, &t), "NULL");

    rtc_board_t b;
    char *s = slurp("data/rtc_board_1025_800_0.json");
    CHECK(rtc_parse_board(s, &b), "the real reply");
    CHECK(!strcmp(b.route, "800") && !strcmp(b.direction, "Colline Parlementaire"), "%s / %s", b.route, b.direction);
    CHECK(!strcmp(b.stop_name, "St-Dominique") && !strcmp(b.stop_desc, "Charest Est / Saint-Dominique"), "%s", b.stop_name);
    CHECK(!b.not_served && !b.drop_off_only, "flags");
    CHECK(b.n == 5, "%d departures", b.n);
    CHECK(b.dep[0].depart == 1791341217 && b.dep[0].live && !b.dep[0].cancelled, "first");
    CHECK(!b.dep[4].live, "the last one is the schedule's (ntr false)");
    free(s);

    // Six departures: only five are kept. A bad time is skipped, the others kept.
    CHECK(rtc_parse_board("{\"parcours\":{\"noParcours\":800},\"arret\":{\"nom\":\"X\"},\"horaires\":["
                          "{\"depart\":\"bad\"},"
                          "{\"depart\":\"2026-10-06T22:00:00-04:00\",\"ntr\":true,\"annule\":true},"
                          "{\"depart\":\"2026-10-06T22:01:00-04:00\"},{\"depart\":\"2026-10-06T22:02:00-04:00\"},"
                          "{\"depart\":\"2026-10-06T22:03:00-04:00\"},{\"depart\":\"2026-10-06T22:04:00-04:00\"},"
                          "{\"depart\":\"2026-10-06T22:05:00-04:00\"}]}", &b), "numbers as route");
    CHECK(!strcmp(b.route, "800") && b.n == 5 && b.dep[0].cancelled && b.dep[0].live, "n %d", b.n);
    CHECK(rtc_parse_board("{\"parcours\":{\"noParcours\":\"1\"},\"arret\":{\"nom\":\"X\"},\"horaires\":[],"
                          "\"arretNonDesservi\":true}", &b) && b.n == 0 && b.not_served, "none left, not served");
    CHECK(!rtc_parse_board("<!DOCTYPE html><html>Not Found</html>", &b) && b.n == 0, "the 404 page");
    CHECK(!rtc_parse_board("{\"parcours\":{}}", &b), "missing fields");
    CHECK(!rtc_parse_board(NULL, &b), "NULL");

    rtc_route_t r;
    s = slurp("data/rtc_route_800.json");
    CHECK(rtc_parse_route(s, &r), "the real reply");
    CHECK(!strcmp(r.dir_code[0], "0") && !strcmp(r.dir_name[0], "Colline Parlementaire"), "%s %s", r.dir_code[0], r.dir_name[0]);
    CHECK(!strcmp(r.dir_code[1], "1") && !strcmp(r.dir_name[1], "Terminus Chute-Montmorency"), "%s", r.dir_name[1]);
    free(s);
    CHECK(!rtc_parse_route("{\"noParcours\":\"9999\"}", &r) && !r.route[0], "unknown route");
    // Route 999 (2026-10-07): HTTP 200 with the body "null"; the device said "the RTC didn't answer"
    CHECK(rtc_reply_none("null") && rtc_reply_none(" null\r\n"), "null is none");
    CHECK(!rtc_reply_none("{}") && !rtc_reply_none("nullx") && !rtc_reply_none("") && !rtc_reply_none(NULL), "not none");
    CHECK(!rtc_parse_route("null", &r), "null isn't a route");

    char url[200];
    rtc_fav_t f = { "1025", "800", "0" };
    CHECK(rtc_board_url(url, sizeof(url), &f, "20261006") && !strcmp(url,
          "https://api-iv.rtcquebec.ca/api/legacy/BorneVirtuelle_ArretParcours?noArret=1025&noParcours=800"
          "&codeDirection=0&date=20261006"), "%s", url);
    rtc_fav_t bad = { "10&25", "800", "0" };
    CHECK(!rtc_board_url(url, sizeof(url), &bad, "20261006"), "a stop that would change the query");
    rtc_fav_t empty = { "", "800", "0" };
    CHECK(!rtc_fav_valid(&empty), "empty stop");
    CHECK(!rtc_board_url(url, 40, &f, "20261006"), "too small");
    CHECK(rtc_route_url(url, sizeof(url), "13a", "20261006"), "letters in a route");
    CHECK(!rtc_route_url(url, sizeof(url), "8 0", "20261006"), "space");
    return check_done("rtc_api");
}
