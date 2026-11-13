(() => {
  'use strict';

  const MIN = 1, MAX = 9999, R_MIN = 15, R_MAX = 480;
  const PRESETS = [30, 60, 75, 120, 144, 165, 240, 360];
  const TICKS = [60, 120, 180, 240, 300, 360, 420, 480];
  const $ = id => document.getElementById(id);
  const host = window.chrome && window.chrome.webview;
  const send = m => host && host.postMessage(m);

  const el = {
    input: $('fpsInput'), range: $('fpsRange'), fill: $('fill'), ticks: $('ticks'), presets: $('presets'),
    badge: $('syncBadge'), frame: $('frameTime'), sFile: $('statFile'), sHz: $('statHz'), sDelta: $('statDelta'),
    apply: $('btnApply'), revert: $('btnRevert'), lock: $('tglLock'), enforce: $('tglEnforce'),
    enforceTarget: $('enforceTarget'), path: $('filePath'), tag: $('fileTag'), size: $('fileSize'),
    mod: $('fileMod'), backup: $('fileBackup'), pill: $('robloxPill'), log: $('log'),
    open: $('btnOpen'), reload: $('btnReload'), bak: $('btnBackup'), restore: $('btnRestore')
  };

  let state = null;
  let value = 240;
  let touched = false;
  let syncNext = false;
  const resync = cmd => () => { syncNext = true; touched = false; el.input.blur(); send(cmd); };
  let doneTimer = 0;
  const chips = [];

  const clamp = v => Math.min(MAX, Math.max(MIN, v));
  const parse = () => {
    const v = parseInt(el.input.value, 10);
    return Number.isFinite(v) && v >= MIN && v <= MAX ? v : null;
  };

  function buildStatic() {
    const frag = document.createDocumentFragment();
    TICKS.forEach(t => {
      const s = document.createElement('span');
      s.textContent = t;
      s.style.left = ((t - R_MIN) / (R_MAX - R_MIN) * 100) + '%';
      frag.appendChild(s);
    });
    el.ticks.appendChild(frag);

    const add = (v, label, extra) => {
      const b = document.createElement('button');
      b.className = 'chip';
      b.dataset.v = v;
      b.textContent = label;
      if (extra) {
        const sm = document.createElement('small');
        sm.textContent = extra;
        b.appendChild(sm);
      }
      b.addEventListener('click', () => { setValue(+b.dataset.v, true); el.input.focus(); });
      el.presets.appendChild(b);
      chips.push(b);
      return b;
    };
    PRESETS.forEach(v => add(v, String(v)));
    chips.hz = add(60, '60', 'Hz');
    chips.hz.title = 'Match display refresh rate';
    add(MAX, 'Max').title = 'Uncapped (9999)';
    clearLog();
  }

  function setValue(v, user) {
    value = clamp(v);
    if (user) touched = true;
    if (document.activeElement !== el.input || parse() !== value) el.input.value = value;
    el.input.classList.remove('bad');
    render();
  }

  function render() {
    const v = parse();
    const valid = v !== null;
    el.input.classList.toggle('bad', !valid);

    const r = Math.min(R_MAX, Math.max(R_MIN, valid ? v : value));
    el.range.value = r;
    el.fill.style.width = ((r - R_MIN) / (R_MAX - R_MIN) * 100) + '%';
    el.frame.textContent = valid ? (v >= MAX ? '~0 ms' : (1000 / v).toFixed(2) + ' ms') : '-';
    chips.forEach(c => c.classList.toggle('active', valid && +c.dataset.v === v));

    const found = !!(state && state.found);
    document.body.classList.toggle('nofile', !found);
    const fileFps = found && state.hasKey ? state.fps : null;
    const dirty = valid && v !== fileFps;

    let cls = '', text = 'Loading';
    if (state) {
      if (!found) { cls = 'err'; text = 'No settings file'; }
      else if (!valid) { cls = 'err'; text = 'Invalid'; }
      else if (dirty) { cls = 'warn'; text = 'Unsaved'; }
      else { cls = 'ok'; text = 'Synced'; }
    }
    el.badge.className = 'badge ' + cls;
    el.badge.textContent = text;

    if (valid && fileFps !== null) {
      const d = v - fileFps;
      el.sDelta.textContent = d === 0 ? '0' : (d > 0 ? '+' : '') + d;
      el.sDelta.className = 'num' + (d > 0 ? ' up' : d < 0 ? ' down' : '');
    } else {
      el.sDelta.textContent = '-';
      el.sDelta.className = 'num';
    }

    el.apply.disabled = !found || !valid || !dirty;
    el.revert.disabled = !found || fileFps === null || !dirty;
  }

  function fmtSize(n) {
    return n < 1024 ? n + ' B' : (n / 1024).toFixed(1) + ' KB';
  }

  function onState(s) {
    const first = !state;
    const prevFile = state && state.hasKey ? state.fps : null;
    state = s;

    el.sFile.textContent = s.found ? (s.hasKey ? s.fps : 'none') : '-';
    el.sHz.textContent = s.hz ? s.hz + ' Hz' : '-';
    if (s.hz) {
      chips.hz.dataset.v = s.hz;
      chips.hz.firstChild.nodeValue = String(s.hz);
    }
    el.pill.classList.toggle('on', s.roblox);
    el.pill.lastElementChild.textContent = s.roblox ? 'Roblox running' : 'Roblox idle';

    el.lock.checked = s.readonly;
    el.lock.disabled = !s.found;
    el.enforce.checked = s.enforce;
    el.enforce.disabled = !s.found;
    el.enforceTarget.textContent = s.target;

    el.path.textContent = s.path || 'Not found';
    el.path.title = s.path || '';
    el.tag.textContent = s.found ? (s.readonly ? 'read-only' : 'writable') : 'missing';
    el.size.textContent = s.found ? fmtSize(s.size) : '-';
    el.mod.textContent = s.found ? s.modified : '-';
    el.backup.textContent = s.backup ? 'saved' : 'none';
    el.open.disabled = !s.found;
    el.bak.disabled = !s.found;
    el.restore.disabled = !s.found || !s.backup;

    const fileFps = s.hasKey ? s.fps : null;
    if (first) {
      setValue(fileFps !== null ? fileFps : s.target, false);
    } else if (fileFps !== null && (syncNext || ((!touched || parse() === prevFile) && document.activeElement !== el.input))) {
      touched = false;
      syncNext = false;
      setValue(fileFps, false);
    } else {
      render();
    }
  }

  function clearLog() {
    el.log.textContent = '';
    const li = document.createElement('li');
    li.className = 'empty';
    li.textContent = 'No activity yet';
    el.log.appendChild(li);
  }

  function addLog(kind, msg) {
    const empty = el.log.querySelector('.empty');
    if (empty) empty.remove();
    const li = document.createElement('li');
    li.className = kind;
    const t = document.createElement('time');
    t.textContent = new Date().toTimeString().slice(0, 8);
    const dot = document.createElement('i');
    const span = document.createElement('span');
    span.textContent = msg;
    li.append(t, dot, span);
    el.log.prepend(li);
    while (el.log.children.length > 200) el.log.lastElementChild.remove();
  }

  function apply() {
    const v = parse();
    if (v === null || el.apply.disabled) return;
    touched = false;
    send('apply:' + v);
    el.apply.classList.add('done');
    el.apply.firstChild.textContent = 'Applied';
    clearTimeout(doneTimer);
    doneTimer = setTimeout(() => {
      el.apply.classList.remove('done');
      el.apply.firstChild.textContent = 'Apply';
    }, 900);
  }

  el.input.addEventListener('input', () => {
    const clean = el.input.value.replace(/\D+/g, '').slice(0, 4);
    if (clean !== el.input.value) el.input.value = clean;
    touched = true;
    const v = parse();
    if (v !== null) value = v;
    render();
  });
  el.input.addEventListener('blur', () => { if (parse() === null) setValue(value, false); });
  el.input.addEventListener('keydown', e => {
    const step = e.shiftKey ? 10 : 1;
    if (e.key === 'ArrowUp') { e.preventDefault(); setValue((parse() ?? value) + step, true); }
    else if (e.key === 'ArrowDown') { e.preventDefault(); setValue((parse() ?? value) - step, true); }
    else if (e.key === 'Escape') { el.input.blur(); }
  });
  el.input.addEventListener('wheel', e => {
    e.preventDefault();
    const step = e.shiftKey ? 10 : 1;
    setValue((parse() ?? value) + (e.deltaY < 0 ? step : -step), true);
  }, { passive: false });

  el.range.addEventListener('input', () => setValue(+el.range.value, true));

  el.apply.addEventListener('click', apply);
  el.revert.addEventListener('click', () => {
    if (state && state.hasKey) { touched = false; setValue(state.fps, false); }
  });
  el.lock.addEventListener('change', () => send('lock:' + (el.lock.checked ? 1 : 0)));
  el.enforce.addEventListener('change', () => send('enforce:' + (el.enforce.checked ? 1 : 0)));
  el.open.addEventListener('click', () => send('open'));
  el.reload.addEventListener('click', resync('reload'));
  el.bak.addEventListener('click', () => send('backup'));
  el.restore.addEventListener('click', resync('restore'));
  $('btnClear').addEventListener('click', clearLog);
  $('btnMin').addEventListener('click', () => send('min'));
  $('btnMax').addEventListener('click', () => send('max'));
  $('btnClose').addEventListener('click', () => send('close'));

  document.addEventListener('keydown', e => {
    if (e.key === 'Enter' && !e.repeat) { e.preventDefault(); apply(); }
    else if (e.ctrlKey && e.key.toLowerCase() === 's') { e.preventDefault(); apply(); }
  });
  document.addEventListener('contextmenu', e => e.preventDefault());
  document.addEventListener('dragstart', e => e.preventDefault());

  if (host) {
    host.addEventListener('message', e => {
      const d = e.data;
      if (!d || typeof d !== 'object') return;
      if (d.t === 'state') onState(d);
      else if (d.t === 'log') addLog(d.k, d.m);
      else if (d.t === 'focus') document.body.classList.toggle('blur', !d.v);
      else if (d.t === 'win') {
        document.body.classList.toggle('max', !!d.max);
        $('btnMax').title = d.max ? 'Restore' : 'Maximize';
      }
    });
  }

  buildStatic();
  render();
  send('ready');
})();
