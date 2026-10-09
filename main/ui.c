// The screens (see ui.h): the main screen (a row alerts | stops | map, the favourite stops in a column), Settings,
// Wi-Fi setup, messages.
#include "ui.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_timer.h"
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
#include "forge_settings.h"

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
static lv_obj_t *scr_main, *scr_msg, *scr_setup;
// The main screen: a row of three pages (alerts | stops | map), the middle one holding a column of stop pages
static lv_obj_t *row, *stops;            // the pagers: row horizontal, stops vertical (on row's middle page)
static lv_obj_t *al_page, *st_page, *mp_page;   // row's pages
static int n_favs;                       // stop pages shown (0: the first one says how to add some)
static int mp_fav = -1;                  // the favourite the map page is about (the stop on view), -1 none
static bool mp_live;                     // the map is on view: its buses are fetched (deps_track)
static void map_refresh(void);
static void map_leave(void);
static void map_path(void);
static lv_obj_t *m_title, *m_body;

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

static bool finger_down(void)
{
    lv_indev_t *in = lv_indev_get_next(NULL);
    return in && lv_indev_get_state(in) == LV_INDEV_STATE_PRESSED;
}

// Where the finger came down on the main screen (long_pressed)
static lv_point_t pressed_at;
static void pressed(lv_event_t *e)
{
    lv_indev_t *in = lv_indev_active();
    if (in) lv_indev_get_point(in, &pressed_at);
}

// A long press anywhere on the main screen: Settings (forge_settings); offline, Wi-Fi setup is what's needed (as
// weather_amoled). Only a finger that stayed put: LVGL fires it for a press held 400 ms however far it moved, and a
// swipe on the map whose first reads came late (LVGL busy redrawing the map as its tiles arrived) was never a drag
// and opened Settings (harness row_alerts_stop_map, v0.4.0-nav.4).
#define HOLD_PX 24
static void long_pressed(lv_event_t *e)
{
    lv_indev_t *in = lv_indev_active();
    lv_point_t now;
    if (in) {
        lv_indev_get_point(in, &now);
        int dx = now.x - pressed_at.x, dy = now.y - pressed_at.y;
        if (dx * dx + dy * dy > HOLD_PX * HOLD_PX) {
            ESP_LOGI(TAG, "long-press after the finger moved %d,%d: not a hold", dx, dy);
            return;
        }
    }
    if (net_is_connected()) {
        ESP_LOGI(TAG, "long-press: settings");
        settings_open();
        return;
    }
    ESP_LOGI(TAG, "long-press while offline: Wi-Fi setup");
    ui_wifi_setup(NULL);
}

static void msg_long_pressed(lv_event_t *e)          // the start-up message: Wi-Fi setup
{
    ESP_LOGI(TAG, "long-press: Wi-Fi setup");
    ui_wifi_setup(NULL);
}

/* ---------- dots ----------
 * Where you are, drawn on each page (a drag's picture of a page holds its own dots): the row's three pages along the
 * bottom (alerts, stops, map), the stops down the right edge. This page's dot is the long one. */

#define DOT 7
#define DOT_LONG 18
#define DOT_GAP 5
static void set_hidden(lv_obj_t *o, bool hide);

static void dot_style(lv_obj_t *d, bool on, bool vertical)
{
    lv_obj_set_size(d, vertical ? DOT : (on ? DOT_LONG : DOT), vertical ? (on ? DOT_LONG : DOT) : DOT);
    lv_obj_set_style_bg_color(d, on ? C_TEXT : C_DIM, 0);
}

