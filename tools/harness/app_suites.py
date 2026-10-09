"""The app's own tests (main/: the stops, their alerts and map, Settings on a long press, Wi-Fi setup). Same registry (@test from
board.py), same ctx as core_suites.py.

    APP_ORDER       the app's suites, run after the core start-up checks (harness.py)
    APP_WATCH       log lines that aren't failures but must not go unseen: (regex, what), counted per test
    METRIC_SUITES   which suite produces a metric (prefix -> suite): a baseline metric is only expected when its
                    suite ran
"""
import re
import time

from board import CFG, SCREEN_C, check, test

APP_ORDER = ['navigation', 'perf', 'presence']
APP_WATCH = [
    (r'display: .*did not finish', 'a panel transfer timed out'),
    (r'lvgl: .*(out of memory|alloc failed)', 'LVGL could not allocate'),
]
METRIC_SUITES = [('swipe_', 'perf'), ('setup_page_', 'navigation'), ('easy_connect_', 'navigation')]

# The main screen (v0.4.0, the user's design, 2026-10-07): a row alerts | stops | map, the stops a column (stop,
# stop2 .. stop8, one per favourite; at least "stop": without favourites it says how to add some). Swipe up / down
# between stops, right for the stop's alerts, left for its map; a long press opens Settings. The tests read the
# display's favourites and go through all of them.
HOME, NEXT = 'stop', 'stop'


def stops(ctx):
    """The stop pages, top to bottom, for the favourites the display has now."""
    n = len(ctx.board.api('/api/favs').get('favs', []))
    return ['stop'] + [f'stop{i}' for i in range(2, n + 1)]


def favs(ctx):
    return len(ctx.board.api('/api/favs').get('favs', []))


def go_home(ctx):
    ctx.board.show(HOME)
    ctx.board.wait_screen(HOME, 6)
    time.sleep(0.5)


def need_favs(ctx, n=1):
    if favs(ctx) < n:
        ctx.note(f'fewer than {n} favourite stop(s) on this board: not checked')
        return False
    return True


# ---------------------------------------------------------------- navigation

@test('navigation')
def stops_switch_vertically(ctx):
    """Swipe up through every stop, down back; no wrap-around at either end (places in weather_amoled)."""
    b = ctx.board
    go_home(ctx)
    p = stops(ctx)
    route = ([('swipe up', x) for x in p[1:]] + [('swipe up', p[-1])] +       # the bottom: bounces back
             [('swipe down', x) for x in p[-2::-1]] + [('swipe down', HOME)])  # the top: bounces back
    for cmd, want in route:
        b.cmd(cmd)
        time.sleep(0.8)
        b.wait_screen(want, 6)
    ctx.note(f'{" <-> ".join(p)} by vertical swipes, both ends bounce')


@test('navigation')
def row_alerts_stop_map(ctx):
    """From a stop: swipe right for its alerts, left for its map; the row bounces at both ends."""
    b = ctx.board
    if not need_favs(ctx):
        return
    go_home(ctx)
    at = len(ctx.log.lines())
    # The swipe left on the map comes ~1 s after it opened, while its tiles may still arrive: v0.4.0-nav.4 took it
    # for a long press there (Settings opened) when LVGL, busy redrawing the map, read the finger late
    for cmd, want in (('swipe right', 'alerts'), ('swipe right', 'alerts'), ('swipe left', HOME),
                      ('swipe left', 'map'), ('swipe left', 'map'), ('swipe right', HOME)):
        b.cmd(cmd)
        time.sleep(0.8)
        b.wait_screen(want, 6)
    check(not ctx.log.count(r'settings: open', start=at), 'a swipe opened Settings (taken for a long press)')
    ctx.note('alerts <- stop -> map by swipes, both ends bounce')


