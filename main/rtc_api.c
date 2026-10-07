// RTC's website API: URLs and reply parsing (see rtc_api.h). Replies seen on 2026-10-06 are in
// tests/host/data/rtc_*.json.
#include "rtc_api.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"

static bool all(const char *s, int min, int max, int (*ok)(int))
{
    int n = strlen(s);
    if (n < min || n > max) return false;
    for (int i = 0; i < n; i++) if (!ok((unsigned char)s[i])) return false;
    return true;
}

bool rtc_fav_valid(const rtc_fav_t *f)
{
    return all(f->stop, 1, 6, isdigit) && all(f->route, 1, 5, isalnum) && all(f->dir, 1, 3, isdigit);
}

bool rtc_board_url(char *out, size_t n, const rtc_fav_t *f, const char *date)
{
    if (!rtc_fav_valid(f)) return false;
    int len = snprintf(out, n, RTC_API "/BorneVirtuelle_ArretParcours?noArret=%s&noParcours=%s&codeDirection=%s&date=%s",
                       f->stop, f->route, f->dir, date);
    return len > 0 && (size_t)len < n;
}

bool rtc_route_url(char *out, size_t n, const char *route, const char *date)
{
    if (!all(route, 1, 5, isalnum)) return false;
    int len = snprintf(out, n, RTC_API "/Parcours_Periode?noParcours=%s&date=%s", route, date);
    return len > 0 && (size_t)len < n;
}

// Days from 1970-01-01 to a civil date (Howard Hinnant's algorithm): no timegm() on newlib, and mktime() would apply
// the device's time zone
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

bool rtc_parse_time(const char *iso, time_t *out)
{
    int y, mo, d, h, mi, s, oh, om, used = 0;
    char sign;
    if (!iso || sscanf(iso, "%4d-%2d-%2dT%2d:%2d:%2d%c%2d:%2d%n", &y, &mo, &d, &h, &mi, &s, &sign, &oh, &om, &used) != 9
        || used != (int)strlen(iso) || (sign != '+' && sign != '-') || mo < 1 || mo > 12 || d < 1 || d > 31
        || h > 23 || mi > 59 || s > 60)
        return false;
    long off = (oh * 60L + om) * 60 * (sign == '-' ? -1 : 1);
    *out = (time_t)(days_from_civil(y, mo, d) * 86400L + h * 3600L + mi * 60L + s - off);
    return true;
}

// A string field copied (a number is written as one: the API has sent "noParcours" both ways elsewhere)
static bool copy(char *dst, size_t n, const cJSON *obj, const char *name)
{
    const cJSON *v = cJSON_GetObjectItem(obj, name);
    if (cJSON_IsString(v)) snprintf(dst, n, "%s", v->valuestring);
    else if (cJSON_IsNumber(v)) snprintf(dst, n, "%d", v->valueint);
    else { dst[0] = 0; return false; }
    return true;
}

static bool flag(const cJSON *obj, const char *name) { return cJSON_IsTrue(cJSON_GetObjectItem(obj, name)); }

bool rtc_parse_board(const char *json, rtc_board_t *out)
{
    memset(out, 0, sizeof(*out));
    cJSON *j = cJSON_Parse(json);
    const cJSON *parcours = cJSON_GetObjectItem(j, "parcours"), *arret = cJSON_GetObjectItem(j, "arret");
    const cJSON *horaires = cJSON_GetObjectItem(j, "horaires");
    bool ok = cJSON_IsObject(parcours) && cJSON_IsObject(arret) && cJSON_IsArray(horaires)
              && copy(out->route, sizeof(out->route), parcours, "noParcours")
              && copy(out->stop_name, sizeof(out->stop_name), arret, "nom");
    if (ok) {
        copy(out->direction, sizeof(out->direction), parcours, "descriptionDirection");
        copy(out->stop_desc, sizeof(out->stop_desc), arret, "description");
        out->not_served = flag(j, "arretNonDesservi");
        out->drop_off_only = flag(j, "descenteSeulement");
        const cJSON *h;
        cJSON_ArrayForEach(h, horaires) {
            if (out->n == RTC_DEPS_MAX) break;
            rtc_dep_t *d = &out->dep[out->n];
            if (!rtc_parse_time(cJSON_GetStringValue(cJSON_GetObjectItem(h, "depart")), &d->depart)) continue;
            d->live = flag(h, "ntr");
            d->cancelled = flag(h, "annule");
            out->n++;
        }
    }
    cJSON_Delete(j);
    if (!ok) memset(out, 0, sizeof(*out));
    return ok;
}

bool rtc_reply_none(const char *json)
{
    if (!json) return false;
    while (isspace((unsigned char)*json)) json++;
    if (strncmp(json, "null", 4)) return false;
    for (json += 4; *json; json++) if (!isspace((unsigned char)*json)) return false;
    return true;
}

bool rtc_parse_route(const char *json, rtc_route_t *out)
{
    memset(out, 0, sizeof(*out));
    cJSON *j = cJSON_Parse(json);
    bool ok = cJSON_IsObject(j) && copy(out->route, sizeof(out->route), j, "noParcours")
              && copy(out->dir_code[0], sizeof(out->dir_code[0]), j, "codeDirectionPrincipale")
              && copy(out->dir_code[1], sizeof(out->dir_code[1]), j, "codeDirectionRetour");
    if (ok) {
        copy(out->name, sizeof(out->name), j, "description");
        copy(out->dir_name[0], sizeof(out->dir_name[0]), j, "descriptionDirectionPrincipale");
        copy(out->dir_name[1], sizeof(out->dir_name[1]), j, "descriptionDirectionRetour");
    }
    cJSON_Delete(j);
    if (!ok) memset(out, 0, sizeof(*out));
    return ok;
}
