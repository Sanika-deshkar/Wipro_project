/* ==========================================================================
   Smart Vehicle Safety Dashboard - web front end for the C backend

   This page contains NO simulation or safety logic of its own.  Every reading,
   alert, trip statistic and history record comes from the C program
   (`svsm --serve`), which runs the same simulator, rule engine and alert
   logger as the terminal version.  The browser only
        - polls  GET /api/state          to display the live data
        - sends  GET /api/cmd?a=...      to control the simulation
        - reads  GET /api/history        for the alert history

   1 constants  2 icons  3 state  4 C-backend link  5 screen  6 charts
   7 history  8 controls  9 start-up
   ========================================================================== */
'use strict';

/* ---------------------------- 1. constants ------------------------------- */
const TYPES  = ['OVERSPEED', 'LOW_FUEL', 'OVERHEAT', 'OBSTACLE', 'SEATBELT'];
const LEVELS = ['SAFE', 'WARNING', 'CRITICAL'];
const CLS    = ['ok', 'warn', 'crit'];
const BADGE  = ['OK', 'Warning', 'Critical'];
const POLL_MS = 250;

const PRESETS = {
  normal:    { speed: 60,  fuel: 70, temp: 85,  obst: 80, belt: true  },
  overspeed: { speed: 135, fuel: 60, temp: 92,  obst: 80, belt: true  },
  fuel:      { speed: 50,  fuel: 4,  temp: 80,  obst: 90, belt: true  },
  overheat:  { speed: 70,  fuel: 60, temp: 114, obst: 90, belt: true  },
  collision: { speed: 60,  fuel: 60, temp: 85,  obst: 2,  belt: true  },
  belt:      { speed: 70,  fuel: 60, temp: 85,  obst: 90, belt: false },
  all:       { speed: 150, fuel: 10, temp: 112, obst: 2,  belt: false }
};

/* the five monitored sensors: how each one is displayed (thresholds are shown
   for explanation only - the C program decides what is a warning) */
const SENSORS = [
  { t: 'OVERSPEED', key: 'speed', name: 'Speed', short: 'Speed',              icon: 'speed',    unit: 'km/h', max: 200, cmax: 200, warn: 80,  crit: 120,
    lo: 0,   hi: 220, step: 1,   dp: 0, marks: [[80, '80'], [120, '120']],
    value: () => S.speed.toFixed(0),  pct: () => S.speed / 200,
    rule: 'Warning above 80 km/h. Critical above 120 km/h.' },
  { t: 'LOW_FUEL',  key: 'fuel',  name: 'Fuel level', short: 'Fuel',         icon: 'fuel',     unit: '%',    max: 100, cmax: 100, warn: 15,  crit: 5,
    lo: 0,   hi: 100, step: 0.5, dp: 1, marks: [[5, '5'], [15, '15']],
    value: () => S.fuel.toFixed(1),   pct: () => S.fuel / 100,
    rule: 'Warning below 15%. Critical below 5%.' },
  { t: 'OVERHEAT',  key: 'temp',  name: 'Engine temperature', short: 'Engine', icon: 'temp',     unit: '°C',   max: 130, cmax: 130, warn: 100, crit: 110,
    lo: -20, hi: 150, step: 1,   dp: 0, marks: [[100, '100'], [110, '110']],
    value: () => S.temp.toFixed(0),   pct: () => S.temp / 130,
    rule: 'Warning above 100 °C. Critical above 110 °C.' },
  { t: 'OBSTACLE',  key: 'obst',  name: 'Obstacle distance', short: 'Obstacle',  icon: 'obstacle', unit: 'm',    max: 100, cmax: 100, warn: 10,  crit: 3,
    lo: 0,   hi: 200, step: 0.5, dp: 1, marks: [[3, '3'], [10, '10']],
    value: () => S.obst > 100 ? '100+' : S.obst.toFixed(1), pct: () => Math.min(S.obst, 100) / 100,
    rule: 'Warning closer than 10 m. Critical closer than 3 m.' },
  { t: 'SEATBELT',  key: 'belt',  name: 'Seatbelt', short: 'Seatbelt',           icon: 'belt',     unit: '',     max: 0,   cmax: 1,   warn: null, crit: null,
    lo: 0, hi: 1, step: 1, dp: 0, marks: [],
    value: () => S.belt ? 'Fastened' : 'Not fastened', pct: () => 0,
    rule: 'Alert if the car moves unbuckled. Critical above 40 km/h.' }
];

