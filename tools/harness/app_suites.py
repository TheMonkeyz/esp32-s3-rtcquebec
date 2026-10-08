"""The app's own tests (main/: the system page and the stop pages, setup on a long-press). Same registry (@test from
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

# The pager: system | stop | stop2 .. stop8 | alerts, one page per favourite (at least "stop": without favourites it
# says how to add some). The tests read the display's favourites and swipe through all of them.
HOME, NEXT = 'system', 'stop'


def pages(ctx):
    """The pager's pages, left to right, for the favourites the display has now."""
    n = len(ctx.board.api('/api/favs').get('favs', []))
    return [HOME, NEXT] + [f'stop{i}' for i in range(2, n + 1)] + ['alerts']


def go_home(ctx):
    ctx.board.show(HOME)
    ctx.board.wait_screen(HOME, 6)
    time.sleep(0.5)


# ---------------------------------------------------------------- navigation

@test('navigation')
def swipe_between_pages(ctx):
    """Swipe like a person: left through every page, right back; no wrap-around at either end."""
    b = ctx.board
    go_home(ctx)
    p = pages(ctx)
    route = ([('swipe left', x) for x in p[1:]] + [('swipe left', p[-1])] +     # the right end: bounces back
             [('swipe right', x) for x in p[-2::-1]] + [('swipe right', HOME)])  # the left end: bounces back
    for cmd, want in route:
        b.cmd(cmd)
        time.sleep(0.8)
        b.wait_screen(want, 6)
    ctx.note(f'{" <-> ".join(p)} by swipes, both ends bounce')


@test('navigation')
def quick_swipes(ctx):
    """A swipe that lands while the previous move's release animation still runs is a swipe too. LVGL reads nothing
    during a move, so slide.c's read hook never saw the first press end, and took the second for it: nothing moved
    (LESSONS L181). 'swipe left right' leaves 150 ms of "up" between the two
    (70 ms was sometimes read as one of the chip's brief false "ups": one drag, left then right)."""
    b = ctx.board
    go_home(ctx)
    at = len(ctx.log.lines())
    b.cmd('swipe left right')
    time.sleep(1.0)
    drags = [l for l in ctx.log.lines()[at:] if 'slide: drag: first frame' in l]
    check(len(drags) == 2, f'two quick swipes made {len(drags)} drag(s); the second was taken for the first')
    # Where the second one ends is timing: it starts when the first's release animation ends, by then the simulated
    # finger has mostly moved on ("samples 1", "back" once on v0.1.1-rc.1, on to the other page in the run before)
    second = 'back' if ' back |' in drags[1] else 'on'
    check(not ctx.log.count(r'ui: map of favourite', start=at), 'a quick swipe opened the map (read as a tap)')
    ctx.note(f'two swipes 150 ms apart: 2 drags, the second went {second}; now on {b.screen()}')
    go_home(ctx)


@test('navigation')
def long_press_opens_setup(ctx):
    """A long-press opens Wi-Fi setup; a tap closes it, back where it was."""
    b = ctx.board
    if 'setup' not in CFG['screens']:
        ctx.note('no "setup" screen in forge.json: not checked')
        return
    go_home(ctx)
    b.press()
    b.wait_screen('setup', 6)
    time.sleep(1)                                      # a tap within the long-press's own release window is ignored
    b.tap()
    b.wait_screen(HOME, 6)
    ctx.note(f'long-press at {SCREEN_C}: setup; tap: back to {HOME}')


@test('navigation')
def tap_opens_map(ctx):
    """A tap on a stop page opens its map: the street map's tiles arrive, the route's buses are asked for every 20 s
    while it is open and not after; a tap closes it (user's request, 2026-10-07)."""
    b = ctx.board
    if not ctx.board.api('/api/favs').get('favs'):
        ctx.note('no favourite stop on this board: not checked')
        return
    b.show(NEXT)
    b.wait_screen(NEXT, 6)
    time.sleep(1)
    at = len(ctx.log.lines())
    b.tap()
    b.wait_screen('map', 6)
    # The tiles: downloaded at the first opening of this run (the screens suite's, or this one), then kept
    if not ctx.log.count(r'map: zoom \d+ at', start=0):
        ctx.log.wait(r'map: zoom \d+ at', 30, 'the map tiles', start=at)
    tiles = re.findall(r'map: zoom \d+ at \S+: (\d+)/(\d+) tiles', '\n'.join(ctx.log.lines()))
    check(tiles and tiles[-1][0] == tiles[-1][1], f'map tiles: {tiles[-1] if tiles else "none"}')
    ctx.log.wait(r'deps: GET /ListeAutobus_Parcours', 30, 'the buses asked for', start=at)
    b.tap()
    b.wait_screen(NEXT, 6)
    closed = len(ctx.log.lines())
    time.sleep(25)
    n = ctx.log.count(r'deps: GET /ListeAutobus_Parcours', start=closed)
    check(n == 0, f'{n} bus position request(s) after the map closed')
    ctx.note('tap: map with tiles and buses; tap: back to the stop; no positions asked once closed')