@test('navigation')
def alerts_of_this_stop(ctx):
    """The alerts page is the stop's: after moving to another stop, swipe right shows that stop's (its route in its
    direction), not every favourite's (before v0.4.0: one page for all)."""
    b = ctx.board
    if not need_favs(ctx, 2):
        return
    seen = []
    for i, name in enumerate(stops(ctx)[:2]):
        b.show(name)
        b.wait_screen(name, 6)
        time.sleep(1)
        b.cmd('swipe right')
        b.wait_screen('alerts', 6)
        lines = [l for l in ctx.log.lines() if 'ui: alerts of favourite' in l]
        m = re.search(r'alerts of favourite (-?\d+): (\d+)', lines[-1] if lines else '')
        check(m and int(m.group(1)) == i, f'{name}: the alerts page shows favourite '
                                          f'{m.group(1) if m else "?"}, not {i}')
        seen.append(f'{name}: {m.group(2)}')
        b.cmd('swipe left')
        b.wait_screen(name, 6)
    ctx.note('alerts of ' + ', '.join(seen))


@test('navigation')
def quick_swipes(ctx):
    """A swipe that lands while the previous move's release animation still runs is a swipe too. LVGL reads nothing
    during a move, so slide.c's read hook never saw the first press end, and took the second for it: nothing moved
    (LESSONS L181). 'swipe right left' leaves 150 ms of "up" between the two
    (70 ms was sometimes read as one of the chip's brief false "ups": one drag, left then right)."""
    b = ctx.board
    if not need_favs(ctx):
        return
    go_home(ctx)
    at = len(ctx.log.lines())
    b.cmd('swipe right left')
    time.sleep(1.0)
    drags = [l for l in ctx.log.lines()[at:] if 'slide: drag: first frame' in l]
    check(len(drags) == 2, f'two quick swipes made {len(drags)} drag(s); the second was taken for the first')
    # Where the second one ends is timing: it starts when the first's release animation ends, by then the simulated
    # finger has mostly moved on ("samples 1", "back" once on v0.1.1-rc.1, on to the other page in the run before)
    second = 'back' if ' back |' in drags[1] else 'on'
    ctx.note(f'two swipes 150 ms apart: 2 drags, the second went {second}; now on {b.screen()}')
    go_home(ctx)


@test('navigation')
def tap_on_stop_does_nothing(ctx):
    """A tap on a stop does nothing (the map is a swipe left; before v0.4.0 a tap opened it)."""
    b = ctx.board
    if not need_favs(ctx):
        return
    go_home(ctx)
    at = len(ctx.log.lines())
    b.tap()
    time.sleep(1.5)
    check(b.screen() == HOME, f'a tap on the stop went to {b.screen()}')
    check(not ctx.log.count(r'ui: map of favourite', start=at), 'a tap opened the map')
    ctx.note('tap on a stop: still on it, no map')


@test('navigation')
def long_press_opens_settings(ctx):
    """A long press opens Settings (online); Done goes back, and so does a swipe right (weather_amoled's)."""
    b = ctx.board
    go_home(ctx)
    for close, how in ((lambda: b.tap(SCREEN_C[0], 42), 'Done'), (lambda: b.cmd('swipe right'), 'a swipe right')):
        b.press()
        b.wait_screen('settings', 6)
        time.sleep(1)                                  # its slide up, and the long-press's own release
        close()
        b.wait_screen(HOME, 6)
        time.sleep(0.5)
    ctx.note(f'long-press at {SCREEN_C}: Settings; Done and a swipe right: back to {HOME}')


@test('navigation')
def settings_rows(ctx):
    """A Settings row acts through the same setting as the phone's page: a tap on "Dim when quiet" (the first row, under
    the SCREEN title: list at y 70, title 22 px, 6 px gap, a 52 px row) switches dimming, and the page sees it."""
    b = ctx.board
    saved = b.api('/api/presence')
    try:
        go_home(ctx)
        b.press()
        b.wait_screen('settings', 6)
        time.sleep(1)
        at = len(ctx.log.lines())
        b.tap(SCREEN_C[0], 70 + 22 + 6 + 26)
        ctx.log.wait(r'settings: row 1\b', 4, 'the "Dim when quiet" row tapped', start=at)
        time.sleep(0.5)
        now = b.api('/api/presence')
        check(now['enabled'] != saved['enabled'], f'dimming still {saved["enabled"]} after a tap on its row')
        b.tap(SCREEN_C[0], 42)
        b.wait_screen(HOME, 6)
    finally:
        keys = ('enabled', 'margin_db', 'wake_s', 'dim_s', 'off_s', 'bright_pct', 'dim_pct', 'motion_wake')
        b.api('/api/presence', {k: saved[k] for k in keys if k in saved})
    ctx.note(f'a tap on "Dim when quiet" switched dimming {"off" if saved["enabled"] else "on"} (the page saw it); '
             'put back')