/* ------------------------------ 2. icons --------------------------------- */
const IC = {
  speed:    '<path d="M4 18a8 8 0 1 1 16 0"/><path d="M12 18l4-6"/>',
  gauge:    '<path d="M4 18a8 8 0 1 1 16 0"/><path d="M12 18l4-6"/>',
  fuel:     '<rect x="4" y="3" width="9" height="18" rx="1.5"/><path d="M7 7h3"/><path d="M13 8h2.5A1.5 1.5 0 0 1 17 9.5V16a1.5 1.5 0 0 0 3 0V9l-2-2"/>',
  temp:     '<path d="M10 14.5V5a2 2 0 0 1 4 0v9.5a4 4 0 1 1-4 0z"/><path d="M12 9v7"/>',
  obstacle: '<path d="M5 17l1.6-5.4A2 2 0 0 1 8.5 10h7a2 2 0 0 1 1.9 1.6L19 17"/><rect x="4" y="17" width="16" height="4" rx="1.5"/><path d="M12 3v4M7 5l1.5 2M17 5l-1.5 2"/>',
  belt:     '<circle cx="12" cy="5" r="2.3"/><path d="M7 21v-8a5 5 0 0 1 10 0v8"/><path d="M8 11l9 8"/>',
  play:     '<path d="M7 4l13 8-13 8z"/>',
  pause:    '<path d="M8 5v14M16 5v14"/>',
  reset:    '<path d="M3 12a9 9 0 1 0 3-6.7"/><path d="M3 4v5h5"/>',
  demo:     '<rect x="3" y="5" width="18" height="14" rx="2"/><path d="M10 9.5v5l4.5-2.5z"/>',
  up:       '<path d="M12 19V5M5 12l7-7 7 7"/>',
  down:     '<path d="M12 5v14M5 12l7 7 7-7"/>',
  sliders:  '<path d="M4 7h10M18 7h2M4 17h2M10 17h10"/><circle cx="16" cy="7" r="2"/><circle cx="8" cy="17" r="2"/>',
  sound:    '<path d="M4 9v6h4l5 4V5L8 9z"/><path d="M16.5 8.5a5 5 0 0 1 0 7"/>',
  clock:    '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
  route:    '<circle cx="6" cy="18" r="2"/><circle cx="18" cy="6" r="2"/><path d="M8 18h7a3 3 0 0 0 0-6H9a3 3 0 0 1 0-6h7"/>',
  trophy:   '<path d="M8 4h8v5a4 4 0 0 1-8 0z"/><path d="M8 6H4v2a3 3 0 0 0 4 3M16 6h4v2a3 3 0 0 1-4 3M12 13v4M8 20h8"/>',
  warning:  '<path d="M12 3l10 18H2z"/><path d="M12 10v5M12 18v.01"/>',
  check:    '<path d="M5 12.5l4.5 4.5L19 7.5"/>',
  critical: '<path d="M8 3h8l5 5v8l-5 5H8l-5-5V8z"/><path d="M12 7.5v6M12 16.5v.01"/>',
  history:  '<path d="M8 6h13M8 12h13M8 18h13"/><path d="M3 6h.01M3 12h.01M3 18h.01"/>',
  download: '<path d="M12 4v11M7 11l5 5 5-5M5 20h14"/>',
  trash:    '<path d="M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3"/>',
  info:     '<circle cx="12" cy="12" r="9"/><path d="M12 11v5M12 8v.01"/>',
  sun:      '<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/>',
  moon:     '<path d="M21 13A9 9 0 1 1 11 3a7 7 0 0 0 10 10z"/>',
  close:    '<path d="M6 6l12 12M18 6L6 18"/>',
  server:   '<rect x="3" y="4" width="18" height="6" rx="1.5"/><rect x="3" y="14" width="18" height="6" rx="1.5"/><path d="M7 7h.01M7 17h.01"/>'
};
const svg = n => `<svg viewBox="0 0 24 24" aria-hidden="true">${IC[n]}</svg>`;
function paintIcons(root = document) {
  root.querySelectorAll('[data-ic]').forEach(el => { el.innerHTML = svg(el.dataset.ic); });
}

/* ------------------------------ 3. state --------------------------------- */
const $ = id => document.getElementById(id);
const clamp = (x, lo, hi) => Math.min(hi, Math.max(lo, x));

/* mirror of what the C program reports */
const S = { speed: 0, fuel: 100, temp: 25, obst: 200, belt: false, target: 0, odo: 0, ticks: 0 };
let running = false, manual = false, demoOn = false, rate = 1;
let maxSpeed = 0, avgSpeed = 0;
let report = { a: {}, count: 0, overall: 0 }, prevReport = null;

/* page-only state */
let history = [], soundOn = false, audio = null;
const MAXS = 60;                               // samples kept for the charts
let samples = [], openType = null;

function sample() {
  samples.push({ speed: S.speed, fuel: S.fuel, temp: S.temp, obst: S.obst, belt: S.belt ? 1 : 0 });
  if (samples.length > MAXS) samples.shift();
}

/* ------------------------- 4. link to the C backend ---------------------- */
let BASE = null, online = false, fails = 0, busy = false;
let lastTicks = -1, lastSeq = -1, lastJson = '';
const API_STORAGE_KEY = 'svsm_backend_url';

async function jget(path, timeout = 3500) {
  const ctl = new AbortController();
  const t = setTimeout(() => ctl.abort(), timeout);
  try {
    const url = (BASE || '') + path;
    const r = await fetch(url, { signal: ctl.signal, cache: 'no-store' });
    if (!r.ok) throw new Error('HTTP ' + r.status);
    return await r.json();
  } finally { clearTimeout(t); }
}