@test('navigation')
def map_zoom(ctx):
    r"""On the map, swipe down zooms in and swipe up zooms out (as weather_amoled's radar; user's request, 2026-10-07):
    each zoom's tiles arrive, the map stays open (a swipe's release is not a tap), and a tap still closes it."""
    b = ctx.board
    if not ctx.board.api('/api/favs').get('favs'):
        ctx.note('no favourite stop on this board: not checked')
        return
    b.show(NEXT)
    b.wait_screen(NEXT, 6)
    time.sleep(1)
    b.tap()
    b.wait_screen('map', 6)
    time.sleep(3)
    seen = []
    for cmd in ('swipe down', 'swipe up', 'swipe up'):
        at = len(ctx.log.lines())                          # both lines come after the swipe: look from there
        b.cmd(cmd)
        m = ctx.log.wait(r'ui: map zoom (\d+)', 5, f'{cmd}: a new zoom', start=at)
        z = int(m.group(1))
        ctx.log.wait(r'map: zoom %d at' % z, 30, f'zoom {z} tiles (downloaded or kept)', start=at)
        check(b.screen() == 'map', f'{cmd} closed the map')
        seen.append(z)
    check(seen[1] == seen[0] - 1 and seen[2] == seen[1] - 1, f'zooms {seen}')
    b.tap()
    b.wait_screen(NEXT, 6)
    ctx.note(f'zooms {seen} by swipes down, up, up; the map stayed open; a tap closed it')


@test('navigation')
def setup_pages_slide(ctx):
    """Setup's two pages (setup network | Easy Connect) follow the finger like system | stop, and the Easy Connect QR
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
    b = ctx.board
    go_home(ctx)
    measure(ctx, f'{HOME}_to_{NEXT}', ['swipe left'])
    b.wait_screen(NEXT, 4)
    measure(ctx, f'{NEXT}_to_{HOME}', ['swipe right'])
    b.wait_screen(HOME, 4)
    w, h = CFG['screen']['w'], CFG['screen']['h']        # a slow drag, finger-following, then the snap
    measure(ctx, 'drag_slow', [f'drag {w * 3 // 4} {h // 2} {w // 4} {h // 2} 600'], settle=1.5)
    b.wait_screen(NEXT, 4)
    go_home(ctx)


# ---------------------------------------------------------------- presence (espforge's forge_presence)

def presence_state(b):
    m = re.search(r'state=(\d) brightness=(\d+) .*mic=(\d)', b.cmd('presence', r'test: presence (.*)').group(1))
    return int(m.group(1)), int(m.group(2)), m.group(3) == '1'


@test('presence')
def dim_off_wake(ctx):
    """Short delays through the API (the user's settings put back after): ACTIVE -> DIM -> OFF on a stop page, then a
    tap on the dark screen only wakes it: the board's press filter swallows it, so the map doesn't open (v0.3.0's
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
        b.tap()                                          # on a lit stop page: opens its map
        ctx.log.wait(r'presence: touch on a dark screen', 6, 'the press filter woke it', start=at)
        time.sleep(1.5)
        state, bright, _ = presence_state(b)
        check(state == 0 and bright > 0, f'not awake: state {state}, brightness {bright}')
        check(b.screen() == NEXT, f'the waking tap also acted: on {b.screen()}, not {NEXT}')
        ctx.note('dim after 2 s, off 2 s later; a tap on the dark stop page only woke it')
    finally:
        b.api('/api/presence', orig)
        b.cmd('wake')
