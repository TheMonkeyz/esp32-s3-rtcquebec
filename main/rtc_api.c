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

void rtc_iso_local(time_t now, const struct tm *l, char *out, size_t n)
{
    long as_utc = days_from_civil(l->tm_year + 1900, l->tm_mon + 1, l->tm_mday) * 86400L
                  + l->tm_hour * 3600L + l->tm_min * 60L + l->tm_sec;
    long off = (as_utc - (long)now) / 60;                     // minutes east of UTC (-240 in Québec in summer)
    long a = off < 0 ? -off : off;
    snprintf(out, n, "%04d-%02d-%02dT%02d:%02d:00%c%02ld:%02ld", l->tm_year + 1900, l->tm_mon + 1, l->tm_mday,
             l->tm_hour, l->tm_min, off < 0 ? '-' : '+', a / 60, a % 60);
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
        const cJSON *la = cJSON_GetObjectItem(arret, "latitude"), *lo = cJSON_GetObjectItem(arret, "longitude");
        if (cJSON_IsNumber(la) && cJSON_IsNumber(lo)) { out->lat = la->valuedouble; out->lon = lo->valuedouble; }
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

/* ---------- notices (rtcquebec.ca's Drupal JSON:API) ---------- */

// The website's own query (seen on rtcquebec.ca, 2026-10-07): published, started, not ended (or no end), naming the
// route, newest first, only the fields and related items the display uses
bool rtc_notices_url(char *out, size_t n, const char *route, const char *now_iso)
{
    if (!all(route, 1, 5, isalnum) || strlen(now_iso) != 25) return false;
    char t[48];                                               // ':' and '+' percent-encoded
    int k = 0;
    for (const char *s = now_iso; *s && k < (int)sizeof(t) - 4; s++) {
        if (*s == ':') { memcpy(t + k, "%3A", 3); k += 3; }
        else if (*s == '+') { memcpy(t + k, "%2B", 3); k += 3; }
        else t[k++] = *s;
    }
    t[k] = 0;
    // Plain pieces around the values (they hold "%5B": never a printf format)
#define F "&filter%5B"
#define C "%5D%5Bcondition%5D%5B"
    static const char p0[] = RTC_NOTICES "?filter%5Br" C "path%5D=parcours.routes.name" F "r" C "value%5D=";
    static const char p1[] = F "st" C "path%5D=status" F "st" C "value%5D=1"
        F "s" C "path%5D=date_notice_start" F "s" C "operator%5D=%3C%3D" F "s" C "value%5D=";
    static const char p2[] = F "eg%5D%5Bgroup%5D%5Bconjunction%5D=OR"
        F "e" C "path%5D=date_notice_end" F "e" C "operator%5D=%3E" F "e" C "memberOf%5D=eg" F "e" C "value%5D=";
    static const char p3[] = F "en" C "path%5D=date_notice_end" F "en" C "operator%5D=IS%20NULL" F "en" C "memberOf%5D=eg"
        "&fields%5Bnotices_v2%5D=title%2Csubtitle%2Cparcours%2Cdate_notice_start%2Cdate_notice_end"
        "%2Cdescription_work_begin%2Cdescription_work_end%2Curgent"
        "&fields%5Bparagraph--avis_parcours%5D=routes&fields%5BtaxonomyTermsRoutes%5D=name%2Ccode_direction"
        "&include=parcours.routes&sort=-date_notice_start&page%5Blimit%5D=";
#undef F
#undef C
    int len = snprintf(out, n, "%s%s%s%s%s%s%s%d", p0, route, p1, t, p2, t, p3, RTC_NOTICES_MAX);
    return len > 0 && (size_t)len < n;
}

// RTC's short HTML ("<p><s>25 septembre</s> / indéterminée</p>") as plain text: struck-out parts dropped (a date
// replaced by a new one), tags removed, a few entities decoded, a leading " / " left by the dropped part trimmed
static void html_text(const char *h, char *out, size_t n)
{
    size_t k = 0;
    int strike = 0;
    for (const char *s = h; s && *s && k + 1 < n; ) {
        if (*s == '<') {
            if (!strncmp(s, "<s>", 3) || !strncmp(s, "<del>", 5) || !strncmp(s, "<strike>", 8)) strike++;
            else if ((!strncmp(s, "</s>", 4) || !strncmp(s, "</del>", 6) || !strncmp(s, "</strike>", 9)) && strike) strike--;
            else if ((!strncmp(s, "<br", 3) || !strncmp(s, "</p>", 4)) && k && out[k - 1] != ' ' && !strike) out[k++] = ' ';
            const char *e = strchr(s, '>');
            s = e ? e + 1 : s + strlen(s);
            continue;
        }
        static const struct { const char *ent, *txt; } ents[] = {
            { "&nbsp;", " " }, { "&amp;", "&" }, { "&quot;", "\"" }, { "&#039;", "'" }, { "&lt;", "<" }, { "&gt;", ">" },
        };
        bool done = false;
        for (size_t i = 0; *s == '&' && i < sizeof(ents) / sizeof(ents[0]) && !done; i++) {
            size_t l = strlen(ents[i].ent);
            if (!strncmp(s, ents[i].ent, l)) {
                if (!strike) out[k++] = ents[i].txt[0];
                s += l;
                done = true;
            }
        }
        if (done) continue;
        if (!strike) out[k++] = *s;
        s++;
    }
    out[k] = 0;
    char *p = out;                                            // trim: spaces and the separator a dropped date left
    while (*p == ' ' || *p == '/') p++;
    memmove(out, p, strlen(p) + 1);
    for (int i = (int)strlen(out) - 1; i >= 0 && (out[i] == ' ' || out[i] == '/'); i--) out[i] = 0;
}

static const char *html_of(const cJSON *v)                   // {"value": "<p>...</p>", "processed": ...} or null
{
    return cJSON_GetStringValue(cJSON_GetObjectItem(v, "value"));
}

// An item of "included" by type and id
static const cJSON *included(const cJSON *inc, const cJSON *ref)
{
    const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(ref, "type"));
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(ref, "id"));
    const cJSON *it;
    if (type && id) cJSON_ArrayForEach(it, inc) {
        const char *t = cJSON_GetStringValue(cJSON_GetObjectItem(it, "type"));
        const char *i = cJSON_GetStringValue(cJSON_GetObjectItem(it, "id"));
        if (t && i && !strcmp(t, type) && !strcmp(i, id)) return it;
    }
    return NULL;
}