function setOnline(on, msg) {
  online = on;
  $('offline').hidden = on;
  $('link').className = 'link ' + (on ? 'on' : 'off');
  $('linkText').textContent = on ? 'C backend' : 'Offline';
  const statusEl = $('offlineStatus');
  if (statusEl) {
    if (msg) statusEl.textContent = msg;
    else if (!on) statusEl.textContent = 'C backend not connected. In local development run make serve. If hosted on Render, please allow up to 45s for the free instance to spin up.';
  }
}

function getBackendCandidates() {
  const cands = [];
  const q = new URLSearchParams(location.search).get('api');
  if (q && q.trim()) {
    const clean = q.trim().replace(/\/$/, '');
    try { localStorage.setItem(API_STORAGE_KEY, clean); } catch (e) {}
    cands.push(clean);
  }
  try {
    const stored = localStorage.getItem(API_STORAGE_KEY);
    if (stored && stored.trim() && !cands.includes(stored.trim().replace(/\/$/, ''))) {
      cands.push(stored.trim().replace(/\/$/, ''));
    }
  } catch (e) {}
  if (window.BACKEND_API_URL && !cands.includes(window.BACKEND_API_URL.replace(/\/$/, ''))) {
    cands.push(window.BACKEND_API_URL.replace(/\/$/, ''));
  }
  if (location.protocol.startsWith('http') && !cands.includes('')) {
    cands.push(''); // Netlify proxy or same-host deployment
  }
  cands.push('http://localhost:8080', 'http://127.0.0.1:8080');
  return cands;
}

/**
 * Probe a candidate URL. For remote cloud backends (e.g. Render), retries with
 * exponential backoff to gracefully handle cold-start wakeups (usually 20-45s).
 */
async function probeCandidate(cand) {
  const isLocal = cand.includes('localhost') || cand.includes('127.0.0.1');
  const maxRetries = isLocal ? 1 : 4;
  let delay = 1500;

  for (let attempt = 1; attempt <= maxRetries; attempt++) {
    const timeout = isLocal ? 1200 : (2500 + attempt * 2000);
    const ctl = new AbortController();
    const t = setTimeout(() => ctl.abort(), timeout);

    if (!isLocal && attempt > 1) {
      setOnline(false, `Waking up C backend on Render... attempt ${attempt}/${maxRetries} (please wait)`);
    }

    try {
      const url = (cand || '') + '/api/state';
      const res = await fetch(url, { signal: ctl.signal, cache: 'no-store' });
      clearTimeout(t);
      if (res.ok) {
        const data = await res.json();
        return data;
      }
      if (res.status === 404) return null; // immediate 404: skip candidate
    } catch (e) {
      clearTimeout(t);
      if (isLocal) return null;
    }

    if (attempt < maxRetries) {
      await new Promise(r => setTimeout(r, delay));
      delay *= 2; // exponential backoff
    }
  }
  return null;
}

/** Find the server: ?api=URL, stored URL, same origin (proxy), then localhost:8080. */
async function findBackend() {
  const cands = getBackendCandidates();
  for (const c of cands) {
    const data = await probeCandidate(c);
    if (data) {
      BASE = c;
      applyState(data, false);
      setOnline(true);
      return true;
    }
  }
  BASE = null;
  setOnline(false);
  return false;
}

async function poll() {
  if (busy) return;
  if (BASE === null) { if (!await findBackend()) return; refreshHistory(); return; }
  busy = true;
  try { applyState(await jget('/api/state', 3500), false); }
  catch (e) { if (++fails >= 3) { setOnline(false); BASE = null; } }
  finally { busy = false; }
}

/** Send a command to the C program and show the state it returns. */
async function send(qs) {
  if (BASE === null) return;
  try { applyState(await jget('/api/cmd?' + qs, 3500), true); }
  catch (e) { if (++fails >= 3) { setOnline(false); BASE = null; } }
}

/** Rate-limited send for sliders (keeps the newest value). */
const lastSent = {}, queued = {};
function sendSoon(key, qs) {
  const now = Date.now();
  if (!lastSent[key] || now - lastSent[key] > 90) { lastSent[key] = now; send(qs); return; }
  queued[key] = qs;
  if (!queued[key + '#t']) queued[key + '#t'] = setTimeout(() => {
    queued[key + '#t'] = null; lastSent[key] = Date.now(); send(queued[key]);
  }, 90);
}

let mlState = { on: false };

function applyState(j, fromCmd) {
  const prevTicks = lastTicks, wasDemo = demoOn;
  fails = 0;
  if (!online) setOnline(true);

  Object.assign(S, j.v, { ticks: j.ticks });
  running = j.running; manual = j.manual; demoOn = j.demo; rate = j.rate;
  maxSpeed = j.max_speed; avgSpeed = j.avg_speed;
  report = { a: j.report.alerts, count: j.report.count, overall: j.report.overall };
  mlState = j.ml || { on: false };

  if (j.ticks < prevTicks) samples = [];                    // simulation was reset
  if (j.ticks !== prevTicks || fromCmd || !samples.length) sample();
  lastTicks = j.ticks;

  if (wasDemo && !j.demo && !j.running && j.ticks >= 45) say('Demo finished.');
  if (j.seq !== lastSeq) { lastSeq = j.seq; refreshHistory(); }

  const sig = JSON.stringify(j);
  if (fromCmd || sig !== lastJson || !prevReport) { lastJson = sig; render(); }
}