@test('navigation')
def swipe_left_opens_map(ctx):
    """Swipe left from a stop: its map, the street map's tiles arrive, the route's buses are asked for every 20 s while
    it is on view and not after; swipe right goes back (user's request, 2026-10-07)."""
    b = ctx.board
    if not need_favs(ctx):
        return
    go_home(ctx)
    at = len(ctx.log.lines())
    b.cmd('swipe left')
    b.wait_screen('map', 6)
    ctx.log.wait(r'ui: map of favourite 0', 5, 'the map of the stop on view', start=at)
    # The tiles: downloaded at the first opening of this run (the screens suite's, or this one), then kept
    if not ctx.log.count(r'fmap: zoom \d+ at', start=0):
        ctx.log.wait(r'fmap: zoom \d+ at', 30, 'the map tiles', start=at)
    tiles = re.findall(r'fmap: zoom \d+ at \S+: (\d+)/(\d+) tiles', '\n'.join(ctx.log.lines()))
    check(tiles and tiles[-1][0] == tiles[-1][1], f'map tiles: {tiles[-1] if tiles else "none"}')
    ctx.log.wait(r'deps: GET /ListeAutobus_Parcours', 30, 'the buses asked for', start=at)
    b.cmd('swipe right')
    b.wait_screen(HOME, 6)
    closed = len(ctx.log.lines())
    time.sleep(25)
    n = ctx.log.count(r'deps: GET /ListeAutobus_Parcours', start=closed)
    check(n == 0, f'{n} bus position request(s) after the map was left')
    ctx.note('swipe left: map with tiles and buses; swipe right: back to the stop; no positions asked once left')


@test('navigation')
def map_zoom(ctx):
    r"""On the map, swipe down zooms in and swipe up zooms out (as weather_amoled's radar; user's request, 2026-10-07):
    each zoom's tiles arrive and the map stays on view; swipe right goes back."""
    b = ctx.board
    if not need_favs(ctx):
        return
    go_home(ctx)
    b.cmd('swipe left')
    b.wait_screen('map', 6)
    time.sleep(3)
    seen = []
    for cmd in ('swipe down', 'swipe up', 'swipe up'):
        at = len(ctx.log.lines())                          # both lines come after the swipe: look from there
        b.cmd(cmd)
        m = ctx.log.wait(r'ui: map zoom (\d+)', 5, f'{cmd}: a new zoom', start=at)
        z = int(m.group(1))
        ctx.log.wait(r'fmap: zoom %d at' % z, 30, f'zoom {z} tiles (downloaded or kept)', start=at)
        check(b.screen() == 'map', f'{cmd} left the map')
        seen.append(z)
    check(seen[1] == seen[0] - 1 and seen[2] == seen[1] - 1, f'zooms {seen}')
    b.cmd('swipe right')
    b.wait_screen(HOME, 6)
    ctx.note(f'zooms {seen} by swipes down, up, up; the map stayed; a swipe right went back')