static void trim(char *s)
{
    char *p = s;
    while (*p == ' ') p++;
    memmove(s, p, strlen(p) + 1);
    for (int i = (int)strlen(s) - 1; i >= 0 && s[i] == ' '; i--) s[i] = 0;
}

int rtc_parse_notices(const char *json, rtc_notice_t *out, int max)
{
    cJSON *j = cJSON_Parse(json);
    const cJSON *data = cJSON_GetObjectItem(j, "data"), *inc = cJSON_GetObjectItem(j, "included");
    if (!cJSON_IsArray(data)) { cJSON_Delete(j); return -1; }
    int n = 0;
    const cJSON *d;
    cJSON_ArrayForEach(d, data) {
        if (n == max) break;
        const cJSON *a = cJSON_GetObjectItem(d, "attributes");
        const char *title = cJSON_GetStringValue(cJSON_GetObjectItem(a, "title"));
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(d, "id"));
        if (!title || !id) continue;
        rtc_notice_t *x = &out[n];
        memset(x, 0, sizeof(*x));
        snprintf(x->id, sizeof(x->id), "%s", id);
        snprintf(x->title, sizeof(x->title), "%s", title);
        trim(x->title);
        const char *sub = cJSON_GetStringValue(cJSON_GetObjectItem(a, "subtitle"));
        if (sub) { snprintf(x->subtitle, sizeof(x->subtitle), "%s", sub); trim(x->subtitle); }
        html_text(html_of(cJSON_GetObjectItem(a, "description_work_begin")), x->begin, sizeof(x->begin));
        html_text(html_of(cJSON_GetObjectItem(a, "description_work_end")), x->end, sizeof(x->end));
        x->urgent = cJSON_IsTrue(cJSON_GetObjectItem(a, "urgent"));
        rtc_parse_time(cJSON_GetStringValue(cJSON_GetObjectItem(a, "date_notice_start")), &x->start);
        // notice -> parcours (paragraphs) -> routes (taxonomy terms: name = route number, code_direction)
        const cJSON *pars = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(d, "relationships"), "parcours"), "data");
        const cJSON *p;
        cJSON_ArrayForEach(p, pars) {
            const cJSON *par = included(inc, p);
            const cJSON *rs = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(par, "relationships"), "routes"), "data");
            const cJSON *r;
            cJSON_ArrayForEach(r, rs) {
                const cJSON *term = cJSON_GetObjectItem(included(inc, r), "attributes");
                if (!term || x->n_routes == RTC_NOTICE_ROUTES) continue;
                if (!copy(x->routes[x->n_routes].route, sizeof(x->routes[0].route), term, "name")) continue;
                copy(x->routes[x->n_routes].dir, sizeof(x->routes[0].dir), term, "code_direction");
                x->n_routes++;
            }
        }
        n++;
    }
    cJSON_Delete(j);
    return n;
}