/* ------------------------------ 5. screen -------------------------------- */
const fmtTime = s => [Math.floor(s / 3600), Math.floor(s / 60) % 60, s % 60]
  .map(n => String(n).padStart(2, '0')).join(':');
const STATUS_ICON = ['check', 'warning', 'critical'];
const SUBTEXT = ['No active alerts - everything is normal.', null, null];

function buildScreen() {
  $('sensors').innerHTML = SENSORS.map(s => `
    <article class="sensor ok" id="s-${s.t}" data-k="${s.key}" tabindex="0" role="button" title="${s.name}: ${s.rule} Click for details."
             aria-label="${s.name} - open details">
      <div class="s-head"><span class="ic">${svg(s.icon)}</span><h3>${s.short}</h3><span class="badge">OK</span></div>
      <div class="s-val"><b class="${s.max ? '' : 'text'}">0</b><small>${s.unit}</small></div>
      ${s.max ? `<div class="meter"><div class="fill"></div>${s.marks.map(m =>
          `<i class="mark" style="left:${m[0] / s.max * 100}%"></i>`).join('')}</div>`
        : '<div class="belt-gap"></div>'}
      <div class="spark"></div>
    </article>`).join('');

  $('trip').innerHTML = [
    ['clock', 'Time', 'tTime'], ['route', 'Distance', 'tOdo'], ['speed', 'Target', 'tTarget'],
    ['trophy', 'Top speed', 'tMax'], ['history', 'Alerts', 'tAlerts']
  ].map(([ic, l, id]) => `<div class="chip"><span>${svg(ic)}${l}</span><b id="${id}">-</b></div>`).join('');

  const guide = [
    ['speed',    '', 'Speed',       'Amber above 80 km/h, red above 120.'],
    ['fuel',     '', 'Fuel',        'Amber below 15%, red below 5%.'],
    ['temp',     '', 'Engine',      'Amber above 100 °C, red above 110 °C.'],
    ['obstacle', '', 'Obstacle',    'Amber under 10 m, red under 3 m.'],
    ['belt',     '', 'Seatbelt',    'Alerts if moving unbuckled; red above 40 km/h.'],
    ['check',    'ok', 'Green / SAFE',      'Nothing is wrong.'],
    ['warning',  'warn', 'Amber / WARNING', 'A limit has been passed.'],
    ['critical', 'crit', 'Red / CRITICAL',  'Dangerous - act now.'],
    ['play',     '', 'Start / Stop', 'Runs or pauses the drive.'],
    ['reset',    '', 'Reset',       'Full tank, parked. History kept.'],
    ['demo',     '', 'Demo',        '45-second drive that triggers alerts.'],
    ['up',       '', 'Faster / Slower', 'Target speed +/- 10 km/h.'],
    ['sliders',  '', 'Manual',      'Set the sensor values yourself.'],
    ['sound',    '', 'Alert sound', 'Beep on each new alert.'],
    ['history',  '', 'History',     'Written by the C program to alert_history.log.'],
    ['download', '', 'Download',    'Export the real log file or a CSV.'],
    ['server',   '', 'C backend',   'Every value on this page comes from the C program.'],
    ['info',     '', 'Keys',        'S R W X B F M D T']
  ];
  $('guide').innerHTML = guide.map(([ic, c, t, d]) =>
    `<div class="g ${c}"><span class="ic">${svg(ic)}</span><div><b>${t}</b><span>${d}</span></div></div>`).join('');
}

function showTab(name) {
  document.querySelectorAll('.tab').forEach(t => t.setAttribute('aria-selected', t.dataset.tab === name));
  document.querySelectorAll('.pane').forEach(p => { p.hidden = p.dataset.pane !== name; });
}
function showView(name) {
  $('app').dataset.view = name;
  document.querySelectorAll('.nav button').forEach(b => b.classList.toggle('on', b.dataset.view === name));
}

