// The screens (see ui.h): the favourite stops' departures and the system page in one pager, Wi-Fi setup, messages.
#include "ui.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_app_desc.h"
#include "board.h"
#include "pager.h"
#include "screens.h"
#include "slide.h"
#include "textfit.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net.h"
#include "web.h"
#include "app_text.h"
#include "departures.h"
#include "map.h"
#include "geo.h"

static const char *TAG = "ui";

#define C_BG     lv_color_hex(0x000000)       // AMOLED: black pixels are off
#define C_TEXT   lv_color_hex(0xF2F4F7)
#define C_DIM    lv_color_hex(0x8B95A1)
#define C_ACCENT lv_color_hex(0x4DA3FF)
#define C_LIVE   lv_color_hex(0x5BD68A)       // a real-time departure
#define C_BAD    lv_color_hex(0xFF6B6B)
#define C_WARN   lv_color_hex(0xFFB547)       // an alert

#ifdef EMU_BUILD                             // the browser emulator (web/emu): the font is an array, its end a pointer
extern const uint8_t ttf_start[];
extern const uint8_t *const ttf_end;
#else
extern const uint8_t ttf_start[] asm("_binary_montserrat_ttf_start");
extern const uint8_t ttf_end[]   asm("_binary_montserrat_ttf_end");
#endif

static lv_font_t *f_big, *f_mid, *f_small, *f_tiny;
static lv_obj_t *scr_main, *pager, *scr_msg, *scr_setup;
static int n_favs;                       // favourite pages shown (pages 1..n_favs); 0: page 1 says how to add some
static lv_obj_t *scr_map;                // the map screen (below: map_create)
static int mp_fav = -1;
static void map_refresh(void);
static void map_leave(void);
static uint32_t mp_opened;                // lv_tick when the map was opened (it fades in for 200 ms)
static void map_path(void);
static void stop_tapped(lv_event_t *e);
static lv_obj_t *s_title, *s_lines, *s_qr, *s_scan;
static lv_obj_t *m_title, *m_body;
static ota_status_t ota_st;              // last forge_ota status (copied under the display lock)

static lv_font_t *mkfont(int px)
{
    // No kerning: it cost 71 % of render time in weather_amoled. 96 cached glyphs per size.
    return lv_tiny_ttf_create_data_ex(ttf_start, ttf_end - ttf_start, px, LV_FONT_KERNING_NONE, 96);
}

static lv_obj_t *base_screen(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s, C_BG, 0);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    return s;
}

// Labels are never clickable: a decorative object that is clickable swallows presses (long-press never fired)
static lv_obj_t *label(lv_obj_t *parent, lv_font_t *f, lv_color_t c, int y, int w)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, w);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_label_set_text(l, "");
    return l;
}

// Set only when it changes: a redraw that changes nothing still costs a frame (and invalidates cached pictures)
static void set_text(lv_obj_t *l, const char *s)
{
    if (strcmp(lv_label_get_text(l), s)) lv_label_set_text(l, s);
}