bool rtc_notice_for(const rtc_notice_t *n, const char *route, const char *dir)
{
    for (int i = 0; i < n->n_routes; i++)
        if (!strcmp(n->routes[i].route, route) && (!n->routes[i].dir[0] || !strcmp(n->routes[i].dir, dir))) return true;
    return false;
}

bool rtc_reply_none(const char *json)
{
    if (!json) return false;
    while (isspace((unsigned char)*json)) json++;
    if (strncmp(json, "null", 4)) return false;
    for (json += 4; *json; json++) if (!isspace((unsigned char)*json)) return false;
    return true;
}

bool rtc_buses_url(char *out, size_t n, const char *route, const char *dir)
{
    if (!all(route, 1, 5, isalnum) || !all(dir, 1, 3, isdigit)) return false;
    int len = snprintf(out, n, RTC_API "/ListeAutobus_Parcours?noParcours=%s&codeDirection=%s", route, dir);
    return len > 0 && (size_t)len < n;
}

int rtc_parse_buses(const char *json, rtc_bus_t *out, int max)
{
    cJSON *j = cJSON_Parse(json);
    if (!cJSON_IsArray(j)) { cJSON_Delete(j); return -1; }
    int n = 0;
    const cJSON *b;
    cJSON_ArrayForEach(b, j) {
        if (n == max) break;
        const cJSON *la = cJSON_GetObjectItem(b, "latitude"), *lo = cJSON_GetObjectItem(b, "longitude");
        if (!cJSON_IsNumber(la) || !cJSON_IsNumber(lo) || (la->valuedouble == 0 && lo->valuedouble == 0)) continue;
        rtc_bus_t *x = &out[n];
        memset(x, 0, sizeof(*x));
        copy(x->id, sizeof(x->id), b, "idAutobus");
        copy(x->updated, sizeof(x->updated), b, "dateMiseJour");
        x->lat = la->valuedouble;
        x->lon = lo->valuedouble;
        n++;
    }
    cJSON_Delete(j);
    return n;
}

bool rtc_trace_url(char *out, size_t n, const char *route, const char *dir, const char *date)
{
    if (!all(route, 1, 5, isalnum) || !all(dir, 1, 3, isdigit) || !all(date, 8, 8, isdigit)) return false;
    int len = snprintf(out, n, RTC_API "/ListeParcoursTypeTrace_ParcoursPeriode?noParcours=%s&codeDirection=%s&date=%s",
                       route, dir, date);
    return len > 0 && (size_t)len < n;
}

int rtc_parse_traces(const char *json, void (*cb)(const char *polyline, void *user), void *user)
{
    cJSON *j = cJSON_Parse(json);
    if (!cJSON_IsArray(j)) { cJSON_Delete(j); return -1; }
    int n = 0;
    const cJSON *v;
    cJSON_ArrayForEach(v, j) {
        const char *p = cJSON_GetStringValue(cJSON_GetObjectItem(v, "polyligne"));
        if (!p || !*p) continue;
        cb(p, user);
        n++;
    }
    cJSON_Delete(j);
    return n;
}

// Google's encoding: each value a zig-zag signed delta in 5-bit groups + 63, lat then lon, 1e-5 degrees
int rtc_polyline_decode(const char *s, float *latlon, int max)
{
    long lat = 0, lon = 0;
    int n = 0;
    while (*s && n < max) {
        long v[2];
        for (int k = 0; k < 2; k++) {
            long res = 0;
            int shift = 0, b;
            do {
                if (!*s) return n;                            // cut short: the points so far
                b = *s++ - 63;
                if (b < 0 || b > 63) return n;                // not a polyline character
                res |= (long)(b & 0x1f) << shift;
                shift += 5;
            } while (b >= 0x20 && shift < 60);
            v[k] = res & 1 ? ~(res >> 1) : res >> 1;
        }
        lat += v[0];
        lon += v[1];
        latlon[2 * n] = lat / 1e5f;
        latlon[2 * n + 1] = lon / 1e5f;
        n++;
    }
    return n;
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