function render() {
  const rep = report;
  const mlc = $('mlChip');                                  // ML prediction (independent of the rules)
  if (mlc) {
    if (mlState.label) {
      mlc.dataset.level = ['ok', 'warn', 'crit'][mlState.level];
      $('mlText').textContent = `${mlState.label} (${Math.round(mlState.conf * 100)}%)`;
    } else { mlc.dataset.level = 'off'; $('mlText').textContent = 'n/a'; }
  }
  announceNewAlerts(rep);

  /* overall banner */
  $('status').dataset.level = CLS[rep.overall];
  $('statusIcon').innerHTML = svg(STATUS_ICON[rep.overall]);
  $('statusText').textContent = LEVELS[rep.overall];
  $('statusSub').textContent = SUBTEXT[rep.overall] ||
    `${rep.count} active alert${rep.count > 1 ? 's' : ''} - see the list below.`;

  /* sensor cards */
  SENSORS.forEach(s => {
    const el = $('s-' + s.t), lvl = rep.a[s.t] ? rep.a[s.t].level : 0;
    el.className = 'sensor ' + CLS[lvl];
    el.querySelector('.badge').textContent = BADGE[lvl];
    el.querySelector('.s-val b').textContent = s.value();
    const fill = el.querySelector('.fill');
    if (fill) fill.style.width = (clamp(s.pct(), 0, 1) * 100).toFixed(1) + '%';
    el.querySelector('.spark').innerHTML = sparkSVG(s);
  });

  /* alert list */
  $('alertCount').textContent = rep.count;
  const items = TYPES.filter(t => rep.a[t]).sort((x, y) => rep.a[y].level - rep.a[x].level);
  $('alertList').innerHTML = items.length
    ? items.map(t => `<li class="${CLS[rep.a[t].level]}" data-t="${t}" title="Click for details"><span class="ic">${svg(SENSORS.find(s => s.t === t).icon)}</span>` +
        `<div><b>${LEVELS[rep.a[t].level]}</b>${esc(rep.a[t].msg)}</div></li>`).join('')
    : `<li class="none"><span class="ic">${svg('check')}</span><div>No alerts. All five checks are normal.</div></li>`;

  /* state pill + start button */
  $('state').className = 'state ' + (manual ? 'manual' : running ? 'running' : '');
  $('stateText').textContent = manual ? 'Manual input' : running ? 'Running' : 'Stopped';
  $('btnStart').classList.toggle('stop', running);
  $('startText').textContent = running ? 'Stop' : 'Start';
  $('startIc').innerHTML = svg(running ? 'pause' : 'play');

  /* trip chips */
  $('tTime').textContent   = fmtTime(S.ticks);
  $('tOdo').textContent    = S.odo.toFixed(2) + ' km';
  $('tTarget').textContent = S.target.toFixed(0) + ' km/h';
  $('tMax').textContent    = maxSpeed.toFixed(0) + ' km/h';
  $('tAlerts').textContent = history.length;

  /* controls mirror the C state (a slider being dragged is left alone) */
  syncSlider('throttle', 'thrVal', S.target, 0);
  $('belt').checked = S.belt;
  $('manualMode').checked = manual;
  document.querySelectorAll('#rateSeg button').forEach(b => b.classList.toggle('on', +b.dataset.rate === rate));
  syncSlider('mSpeed', 'mSpeedV', S.speed, 0);
  syncSlider('mFuel',  'mFuelV',  S.fuel,  1);
  syncSlider('mTemp',  'mTempV',  S.temp,  0);
  syncSlider('mObst',  'mObstV',  S.obst,  1);
  updateDetail();
}

function syncSlider(id, labelId, value, dp) {
  const el = $(id);
  if (document.activeElement !== el) el.value = value;
  $(labelId).textContent = (+el.value).toFixed(dp);
}

const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
function say(msg) { $('notice').textContent = msg; }

/** Pop-up + beep when the C engine reports a NEW or WORSE alert. */
function announceNewAlerts(rep) {
  if (prevReport) {
    TYPES.forEach(t => {
      const c = rep.a[t], p = prevReport.a[t];
      if (!c || (p && c.level <= p.level)) return;
      beep(c.level);
      toast(c.level, `${LEVELS[c.level]} - ${t.replace('_', ' ')}`, esc(c.msg));
    });
  }
  prevReport = rep;
}

/* ------------------------- 6. charts (sparkline + detail) ---------------- */
function seriesOf(def) { return samples.map(p => p[def.key]); }

function sparkSVG(def) {
  const vals = seriesOf(def), W = 120, H = 30;
  if (vals.length < 2) return '';
  const y = v => (H - 2 - clamp(v / def.cmax, 0, 1) * (H - 4)).toFixed(1);
  const x = i => (i / (MAXS - 1) * W).toFixed(1);
  const pts = vals.map((v, i) => `${x(i)},${y(v)}`).join(' ');
  const warn = def.warn != null ? `<line x1="0" x2="${W}" y1="${y(def.warn)}" y2="${y(def.warn)}" class="sp-thr"/>` : '';
  return `<svg viewBox="0 0 ${W} ${H}" preserveAspectRatio="none" aria-hidden="true">${warn}` +
         `<polyline points="${x(0)},${H} ${pts} ${x(vals.length - 1)},${H}" class="sp-area"/>` +
         `<polyline points="${pts}" class="sp-line"/></svg>`;
}