static lv_obj_t *make_qr(lv_obj_t *parent, int size)
{
    lv_obj_t *qr = lv_qrcode_create(parent);
    lv_qrcode_set_size(qr, size);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_obj_set_style_border_color(qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(qr, 6, 0);
    lv_obj_remove_flag(qr, LV_OBJ_FLAG_CLICKABLE);
    return qr;
}

static void long_pressed(lv_event_t *e)
{
    ESP_LOGI(TAG, "long-press: Wi-Fi setup");
    ui_wifi_setup(NULL);
}

/* ---------- system page ---------- */

static void fmt_uptime(char *out, int n)
{
    int64_t s = esp_timer_get_time() / 1000000;
    if (s < 3600) snprintf(out, n, "%d min", (int)(s / 60));
    else if (s < 86400) snprintf(out, n, "%d h %02d", (int)(s / 3600), (int)(s / 60 % 60));
    else snprintf(out, n, "%d d %d h", (int)(s / 86400), (int)(s / 3600 % 24));
}

static void update_line(char *out, int n)
{
    switch (ota_st.state) {
    case OTA_CHECKING:    snprintf(out, n, "%s", tr(T_UPD_CHECKING)); break;
    case OTA_UP_TO_DATE:  snprintf(out, n, "%s", tr(T_UPD_UP_TO_DATE)); break;
    case OTA_AVAILABLE:   snprintf(out, n, tr(T_UPD_AVAILABLE), ota_st.latest); break;
    case OTA_DOWNLOADING: snprintf(out, n, tr(T_UPD_DOWNLOADING), ota_st.progress); break;
    case OTA_DONE:        snprintf(out, n, "%s", tr(T_UPD_DONE)); break;
    case OTA_FAILED: {
        static const tid_t why[] = { [OTA_E_NO_SITE] = T_OTA_NO_SITE, [OTA_E_BAD_SITE] = T_OTA_BAD_SITE,
            [OTA_E_NO_IMAGE] = T_OTA_NO_IMAGE, [OTA_E_NO_START] = T_OTA_NO_START, [OTA_E_WRONG] = T_OTA_WRONG,
            [OTA_E_INTERRUPTED] = T_OTA_INTERRUPTED, [OTA_E_INVALID] = T_OTA_INVALID };
        int e = ota_st.err;
        snprintf(out, n, tr(T_UPD_FAILED), e > 0 && e <= OTA_E_INVALID ? tr(why[e]) : ota_st.error);
        break;
    }
    default:              snprintf(out, n, "%s", tr(T_UPD_IDLE));
    }
}

static void system_refresh(void)
{
    char lines[400], up[24], upd[96], ip[20] = "-", ssid[NET_SSID_MAX + 1] = "", wifi[80];
    wifi_ap_record_t ap;
    char name[NET_SSID_MAX + 1];
    if (net_is_connected() && net_get_ssid(ssid, sizeof(ssid)) && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        textfit(ssid, name, sizeof(name));                // a network name may hold emoji: the font has none
        snprintf(wifi, sizeof(wifi), tr(T_SYS_WIFI), name, ap.rssi);
    }
    else
        snprintf(wifi, sizeof(wifi), "%s", tr(T_SYS_OFFLINE));
    net_get_ip(ip, sizeof(ip));
    fmt_uptime(up, sizeof(up));
    update_line(upd, sizeof(upd));
    int n = snprintf(lines, sizeof(lines), tr(T_SYS_VERSION), esp_app_get_description()->version);
    n += snprintf(lines + n, sizeof(lines) - n, "\n%s\n", wifi);
    n += snprintf(lines + n, sizeof(lines) - n, tr(T_SYS_IP), ip);
    n += snprintf(lines + n, sizeof(lines) - n, "\n");
    n += snprintf(lines + n, sizeof(lines) - n, tr(T_SYS_MEMORY),
                  (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                  (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    n += snprintf(lines + n, sizeof(lines) - n, "\n");
    n += snprintf(lines + n, sizeof(lines) - n, tr(T_SYS_UPTIME), up);
    snprintf(lines + n, sizeof(lines) - n, "\n%s", upd);
    set_text(s_title, tr(T_SYSTEM));
    set_text(s_lines, lines);
    set_text(s_scan, tr(T_SYS_SCAN));
    static char qr_text[80];                              // the settings page with the key (web.c)
    char url[80];
    if (web_url(url, sizeof(url))) {
        if (strcmp(url, qr_text)) {
            strlcpy(qr_text, url, sizeof(qr_text));
            lv_qrcode_update(s_qr, qr_text, strlen(qr_text));
        }
        lv_obj_remove_flag(s_qr, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_scan, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_qr, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_scan, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------- stop pages ----------
 * One page per favourite: the route and its direction, the stop, the next departure big (minutes, real time or
 * scheduled), the three after it, and how fresh it is. Minutes count down from the departure times between fetches
 * (departures.c fetches the page on view every 30 s). */

typedef struct {
    lv_obj_t *page;                           // its page of the pager (pager_set_order moves pages, not this)
    lv_obj_t *clock, *badge, *route, *dir, *stop, *big, *kind, *next[3], *status, *alert;
} stop_page_t;
static stop_page_t sp[FAVS_MAX];
static lv_obj_t *empty_title, *empty_how;     // page 1 without favourites
static lv_obj_t *al_page;                     // the alerts page: always the last one shown

static lv_obj_t *page_of(int i) { return sp[i].page; }

static void set_color(lv_obj_t *l, lv_color_t c)
{
    if (!lv_color_eq(lv_obj_get_style_text_color(l, 0), c)) lv_obj_set_style_text_color(l, c, 0);
}

static void set_hidden(lv_obj_t *o, bool hide)
{
    if (hide != lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        if (hide) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_strike(lv_obj_t *l, bool on)
{
    lv_text_decor_t d = on ? LV_TEXT_DECOR_STRIKETHROUGH : LV_TEXT_DECOR_NONE;
    if (lv_obj_get_style_text_decor(l, 0) != d) lv_obj_set_style_text_decor(l, d, 0);
}

static void hhmm(time_t t, char *out, int n)
{
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

// "16 min", "< 1 min", or the time itself an hour or more away ("23:45")
static void when(time_t dep, time_t now, char *out, int n)
{
    long s = (long)(dep - now);
    if (s >= 3600) hhmm(dep, out, n);
    else if (s < 60) snprintf(out, n, "%s", tr(T_DEP_NOW));
    else snprintf(out, n, tr(T_DEP_MIN), (int)(s / 60));
}

static void stop_refresh(int i)
{
    stop_page_t *p = &sp[i];
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[96];
    bool clock_set = tm.tm_year > 120;
    if (clock_set) hhmm(now, buf, sizeof(buf));
    set_text(p->clock, clock_set ? buf : "");

    dep_entry_t e;
    if (!deps_get(i, &e)) return;
    const rtc_board_t *b = &e.board;
    set_text(p->route, e.fav.route);
    set_text(p->dir, e.state == DEP_OK ? b->direction : "");
    if (e.state == DEP_OK) {
        char name[64];
        textfit(b->stop_name, name, sizeof(name));
        snprintf(buf, sizeof(buf), "%s  ·  %s", name, e.fav.stop);
    } else snprintf(buf, sizeof(buf), "%s", e.fav.stop);
    set_text(p->stop, buf);

    // The departures still ahead (one that left more than 30 s ago is gone; the next fetch drops it anyway)
    const rtc_dep_t *d[RTC_DEPS_MAX];
    int nd = 0;
    if (e.state == DEP_OK && clock_set)
        for (int k = 0; k < b->n; k++) if (b->dep[k].depart > now - 30) d[nd++] = &b->dep[k];
    // Older than 10 min (offline for a while): the times may be wrong, show none rather than stale ones
    bool stale = e.state == DEP_OK && now - e.fetched > 10 * 60;
    if (stale) nd = 0;

    if (nd > 0) {
        when(d[0]->depart, now, buf, sizeof(buf));
        set_text(p->big, buf);
        set_strike(p->big, d[0]->cancelled);
        set_text(p->kind, tr(d[0]->cancelled ? T_DEP_CANCELLED : d[0]->live ? T_DEP_LIVE : T_DEP_SCHED));
        set_color(p->kind, d[0]->cancelled ? C_BAD : d[0]->live ? C_LIVE : C_DIM);
    } else {
        set_text(p->big, "--");
        set_strike(p->big, false);
        bool none = e.state == DEP_OK && !stale && clock_set && b->n == 0;
        set_text(p->kind, none ? tr(T_DEP_NONE) : "");
        set_color(p->kind, C_DIM);
    }
    for (int k = 0; k < 3; k++) {
        const rtc_dep_t *x = k + 1 < nd ? d[k + 1] : NULL;
        if (x) when(x->depart, now, buf, sizeof(buf));
        set_text(p->next[k], x ? buf : "");
        if (x) set_color(p->next[k], x->cancelled ? C_BAD : x->live ? C_LIVE : C_DIM);
        set_strike(p->next[k], x && x->cancelled);
    }

    // The line at the bottom: what's wrong, else how fresh it is
    lv_color_t sc = C_DIM;
    if (e.state == DEP_NOT_FOUND) { snprintf(buf, sizeof(buf), tr(T_DEP_NOT_FOUND), e.fav.route); sc = C_BAD; }
    else if (e.state == DEP_WAITING) {
        snprintf(buf, sizeof(buf), "%s", e.failing ? tr(T_DEP_OFFLINE) : tr(T_DEP_LOADING));
        if (e.failing) sc = C_BAD;
    }
    else if (e.failing && now - e.fetched > 2 * 60) { snprintf(buf, sizeof(buf), "%s", tr(T_DEP_OFFLINE)); sc = C_BAD; }
    else if (b->not_served) { snprintf(buf, sizeof(buf), "%s", tr(T_DEP_NOT_SERVED)); sc = C_BAD; }
    else if (b->drop_off_only) snprintf(buf, sizeof(buf), "%s", tr(T_DEP_DROP_OFF));
    else {
        char at[8];
        hhmm(e.fetched, at, sizeof(at));
        snprintf(buf, sizeof(buf), tr(T_DEP_UPDATED), at);
    }
    set_text(p->status, buf);
    set_color(p->status, sc);

    int na = deps_alerts_for(i);                          // details on the alerts page, the last one
    if (na == 1) snprintf(buf, sizeof(buf), "%s", tr(T_STOP_ALERT1));
    else if (na > 1) snprintf(buf, sizeof(buf), tr(T_STOP_ALERTS), na);
    set_text(p->alert, na ? buf : "");
}

/* ---------- alerts page ----------
 * RTC's notices for the favourite routes in their directions (departures.c fetches each route's every 10 min): the
 * routes concerned on a badge, the title (orange when RTC marks it urgent), the subtitle, start and end as RTC writes
 * them. A list that scrolls up and down when it doesn't fit; rebuilt only when the alerts change. */

static lv_obj_t *al_title, *al_list, *al_none, *al_status;
static EXT_RAM_BSS_ATTR dep_alert_t al[DEPS_ALERTS_MAX];
static char al_sig[DEPS_ALERTS_MAX * 40 + 8];         // what the list shows now (ids, language)

static lv_obj_t *al_label(lv_obj_t *parent, lv_font_t *f, lv_color_t c, int w)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    return l;
}

static lv_obj_t *al_box(lv_obj_t *parent, lv_flex_flow_t flow)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_flex_flow(o, flow);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    return o;
}

static void alerts_refresh(void)
{
    time_t fetched;
    bool failing;
    int n = deps_alerts(al, DEPS_ALERTS_MAX, &fetched, &failing);
    set_text(al_title, tr(T_ALERTS));

    char sig[sizeof(al_sig)];
    int k = snprintf(sig, sizeof(sig), "%d:", (int)i18n_lang());
    for (int i = 0; i < n && k < (int)sizeof(sig) - 40; i++) k += snprintf(sig + k, sizeof(sig) - k, "%.36s,", al[i].n.id);
    if (strcmp(sig, al_sig)) {                            // rebuild the list
        strlcpy(al_sig, sig, sizeof(al_sig));
        lv_obj_clean(al_list);
        lv_obj_scroll_to_y(al_list, 0, LV_ANIM_OFF);
        for (int i = 0; i < n; i++) {
            const rtc_notice_t *x = &al[i].n;
            lv_obj_t *row = al_box(al_list, LV_FLEX_FLOW_ROW);
            lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_style_pad_column(row, 10, 0);
            lv_obj_set_style_pad_bottom(row, 16, 0);
            lv_obj_t *badge = al_box(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_size(badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(badge, C_ACCENT, 0);
            lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(badge, 6, 0);
            lv_obj_set_style_pad_hor(badge, 6, 0);
            lv_obj_t *r = al_label(badge, f_small, C_BG, LV_SIZE_CONTENT);
            lv_label_set_long_mode(r, LV_LABEL_LONG_CLIP);
            lv_label_set_text(r, al[i].routes);
            lv_obj_t *col = al_box(row, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_grow(col, 1);
            lv_obj_set_height(col, LV_SIZE_CONTENT);
            char t[240], line[120];
            textfit(x->title, t, sizeof(t));
            if (x->subtitle[0]) {
                char s[120];
                textfit(x->subtitle, s, sizeof(s));
                strlcat(t, "\n", sizeof(t));
                strlcat(t, s, sizeof(t));
            }
            lv_label_set_text(al_label(col, f_small, x->urgent ? C_WARN : C_TEXT, lv_pct(100)), t);
            line[0] = 0;
            if (x->begin[0]) snprintf(line, sizeof(line), tr(T_ALERT_BEGIN), x->begin);
            if (x->end[0]) {
                char e[64];
                snprintf(e, sizeof(e), tr(T_ALERT_END), x->end);
                if (line[0]) strlcat(line, "\n", sizeof(line));
                strlcat(line, e, sizeof(line));
            }
            if (line[0]) {
                char lf[120];
                textfit(line, lf, sizeof(lf));
                lv_label_set_text(al_label(col, f_small, C_DIM, lv_pct(100)), lf);
            }
        }
    }
    time_t now = time(NULL);
    set_hidden(al_none, n > 0 || !fetched);
    set_text(al_none, tr(T_ALERTS_NONE));
    char buf[64];
    lv_color_t sc = C_DIM;
    if (failing && (!fetched || now - fetched > 30 * 60)) { snprintf(buf, sizeof(buf), "%s", tr(T_DEP_OFFLINE)); sc = C_BAD; }
    else if (!fetched) snprintf(buf, sizeof(buf), "%s", n_favs ? tr(T_DEP_LOADING) : "");
    else {
        char at[8];
        hhmm(fetched, at, sizeof(at));
        snprintf(buf, sizeof(buf), tr(T_DEP_UPDATED), at);
    }
    set_text(al_status, buf);
    set_color(al_status, sc);
}

static void alerts_create(lv_obj_t *pg)
{
    al_page = pg;
    al_title = label(pg, f_mid, C_ACCENT, 36, 300);
    al_list = lv_obj_create(pg);                          // scrolls up and down (a swipe sideways is the pager's)
    lv_obj_remove_style_all(al_list);
    lv_obj_set_size(al_list, 340, 290);
    lv_obj_align(al_list, LV_ALIGN_TOP_MID, 0, 86);
    lv_obj_set_flex_flow(al_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(al_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(al_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(al_list, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    al_none = label(pg, f_small, C_DIM, 200, 320);
    al_status = label(pg, f_small, C_DIM, 392, 300);
}

static void empty_refresh(void)
{
    bool empty = n_favs == 0;
    lv_obj_t *pg = page_of(0);
    for (uint32_t k = 0; k < lv_obj_get_child_count(pg); k++) {
        lv_obj_t *o = lv_obj_get_child(pg, k);
        set_hidden(o, (o == empty_title || o == empty_how) ? !empty : empty);
    }
    if (empty) {
        set_text(empty_title, tr(T_NO_STOPS));
        set_text(empty_how, tr(T_NO_STOPS_HOW));
    }
}

static void stops_refresh(void)
{
    empty_refresh();
    for (int i = 0; i < n_favs; i++) stop_refresh(i);
    alerts_refresh();
}

// Every second, every page: the ones not shown too, because a swipe shows a picture of a neighbour rendered in the
// background (slide.c, refreshed every 2 s): updated only when shown, a swipe showed stale texts for a moment.
// Changing an off-screen label costs no redraw.
static void tick(lv_timer_t *t)
{
    // A map left by another path (setup, the console) stops its tracking; not one still fading in (lv_screen_active()
    // is the previous screen during its 200 ms load: a tick then closed it as it opened, v0.2.0, harness map_zoom)
    if (mp_fav >= 0 && lv_screen_active() != scr_map && lv_tick_elaps(mp_opened) > 500) map_leave();
    if (lv_screen_active() == scr_map) map_refresh();
    if (lv_screen_active() != scr_main) return;
    stops_refresh();
    system_refresh();
}

static int fav_shown(void)                    // the favourite on view, -1 if none
{
    int p = pager_current(pager);
    return lv_screen_active() == scr_main && p >= 1 && p <= n_favs ? p - 1 : -1;
}

static uint32_t settled_tick;                 // lv_tick of the last page settle (a tap just after is a swipe's tail)

static void page_settled(int page, void *user)
{
    settled_tick = lv_tick_get();
    bool alerts = pager_page(pager, page) == al_page;
    ESP_LOGI(TAG, "page %d (%s)", page, page == 0 ? "system" : alerts ? "alerts" : "stop");
    if (page == 0) system_refresh();
    else if (alerts) alerts_refresh();
    else if (page <= n_favs) stop_refresh(page - 1);
    deps_show(fav_shown());
}

static void stop_create(int i, lv_obj_t *pg)
{
    stop_page_t *p = &sp[i];
    p->page = pg;
    p->clock = label(pg, f_small, C_DIM, 22, 120);
    p->badge = lv_obj_create(pg);                         // the route number on the accent colour
    lv_obj_remove_style_all(p->badge);
    lv_obj_remove_flag(p->badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p->badge, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_color(p->badge, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(p->badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p->badge, 10, 0);
    lv_obj_set_style_pad_hor(p->badge, 14, 0);
    lv_obj_set_style_pad_ver(p->badge, 2, 0);
    lv_obj_set_size(p->badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(p->badge, LV_ALIGN_TOP_MID, 0, 56);
    p->route = lv_label_create(p->badge);
    lv_obj_set_style_text_font(p->route, f_mid, 0);
    lv_obj_set_style_text_color(p->route, C_BG, 0);
    lv_obj_remove_flag(p->route, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(p->route, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_label_set_text(p->route, "");
    p->dir = label(pg, f_small, C_TEXT, 106, 340);
    lv_label_set_long_mode(p->dir, LV_LABEL_LONG_DOT);
    p->stop = label(pg, f_small, C_DIM, 134, 380);
    lv_label_set_long_mode(p->stop, LV_LABEL_LONG_DOT);
    p->big = label(pg, f_big, C_TEXT, 178, 400);
    p->kind = label(pg, f_small, C_DIM, 256, 360);
    for (int k = 0; k < 3; k++) {
        p->next[k] = label(pg, f_mid, C_DIM, 300, 130);
        lv_obj_align(p->next[k], LV_ALIGN_TOP_MID, (k - 1) * 130, 300);
    }
    p->status = label(pg, f_small, C_DIM, 360, 320);
    p->alert = label(pg, f_small, C_WARN, 392, 290);
}

// The pages shown, in order: system, the favourites' (at least one: it says how to add some), the alerts
static void pages_order(void)
{
    lv_obj_t *order[2 + FAVS_MAX];
    int k = 0;
    order[k++] = pager_page(pager, 0);
    for (int i = 0; i < (n_favs ? n_favs : 1); i++) order[k++] = sp[i].page;
    order[k++] = al_page;
    pager_set_order(pager, order, k);
}

static void main_create(void)
{
    scr_main = base_screen();
    pager = pager_create(scr_main, false, 2 + FAVS_MAX, NULL, page_settled, NULL);   // system, stops, alerts
    lv_obj_add_event_cb(pager, long_pressed, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(pager, stop_tapped, LV_EVENT_SHORT_CLICKED, NULL);
    slide_pager(pager);                                   // drags drawn as pictures (~60 fps), not LVGL scrolling
    lv_obj_t *p0 = pager_page(pager, 0);
    s_title = label(p0, f_mid, C_ACCENT, 40, 300);
    s_lines = label(p0, f_small, C_TEXT, 84, 380);
    s_qr = make_qr(p0, 92);
    lv_obj_align(s_qr, LV_ALIGN_TOP_MID, 0, 262);
    s_scan = label(p0, f_small, C_DIM, 372, 300);   // y 372 + 2 lines: still inside the circle
    for (int i = 0; i < FAVS_MAX; i++) stop_create(i, pager_page(pager, 1 + i));
    alerts_create(pager_page(pager, 1 + FAVS_MAX));
    empty_title = label(page_of(0), f_mid, C_ACCENT, 140, 340);
    empty_how = label(page_of(0), f_small, C_TEXT, 200, 360);
    pages_order();                                        // ui_favs_changed() shows as many as there are favourites
    empty_refresh();
    system_refresh();
    lv_timer_create(tick, 1000, NULL);
}

void ui_favs_changed(void)
{
    int n = 0;
    dep_entry_t e;
    while (n < FAVS_MAX && deps_get(n, &e)) n++;
    display_lock(-1);
    n_favs = n;
    pages_order();
    stops_refresh();
    display_unlock();
    deps_show(fav_shown());
    ESP_LOGI(TAG, "%d favourite page(s)", n);
}

void ui_deps_changed(int i)
{
    display_lock(-1);
    if (i == DEPS_CHANGED_TRACE && lv_screen_active() == scr_map) map_path();
    if (i == DEPS_CHANGED_BUSES || (i >= 0 && i == mp_fav)) map_refresh();
    if (i == DEPS_CHANGED_ALERTS) stops_refresh();        // alerts: their page and every stop's line
    else if (i >= 0 && i < n_favs) stop_refresh(i);
    display_unlock();
}

/* ---------- map screen ----------
 * A tap on a stop page: the street map around the stop (map.c: OpenStreetMap tiles, dimmed), the route's path in the
 * favourite's direction (RTC's polylines, all its variants) in blue, the stop at the centre, the route's buses heading
 * that way (departures.c, every 20 s while the map is open) as small green buses, only those inside the round map (the
 * user's choice, 2026-10-07: a bus beyond it isn't shown). Swipe down to zoom in, up to zoom out (as weather_amoled's
 * radar), MAP_ZOOM_MIN..MAP_ZOOM_MAX; until the new zoom's tiles are all there the picture on view is the previous
 * one, scaled. A tap anywhere goes back; after MAP_IDLE_MS without a touch it goes back by itself (no positions are
 * fetched for a map nobody looks at). */

#define MAP_IDLE_MS (5 * 60 * 1000)
#define MAP_TITLE_W 286                                   // the round screen's width around y 50..80, less a margin
#define MAP_VISIBLE 220                                   // a bus farther from the centre than this isn't shown

static lv_obj_t *mp_img, *mp_stop, *mp_top, *mp_bottom, *mp_attrib, *mp_bus[RTC_BUSES_MAX];
static lv_image_dsc_t mp_dsc;
static lv_obj_t *mp_path[DEPS_TRACE_VARIANTS];            // the route's path: one line per variant
static lv_point_precise_t *mp_pts;                         // their points on the screen (PSRAM)
static float *mp_ll;                                       // ...as deps_trace() gives them (lat, lon; PSRAM)
#define PATH_STEP 3                                        // a point closer than this to the last one drawn is skipped
// mp_fav (top of the file): the favourite whose map is open, -1 if none
static bool mp_view_set;                                  // the picture for its stop is chosen (map_show)
static map_view_t mp_view;                                // the zoom asked for (markers and path use it)
static map_view_t mp_shown;                               // the picture on view: mp_view's, or the previous zoom's, scaled
static int mp_zoom = MAP_ZOOM;                            // the last zoom chosen (kept while the device runs)
static lv_timer_t *mp_idle;

static lv_obj_t *pill(lv_obj_t *parent, lv_font_t *f, int y, int w)
{
    lv_obj_t *l = label(parent, f, C_TEXT, y, w);
    lv_obj_set_width(l, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(l, w, 0);
    lv_obj_set_style_bg_color(l, C_BG, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_70, 0);
    lv_obj_set_style_radius(l, 12, 0);
    lv_obj_set_style_pad_hor(l, 12, 0);
    lv_obj_set_style_pad_ver(l, 4, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
    return l;
}

// A small bus seen from the front: green body, dark windshield and bumper, a dark outline that keeps it visible on the
// map (Montserrat and LVGL's symbols have no bus)
static lv_obj_t *bus_icon(lv_obj_t *parent)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 22, 26);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_bg_color(b, C_LIVE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, C_BG, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_HIDDEN);
    static const struct { int x, y, w, h, r; } parts[] = {
        { 3, 3, 12, 8, 2 },                                // windshield (inside the 2 px outline: 18 x 22)
        { 2, 15, 4, 3, 1 }, { 12, 15, 4, 3, 1 },           // headlights
    };
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        lv_obj_t *p = lv_obj_create(b);
        lv_obj_remove_style_all(p);
        lv_obj_set_pos(p, parts[i].x, parts[i].y);
        lv_obj_set_size(p, parts[i].w, parts[i].h);
        lv_obj_set_style_radius(p, parts[i].r, 0);
        lv_obj_set_style_bg_color(p, i ? lv_color_hex(0xFFF2B0) : lv_color_hex(0x16323A), 0);
        lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
        lv_obj_remove_flag(p, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    return b;
}

static lv_obj_t *dot(lv_obj_t *parent, int size)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, size, size);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_HIDDEN);
    return o;
}

// A place's position on the screen, relative to the picture's top-left corner; false without a picture
static bool on_screen(double lat, double lon, double *sx, double *sy)
{
    if (!mp_view_set) return false;
    double x, y;
    geo_world_px(lat, lon, mp_view.zoom, &x, &y);
    *sx = x - mp_view.ox;
    *sy = y - mp_view.oy;
    return true;
}

// Put a picture on view at its own scale
static void show_picture(const map_view_t *v)
{
    mp_shown = *v;
    mp_dsc.data = (const uint8_t *)v->px;
    lv_image_cache_drop(&mp_dsc);
    lv_image_set_src(mp_img, &mp_dsc);
    lv_image_set_scale(mp_img, LV_SCALE_NONE);
    lv_obj_invalidate(mp_img);
}

static void place_dot(lv_obj_t *o, double sx, double sy)
{
    lv_obj_set_pos(o, (int)lround(sx) - lv_obj_get_style_width(o, 0) / 2, (int)lround(sy) - lv_obj_get_style_height(o, 0) / 2);
}

// The route's path on the picture (deps_trace: its variants), when both are there
static void map_path(void)
{
    int len[DEPS_TRACE_VARIANTS], nv = mp_view_set && mp_pts && mp_ll ? deps_trace(mp_ll, DEPS_TRACE_POINTS, len) : 0;
    int src = 0, dst = 0;
    for (int v = 0; v < DEPS_TRACE_VARIANTS; v++) {
        if (v >= nv) { set_hidden(mp_path[v], true); continue; }
        int first = dst;
        for (int k = 0; k < len[v]; k++, src++) {
            double sx = 0, sy = 0;
            on_screen(mp_ll[2 * src], mp_ll[2 * src + 1], &sx, &sy);   // (mp_view_set: always true here)
            bool last = k == len[v] - 1;
            if (dst > first && !last && fabs(sx - mp_pts[dst - 1].x) < PATH_STEP && fabs(sy - mp_pts[dst - 1].y) < PATH_STEP)
                continue;
            mp_pts[dst].x = (lv_value_precise_t)sx;
            mp_pts[dst].y = (lv_value_precise_t)sy;
            dst++;
        }
        lv_line_set_points(mp_path[v], mp_pts + first, dst - first);
        set_hidden(mp_path[v], dst - first < 2);
    }
}

static void map_refresh(void)
{
    if (mp_fav < 0) return;
    dep_entry_t e;
    if (!deps_get(mp_fav, &e)) return;
    char buf[96];
    // The picture: once the stop's place is known (its first departures reply)
    if (!mp_view_set && e.state == DEP_OK && (e.board.lat || e.board.lon)) {
        map_show(e.board.lat, e.board.lon, mp_zoom, &mp_view);
        mp_view_set = true;
        show_picture(&mp_view);
        lv_obj_remove_flag(mp_img, LV_OBJ_FLAG_HIDDEN);
        map_path();                                       // a path already fetched (the same route's map again)
    }
    if (mp_view_set) {
        map_view_t now;
        map_status(&now);
        if (now.px == mp_view.px) {
            mp_view = now;
            if (mp_shown.px != mp_view.px) {
                // A new zoom: its picture replaces the scaled one once complete (or once loading gave up)
                if (mp_view.state != MAP_LOADING) show_picture(&mp_view);
            } else if (now.done != mp_shown.done || now.state != mp_shown.state) {
                mp_shown = now;
                lv_image_cache_drop(&mp_dsc);
                lv_obj_invalidate(mp_img);
            }
        }
    }

    snprintf(buf, sizeof(buf), "%s  %s", e.fav.route, e.state == DEP_OK ? e.board.direction : "");
    if (strcmp(lv_label_get_text(mp_top), buf)) {
        // The text's own width, at most MAP_TITLE_W (then it wraps): a label sized to its content can't wrap
        lv_point_t sz;
        lv_text_get_size(&sz, buf, f_small, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_obj_set_width(mp_top, LV_MIN(sz.x + 1, MAP_TITLE_W - 24) + 24);   // + the pill's padding
        lv_label_set_text(mp_top, buf);
    }

    double sx, sy;
    bool stop = e.state == DEP_OK && on_screen(e.board.lat, e.board.lon, &sx, &sy);
    set_hidden(mp_stop, !stop);
    if (stop) place_dot(mp_stop, sx, sy);

    static rtc_bus_t bus[RTC_BUSES_MAX];
    time_t fetched;
    bool failing;
    int nb = deps_buses(bus, RTC_BUSES_MAX, &fetched, &failing);
    for (int k = 0; k < RTC_BUSES_MAX; k++) {             // only the buses inside the round map
        bool show = k < nb && on_screen(bus[k].lat, bus[k].lon, &sx, &sy)
                    && hypot(sx - MAP_SIZE / 2, sy - MAP_SIZE / 2) <= MAP_VISIBLE;
        set_hidden(mp_bus[k], !show);
        if (show) place_dot(mp_bus[k], sx, sy);
    }

    // The bottom line: the map or the RTC failing, else the next bus
    time_t t = time(NULL);
    lv_color_t c = C_TEXT;
    if (mp_view_set && mp_view.state == MAP_FAILED && mp_view.done == 0) { snprintf(buf, sizeof(buf), "%s", tr(T_MAP_NO_TILES)); c = C_BAD; }
    else if (!mp_view_set || (mp_view.state == MAP_LOADING && mp_view.done == 0)) snprintf(buf, sizeof(buf), "%s", tr(T_MAP_LOADING));
    else if (failing && (!fetched || t - fetched > 2 * 60)) { snprintf(buf, sizeof(buf), "%s", tr(T_DEP_OFFLINE)); c = C_BAD; }
    else if (fetched && nb == 0) snprintf(buf, sizeof(buf), "%s", tr(T_MAP_NO_BUS));
    else {
        const rtc_dep_t *d = NULL;
        for (int k = 0; k < e.board.n && !d && e.state == DEP_OK; k++) if (e.board.dep[k].depart > t - 30) d = &e.board.dep[k];
        if (d) {
            char w[24];
            when(d->depart, t, w, sizeof(w));
            snprintf(buf, sizeof(buf), tr(T_MAP_NEXT), w);
        } else buf[0] = 0;
    }
    set_text(mp_bottom, buf);
    set_color(mp_bottom, c);
    set_hidden(mp_bottom, !buf[0]);
}

// Swipe down: zoom in, up: zoom out (as weather_amoled's radar). Markers and path move to the new zoom at once; the
// picture stays the previous one, scaled about the stop, until the new zoom's tiles are there.
static void map_zoom(int step)
{
    int z = mp_zoom + step;
    if (z < MAP_ZOOM_MIN || z > MAP_ZOOM_MAX || mp_fav < 0) return;
    mp_zoom = z;
    if (mp_idle) lv_timer_reset(mp_idle);                 // a touch: the map stays
    if (!mp_view_set) return;                             // the first picture will take the new zoom
    dep_entry_t e;
    if (!deps_get(mp_fav, &e) || e.state != DEP_OK) return;
    ESP_LOGI(TAG, "map zoom %d", z);
    map_show(e.board.lat, e.board.lon, z, &mp_view);
    if (mp_view.state != MAP_LOADING) show_picture(&mp_view);
    else {                                                // the previous picture, scaled, meanwhile
        int scale = (int)lround(256 * pow(2, mp_view.zoom - mp_shown.zoom));
        lv_image_set_scale(mp_img, scale < 32 ? 32 : scale > 2048 ? 2048 : scale);
    }
    map_path();
    map_refresh();
}

static void map_gesture(lv_event_t *e)
{
    lv_indev_t *in = lv_indev_active();
    lv_dir_t dir = in ? lv_indev_get_gesture_dir(in) : LV_DIR_NONE;
    if (dir == LV_DIR_BOTTOM) map_zoom(+1);
    else if (dir == LV_DIR_TOP) map_zoom(-1);
    if (in) lv_indev_wait_release(in);                    // its release isn't a tap (that closes the map)
}

static void map_leave(void)
{
    if (mp_fav < 0) return;
    ESP_LOGI(TAG, "map closed");
    mp_fav = -1;
    deps_track(-1);
    if (mp_idle) { lv_timer_delete(mp_idle); mp_idle = NULL; }
}

static void map_close(void)
{
    map_leave();
    lv_screen_load_anim(scr_main, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
    deps_show(fav_shown());
}

static void map_idle(lv_timer_t *t) { if (lv_screen_active() == scr_map) map_close(); }
static void map_tapped(lv_event_t *e) { map_close(); }

static void map_open(int i)
{
    if (i < 0 || i >= n_favs) return;
    ESP_LOGI(TAG, "map of favourite %d", i);
    mp_fav = i;
    mp_opened = lv_tick_get();
    mp_view_set = false;
    lv_obj_add_flag(mp_img, LV_OBJ_FLAG_HIDDEN);
    for (int v = 0; v < DEPS_TRACE_VARIANTS; v++) lv_obj_add_flag(mp_path[v], LV_OBJ_FLAG_HIDDEN);
    deps_track(i);
    deps_show(i);                                         // its departures go on: the next bus at the bottom
    map_refresh();
    if (mp_idle) lv_timer_delete(mp_idle);
    mp_idle = lv_timer_create(map_idle, MAP_IDLE_MS, NULL);
    lv_timer_set_repeat_count(mp_idle, 1);
    lv_screen_load_anim(scr_map, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
    lv_indev_t *in = lv_indev_active();
    if (in) lv_indev_wait_release(in);                    // the tap that opened it doesn't also close it
}

// A tap on a stop page opens its map (a drag is slide.c's, a long-press is Wi-Fi setup). Not within 600 ms of a page
// settling: a second quick swipe's press can reach LVGL as a tap (harness quick_swipes, 2026-10-07: it opened the map)
#define TAP_AFTER_SWIPE_MS 600
static void stop_tapped(lv_event_t *e)
{
    if (slide_busy() || lv_tick_elaps(settled_tick) < TAP_AFTER_SWIPE_MS) {
        ESP_LOGI(TAG, "tap %u ms after a swipe: not a tap", (unsigned)lv_tick_elaps(settled_tick));
        return;
    }
    int i = fav_shown();
    if (i >= 0) map_open(i);
}

static void map_updated(void)                             // map.c's task: a tile was drawn
{
    display_lock(-1);
    if (lv_screen_active() == scr_map) map_refresh();
    display_unlock();
}

static void map_create(void)
{
    scr_map = base_screen();
    lv_obj_add_flag(scr_map, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr_map, map_tapped, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(scr_map, map_gesture, LV_EVENT_GESTURE, NULL);
    mp_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    mp_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    mp_dsc.header.w = MAP_SIZE;
    mp_dsc.header.h = MAP_SIZE;
    mp_dsc.header.stride = MAP_SIZE * 2;
    mp_dsc.data_size = MAP_SIZE * MAP_SIZE * 2;
    mp_img = lv_image_create(scr_map);
    lv_obj_set_pos(mp_img, 0, 0);
    lv_image_set_pivot(mp_img, MAP_SIZE / 2, MAP_SIZE / 2);   // a zoom's stand-in scales about the stop
    lv_obj_remove_flag(mp_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(mp_img, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_HIDDEN);
    mp_pts = heap_caps_malloc(DEPS_TRACE_POINTS * sizeof(lv_point_precise_t), MALLOC_CAP_SPIRAM);
    mp_ll = heap_caps_malloc(2 * DEPS_TRACE_POINTS * sizeof(float), MALLOC_CAP_SPIRAM);
    for (int v = 0; v < DEPS_TRACE_VARIANTS; v++) {       // the route's path, under everything else
        mp_path[v] = lv_line_create(scr_map);
        lv_obj_set_style_line_color(mp_path[v], C_ACCENT, 0);
        lv_obj_set_style_line_width(mp_path[v], 6, 0);
        lv_obj_set_style_line_rounded(mp_path[v], true, 0);
        lv_obj_set_style_line_opa(mp_path[v], LV_OPA_80, 0);
        lv_obj_remove_flag(mp_path[v], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(mp_path[v], LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_HIDDEN);
    }
    // The route and direction: low enough for the round screen to be ~290 px wide there (at y 26 it is ~210 px and cut
    // "800  Colline Parlementaire"); a longer one wraps to a second line
    mp_top = pill(scr_map, f_small, 50, MAP_TITLE_W);
    lv_label_set_long_mode(mp_top, LV_LABEL_LONG_WRAP);
    mp_bottom = pill(scr_map, f_small, 372, 300);
    mp_attrib = pill(scr_map, f_tiny, 410, 200);           // OSM's licence asks for it on the map; backed: the path
    lv_obj_set_style_text_color(mp_attrib, C_DIM, 0);      // may cross it
    lv_obj_set_style_pad_ver(mp_attrib, 1, 0);
    lv_label_set_text(mp_attrib, "© OpenStreetMap");
    // The markers above the texts: a bus on the edge is never hidden by them
    mp_stop = dot(scr_map, 22);                           // the stop: a blue dot in a white ring
    lv_obj_set_style_border_color(mp_stop, C_TEXT, 0);
    lv_obj_set_style_border_width(mp_stop, 4, 0);
    lv_obj_set_style_bg_color(mp_stop, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(mp_stop, LV_OPA_COVER, 0);
    for (int k = 0; k < RTC_BUSES_MAX; k++) mp_bus[k] = bus_icon(scr_map);   // the buses
}

/* ---------- message screen (start-up) ---------- */

static void msg_create(void)
{
    scr_msg = base_screen();
    m_title = label(scr_msg, f_mid, C_ACCENT, 120, 320);
    m_body = label(scr_msg, f_small, C_TEXT, 180, 340);
    lv_obj_add_flag(scr_msg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr_msg, long_pressed, LV_EVENT_LONG_PRESSED, NULL);
}

void ui_message(const char *title, const char *body)
{
    static char fitted[200];                              // network names in it may hold emoji (the font has none)
    textfit(body, fitted, sizeof(fitted));
    display_lock(-1);
    set_text(m_title, title);
    set_text(m_body, fitted);
    if (lv_screen_active() != scr_msg && lv_screen_active() != scr_setup) lv_screen_load(scr_msg);
    display_unlock();
}

void ui_home(void)
{
    display_lock(-1);
    stops_refresh();
    pager_go(pager, 1, false);
    if (lv_screen_active() != scr_main) lv_screen_load_anim(scr_main, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
    display_unlock();
    deps_show(fav_shown());
}

/* ---------- Wi-Fi setup ----------
 * Page 1: the setup network (this device's own access point and captive portal): scan to join, the settings page
 * opens by itself. Page 2 (Android 10+): Wi-Fi Easy Connect (DPP). The phone scans this QR code and sends the
 * network it's connected to, password included. The setup network stays up on page 2: it holds the radio on Easy
 * Connect's channel (forge_net's dpp_hold_channel). The two pages are a pager like stop | system, so they follow the finger
 * (slide.c); each page has all its own objects (title, note, QR code, text, dots): a drag's picture of the page coming
 * in holds only that page. */

typedef struct { lv_obj_t *title, *note, *qr, *body; } su_page_t;
static su_page_t su[2];
static lv_obj_t *su_pager;
static int su_page;
static bool su_can_close;           // a tap closes it (not in first-time setup: there is no saved network)
static volatile bool su_open;
static lv_timer_t *su_timer;
static char su_note_text[96];
static char su_ap_qr[96];           // "WIFI:T:WPA;S:<setup SSID>;P:<this device's password>;;"

/* Easy Connect's code exists ~2 s after the page settles (a channel scan first, LESSONS L166). Until then the page
 * shows a placeholder code of the same size and density, faint and low-contrast ("loading"), so nothing pops in and a
 * drag's picture of the page already has it; the real code replaces it and fades up (the user found the pop-in janky).
 * LVGL 9.2 has no blur filter: low opacity and grey modules stand in for it. */
#define QR_FAINT LV_OPA_30
// As long as a real DPP URI (~100 characters): the same module count. Plain text, not a DPP URI: a phone that scans
// the faint placeholder gets a harmless message, not a broken Easy Connect link.
static const char QR_PLACEHOLDER[] =
    "Wait a moment: the Easy Connect code is being made. Scan again once it is bright, not faint........";
static bool qr_real;                                 // su[1].qr holds the real code

static void qr_placeholder(void)
{
    lv_qrcode_set_dark_color(su[1].qr, lv_color_hex(0x606060));
    lv_qrcode_update(su[1].qr, QR_PLACEHOLDER, strlen(QR_PLACEHOLDER));
    lv_obj_set_style_opa(su[1].qr, QR_FAINT, 0);
    lv_obj_remove_flag(su[1].qr, LV_OBJ_FLAG_HIDDEN);
    qr_real = false;
}

static void qr_opa(void *obj, int32_t v) { lv_obj_set_style_opa(obj, v, 0); }

// Easy Connect callbacks (system event task: take the display lock)
static void su_dpp_uri(const char *uri)
{
    display_lock(-1);
    lv_qrcode_set_dark_color(su[1].qr, lv_color_black());
    lv_qrcode_update(su[1].qr, uri, strlen(uri));
    lv_obj_remove_flag(su[1].qr, LV_OBJ_FLAG_HIDDEN);
    qr_real = true;
    lv_anim_t a;                                     // faint placeholder -> the real code (a 140 px square: cheap)
    lv_anim_init(&a);
    lv_anim_set_var(&a, su[1].qr);
    lv_anim_set_values(&a, QR_FAINT, LV_OPA_COVER);
    lv_anim_set_time(&a, 300);
    lv_anim_set_exec_cb(&a, qr_opa);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    display_unlock();
}

static void su_dpp_done(bool ok, const char *ssid)
{
    display_lock(-1);
    if (ok) {
        char name[NET_SSID_MAX + 1];
        textfit(ssid, name, sizeof(name));
        lv_label_set_text(su[1].title, tr(T_WIFI_RECEIVED));
        lv_label_set_text_fmt(su[1].body, tr(T_WIFI_GOT), name);
        lv_obj_add_flag(su[1].qr, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(su[1].body, tr(T_WIFI_DPP_FAIL));
    }
    display_unlock();
}

static void su_texts(void)                          // both pages' texts (snapshots use this without starting anything)
{
    lv_label_set_text(su[0].title, tr(T_WIFI_SETUP));
    snprintf(su_ap_qr, sizeof(su_ap_qr), "WIFI:T:WPA;S:" SETUP_AP_SSID ";P:%s;;", net_setup_ap_pass());
    lv_qrcode_update(su[0].qr, su_ap_qr, strlen(su_ap_qr));
    lv_label_set_text_fmt(su[0].body, tr(T_WIFI_JOIN), SETUP_AP_SSID, net_setup_ap_pass());
    lv_label_set_text(su[1].title, tr(T_WIFI_DPP_TITLE));
    if (!net_dpp_active() || !qr_real) qr_placeholder();   // until the code is generated
    lv_label_set_text(su[1].body, tr(T_WIFI_DPP_HOW));
    const char *note = su_note_text[0] ? su_note_text : !su_can_close ? "" :
                       net_is_connected() ? tr(T_TAP_CANCEL) : tr(T_TAP_RETRY);
    for (int i = 0; i < 2; i++) lv_label_set_text(su[i].note, note);
}

/* The radio work of a page (stopping the other mode, Easy Connect's channel scan: up to a few seconds) runs in its own
 * task: done in the swipe's handler it froze the screen, and the page change looked slow. Only the latest page request
 * counts; stops are never skipped (a stop queued by ui_wifi_setup_close() was replaced by a later request, and
 * main.c then stopped Easy Connect from its own task at the same time: two deinits). RADIO_DPP_OFF wakes
 * ui_wifi_setup_end() when done (weather_amoled v1.13.0). */
static QueueHandle_t su_q;
static SemaphoreHandle_t su_dpp_off;
enum { RADIO_OFF = -1, RADIO_AP = 0, RADIO_DPP = 1, RADIO_DPP_OFF = 2 };

static void su_radio_do(int mode);

static void su_radio_task(void *arg)
{
    int mode, next;
    while (xQueueReceive(su_q, &mode, portMAX_DELAY)) {
        while (xQueueReceive(su_q, &next, 0)) {
            if (mode != RADIO_AP && mode != RADIO_DPP) su_radio_do(mode);   // a stop: done, not replaced
            mode = next;
        }
        su_radio_do(mode);
    }
}

static void su_radio_do(int mode)
{
    if (mode == RADIO_AP) {
        net_dpp_stop();
        net_setup_ap_start();
    } else if (mode == RADIO_DPP) {
        // The setup network stays up: net_dpp_start keeps it on the Easy Connect channel (it holds the radio there)
        if (!net_dpp_start(su_dpp_uri, su_dpp_done)) {
            display_lock(-1);
            lv_label_set_text(su[1].body, tr(T_WIFI_DPP_NONE));
            lv_obj_add_flag(su[1].qr, LV_OBJ_FLAG_HIDDEN);   // no code is coming: no placeholder either
            display_unlock();
        }
    } else if (mode == RADIO_DPP_OFF) {
        net_dpp_stop();
        xSemaphoreGive(su_dpp_off);
    } else {
        net_dpp_stop();
        net_setup_ap_stop();
    }
}

static void su_radio(int mode) { if (su_q) xQueueSend(su_q, &mode, 0); }

static void su_show_page(int page)                  // the radio for that page (in its task)
{
    if (page == 0 && qr_real) qr_placeholder();      // Easy Connect stops: its next code will be a new one
    su_page = page;
    su_radio(page ? RADIO_DPP : RADIO_AP);
    ESP_LOGI(TAG, "Wi-Fi setup page %d (%s)", page, page ? "Easy Connect" : "setup network");
}

static void su_settled(int page, void *user) { if (su_open && page != su_page) su_show_page(page); }

// Leaving setup by any path stops what it started: the setup network pauses the saved network's retries, so one
// left open behind another screen kept the device offline (the harness's "screen hello" after "screen setup")
static void su_leave(void)
{
    if (!su_open) return;
    ESP_LOGI(TAG, "Wi-Fi setup closed");
    su_open = false;
    su_note_text[0] = 0;                // it was for that opening (and in that opening's language)
    if (su_timer) { lv_timer_delete(su_timer); su_timer = NULL; }
    su_radio(RADIO_OFF);
}

static void su_close(void)
{
    su_leave();
    lv_screen_load_anim(scr_main, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
}

// Online: close after 10 min. Offline: close after 5 idle min so the saved network is tried again (setup pauses
// those attempts, and the router may just have been rebooting); main.c reopens setup if it still fails.
static void su_timeout(lv_timer_t *t)
{
    if (!su_can_close) return;                          // first-time setup stays
    if (!net_is_connected() && net_ap_clients() > 0) return;   // a phone is on the setup network
    su_close();
}

static void su_tap(lv_event_t *e)
{
    if (su_can_close) su_close();
}

static void setup_create(void)
{
    scr_setup = base_screen();
    lv_obj_add_flag(scr_setup, LV_OBJ_FLAG_CLICKABLE);
    su_pager = pager_create(scr_setup, false, 2, NULL, su_settled, NULL);
    for (int p = 0; p < 2; p++) {
        lv_obj_t *pg = pager_page(su_pager, p);
        su[p].title = label(pg, f_mid, C_ACCENT, 40, 300);
        su[p].note = label(pg, f_small, C_DIM, 76, 330);   // up to 2 lines (T_CANT_REACH)
        su[p].qr = make_qr(pg, 140);
        lv_obj_align(su[p].qr, LV_ALIGN_TOP_MID, 0, 132);
        su[p].body = label(pg, f_small, C_TEXT, 292, 360);
        for (int i = 0; i < 2; i++) {                     // the dots, on each page: this one's is long
            lv_obj_t *d = lv_obj_create(pg);
            lv_obj_remove_style_all(d);
            lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_radius(d, 4, 0);
            lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
            lv_obj_set_size(d, i == p ? 18 : 7, 7);
            lv_obj_set_style_bg_color(d, i == p ? C_TEXT : C_DIM, 0);
            lv_obj_align(d, LV_ALIGN_BOTTOM_MID, i == 0 ? -10 : 10, -16);
        }
    }
    slide_pager(su_pager);                                 // drags follow the finger (pictures, ~60 fps)
    lv_obj_add_event_cb(scr_setup, su_tap, LV_EVENT_SHORT_CLICKED, NULL);
}

void ui_wifi_setup(const char *note)
{
    display_lock(-1);
    if (note) strlcpy(su_note_text, note, sizeof(su_note_text));
    else su_note_text[0] = 0;
    su_can_close = !net_in_portal();
    su_open = true;
    su_texts();
    pager_go(su_pager, 0, false);
    su_show_page(0);
    if (su_timer) lv_timer_delete(su_timer);
    su_timer = lv_timer_create(su_timeout, (net_is_connected() ? 10 : 5) * 60 * 1000, NULL);
    if (lv_screen_active() != scr_setup) lv_screen_load(scr_setup);
    lv_indev_t *in = lv_indev_active();
    if (in) lv_indev_wait_release(in);                              // the long-press isn't also a tap
    display_unlock();
}

bool ui_wifi_setup_open(void) { return su_open; }

bool ui_wifi_setup_close(void)
{
    display_lock(-1);
    bool close = su_open && su_can_close && net_ap_clients() == 0;
    if (close) su_close();
    display_unlock();
    return close;
}

void ui_wifi_setup_end(void)
{
    display_lock(-1);
    if (su_timer) { lv_timer_delete(su_timer); su_timer = NULL; }
    display_unlock();
    int mode = RADIO_DPP_OFF;                       // through the radio task: never at the same time as its own stop
    xSemaphoreTake(su_dpp_off, 0);
    if (su_q && xQueueSend(su_q, &mode, pdMS_TO_TICKS(1000)) == pdTRUE) xSemaphoreTake(su_dpp_off, pdMS_TO_TICKS(10000));
}

/* ---------- updates, language ---------- */

void ui_ota(const ota_status_t *st)
{
    display_lock(-1);
    ota_st = *st;
    if (lv_screen_active() == scr_main && pager_current(pager) == 0) system_refresh();
    display_unlock();
}

void ui_texts_changed(void)
{
    display_lock(-1);
    stops_refresh();
    system_refresh();
    if (su_open) su_texts();
    display_unlock();
}

/* ---------- screen registry (test console, snapshots) ---------- */

static lv_obj_t *get_stop(void) { return page_of(0); }
static lv_obj_t *get_system(void) { return pager_page(pager, 0); }
static lv_obj_t *get_setup(void) { return pager_page(su_pager, 0); }
static lv_obj_t *get_setup1(void) { return pager_page(su_pager, 1); }
static lv_obj_t *get_msg(void) { return scr_msg; }
static void show_stop(void)
{
    su_leave();
    stops_refresh();
    pager_go(pager, 1, false);
    lv_screen_load(scr_main);
    deps_show(fav_shown());
}
static void show_system(void)
{
    su_leave();
    system_refresh();
    pager_go(pager, 0, false);
    lv_screen_load(scr_main);
    deps_show(-1);
}
static void show_setup(void) { ui_wifi_setup(NULL); }
static bool shown_stop(void) { return lv_screen_active() == scr_main && pager_current(pager) == 1; }
static bool shown_system(void) { return lv_screen_active() == scr_main && pager_current(pager) == 0; }
static bool shown_setup(void) { return lv_screen_active() == scr_setup && pager_current(su_pager) == 0; }
static bool shown_setup1(void) { return lv_screen_active() == scr_setup && pager_current(su_pager) == 1; }
static void show_setup1(void) { ui_wifi_setup(NULL); pager_switch(su_pager, 1); }
static bool shown_msg(void) { return lv_screen_active() == scr_msg; }
static void prep_setup(void)                         // texts only: no access point or Easy Connect is started
{
    if (su_open) return;
    su_can_close = !net_in_portal();
    su_texts();
}

// The other stops' pages: "stop2".."stop8" (page 1 + N - 1), for the harness's swipes and snapshots. A page not in use
// (fewer favourites) is never shown and can't be shown.
#define STOP_N(n) \
    static lv_obj_t *get_stop##n(void) { return page_of(n - 1); } \
    static bool shown_stop##n(void) { return lv_screen_active() == scr_main && pager_current(pager) == n; } \
    static void show_stop##n(void) \
    { \
        if (n_favs < n) return; \
        su_leave(); \
        stops_refresh(); \
        pager_go(pager, n, false); \
        lv_screen_load(scr_main); \
        deps_show(fav_shown()); \
    }
STOP_N(2) STOP_N(3) STOP_N(4) STOP_N(5) STOP_N(6) STOP_N(7) STOP_N(8)
#define STOP_DEF(n) { "stop" #n, get_stop##n, show_stop##n, stops_refresh, shown_stop##n }

static lv_obj_t *get_alerts(void) { return al_page; }
static bool shown_alerts(void)
{
    return lv_screen_active() == scr_main && pager_page(pager, pager_current(pager)) == al_page;
}
static void show_alerts(void)
{
    su_leave();
    alerts_refresh();
    pager_go(pager, pager_index(pager, al_page), false);
    lv_screen_load(scr_main);
    deps_show(-1);
}
_Static_assert(FAVS_MAX == 8, "one STOP_N per favourite page");

static lv_obj_t *get_map(void) { return scr_map; }
static bool shown_map(void) { return lv_screen_active() == scr_map; }
static void show_map(void) { su_leave(); map_open(fav_shown() >= 0 ? fav_shown() : 0); }   // the stop shown, else the first

static const screen_def_t screens[] = {
    { "stop",    get_stop,   show_stop,   stops_refresh,  shown_stop },
    { "system",  get_system, show_system, system_refresh, shown_system },
    { "setup",   get_setup,  show_setup,  prep_setup,     shown_setup },
    { "setup1",  get_setup1, show_setup1, prep_setup,     shown_setup1 },
    STOP_DEF(2), STOP_DEF(3), STOP_DEF(4), STOP_DEF(5), STOP_DEF(6), STOP_DEF(7), STOP_DEF(8),
    { "alerts",  get_alerts, show_alerts, stops_refresh,  shown_alerts },
    { "map",     get_map,    show_map,    map_refresh,    shown_map },
    { "message", get_msg,    NULL,        NULL,           shown_msg },
};

void ui_init(void)
{
    textfit_init(ttf_start, ttf_end - ttf_start);
    su_q = xQueueCreate(4, sizeof(int));
    su_dpp_off = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(su_radio_task, "setup_radio", 4096, NULL, 3, NULL, 0);   // internal RAM: NVS writes
    display_lock(-1);
    f_big = mkfont(64);
    f_mid = mkfont(30);
    f_small = mkfont(20);
    f_tiny = mkfont(14);
    main_create();
    map_create();
    msg_create();
    setup_create();
    screens_register(screens, sizeof(screens) / sizeof(screens[0]));
    lv_screen_load(scr_msg);
    display_unlock();
    web_set_snapshot(screens_snapshot, screens_snapshot_free);
    map_init(map_updated);
}