@test('navigation')
def stop_on_view_every_30s_after_start(ctx):
    """After a restart, the stop on view is fetched every 30 s without anyone touching the display. Before v0.4.0 the
    start-up's fade into the stops told the fetcher "no stop on view" (the old screen is the active one during a fade):
    it was fetched every 5 min, as the others, until the first swipe ("updated 5 minutes ago", the user, 2026-10-09)."""
    b = ctx.board
    favs_now = b.api('/api/favs').get('favs', [])
    if not favs_now:
        ctx.note('no favourite stop on this board: not checked')
        return
    f = favs_now[0]
    stop, route, d = (str(f.get(k, '')) for k in ('stop', 'route', 'dir'))
    b.stop_log()                                         # then a restart, as at power-on, and a new log window
    b.start_log(40 * 60)                                 # (the harness's own length: 40 min)
    ready = ctx.log.wait(CFG['ready_line'], 90, 'start-up', start=0)
    t0 = time.time()
    time.sleep(75)
    pat = re.compile(r'deps: GET /BorneVirtuelle_ArretParcours\?noArret=%s&noParcours=%s&codeDirection=%s&' %
                     (re.escape(stop), re.escape(route), re.escape(d)))
    lines = ctx.log.lines()
    at = next(i for i, l in enumerate(lines) if ready.string in l)
    gets = [l for l in lines[at:] if pat.search(l)]
    check(len(gets) >= 3, f'the stop on view ({route} at {stop}) was fetched {len(gets)} time(s) in the '
                          f'{time.time() - t0:.0f} s after start-up, not every 30 s')
    ctx.note(f'{route} at {stop}, untouched after a restart: fetched {len(gets)} times in 75 s')


@test('navigation')
def setup_pages_slide(ctx):
    """Setup's two pages (setup network | Easy Connect) follow the finger like the stops, and the Easy Connect QR
    code shows up quickly. User reports, October 4: the setup pages only switched after the swipe (and froze while
    the radio switched), and the QR code took ~2 s (a channel scan the connected device doesn't need)."""
    b = ctx.board
    if 'setup1' not in CFG['screens']:
        ctx.note('no "setup1" screen in forge.json: not checked')
        return
    b.show('setup')
    b.wait_screen('setup', 6)
    time.sleep(1)
    at = len(ctx.log.lines())                         # own position (log.mark() is the harness's crash check)
    t0 = time.time()
    b.cmd('swipe left')
    b.wait_screen('setup1', 3)
    m = ctx.log.wait(r'slide: drag: first frame after (\d+) ms, (\d+) frames in (\d+) ms \((\d+) fps\), to next', 5,
                     'the setup page following the finger', start=at)
    fps = int(m.group(4))
    check(fps >= 40, f'setup page drag at {fps} fps')
    ctx.metric('setup_page_fps', fps)
    page = ctx.log.wait(r'ui: Wi-Fi setup page 1', 5, 'Easy Connect started', start=at)
    qr = ctx.log.wait(r'net: Easy Connect: QR code ready', 10, 'the Easy Connect QR code', start=at)
    qr_s = (log_ms(qr.string) - log_ms(page.string)) / 1000
    ctx.metric('easy_connect_qr_s', round(qr_s, 2))
    b.cmd('swipe right')                                # back to the first page, then closed (online again)
    b.wait_screen('setup', 3)
    go_home(ctx)
    ctx.note(f'setup -> Easy Connect by a drag at {fps} fps, its QR code {qr_s:.2f} s after the page '
             f'({time.time() - t0:.1f} s in all, console round trips included)')


def log_ms(line):
    """The ESP-IDF timestamp of a log line, in ms ("I (12345) tag: ...")."""
    m = re.search(r'\((\d+)\)', line)
    return int(m.group(1)) if m else 0


# ---------------------------------------------------------------- perf

def measure(ctx, name, action, settle=1.0):
    """Frame rate during `action` (console commands): fps reset, act, wait for the animation to end, read fps.
    anim_fps counts frames less than 250 ms apart; gap_max_ms is the longest wait between two of them."""
    b = ctx.board
    if time.localtime().tm_sec > 55:                 # not across a minute change: the clock's redraw right after a
        time.sleep(62 - time.localtime().tm_sec)     # move counts in swipe_gap_max_ms (weather_amoled: 127 ms once)
    b.cmd('fps reset')
    for c in action:
        b.cmd(c)
    time.sleep(settle)
    line = b.cmd('fps', r'test: fps (.*)').group(1)
    kv = dict(x.split('=', 1) for x in line.split() if '=' in x)
    v = {k: float(x) for k, x in kv.items() if re.fullmatch(r'-?[\d.]+', x)}   # gap_max_kind is text
    check(v.get('anim_frames', 0) > 0, f'{name}: no animation frames counted ({line})')
    ctx.metric(f'swipe_fps.{name}', round(v['anim_fps'], 1))
    ctx.metric(f'swipe_gap_max_ms.{name}', round(v['gap_max_ms']))
    ctx.metric(f'swipe_render_avg_ms.{name}', round(v['render_avg_ms'], 1))
    ctx.note(f'{name}: {v["anim_fps"]:.1f} fps, {int(v["anim_frames"])} frames, render avg {v["render_avg_ms"]:.1f} ms '
             f'max {v["render_max_ms"]:.1f} ms, worst gap {v["gap_max_ms"]:.0f} ms' +
             (f' ({kv["gap_max_kind"]}, {kv["gap_max_at_ms"]} ms after the reset)' if 'gap_max_kind' in kv else ''))