function bigChart(def) {
  const vals = seriesOf(def), W = 640, H = 200, L = 46, R = 12, T = 12, B = 24;
  const pw = W - L - R, ph = H - T - B;
  const Y = v => T + ph - clamp(v / def.cmax, 0, 1) * ph;
  const X = i => L + i / (MAXS - 1) * pw;
  const isBelt = def.key === 'belt';
  let h = `<svg viewBox="0 0 ${W} ${H}" class="chart" role="img" aria-label="${def.name}, last ${MAXS} samples">`;
  [0, 0.5, 1].forEach(f => {
    const yy = T + ph - f * ph;
    const label = isBelt ? ['Open', '', 'Fastened'][f * 2] : Math.round(f * def.cmax);
    h += `<line x1="${L}" x2="${W - R}" y1="${yy}" y2="${yy}" class="grid"/><text x="${L - 6}" y="${yy + 4}" text-anchor="end" class="axis">${label}</text>`;
  });
  if (!isBelt) {
    [['warn', 'Warning', def.warn], ['crit', 'Critical', def.crit]].forEach(([c, n, v]) => {
      h += `<line x1="${L}" x2="${W - R}" y1="${Y(v)}" y2="${Y(v)}" class="thr thr-${c}"/>` +
           `<text x="${W - R - 4}" y="${Y(v) - 4}" text-anchor="end" class="thr-t thr-${c}">${n} ${v} ${def.unit}</text>`;
    });
  }
  if (vals.length > 1) {
    const pts = vals.map((v, i) => `${X(i).toFixed(1)},${Y(v).toFixed(1)}`).join(' ');
    h += `<polygon points="${X(0)},${T + ph} ${pts} ${X(vals.length - 1)},${T + ph}" class="ch-area"/>` +
         `<polyline points="${pts}" class="ch-line"/>` +
         `<circle cx="${X(vals.length - 1)}" cy="${Y(vals[vals.length - 1])}" r="4.5" class="ch-dot"/>`;
  } else {
    h += `<text x="${W / 2}" y="${H / 2}" text-anchor="middle" class="axis">Press Start to see the chart fill in</text>`;
  }
  h += `<text x="${L}" y="${H - 6}" class="axis">${MAXS} samples ago</text><text x="${W - R}" y="${H - 6}" text-anchor="end" class="axis">now</text></svg>`;
  return h;
}

function openDetail(type) {
  const def = SENSORS.find(s => s.t === type); if (!def) return;
  openType = type;
  const tryIt = def.key === 'belt'
    ? `<label class="switch"><input type="checkbox" id="dBelt"><i></i><span><b>Seatbelt fastened</b><small>Toggle and watch the alert react</small></span></label>`
    : `<label class="slider"><span>Drag to simulate a different ${def.name.toLowerCase()} <b><u id="dTryV"></u> ${def.unit}</b></span>` +
      `<input type="range" id="dTry" min="${def.lo}" max="${def.hi}" step="${def.step}"></label>`;
  $('detail').innerHTML = `
    <div class="d-head"><span class="ic">${svg(def.icon)}</span><h2>${def.name}</h2><span class="badge" id="dBadge">OK</span>
      <button class="icon-btn" id="dClose" aria-label="Close">${svg('close')}</button></div>
    <div class="d-val"><b id="dVal">0</b><small>${def.unit}</small></div>
    <div id="dChart"></div>
    <div class="d-stats" id="dStats"></div>
    <p class="rule">${def.rule}</p>
    <div class="d-try"><h3>Try it yourself</h3>${tryIt}<p class="hint">Moving this switches on Manual input in the C program so you can see how alerts react.</p></div>`;
  paintIcons($('detail'));
  $('dClose').onclick = () => $('detail').close();
  const t = $('dTry');
  if (t) t.oninput = e => { $('dTryV').textContent = (+e.target.value).toFixed(def.dp); sendSoon('d-try', `a=set&manual=1&${def.key}=${e.target.value}`); };
  const b = $('dBelt');
  if (b) b.onchange = e => send('a=belt&v=' + (e.target.checked ? 1 : 0));
  if (!$('detail').open) $('detail').showModal();
  updateDetail();
}

function updateDetail() {
  if (!openType || !$('detail').open) return;
  const def = SENSORS.find(s => s.t === openType);
  const lvl = report.a[def.t] ? report.a[def.t].level : 0;
  $('detail').className = CLS[lvl];
  $('dBadge').textContent = BADGE[lvl];
  $('dVal').textContent = def.value();
  $('dChart').innerHTML = bigChart(def);
  const v = seriesOf(def);
  const f = x => x.toFixed(def.dp);
  const avg = v.length ? v.reduce((a, b) => a + b, 0) / v.length : 0;
  $('dStats').innerHTML = v.length
    ? (def.key === 'belt'
        ? `<span>Time fastened <b>${(avg * 100).toFixed(0)}%</b></span>`
        : `<span>Lowest <b>${f(Math.min(...v))}</b></span><span>Highest <b>${f(Math.max(...v))}</b></span><span>Average <b>${f(avg)}</b></span>`) +
      `<span>Samples <b>${v.length}</b></span>`
    : '<span>No data yet</span>';
  const t = $('dTry');
  if (t) { if (document.activeElement !== t) t.value = S[def.key]; $('dTryV').textContent = (+t.value).toFixed(def.dp); }
  const b = $('dBelt'); if (b) b.checked = S.belt;
}

/* ------------------------------ toasts / theme --------------------------- */
function toast(level, title, msg) {
  const t = document.createElement('div');
  t.className = 'toast ' + CLS[level];
  t.innerHTML = `<span class="ic">${svg(level === 2 ? 'critical' : 'warning')}</span><div><b>${title}</b>${msg}</div>`;
  t.onclick = () => t.remove();
  $('toasts').appendChild(t);
  setTimeout(() => t.remove(), 5000);
  while ($('toasts').children.length > 4) $('toasts').firstChild.remove();
}
/* The white (light) theme is the default; the toggle remembers a later choice. */
const THEME_KEY = 'svsm.theme.v2';
function setTheme(t) {
  document.documentElement.dataset.theme = t;
  $('btnTheme').innerHTML = svg(t === 'dark' ? 'sun' : 'moon');
  try { localStorage.setItem(THEME_KEY, t); } catch (e) { /* ignore */ }
}
function initTheme() {
  let t = null;
  try { t = localStorage.getItem(THEME_KEY); } catch (e) { /* ignore */ }
  setTheme(t === 'dark' ? 'dark' : 'light');
}

