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

    // The local time and offset the notices' query needs (the emulator's %z wrote local time as +00:00, 2026-10-07)
    char iso[32];
    struct tm lt = { .tm_year = 126, .tm_mon = 9, .tm_mday = 7, .tm_hour = 1, .tm_min = 0, .tm_sec = 12 };
    rtc_iso_local(1791349212, &lt, iso, sizeof(iso));                 // 05:00:12 UTC = 01:00:12 in Québec (EDT)
    CHECK(!strcmp(iso, "2026-10-07T01:00:00-04:00"), "%s", iso);
    struct tm wt = { .tm_year = 126, .tm_mon = 0, .tm_mday = 15, .tm_hour = 8 };
    rtc_iso_local(1768482000, &wt, iso, sizeof(iso));                 // winter: -05:00
    CHECK(!strcmp(iso, "2026-01-15T08:00:00-05:00"), "%s", iso);
    struct tm nt2 = { .tm_year = 126, .tm_mon = 9, .tm_mday = 7, .tm_hour = 10, .tm_min = 30 };
    rtc_iso_local(1791349200, &nt2, iso, sizeof(iso));                // 05:00 UTC shown as 10:30: +05:30
    CHECK(!strcmp(iso, "2026-10-07T10:30:00+05:30"), "%s", iso);

    rtc_board_t b;
    char *s = slurp("data/rtc_board_1025_800_0.json");
    CHECK(rtc_parse_board(s, &b), "the real reply");
    CHECK(!strcmp(b.route, "800") && !strcmp(b.direction, "Colline Parlementaire"), "%s / %s", b.route, b.direction);
    CHECK(!strcmp(b.stop_name, "St-Dominique") && !strcmp(b.stop_desc, "Charest Est / Saint-Dominique"), "%s", b.stop_name);
    CHECK(!b.not_served && !b.drop_off_only, "flags");
    CHECK(b.lat > 46.8158 && b.lat < 46.8159 && b.lon < -71.2176 && b.lon > -71.2177, "stop at %f %f", b.lat, b.lon);
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

    // Notices (rtcquebec.ca, 2026-10-07): route 800 had one, route 11 two
    static rtc_notice_t nt[RTC_NOTICES_MAX];
    s = slurp("data/rtc_notices_800.json");
    int nn = rtc_parse_notices(s, nt, RTC_NOTICES_MAX);
    CHECK(nn == 1, "%d notices for 800", nn);
    CHECK(!strcmp(nt[0].title, "Arrêt De Ste-Hélène (1263) non desservi"), "%s", nt[0].title);
    CHECK(!strcmp(nt[0].end, "indéterminée"), "struck date dropped: \"%s\"", nt[0].end);
    CHECK(!nt[0].begin[0] && !nt[0].subtitle[0] && nt[0].urgent, "begin \"%s\"", nt[0].begin);
    CHECK(nt[0].n_routes == 15, "%d routes", nt[0].n_routes);
    CHECK(rtc_notice_for(&nt[0], "800", "0") && !rtc_notice_for(&nt[0], "800", "1"), "800 direction 0 only");
    CHECK(rtc_notice_for(&nt[0], "805", "1") && !rtc_notice_for(&nt[0], "11", "0"), "805/1, not 11");
    free(s);
    s = slurp("data/rtc_notices_11.json");
    nn = rtc_parse_notices(s, nt, RTC_NOTICES_MAX);
    CHECK(nn == 2, "%d notices for 11", nn);
    CHECK(!strcmp(nt[0].title, "Arrêt De Salaberry (1851) non desservi"), "trailing space trimmed: \"%s\"", nt[0].title);
    CHECK(!strcmp(nt[1].begin, "10 novembre") && !strcmp(nt[1].end, "indéterminée"), "%s / %s", nt[1].begin, nt[1].end);
    CHECK(rtc_parse_notices(s, nt, 1) == 1, "max");
    free(s);
    CHECK(rtc_parse_notices("{\"data\":[]}", nt, RTC_NOTICES_MAX) == 0, "none");
    CHECK(rtc_parse_notices("{\"errors\":[{}]}", nt, RTC_NOTICES_MAX) == -1, "an error reply");
    CHECK(rtc_parse_notices("{\"data\":[{\"id\":\"x\",\"attributes\":{\"title\":\"T\",\"description_work_end\":"
                            "{\"value\":\"<p>Jusqu&#039;au 3&nbsp;mai &amp; plus<br>tard</p>\"}}}]}", nt, 2) == 1
          && !strcmp(nt[0].end, "Jusqu'au 3 mai & plus tard") && nt[0].n_routes == 0, "entities: \"%s\"", nt[0].end);

    // Bus positions (route 800 direction 0, 01:03 on 2026-10-07: one bus out)
    rtc_bus_t bus[RTC_BUSES_MAX];
    s = slurp("data/rtc_buses_800_0.json");
    int nb = rtc_parse_buses(s, bus, RTC_BUSES_MAX);
    CHECK(nb == 1 && !strcmp(bus[0].id, "1269") && bus[0].lat > 46.81 && bus[0].lon < -71.22, "%d %s", nb, bus[0].id);
    CHECK(!strcmp(bus[0].updated, "2026-10-07T01:02:58"), "%s", bus[0].updated);
    free(s);
    CHECK(rtc_parse_buses("[]", bus, RTC_BUSES_MAX) == 0, "none out");
    CHECK(rtc_parse_buses("[{\"idAutobus\":\"1\",\"latitude\":0,\"longitude\":0},{\"idAutobus\":2,\"latitude\":46.8,"
                          "\"longitude\":-71.2}]", bus, RTC_BUSES_MAX) == 1 && !strcmp(bus[0].id, "2"), "0,0 skipped");
    CHECK(rtc_parse_buses("null", bus, RTC_BUSES_MAX) == -1 && rtc_parse_buses("{}", bus, 2) == -1, "not a list");

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
    CHECK(rtc_buses_url(url, sizeof(url), "800", "0") && !strcmp(url,
          "https://api-iv.rtcquebec.ca/api/legacy/ListeAutobus_Parcours?noParcours=800&codeDirection=0"), "%s", url);
    CHECK(!rtc_buses_url(url, sizeof(url), "800", "x"), "bad direction");
    static char big[2048];
    CHECK(rtc_notices_url(big, sizeof(big), "800", "2026-10-07T00:52:00-04:00"), "notices url");
    CHECK(strstr(big, "condition%5D%5Bvalue%5D=800&") && strstr(big, "value%5D=2026-10-07T00%3A52%3A00-04%3A00&")
          && strstr(big, "operator%5D=%3C%3D&") && strstr(big, "IS%20NULL") && strstr(big, "page%5Blimit%5D=6"), "%s", big);
    CHECK(!rtc_notices_url(big, sizeof(big), "8&0", "2026-10-07T00:52:00-04:00"), "bad route");
    CHECK(!rtc_notices_url(big, 300, "800", "2026-10-07T00:52:00-04:00"), "too small");
    return check_done("rtc_api");
}
