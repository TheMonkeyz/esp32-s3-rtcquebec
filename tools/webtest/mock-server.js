// Mock of the display's settings server for browser tests: serves the real main/web/index.html and answers the API of
// docs/PROTOCOL.md §4 like forge_net's web server, with the state in memory.
//   node mock-server.js [port]     (default 8099)
// Test hooks (not on the device):
//   POST /__reset              the starting state
//   GET  /__state              the state, with a log of every API call (tests check what the page sent)
//   POST /__update {...}       the updater's state: {state, latest, notes, progress, error, rolled_back, channel,
//                              check_result, auto}; every state the page can show must be scriptable here (a page
//                              bug once hid the Install button and nothing tested the "available" state)
//   POST /__key {key}          the display's key (null: no key needed)
//   POST /__setup              the page is on the setup network (no key needed, setup: true)
//   POST /__presence {...}     the screen dimming state (forge_presence): {state, mic_ok, imu_ok, level_db, cal,
//                              calibrating, ...}; {calibrating: false, cal: 'noisy'} ends a calibration as a noisy room
//   RTC in the mock: routes 800 (directions 0 / 1) and 11; route 800 direction 0 stops at 1025 and 1005, direction 1
//   at 1026; any other stop or route is "not served" (404). Route "999" makes the RTC unreachable.
const http = require('http');
const fs = require('fs');
const path = require('path');

const PAGE = path.join(__dirname, '..', '..', 'main', 'web', 'index.html');
const port = Number(process.argv[2] || process.env.PORT || 8099);
const KEY = process.env.MOCK_KEY || '0123456789abcdef';   // the display's key in the tests
const STATES = ['idle', 'checking', 'up_to_date', 'available', 'downloading', 'done', 'failed'];

// RTC's answers in the mock (shapes as main.c sends them)
const ROUTES = {
  800: { route: '800', name: 'Terminus Chute-Montmorency - Colline Parlementaire',
         dirs: [{ code: '0', name: 'Colline Parlementaire' }, { code: '1', name: 'Terminus Chute-Montmorency' }] },
  11: { route: '11', name: "Terminus Place-D'Youville - Pointe-de-Sainte-Foy",
        dirs: [{ code: '0', name: 'Pointe-de-Sainte-Foy' }, { code: '1', name: "Place-D'Youville" }] },
};
const STOPS = {
  '1025/800/0': { stop_name: 'St-Dominique', direction: 'Colline Parlementaire' },
  '1005/800/0': { stop_name: 'Champlain/1005', direction: 'Colline Parlementaire' },
  '1026/800/1': { stop_name: 'St-Dominique', direction: 'Terminus Chute-Montmorency' },
};

function fresh() {
  return {
    info: { app: 'rtc_quebec', version: 'v0.1.0-test', ip: '127.0.0.1', ssid: 'HomeNet', rssi: -52, uptime_s: 300,
            setup: false, lang: 'en', languages: [{ code: 'en', name: 'English' }, { code: 'fr', name: 'Français' }] },
    update: { current: 'v0.1.0-test', latest: '', channel: 'stable', state: 'idle', progress: 0, error: '',
              pending_verify: false, uptime_s: 300, notes: '', rolled_back: false },
    checkResult: 'up_to_date',                // what a check ends in (POST /__update {check_result})
    auto: false,                              // downloading moves on at each poll (after Install)
    scan: [{ ssid: 'HomeNet', rssi: -50, secure: true }, { ssid: 'Cafe', rssi: -75, secure: false }],
    wifi: null,                               // the last POST /api/wifi
    settings: [],                             // every POST /api/settings body
    key: KEY,                                 // POSTs and snapshots need X-Key (null: none)
    log: [],                                  // every API call: {method, url, body} or {..., refused}
    favs: [],                                 // the favourite stops, as POST /api/favs saved them
    presence: {                               // GET /api/presence (espforge's forge_presence: its shape, its defaults)
      ok: true, enabled: true, margin_db: 10, wake_s: 3, dim_s: 600, off_s: 3000, bright_pct: 100, dim_pct: 15,
      baseline_db: -60, level_db: -72, threshold_db: -50, state: 'active', wake_progress: 0, quiet_s: 12,
      calibrating: false, calib_left_s: 0, cal: 'none', cal_spread_db: 0, mic_ok: true, brightness: 100, imu_ok: true, motion_g: 0.01,
      motion_wake: true, motion_thr: 0.1,
    },
  };
}
let st = fresh();