function beep(level) {
  if (!soundOn || !audio || level === 0) return;
  const play = (f, at, dur) => {
    const o = audio.createOscillator(), g = audio.createGain(), t0 = audio.currentTime + at;
    o.type = 'square'; o.frequency.value = f;
    g.gain.setValueAtTime(0.0001, t0);
    g.gain.exponentialRampToValueAtTime(0.06, t0 + 0.01);
    g.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);
    o.connect(g).connect(audio.destination); o.start(t0); o.stop(t0 + dur + 0.02);
  };
  if (level === 2) { play(880, 0, 0.14); play(880, 0.2, 0.14); } else play(520, 0, 0.16);
}

/* ------------------------------ 7. history ------------------------------- */
/** The history lives in the C program's alert_history.log file. */
async function refreshHistory() {
  if (BASE === null) return;
  try { history = await jget('/api/history?n=200'); } catch (e) { return; }
  renderHistory();
}

function renderHistory() {
  const f = $('filter').value;
  const q = ($('search').value || '').trim().toLowerCase();
  const rows = history.filter(h => (f === 'ALL' || h.level === f) &&
    (!q || `${h.type} ${h.msg} ${h.level} ${h.ts}`.toLowerCase().includes(q))).slice(0, 150);
  $('histBody').innerHTML = rows.map(h => {
    const c = h.level === 'CRITICAL' ? 'crit' : 'warn';
    return `<tr><td title="${esc(h.ts)}">${esc(h.ts.slice(11))}</td><td><span class="pill ${c}">${esc(h.level)}</span></td>` +
           `<td>${esc(h.msg)}<small>${esc(h.type.replace('_', ' '))} · ${h.spd} km/h · fuel ${h.fuel}% · ${h.temp} °C · ${h.obst} m · belt ${esc(h.belt)}</small></td></tr>`;
  }).join('');
  $('histEmpty').style.display = rows.length ? 'none' : 'block';
  const n = $('tAlerts'); if (n) n.textContent = history.length;
}

/** Download the real file / CSV generated by the C program. */
function exportFile(path) {
  if (BASE === null) return;
  const a = document.createElement('a');
  a.href = (BASE || '') + path;
  document.body.appendChild(a); a.click(); a.remove();
}

/* ----------------------------- 8. controls ------------------------------- */
/** Pop-up question: resolves true (fastened), false (not fastened) or null (cancelled). */
function askSeatbelt() {
  return new Promise(resolve => {
    const d = $('beltAsk');
    let answer = null;
    d.onclose = () => resolve(answer);
    $('beltYes').onclick    = () => { answer = true;  d.close(); };
    $('beltNo').onclick     = () => { answer = false; d.close(); };
    $('beltCancel').onclick = () => { answer = null;  d.close(); };
    d.onclick = e => { if (e.target === d) d.close(); };        // click outside = cancel
    if (!d.open) d.showModal();
    $('beltYes').focus();
  });
}

async function startStop() {
  if (running) { send('a=stop'); say('Simulation stopped.'); return; }
  const belt = await askSeatbelt();
  if (belt === null) { say('Start cancelled.'); return; }
  send('a=start&belt=' + (belt ? 1 : 0));
  say(belt ? 'Seatbelt fastened. Simulation running.' : 'Seatbelt NOT fastened - expect a seatbelt alert once you move.');
}
function doReset()   { samples = []; send('a=reset'); say('Reset. The alert history was kept.'); }
async function runDemo() {
  const belt = await askSeatbelt();
  if (belt === null) { say('Demo cancelled.'); return; }
  samples = [];
  send('a=demo&belt=' + (belt ? 1 : 0));
  say('Demo running: overspeed, no seatbelt, low fuel...');
}
function setManual(on) { send('a=manual&v=' + (on ? 1 : 0)); if (on) showTab('manual'); }