@test('perf')
def page_swipes(ctx):
    """The moves a person makes most: to the map and back (the row), to the next stop and back (the column, with two
    favourites or more), and a slow drag that follows the finger."""
    b = ctx.board
    go_home(ctx)
    n = favs(ctx)
    if n:
        measure(ctx, 'stop_to_map', ['swipe left'])
        b.wait_screen('map', 4)
        measure(ctx, 'map_to_stop', ['swipe right'])
        b.wait_screen(HOME, 4)
    if n > 1:
        measure(ctx, 'stop_to_stop2', ['swipe up'])
        b.wait_screen('stop2', 4)
        measure(ctx, 'stop2_to_stop', ['swipe down'])
        b.wait_screen(HOME, 4)
    if n:
        w, h = CFG['screen']['w'], CFG['screen']['h']    # a slow drag, finger-following, then the snap
        measure(ctx, 'drag_slow', [f'drag {w // 4} {h // 2} {w * 3 // 4} {h // 2} 600'], settle=1.5)
        b.wait_screen('alerts', 4)
    go_home(ctx)


# ---------------------------------------------------------------- presence (espforge's forge_presence)

def presence_state(b):
    m = re.search(r'state=(\d) brightness=(\d+) .*mic=(\d)', b.cmd('presence', r'test: presence (.*)').group(1))
    return int(m.group(1)), int(m.group(2)), m.group(3) == '1'


@test('presence')
def dim_off_wake(ctx):
    """Short delays through the API (the user's settings put back after): ACTIVE -> DIM -> OFF on a stop page, then a
    tap on the dark screen only wakes it: the board's press filter swallows it, so it does nothing else (v0.3.0's
    rule, now through espforge's forge_presence and board)."""
    b = ctx.board
    saved = b.api('/api/presence')
    keys = ('enabled', 'margin_db', 'wake_s', 'dim_s', 'off_s', 'bright_pct', 'dim_pct', 'motion_wake')
    orig = {k: saved[k] for k in keys}
    try:
        b.show(NEXT)
        b.wait_screen(NEXT, 6)
        _, _, mic = presence_state(b)
        check(mic, 'the microphones are not available (board_mic_open): presence never runs')
        at = len(ctx.log.lines())
        b.api('/api/presence', {'enabled': True, 'margin_db': 60, 'dim_s': 2, 'off_s': 2, 'motion_wake': False})
        ctx.log.wait(r'presence: ACTIVE -> DIM', 15, 'dims after 2 s of quiet', start=at)
        ctx.log.wait(r'presence: DIM -> OFF', 15, 'off 2 s later', start=at)
        time.sleep(1.5)                                  # faded out
        state, bright, _ = presence_state(b)
        check(state == 2 and bright == 0, f'not off: state {state}, brightness {bright}')
        at = len(ctx.log.lines())
        b.tap()
        ctx.log.wait(r'presence: touch on a dark screen', 6, 'the press filter woke it', start=at)
        time.sleep(1.5)
        state, bright, _ = presence_state(b)
        check(state == 0 and bright > 0, f'not awake: state {state}, brightness {bright}')
        check(b.screen() == NEXT, f'the waking tap also acted: on {b.screen()}, not {NEXT}')
        ctx.note('dim after 2 s, off 2 s later; a tap on the dark stop page only woke it')
    finally:
        b.api('/api/presence', orig)
        b.cmd('wake')