function json(res, code, obj) {
  res.writeHead(code, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
  res.end(JSON.stringify(obj));
}

function readBody(req) {
  return new Promise(resolve => {
    let b = '';
    req.on('data', c => (b += c));
    req.on('end', () => resolve(b));
  });
}
const parse = b => { try { return b ? JSON.parse(b) : {}; } catch (e) { return null; } };

function updateView() {                          // GET /api/update: notes only while an update is offered
  const u = st.update;
  if (u.state === 'checking') u.state = st.checkResult;     // a check takes one poll
  else if (u.state === 'downloading' && st.auto) {
    u.progress = Math.min(100, u.progress + 50);
    if (u.progress === 100) u.state = 'done';
  }
  const out = { ...u };
  if (u.state !== 'available' || !u.notes) delete out.notes;
  if (!u.rolled_back) delete out.rolled_back;
  return out;
}

const routes = {
  'GET /api/info': () => [200, st.info],
  'GET /api/scan': () => [200, st.scan],
  'POST /api/wifi': b => {
    if (!b || typeof b.ssid !== 'string' || !b.ssid || b.ssid.length > 32) return [400, { error: 'ssid' }];
    if (typeof b.pass !== 'string' || b.pass.length > 64) return [400, { error: 'pass' }];
    st.wifi = { ssid: b.ssid, pass: b.pass };
    return [200, { ok: true }];
  },
  'GET /api/update': () => [200, updateView()],
  'POST /api/update': b => {
    if (!b) return [400, { error: 'json' }];
    if (b.channel) {
      if (!['stable', 'beta'].includes(b.channel)) return [400, { error: 'channel' }];
      st.update.channel = b.channel;
    }
    if (b.action === 'check') st.update.state = 'checking';
    else if (b.action === 'install') {
      if (st.update.state !== 'available') return [409, { error: 'nothing to install' }];
      st.update.state = 'downloading'; st.update.progress = 0; st.auto = true;
    } else if (b.action) return [400, { error: 'action' }];
    return [200, { ...st.update }];
  },
  'GET /api/settings': () => [200, { lang: st.info.lang }],
  'POST /api/settings': b => {
    if (!b) return [400, { error: 'json' }];
    if ('lang' in b) {
      if (!st.info.languages.some(l => l.code === b.lang)) return [400, { error: 'lang' }];
      st.info.lang = b.lang;
    }
    st.settings.push(b);
    return [200, { ok: true }];
  },
  'GET /api/snapshot': () => [404, { error: 'no snapshots in the mock' }],
  'GET /api/presence': () => {
    const p = st.presence;
    if (p.calibrating && --p.calib_left_s <= 0) {            // 1 s a poll; a quiet room: the new baseline
      Object.assign(p, { calibrating: false, calib_left_s: 0, cal: 'ok', cal_spread_db: 2, baseline_db: -66 });
      p.threshold_db = p.baseline_db + p.margin_db;
    }
    return [200, { ...p }];
  },
  'POST /api/presence': b => {                    // as presence_set_config(): clamped, baseline kept
    if (!b) return [400, { error: 'json' }];
    const p = st.presence;
    const num = (k, lo, hi) => { if (typeof b[k] === 'number') p[k] = Math.min(hi, Math.max(lo, b[k])); };
    if (typeof b.enabled === 'boolean') p.enabled = b.enabled;
    if (typeof b.motion_wake === 'boolean') p.motion_wake = b.motion_wake;
    num('margin_db', 1, 60); num('wake_s', 0.2, 60); num('dim_s', 1, 86400); num('off_s', 1, 86400);
    num('bright_pct', 5, 100); num('dim_pct', 1, 100); num('motion_thr', 0.02, 0.5);
    p.threshold_db = p.baseline_db + p.margin_db;
    p.state = 'active'; p.brightness = p.bright_pct;
    return [200, { ...p, ok: true }];
  },
  'POST /api/calibrate': b => {
    const p = st.presence;
    if (!p.mic_ok) return [200, { ok: false, why: 'no_mic' }];
    if (p.calibrating) return [200, { ok: false, why: 'busy' }];
    p.calibrating = true;
    p.calib_left_s = (b && b.seconds) || 5;
    return [200, { ...p, ok: true }];
  },
  'GET /api/favs': () => [200, { max: 8, favs: st.favs.map(f => ({ ...f, ...(STOPS[f.stop + '/' + f.route + '/' + f.dir] || {}) })) }],
  'POST /api/route': b => {
    if (b && b.route === '999') return [200, { ok: false, why: 'rtc' }];
    const r = b && ROUTES[b.route];
    return [200, r ? { ok: true, ...r } : { ok: false, why: 'no_route' }];
  },
  'POST /api/favs': b => {
    if (!b || !Array.isArray(b.favs) || b.favs.length > 8) return [200, { ok: false, bad: 0, why: 'invalid' }];
    for (let i = 0; i < b.favs.length; i++) {
      const f = b.favs[i];
      if (!/^\d{1,6}$/.test(f.stop) || !/^[0-9A-Za-z]{1,5}$/.test(f.route) || !/^\d{1,3}$/.test(f.dir))
        return [200, { ok: false, bad: i, why: 'invalid' }];
      if (f.route === '999') return [200, { ok: false, bad: i, why: 'rtc' }];
      if (!STOPS[f.stop + '/' + f.route + '/' + f.dir]) return [200, { ok: false, bad: i, why: 'not_served' }];
    }
    st.favs = b.favs.map(f => ({ stop: f.stop, route: f.route, dir: f.dir }));
    return [200, { ok: true }];
  },
};
const GUARDED = new Set(['GET /api/snapshot']);          // GETs that need the key too
const NOT_AP = new Set(['GET /api/snapshot']);           // refused on the setup network (web_route_t.not_ap in the firmware)

http.createServer(async (req, res) => {
  const url = req.url.split('?')[0];
  const route = req.method + ' ' + url;
  if (route === 'POST /__reset') { st = fresh(); return json(res, 200, { ok: true }); }
  if (route === 'GET /__state') return json(res, 200, st);
  if (route === 'POST /__setup') { st.info.setup = true; st.info.ssid = ''; return json(res, 200, { ok: true }); }
  if (route === 'POST /__key') {
    const b = parse(await readBody(req)) || {};
    st.key = b.key || null;
    return json(res, 200, { ok: true });
  }
  if (route === 'POST /__presence') {
    Object.assign(st.presence, parse(await readBody(req)) || {});
    return json(res, 200, st.presence);
  }
  if (route === 'POST /__update') {
    const b = parse(await readBody(req)) || {};
    if (b.state && !STATES.includes(b.state)) return json(res, 400, { error: 'state', states: STATES });
    if ('check_result' in b) { st.checkResult = b.check_result; delete b.check_result; }
    if ('auto' in b) { st.auto = !!b.auto; delete b.auto; }
    Object.assign(st.update, b);
    return json(res, 200, st.update);
  }
  if (req.method === 'GET' && (url === '/' || url === '/index.html')) {
    let page;
    try { page = fs.readFileSync(PAGE); } catch (e) {     // the page may not exist yet: the server still starts
      res.writeHead(404, { 'Content-Type': 'text/plain' });
      return res.end(`no ${path.relative(path.join(__dirname, '..', '..'), PAGE)} yet`);
    }
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' });
    return res.end(page);
  }
  const fn = routes[route];
  if (!fn) { res.writeHead(404); return res.end('not found'); }
  // The device's rules (forge_net web): some routes never on the setup network (403); the key on every POST and on
  // snapshots, except on the setup network; POST bodies are JSON
  if (st.info.setup && NOT_AP.has(route)) {
    st.log.push({ method: req.method, url, refused: 403 });
    return json(res, 403, { error: 'setup' });
  }
  if ((req.method === 'POST' || GUARDED.has(route)) && st.key && !st.info.setup && req.headers['x-key'] !== st.key) {
    st.log.push({ method: req.method, url, refused: 401 });
    return json(res, 401, { error: 'key' });
  }
  let b;
  if (req.method === 'POST') {
    if (!/^application\/json/.test(req.headers['content-type'] || '')) {
      st.log.push({ method: req.method, url, refused: 415 });
      return json(res, 415, { error: 'json only' });
    }
    b = parse(await readBody(req));
  }
  st.log.push({ method: req.method, url, body: b });
  const [code, out] = fn(b);
  json(res, code, out);
}).listen(port, () => console.log(`mock display on http://localhost:${port}/ (page: ${PAGE})`));