function bindControls() {
  $('btnStart').onclick  = startStop;
  $('btnReset').onclick  = doReset;
  $('btnDemo').onclick   = runDemo;
  $('btnAccel').onclick  = () => send('a=accel');
  $('btnBrake').onclick  = () => send('a=brake');
  $('btnRefuel').onclick = () => { send('a=refuel'); say('Tank filled to 100%.'); };
  $('throttle').oninput  = e => { $('thrVal').textContent = e.target.value; sendSoon('thr', 'a=target&v=' + e.target.value); };
  $('belt').onchange     = e => send('a=belt&v=' + (e.target.checked ? 1 : 0));
  $('manualMode').onchange = e => setManual(e.target.checked);
  $('sound').onchange    = e => {
    soundOn = e.target.checked;
    if (soundOn && !audio) { try { audio = new (window.AudioContext || window.webkitAudioContext)(); } catch (x) { soundOn = false; } }
    if (soundOn) beep(1);
  };
  $('retry').onclick = () => { findBackend().then(ok => { if (ok) refreshHistory(); }); };
  const btnSetApi = $('btnSetApi');
  if (btnSetApi) {
    btnSetApi.onclick = () => {
      let current = '';
      try { current = localStorage.getItem(API_STORAGE_KEY) || ''; } catch (e) {}
      const url = prompt('Enter your Render backend URL (e.g. https://your-service.onrender.com):', current);
      if (url !== null) {
        const clean = url.trim().replace(/\/$/, '');
        try {
          if (clean) localStorage.setItem(API_STORAGE_KEY, clean);
          else localStorage.removeItem(API_STORAGE_KEY);
        } catch (e) {}
        findBackend().then(ok => { if (ok) refreshHistory(); });
      }
    };
  }

  [['mSpeed', 'speed', 'mSpeedV', 0], ['mFuel', 'fuel', 'mFuelV', 1], ['mTemp', 'temp', 'mTempV', 0], ['mObst', 'obst', 'mObstV', 1]].forEach(([id, key, lab, dp]) => {
    $(id).oninput = e => { $(lab).textContent = (+e.target.value).toFixed(dp); sendSoon('m-' + key, `a=set&manual=1&${key}=${e.target.value}`); };
  });
  document.querySelectorAll('[data-preset]').forEach(b => b.onclick = () => {
    const p = PRESETS[b.dataset.preset];
    send(`a=set&manual=1&speed=${p.speed}&fuel=${p.fuel}&temp=${p.temp}&obst=${p.obst}&belt=${p.belt ? 1 : 0}`);
  });
  document.querySelectorAll('#rateSeg button').forEach(b => b.onclick = () => send('a=rate&v=' + b.dataset.rate));

  document.querySelectorAll('.tab').forEach(t => t.onclick = () => showTab(t.dataset.tab));
  document.querySelectorAll('.nav button').forEach(b => b.onclick = () => showView(b.dataset.view));
  $('filter').onchange = renderHistory;
  $('search').oninput  = renderHistory;
  $('btnTheme').onclick = () => setTheme(document.documentElement.dataset.theme === 'dark' ? 'light' : 'dark');

  /* click / Enter on a sensor card or an alert opens its detail window */
  $('sensors').addEventListener('click', e => { const c = e.target.closest('.sensor'); if (c) openDetail(c.id.slice(2)); });
  $('sensors').addEventListener('keydown', e => {
    const c = e.target.closest('.sensor');
    if (c && (e.key === 'Enter' || e.key === ' ')) { e.preventDefault(); openDetail(c.id.slice(2)); }
  });
  $('alertList').addEventListener('click', e => { const li = e.target.closest('li[data-t]'); if (li) openDetail(li.dataset.t); });
  $('detail').addEventListener('click', e => { if (e.target === $('detail')) $('detail').close(); });   // click outside = close
  $('detail').addEventListener('close', () => { openType = null; });

  /* keyboard shortcuts */
  document.addEventListener('keydown', e => {
    if (e.ctrlKey || e.metaKey || e.altKey || $('detail').open || $('beltAsk').open) return;
    const tag = e.target.tagName, type = e.target.type;
    if ((tag === 'INPUT' && type !== 'range' && type !== 'checkbox') || tag === 'SELECT' || tag === 'TEXTAREA') return;
    switch (e.key.toLowerCase()) {
      case 's': startStop(); break;
      case 'r': doReset(); break;
      case 'w': send('a=accel'); break;
      case 'x': send('a=brake'); break;
      case 'b': send('a=belt'); break;
      case 'f': send('a=refuel'); say('Tank filled to 100%.'); break;
      case 'm': setManual(!manual); break;
      case 'd': runDemo(); break;
      case 't': $('btnTheme').click(); break;
      default: return;
    }
    e.preventDefault();
  });
  $('btnLog').onclick = () => exportFile('/api/export.log');
  $('btnCsv').onclick = () => exportFile('/api/export.csv');
  $('btnClear').onclick = () => { if (confirm('Clear the whole alert history file?')) send('a=clear'); };
}

/* -------------------------------- 9. start-up ---------------------------- */
async function boot() {
  paintIcons();
  buildScreen();
  bindControls();
  initTheme();
  renderHistory();
  render();

  const ok = await findBackend();
  if (ok) await refreshHistory();
  setInterval(poll, POLL_MS);

  /* deep links (handy for screenshots): index.html#demo&dark&open=OVERSPEED  or  #all */
  for (const tok of location.hash.slice(1).split('&')) {
    if (tok === 'demo') runDemo();
    else if (tok === 'ask') startStop();
    else if (tok === 'dark' || tok === 'light') setTheme(tok);
    else if (PRESETS[tok]) document.querySelector(`[data-preset="${tok}"]`).click();
    else if (tok.startsWith('open=')) setTimeout(() => openDetail(tok.slice(5)), 700);
    else if (tok.startsWith('tab=')) showTab(tok.slice(4));
    else if (tok.startsWith('view=')) showView(tok.slice(5));
  }
}
boot();