static void dots_create(lv_obj_t *page, lv_obj_t **d, int n)
{
    for (int i = 0; i < n; i++) {
        d[i] = lv_obj_create(page);
        lv_obj_remove_style_all(d[i]);
        lv_obj_remove_flag(d[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(d[i], LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_set_style_radius(d[i], DOT / 2 + 1, 0);
        lv_obj_set_style_bg_opa(d[i], LV_OPA_COVER, 0);
    }
}

// n dots shown (none if fewer than 2), number `on` long; along the bottom, or down the right edge
static void dots_layout(lv_obj_t **d, int max, int n, int on, bool vertical)
{
    int len = n > 1 ? (n - 1) * (DOT + DOT_GAP) + DOT_LONG : 0, pos = -len / 2;
    for (int i = 0; i < max; i++) {
        set_hidden(d[i], i >= n || n < 2);
        if (i >= n) continue;
        dot_style(d[i], i == on, vertical);
        int sz = i == on ? DOT_LONG : DOT;
        if (vertical) lv_obj_align(d[i], LV_ALIGN_RIGHT_MID, -14, pos + sz / 2);
        else lv_obj_align(d[i], LV_ALIGN_BOTTOM_MID, pos + sz / 2, -16);
        pos += sz + DOT_GAP;
    }
}

/* ---------- stop pages ----------
 * One page per favourite, in a column (drag up and down): the route and its direction, the stop, the next departure
 * big (minutes, real time or scheduled), the three after it, and how fresh it is. Minutes count down from the
 * departure times between fetches (departures.c fetches the stop on view every 30 s). Swipe right: its alerts; left:
 * its map. */

typedef struct {
    lv_obj_t *page;                           // its page of the stops pager
    lv_obj_t *clock, *badge, *route, *dir, *stop, *big, *kind, *next[3], *status, *alert;
    lv_obj_t *vdot[FAVS_MAX], *hdot[3];       // the stops (right edge), the row (bottom)
} stop_page_t;
static stop_page_t sp[FAVS_MAX];
static lv_obj_t *empty_title, *empty_how;     // the first stop page without favourites

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

    static dep_entry_t e;                                 // (static: see alerts_refresh)
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

    int na = deps_alerts_for(i);                          // details on its alerts page (swipe right)
    if (na == 1) snprintf(buf, sizeof(buf), "%s", tr(T_STOP_ALERT1));
    else if (na > 1) snprintf(buf, sizeof(buf), tr(T_STOP_ALERTS), na);
    set_text(p->alert, na ? buf : "");
}

/* ---------- alerts page ----------
 * The alerts of the stop on view (swipe right from it): RTC's notices for its route in its direction (departures.c
 * fetches each favourite route's every 10 min). The title (orange when RTC marks it urgent), the subtitle, start and
 * end as RTC writes them. A list that scrolls up and down when it doesn't fit; rebuilt only when what it shows changes
 * (another stop, other notices, another language). */

static lv_obj_t *al_title, *al_list, *al_none, *al_status, *al_dot[3];
static EXT_RAM_BSS_ATTR dep_alert_t al[DEPS_ALERTS_MAX];
static char al_sig[DEPS_ALERTS_MAX * 40 + 16];        // what the list shows now (stop, ids, language)
static int fav_on_view(void);

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
    int f = fav_on_view();
    time_t fetched = 0;
    bool failing = false;
    int n = f >= 0 ? deps_alerts(f, al, DEPS_ALERTS_MAX, &fetched, &failing) : 0;
    // Static, not on the stack: this runs in the test console's task too ("screen stop2"), whose stack overflowed
    // with a whole departures board and the signature on it (harness alerts_of_this_stop, v0.4.0-nav.1). The display
    // lock is held: one caller at a time.
    static dep_entry_t e;
    static char sig[sizeof(al_sig)];
    char buf[64];
    snprintf(buf, sizeof(buf), tr(T_ALERTS_ROUTE), f >= 0 && deps_get(f, &e) ? e.fav.route : "");
    set_text(al_title, buf);

    int k = snprintf(sig, sizeof(sig), "%d:%d:", (int)i18n_lang(), f);
    for (int i = 0; i < n && k < (int)sizeof(sig) - 40; i++) k += snprintf(sig + k, sizeof(sig) - k, "%.36s,", al[i].n.id);
    if (strcmp(sig, al_sig)) {                            // rebuild the list
        strlcpy(al_sig, sig, sizeof(al_sig));
        ESP_LOGI(TAG, "alerts of favourite %d: %d", f, n);
        lv_obj_clean(al_list);
        lv_obj_scroll_to_y(al_list, 0, LV_ANIM_OFF);
        for (int i = 0; i < n; i++) {
            const rtc_notice_t *x = &al[i].n;
            lv_obj_t *col = al_box(al_list, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_size(col, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_style_pad_bottom(col, 16, 0);
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
                char en[64];
                snprintf(en, sizeof(en), tr(T_ALERT_END), x->end);
                if (line[0]) strlcat(line, "\n", sizeof(line));
                strlcat(line, en, sizeof(line));
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
    lv_color_t sc = C_DIM;
    if (failing && (!fetched || now - fetched > 30 * 60)) { snprintf(buf, sizeof(buf), "%s", tr(T_DEP_OFFLINE)); sc = C_BAD; }
    else if (!fetched) snprintf(buf, sizeof(buf), "%s", f >= 0 ? tr(T_DEP_LOADING) : "");
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
    al_title = label(pg, f_mid, C_ACCENT, 36, 300);
    lv_label_set_long_mode(al_title, LV_LABEL_LONG_DOT);
    al_list = lv_obj_create(pg);                          // scrolls up and down (a swipe sideways is the row's)
    lv_obj_remove_style_all(al_list);
    lv_obj_set_size(al_list, 340, 290);
    lv_obj_align(al_list, LV_ALIGN_TOP_MID, 0, 86);
    lv_obj_set_flex_flow(al_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(al_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(al_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(al_list, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    al_none = label(pg, f_small, C_DIM, 200, 320);
    al_status = label(pg, f_small, C_DIM, 392, 300);
    dots_create(pg, al_dot, 3);
    dots_layout(al_dot, 3, 3, 0, false);
}

static void empty_refresh(void)
{
    bool empty = n_favs == 0;
    lv_obj_t *pg = page_of(0);
    for (uint32_t k = 0; k < lv_obj_get_child_count(pg); k++) {
        lv_obj_t *o = lv_obj_get_child(pg, k);
        if (o == empty_title || o == empty_how) set_hidden(o, !empty);
        else if (empty) set_hidden(o, true);
    }
    if (empty) {
        set_text(empty_title, tr(T_NO_STOPS));
        set_text(empty_how, tr(T_NO_STOPS_HOW));
    } else {                                              // the page's own objects back (dots: their layout's)
        for (uint32_t k = 0; k < lv_obj_get_child_count(pg); k++) {
            lv_obj_t *o = lv_obj_get_child(pg, k);
            bool dot = false;
            for (int d = 0; d < FAVS_MAX; d++) dot |= o == sp[0].vdot[d];
            for (int d = 0; d < 3; d++) dot |= o == sp[0].hdot[d];
            if (!dot && o != empty_title && o != empty_how) set_hidden(o, false);
        }
    }
}

static void stops_refresh(void)
{
    empty_refresh();
    for (int i = 0; i < n_favs; i++) stop_refresh(i);
    alerts_refresh();
}

static bool row_on(lv_obj_t *page)            // the main screen shows this page of the row
{
    return lv_screen_active() == scr_main && pager_shown(row, pager_current(row)) == page;
}

static bool map_on_view(void) { return row_on(mp_page); }

// The favourite the row is about: the stop on view in the column (also while the row shows its alerts or its map)
static int fav_on_view(void) { return n_favs ? pager_current(stops) : -1; }

// The favourite whose departures are fetched every 30 s: the stop on view, or the one under the map
static int fav_fetched(void)
{
    return lv_screen_active() == scr_main && !row_on(al_page) ? fav_on_view() : -1;
}

// Every second, every page: the ones not shown too, because a drag shows a picture of a neighbour rendered in the
// background (slide.c, refreshed every 2 s): updated only when shown, a drag showed stale texts for a moment.
// Changing an off-screen label costs no redraw.
static void tick(lv_timer_t *t)
{
    // The stop fetched every 30 s follows what is on view, whatever changed it. Told only when a page settled or a
    // screen loaded, it was wrong after a screen faded in: lv_screen_active() is still the old screen during the
    // 200 ms fade (LESSONS L192), so ui_home() at start-up (and v0.3's map closing) said "no stop on view" and the stop
    // on view was fetched every 5 min, as the others, until the next swipe ("updated 5 minutes ago", the user,
    // 2026-10-09).
    static int told = -2;
    int f = fav_fetched();
    if (f != told) {
        told = f;
        deps_show(f);
    }
    // The map left by another path (Settings, setup, the console): its buses are no longer fetched
    if (mp_live && !map_on_view()) map_leave();
    if (lv_screen_active() != scr_main) return;
    stops_refresh();
    map_refresh();
}

static void map_open(void);
static void map_prepare(int i);

static void row_settled(int page, void *user)
{
    lv_obj_t *pg = pager_shown(row, page);
    ESP_LOGI(TAG, "row: %s (favourite %d)", pg == al_page ? "alerts" : pg == mp_page ? "map" : "stops", fav_on_view());
    if (pg == mp_page) map_open();
    else map_leave();
    if (pg == al_page) alerts_refresh();
    deps_show(fav_fetched());
}

// Another stop: its alerts and its map are the row's neighbours now (their pictures are of the old stop)
static void stop_settled(int page, void *user)
{
    ESP_LOGI(TAG, "stop %d of %d", page + 1, n_favs);
    if (page < n_favs) stop_refresh(page);
    alerts_refresh();
    map_prepare(fav_on_view());
    slide_stale();
    deps_show(fav_fetched());
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
    dots_create(pg, p->vdot, FAVS_MAX);
    dots_create(pg, p->hdot, 3);
}

// The pages shown: as many stop pages as favourites (at least one: it says how to add some); the alerts and the map
// beside them only with a favourite to show them for
static void pages_order(void)
{
    pager_set_count(stops, n_favs ? n_favs : 1);
    lv_obj_t *all[] = { al_page, st_page, mp_page };
    lv_obj_t *was = pager_shown(row, pager_current(row));
    if (n_favs) pager_set_order(row, all, 3);
    else pager_set_order(row, &all[1], 1);
    int at = pager_index(row, was);                       // the same page on view (its index moved), else the stops
    pager_go(row, at >= 0 && at < pager_count(row) ? at : pager_index(row, st_page), false);
    for (int i = 0; i < FAVS_MAX; i++) {
        dots_layout(sp[i].vdot, FAVS_MAX, n_favs, i, true);
        dots_layout(sp[i].hdot, 3, n_favs ? 3 : 0, 1, false);
    }
}

static void main_create(void)
{
    scr_main = base_screen();
    row = pager_create(scr_main, false, 3, NULL, row_settled, NULL);   // alerts | stops | map
    lv_obj_add_event_cb(row, pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(row, long_pressed, LV_EVENT_LONG_PRESSED, NULL);
    al_page = pager_page(row, 0);
    st_page = pager_page(row, 1);
    mp_page = pager_page(row, 2);
    stops = pager_create(st_page, true, FAVS_MAX, NULL, stop_settled, NULL);   // the favourites, up and down
    slide_pager(row);                                     // drags drawn as pictures (~60 fps), not LVGL scrolling;
    slide_pager(stops);                                   // each axis its own pager (slide.c)
    for (int i = 0; i < FAVS_MAX; i++) stop_create(i, pager_page(stops, i));
    alerts_create(al_page);
    empty_title = label(page_of(0), f_mid, C_ACCENT, 140, 340);
    empty_how = label(page_of(0), f_small, C_TEXT, 200, 360);
    pages_order();                                        // ui_favs_changed() shows as many as there are favourites
    pager_go(row, pager_index(row, st_page), false);
    empty_refresh();
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
    map_prepare(fav_on_view());
    slide_stale();
    display_unlock();
    deps_show(fav_fetched());
    ESP_LOGI(TAG, "%d favourite page(s)", n);
}

void ui_deps_changed(int i)
{
    display_lock(-1);
    if (i == DEPS_CHANGED_TRACE && map_on_view()) map_path();
    if (i == DEPS_CHANGED_BUSES || (i >= 0 && i == mp_fav)) map_refresh();
    if (i == DEPS_CHANGED_ALERTS) stops_refresh();        // alerts: their page and every stop's line
    else if (i >= 0 && i < n_favs) stop_refresh(i);
    display_unlock();
}

/* ---------- map page ----------
 * Swipe left from a stop: the street map around it (espforge's forge_map: OpenStreetMap tiles, dimmed), the route's
 * path in the favourite's direction (RTC's polylines, all its variants) in blue, the stop at the centre, the route's
 * buses heading that way (departures.c, every 20 s while the map is on view) as small green buses, only those inside
 * the round map (the user's choice, 2026-10-07: a bus beyond it isn't shown). Swipe down to zoom in, up to zoom out (as
 * weather_amoled's radar), MAP_ZOOM_MIN..MAP_ZOOM_MAX; until the new zoom's tiles are all there the picture on view is
 * the previous one, scaled. Swipe right goes back to the stop; after MAP_IDLE_MS without a touch it goes back by
 * itself (no positions are fetched for a map nobody looks at). The page follows the stop on view (map_prepare): its
 * title at once, its picture once the map is on view (tiles are fetched only for a map someone looks at). */

#define MAP_IDLE_MS (5 * 60 * 1000)
#define MAP_TITLE_W 286                                   // the round screen's width around y 50..80, less a margin
#define MAP_VISIBLE 220                                   // a bus farther from the centre than this isn't shown

// The picture on view and the route's path are one canvas (mp_buf: the map's picture copied, the path drawn over it)
// redrawn only when one of them changes: as LVGL objects (a 466 px image under two 6 px lines of ~700 points) every
// frame of the map page took 170-200 ms, and a swipe on it was read too late to be a drag, or not at all (harness
// row_alerts_stop_map and page_swipes, v0.4.0-nav.4 and nav.5). Now a frame copies the canvas.
static lv_obj_t *mp_img, *mp_stop, *mp_top, *mp_bottom, *mp_attrib, *mp_bus[RTC_BUSES_MAX];
static lv_draw_buf_t *mp_buf;                              // the canvas's pixels (PSRAM, 434 KB)
static uint32_t mp_composed;                               // lv_tick of the last compose (tiles arriving: once a second)
static int mp_path_n[DEPS_TRACE_VARIANTS];                 // the route's path: points per variant (0: none)
static lv_point_precise_t *mp_pts;                         // their points on the picture (PSRAM)
static float *mp_ll;                                       // ...as deps_trace() gives them (lat, lon; PSRAM)
#define PATH_STEP 3                                        // a point closer than this to the last one drawn is skipped
// mp_fav, mp_live (top of the file): the favourite the page is about, whether it is on view
static bool mp_view_set;                                  // the picture for its stop is chosen (map_show)
static fmap_view_t mp_view;                                // the zoom asked for (markers and path use it)
static fmap_view_t mp_shown;                               // the picture on view: mp_view's, or the previous zoom's, scaled
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

// The canvas: the picture on view, and the path over it when they are at the same zoom (while a new zoom's tiles
// arrive the previous picture stands in, scaled: the path, at the new zoom, waits for the new picture)
static void map_compose(void)
{
    if (!mp_buf || !mp_shown.px) return;
    const uint8_t *src = (const uint8_t *)mp_shown.px;
    for (int y = 0; y < MAP_SIZE; y++)
        memcpy(mp_buf->data + y * mp_buf->header.stride, src + y * MAP_SIZE * 2, MAP_SIZE * 2);
    if (mp_shown.zoom == mp_view.zoom) {
        lv_layer_t layer;
        lv_canvas_init_layer(mp_img, &layer);
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.color = C_ACCENT;
        d.width = 6;
        d.opa = LV_OPA_80;
        d.round_start = d.round_end = 1;
        const lv_point_precise_t *pt = mp_pts;
        for (int v = 0; v < DEPS_TRACE_VARIANTS; pt += mp_path_n[v], v++)
            for (int k = 0; k + 1 < mp_path_n[v]; k++) {
                d.p1 = pt[k];
                d.p2 = pt[k + 1];
                lv_draw_line(&layer, &d);
            }
        lv_canvas_finish_layer(mp_img, &layer);
    }
    lv_image_cache_drop(mp_buf);
    lv_obj_invalidate(mp_img);
    mp_composed = lv_tick_get();
}

// Put a picture on view at its own scale
static void show_picture(const fmap_view_t *v)
{
    mp_shown = *v;
    lv_image_set_scale(mp_img, LV_SCALE_NONE);
    map_compose();
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
        mp_path_n[v] = 0;
        if (v >= nv) continue;
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
        mp_path_n[v] = dst - first;
    }
    map_compose();
}

static void map_refresh(void)
{
    if (mp_fav < 0) return;
    static dep_entry_t e;                                 // (static: see alerts_refresh)
    if (!deps_get(mp_fav, &e)) return;
    char buf[96];
    // The picture: once the map is on view and the stop's place is known (its first departures reply)
    if (mp_live && !mp_view_set && e.state == DEP_OK && (e.board.lat || e.board.lon)) {
        fmap_set_center(e.board.lat, e.board.lon, mp_zoom, &mp_view);
        mp_view_set = true;
        show_picture(&mp_view);
        lv_obj_remove_flag(mp_img, LV_OBJ_FLAG_HIDDEN);
        map_path();                                       // a path already fetched (the same route's map again)
    }
    // Not while a finger is down: a tile arriving redraws the whole map (the picture and the path), and LVGL reads the
    // touch only between frames: a swipe's first reads came late and it was never a drag (v0.4.0-nav.4). The next
    // refresh after it lifts catches up (tiles, ticks).
    if (mp_view_set && !finger_down()) {
        fmap_view_t now;
        fmap_status(&now);
        if (now.px == mp_view.px) {
            mp_view = now;
            if (mp_shown.px != mp_view.px) {
                // A new zoom: its picture replaces the scaled one once complete (or once loading gave up)
                if (mp_view.state != FMAP_LOADING) show_picture(&mp_view);
            } else if ((now.done != mp_shown.done || now.state != mp_shown.state) &&
                       (now.state != FMAP_LOADING || lv_tick_elaps(mp_composed) >= 1000)) {
                mp_shown = now;                           // tiles arriving: at most a compose a second until done
                map_compose();
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
    for (int k = 0; k < RTC_BUSES_MAX; k++) {             // only the buses inside the round map, while tracked
        bool show = mp_live && k < nb && on_screen(bus[k].lat, bus[k].lon, &sx, &sy)
                    && hypot(sx - MAP_SIZE / 2, sy - MAP_SIZE / 2) <= MAP_VISIBLE;
        set_hidden(mp_bus[k], !show);
        if (show) place_dot(mp_bus[k], sx, sy);
    }

    // The bottom line: the map or the RTC failing, else the next bus
    time_t t = time(NULL);
    lv_color_t c = C_TEXT;
    if (!mp_live && !mp_view_set) snprintf(buf, sizeof(buf), "%s", tr(T_MAP_LOADING));
    else if (mp_view_set && mp_view.state == FMAP_FAILED && mp_view.done == 0) { snprintf(buf, sizeof(buf), "%s", tr(T_MAP_NO_TILES)); c = C_BAD; }
    else if (!mp_view_set || (mp_view.state == FMAP_LOADING && mp_view.done == 0)) snprintf(buf, sizeof(buf), "%s", tr(T_MAP_LOADING));
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
    if (z < MAP_ZOOM_MIN || z > MAP_ZOOM_MAX || mp_fav < 0 || !mp_live) return;
    mp_zoom = z;
    if (mp_idle) lv_timer_reset(mp_idle);                 // a touch: the map stays
    if (!mp_view_set) return;                             // the first picture will take the new zoom
    dep_entry_t e;
    if (!deps_get(mp_fav, &e) || e.state != DEP_OK) return;
    ESP_LOGI(TAG, "map zoom %d", z);
    fmap_set_center(e.board.lat, e.board.lon, z, &mp_view);
    if (mp_view.state != FMAP_LOADING) show_picture(&mp_view);
    else {                                                // the previous picture, scaled, meanwhile
        int scale = (int)lround(256 * pow(2, mp_view.zoom - mp_shown.zoom));
        lv_image_set_scale(mp_img, scale < 32 ? 32 : scale > 2048 ? 2048 : scale);
    }
    map_path();
    map_refresh();
}

// A vertical swipe on the map page (sideways ones are the row's drags, slide.c): zoom. Gestures from every page of the
// main screen come here; only the map's count.
static void map_gesture(lv_event_t *e)
{
    lv_indev_t *in = lv_indev_active();
    if (!in || !map_on_view()) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(in);
    if (dir == LV_DIR_BOTTOM) map_zoom(+1);
    else if (dir == LV_DIR_TOP) map_zoom(-1);
    lv_indev_wait_release(in);                            // its release isn't a tap
}

// Its buses are no longer fetched (the picture and the path stay: back on the map of the same stop, they are there)
static void map_leave(void)
{
    if (!mp_live) return;
    ESP_LOGI(TAG, "map left");
    mp_live = false;
    deps_track(-1);
    if (mp_idle) { lv_timer_delete(mp_idle); mp_idle = NULL; }
    map_refresh();
}

// After MAP_IDLE_MS on the map without a touch: back to the stop (as a swipe right would)
static void map_idle(lv_timer_t *t)
{
    mp_idle = NULL;                                       // (a one-shot timer: deleted after this)
    if (map_on_view()) pager_switch(row, pager_index(row, st_page));
}

// The page now follows favourite i (the stop on view): its title at once; its picture when it is on view
static void map_prepare(int i)
{
    if (i == mp_fav) return;
    map_leave();
    mp_fav = i;
    mp_view_set = false;
    lv_obj_add_flag(mp_img, LV_OBJ_FLAG_HIDDEN);
    memset(mp_path_n, 0, sizeof(mp_path_n));              // its route's path: drawn once fetched (map_path)
    map_refresh();
}

static void map_open(void)                                // the map page is on view
{
    map_prepare(fav_on_view());
    if (mp_fav < 0 || mp_live) return;
    ESP_LOGI(TAG, "map of favourite %d", mp_fav);
    mp_live = true;
    deps_track(mp_fav);
    map_refresh();
    if (mp_view_set) map_path();
    if (mp_idle) lv_timer_delete(mp_idle);
    mp_idle = lv_timer_create(map_idle, MAP_IDLE_MS, NULL);
    lv_timer_set_repeat_count(mp_idle, 1);
}

static void map_touched(lv_event_t *e) { if (mp_idle) lv_timer_reset(mp_idle); }   // a touch: the map stays

static void map_updated(void *user)                       // forge_map's task: a tile was drawn
{
    display_lock(-1);
    if (map_on_view()) map_refresh();
    display_unlock();
}

static lv_obj_t *mp_dot[3];

static void map_create(void)
{
    lv_obj_t *pg = mp_page;                               // the row's third page
    lv_obj_add_event_cb(scr_main, map_gesture, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(row, map_touched, LV_EVENT_PRESSED, NULL);
    mp_buf = lv_draw_buf_create(MAP_SIZE, MAP_SIZE, LV_COLOR_FORMAT_RGB565, 0);   // LVGL's heap: PSRAM
    if (!mp_buf) ESP_LOGE(TAG, "no room for the map's canvas: no map");
    mp_img = lv_canvas_create(pg);                         // (an lv_image: scaled for a zoom's stand-in)
    if (mp_buf) lv_canvas_set_draw_buf(mp_img, mp_buf);
    lv_obj_set_pos(mp_img, 0, 0);
    lv_image_set_pivot(mp_img, MAP_SIZE / 2, MAP_SIZE / 2);   // a zoom's stand-in scales about the stop
    lv_obj_remove_flag(mp_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(mp_img, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_HIDDEN);
    mp_pts = heap_caps_malloc(DEPS_TRACE_POINTS * sizeof(lv_point_precise_t), MALLOC_CAP_SPIRAM);
    mp_ll = heap_caps_malloc(2 * DEPS_TRACE_POINTS * sizeof(float), MALLOC_CAP_SPIRAM);
    // The route and direction: low enough for the round screen to be ~290 px wide there (at y 26 it is ~210 px and cut
    // "800  Colline Parlementaire"); a longer one wraps to a second line
    mp_top = pill(pg, f_small, 50, MAP_TITLE_W);
    lv_label_set_long_mode(mp_top, LV_LABEL_LONG_WRAP);
    mp_bottom = pill(pg, f_small, 372, 300);
    mp_attrib = pill(pg, f_tiny, 410, 200);           // OSM's licence asks for it on the map; backed: the path
    lv_obj_set_style_text_color(mp_attrib, C_DIM, 0);      // may cross it
    lv_obj_set_style_pad_ver(mp_attrib, 1, 0);
    lv_label_set_text(mp_attrib, "© OpenStreetMap");
    // The markers above the texts: a bus on the edge is never hidden by them
    mp_stop = dot(pg, 22);                           // the stop: a blue dot in a white ring
    lv_obj_set_style_border_color(mp_stop, C_TEXT, 0);
    lv_obj_set_style_border_width(mp_stop, 4, 0);
    lv_obj_set_style_bg_color(mp_stop, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(mp_stop, LV_OPA_COVER, 0);
    for (int k = 0; k < RTC_BUSES_MAX; k++) mp_bus[k] = bus_icon(pg);   // the buses
    dots_create(pg, mp_dot, 3);
    dots_layout(mp_dot, 3, 3, 2, false);
}

/* ---------- message screen (start-up) ---------- */

static void msg_create(void)
{
    scr_msg = base_screen();
    m_title = label(scr_msg, f_mid, C_ACCENT, 120, 320);
    m_body = label(scr_msg, f_small, C_TEXT, 180, 340);
    lv_obj_add_flag(scr_msg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr_msg, msg_long_pressed, LV_EVENT_LONG_PRESSED, NULL);
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

// The row on its stops, the column on stop n (0-based); quietly (no callbacks: the caller refreshes)
static void go_stop(int n)
{
    pager_go(row, pager_index(row, st_page), false);
    pager_go(stops, n, false);
    map_leave();
    map_prepare(fav_on_view());
    stops_refresh();
    slide_stale();
}

void ui_home(void)
{
    display_lock(-1);
    go_stop(0);
    if (lv_screen_active() != scr_main) lv_screen_load_anim(scr_main, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
    display_unlock();
    deps_show(fav_fetched());
}

/* ---------- Wi-Fi setup ----------
 * Page 1: the setup network (this device's own access point and captive portal): scan to join, the settings page
 * opens by itself. Page 2 (Android 10+): Wi-Fi Easy Connect (DPP). The phone scans this QR code and sends the
 * network it's connected to, password included. The setup network stays up on page 2: it holds the radio on Easy
 * Connect's channel (forge_net's dpp_hold_channel). The two pages are a pager like the main screen's, so they follow the finger
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
    if (settings_resume()) return;                   // opened from Settings' Wi-Fi row: back there
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

void ui_ota(const ota_status_t *st) {}             // Settings' Updates row reads forge_ota itself (every second)

static void texts_refresh(void)                    // display lock held
{
    stops_refresh();
    map_refresh();
    settings_refresh();
    if (su_open) su_texts();
    slide_stale();
}

void ui_texts_changed(void)
{
    display_lock(-1);
    texts_refresh();
    display_unlock();
}

/* ---------- Settings (forge_settings) ----------
 * A long press on the main screen. The rows weather_amoled's has that make sense here (screen dimming, language, the
 * phone's page, Wi-Fi, updates, restart) and an About section that replaces the system page (until v0.4.0). The stops,
 * the noise measurement and custom timings stay on the phone (typing, a list, a live meter). */

// The app's words for forge_settings' codes, in settings_text_t's order
static const int set_texts[] = {
    T_SET_DONE, T_SET_SEC_SCREEN, T_SET_DIM_QUIET, T_SET_WAKE_PICKUP, T_SET_TIMING, T_SET_SHORT, T_SET_NORMAL,
    T_SET_LONG, T_SET_CUSTOM, T_SET_BRIGHTNESS, T_SET_LANGUAGE, T_SET_SEC_MORE, T_SET_PHONE, T_SET_PHONE_SCAN,
    T_SET_PHONE_NONE, T_SET_WIFI, T_SET_UPDATES, T_SET_CHECK_NOW, T_SET_CHECKING, T_SET_UP_TO_DATE, T_SET_FAILED,
    T_SET_INSTALL, T_SET_TAP_AGAIN, T_SET_RESTART, T_SET_RESTARTING, T_SET_SEC_ABOUT, T_SET_VERSION, T_SET_NETWORK,
    T_SET_OFFLINE, T_SET_IP, T_SET_MEMORY, T_SET_MEMORY_KB, T_SET_UPTIME, T_SET_UPTIME_MIN, T_SET_UPTIME_H,
    T_SET_UPTIME_D,
};
_Static_assert(sizeof(set_texts) / sizeof(set_texts[0]) == SET_T_COUNT, "one text per settings_text_t code");

static void wifi_from_settings(void) { ui_wifi_setup(NULL); }

static void settings_make(void)
{
    const settings_row_t rows[] = {
        settings_std_section(SET_T_SEC_SCREEN),
        settings_std_dim(), settings_std_motion(), settings_std_timing(),
        settings_std_language(texts_refresh),
        settings_std_section(SET_T_SEC_MORE),
        settings_std_phone(), settings_std_wifi(wifi_from_settings), settings_std_updates(), settings_std_restart(),
        settings_std_section(SET_T_SEC_ABOUT),
        settings_std_version(), settings_std_network(), settings_std_ip(), settings_std_memory(), settings_std_uptime(),
    };
    settings_opts_t o = { .texts = set_texts, .font = f_small, .font_small = f_tiny, .brightness = true };
    settings_create(&o, rows, sizeof(rows) / sizeof(rows[0]));
}

/* ---------- screen registry (test console, snapshots) ---------- */

static lv_obj_t *get_setup(void) { return pager_page(su_pager, 0); }
static lv_obj_t *get_setup1(void) { return pager_page(su_pager, 1); }
static lv_obj_t *get_msg(void) { return scr_msg; }
static void show_setup(void) { ui_wifi_setup(NULL); }
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

static void show_main(void)
{
    su_leave();
    lv_screen_load(scr_main);
    deps_show(fav_fetched());
}

// The stops: "stop" is the first favourite, "stop2".."stop8" the others, for the harness's drags and snapshots. A page
// not in use (fewer favourites) is never shown and can't be shown.
static bool shown_stop_n(int n)
{
    return row_on(st_page) && pager_current(stops) == n - 1;
}
static void show_stop_n(int n)
{
    if (n > 1 && n_favs < n) return;
    go_stop(n - 1);
    show_main();
}
#define STOP_N(n) \
    static lv_obj_t *get_stop##n(void) { return page_of(n - 1); } \
    static bool shown_stop##n(void) { return shown_stop_n(n); } \
    static void show_stop##n(void) { show_stop_n(n); }
STOP_N(1) STOP_N(2) STOP_N(3) STOP_N(4) STOP_N(5) STOP_N(6) STOP_N(7) STOP_N(8)
#define STOP_DEF(n) { "stop" #n, get_stop##n, show_stop##n, stops_refresh, shown_stop##n }
_Static_assert(FAVS_MAX == 8, "one STOP_N per favourite page");

// The alerts and the map of the stop on view (the first if none is)
static lv_obj_t *get_alerts(void) { return al_page; }
static bool shown_alerts(void) { return row_on(al_page); }
static void show_alerts(void)
{
    if (!n_favs) return;
    pager_go(row, pager_index(row, al_page), false);
    alerts_refresh();
    show_main();
}
static lv_obj_t *get_map(void) { return mp_page; }
static void show_map(void)
{
    if (!n_favs) return;
    pager_go(row, pager_index(row, mp_page), false);
    show_main();
    map_open();
}

static lv_obj_t *get_settings(void) { return settings_screen(); }
static void show_settings(void) { su_leave(); settings_show_page(0); }
static void show_settings1(void) { su_leave(); settings_show_page(1); }
static void prep_settings(void) { settings_scroll(0); }
static void prep_settings1(void) { settings_scroll(1); }
static void prep_settings2(void) { settings_scroll(2); }
static void show_settings2(void) { su_leave(); settings_show_page(2); }
static bool shown_settings(void) { return lv_screen_active() == settings_screen(); }
static bool shown_never(void) { return false; }      // settings1: the same screen, never named as the one shown

static const screen_def_t screens[] = {
    { "stop",    get_stop1,  show_stop1,  stops_refresh,  shown_stop1 },
    { "setup",   get_setup,  show_setup,  prep_setup,     shown_setup },
    { "setup1",  get_setup1, show_setup1, prep_setup,     shown_setup1 },
    STOP_DEF(2), STOP_DEF(3), STOP_DEF(4), STOP_DEF(5), STOP_DEF(6), STOP_DEF(7), STOP_DEF(8),
    { "alerts",  get_alerts, show_alerts, alerts_refresh, shown_alerts },
    { "map",     get_map,    show_map,    map_refresh,    map_on_view },
    { "settings", get_settings, show_settings, prep_settings, shown_settings },
    { "settings1", get_settings, show_settings1, prep_settings1, shown_never },   // the list scrolled down
    { "settings2", get_settings, show_settings2, prep_settings2, shown_never },   // ...and further (About)
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
    settings_make();
    screens_register(screens, sizeof(screens) / sizeof(screens[0]));
    lv_screen_load(scr_msg);
    display_unlock();
    web_set_snapshot(screens_snapshot, screens_snapshot_free);
    fmap_opts_t mo;
    fmap_opts_default(&mo, MAP_SIZE, MAP_SIZE);          // OSM's tiles, dimmed as before, "OpenStreetMap" service
    mo.zoom_min = MAP_ZOOM_MIN;
    mo.zoom_max = MAP_ZOOM_MAX;
    mo.psram_slots = MAP_SLOTS;
    mo.updated = map_updated;
    if (!fmap_create(&mo)) ESP_LOGE(TAG, "no street map (forge_map)");
}
