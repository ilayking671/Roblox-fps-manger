(() => {
  'use strict';

  const MIN = 1, MAX = 9999, R_MIN = 15, R_MAX = 480;
  const PRESETS = [30, 60, 75, 120, 144, 165, 240, 360];
  const TICKS = [60, 120, 180, 240, 300, 360, 420, 480];
  const WINDOW_MS = 10000;
  const PRIORITY = ['Normal', 'Above normal', 'High'];
  const $ = id => document.getElementById(id);
  const host = window.chrome && window.chrome.webview;
  const send = m => host && host.postMessage(m);
  const make = (tag, cls, text) => {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  };

  const FLAGS = [
    { g: 'Rendering', n: 'FFlagDebugGraphicsPreferD3D11', t: 'bool', v: 'True', d: 'Force Direct3D 11. The most stable renderer on Windows, and the one the Live tab can measure.' },
    { g: 'Rendering', n: 'FFlagDebugGraphicsPreferVulkan', t: 'bool', v: 'True', d: 'Force Vulkan. Can raise FPS on some GPUs, but the Live tab cannot measure Vulkan frames.' },
    { g: 'Rendering', n: 'FFlagDebugGraphicsPreferOpenGL', t: 'bool', v: 'True', d: 'Force OpenGL. Legacy fallback, usually slower than Direct3D 11.' },
    { g: 'Rendering', n: 'DFIntDebugFRMQualityLevelOverride', t: 'int', v: '1', d: 'Force the render quality level regardless of the in-game graphics slider. 1 is the lowest.' },
    { g: 'Rendering', n: 'DFFlagTextureQualityOverrideEnabled', t: 'bool', v: 'True', d: 'Turns on the texture quality override below.' },
    { g: 'Rendering', n: 'DFIntTextureQualityOverride', t: 'int', v: '0', d: 'Texture quality when the override is enabled. 0 is lowest, 3 is highest. Lower saves VRAM.' },
    { g: 'Rendering', n: 'FIntDebugForceMSAASamples', t: 'int', v: '1', d: 'Anti-aliasing samples: 1 (off), 2, 4 or 8. Lower is faster.' },
    { g: 'Rendering', n: 'DFFlagDisableDPIScale', t: 'bool', v: 'True', d: 'Render at native resolution instead of the DPI-scaled one. Sharper, but can cost FPS on high-DPI screens.' },
    { g: 'Rendering', n: 'FFlagHandleAltEnterFullscreenManually', t: 'bool', v: 'False', d: 'False lets Alt+Enter use exclusive fullscreen, which can lower input latency.' },
    { g: 'Rendering', n: 'FFlagDebugSkyGray', t: 'bool', v: 'True', d: 'Replace the skybox with flat gray. Small FPS gain.' },
    { g: 'Rendering', n: 'DFFlagDebugPauseVoxelizer', t: 'bool', v: 'True', d: 'Pause voxel lighting updates. Lighting stops changing, but CPU work drops.' },
    { g: 'Rendering', n: 'FIntFRMMinGrassDistance', t: 'int', v: '0', d: 'Distance where grass starts. Set with max grass distance 0 to remove grass.' },
    { g: 'Rendering', n: 'FIntFRMMaxGrassDistance', t: 'int', v: '0', d: 'Grass draw distance. 0 removes grass entirely.' },
    { g: 'Geometry', n: 'DFIntCSGLevelOfDetailSwitchingDistance', t: 'int', v: '0', d: 'Distance where unions and meshes begin dropping detail. 0 uses the lowest detail right away.' },
    { g: 'Geometry', n: 'DFIntCSGLevelOfDetailSwitchingDistanceL12', t: 'int', v: '0', d: 'Distance for the switch from detail level 1 to 2.' },
    { g: 'Geometry', n: 'DFIntCSGLevelOfDetailSwitchingDistanceL23', t: 'int', v: '0', d: 'Distance for the switch from detail level 2 to 3.' },
    { g: 'Geometry', n: 'DFIntCSGLevelOfDetailSwitchingDistanceL34', t: 'int', v: '0', d: 'Distance for the switch from detail level 3 to 4.' },
    { g: 'User interface', n: 'FIntGrassMovementReducedMotionFactor', t: 'int', v: '0', d: 'Grass sway amount. 0 stops grass from moving.' },
    { g: 'Not on allowlist', n: 'DFIntTaskSchedulerTargetFps', t: 'int', v: '9999', d: 'Old FPS unlock flag. Roblox ignores it since the allowlist; use the FPS Cap tab instead.', x: true },
    { g: 'Not on allowlist', n: 'FFlagTaskSchedulerLimitTargetFpsTo2402', t: 'bool', v: 'False', d: 'Old companion to the FPS unlock flag. Ignored by Roblox.', x: true }
  ];
  const FLAG_INFO = new Map(FLAGS.map(f => [f.n, f]));
  const isIgnored = n => { const f = FLAG_INFO.get(n); return !f || !!f.x; };

  const el = {
    input: $('fpsInput'), range: $('fpsRange'), fill: $('fill'), ticks: $('ticks'), presets: $('presets'),
    badge: $('syncBadge'), frame: $('frameTime'), sFile: $('statFile'), sHz: $('statHz'), sDelta: $('statDelta'),
    apply: $('btnApply'), revert: $('btnRevert'), lock: $('tglLock'), enforce: $('tglEnforce'),
    enforceTarget: $('enforceTarget'), path: $('filePath'), tag: $('fileTag'), size: $('fileSize'),
    mod: $('fileMod'), backup: $('fileBackup'), pill: $('robloxPill'), log: $('log'),
    open: $('btnOpen'), reload: $('btnReload'), bak: $('btnBackup'), restore: $('btnRestore'),
    hint: $('hint'), hintText: $('hintText'), hintAction: $('hintAction'), toast: $('toast')
  };

  let state = null;
  let value = 240;
  let touched = false;
  let syncNext = false;
  let doneTimer = 0;
  let view = 'cap';
  const chips = [];

  const clamp = v => Math.min(MAX, Math.max(MIN, v));
  const parse = () => {
    const v = parseInt(el.input.value, 10);
    return Number.isFinite(v) && v >= MIN && v <= MAX ? v : null;
  };

  function setView(name) {
    view = name;
    document.querySelectorAll('.tab').forEach(t => t.classList.toggle('active', t.dataset.view === name));
    document.querySelectorAll('.view').forEach(v => v.classList.toggle('active', v.dataset.view === name));
    if (name === 'live') live.resize();
    if (name === 'profiles') profiles.render();
  }
  $('tabs').addEventListener('click', e => {
    const b = e.target.closest('.tab');
    if (b) setView(b.dataset.view);
  });

  let toastTimer = 0;
  function toast(kind, msg) {
    el.toast.className = 'toast ' + kind;
    el.toast.textContent = msg;
    void el.toast.offsetWidth;
    el.toast.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => el.toast.classList.remove('show'), 2600);
  }

  function buildStatic() {
    const frag = document.createDocumentFragment();
    TICKS.forEach(t => {
      const s = make('span', '', String(t));
      s.style.left = ((t - R_MIN) / (R_MAX - R_MIN) * 100) + '%';
      frag.appendChild(s);
    });
    el.ticks.appendChild(frag);

    const add = (v, label, extra) => {
      const b = make('button', 'chip', label);
      b.dataset.v = v;
      if (extra) b.appendChild(make('small', '', extra));
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

  function hintFor(v) {
    const hz = state && state.hz;
    if (v === null) return { k: 'warn', t: 'Enter a value between 1 and 9999.' };
    if (!hz) return { k: '', t: 'Roblox reads this value when it starts.' };
    const vrr = Math.max(1, hz - 3);
    let h;
    if (v >= MAX) h = { k: 'warn', t: 'Uncapped. Lowest input latency, but maximum GPU load, heat and fan noise.', a: vrr };
    else if (v > hz) h = { k: 'info', t: `Above your ${hz} Hz display. Input latency drops, but frames past ${hz} are never shown and tearing is possible.`, a: vrr };
    else if (v === hz) h = { k: 'ok', t: `Matches your ${hz} Hz display for even frame pacing. Pair with V-Sync to remove tearing.`, a: vrr };
    else if (v >= hz - 4) h = { k: 'ok', t: `Just under ${hz} Hz. Ideal with G-Sync or FreeSync: stays inside the VRR range with no tearing and low latency.` };
    else if (hz % v === 0) h = { k: 'ok', t: `Divides evenly into ${hz} Hz, so pacing stays smooth while saving power and heat.` };
    else {
      const div = [2, 3, 4].map(d => Math.round(hz / d)).find(d => d <= v) || Math.round(hz / 2);
      h = { k: 'warn', t: `Does not divide evenly into ${hz} Hz, so motion can look uneven without VRR.`, a: div };
    }
    const fps = live.recentFps();
    if (fps && v > fps * 1.15 && v < MAX) h.t += ` Roblox is reaching about ${Math.round(fps)} FPS now, so this cap is not the limit.`;
    if (h.a === v) delete h.a;
    return h;
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

    const h = hintFor(v);
    el.hint.className = 'hint ' + h.k;
    el.hintText.textContent = h.t;
    el.hintAction.textContent = h.a ? `Use ${h.a}` : '';
    el.hintAction.dataset.v = h.a || '';

    el.apply.disabled = !found || !valid || !dirty;
    el.revert.disabled = !found || fileFps === null || !dirty;
  }

  const fmtSize = n => n < 1024 ? n + ' B' : (n / 1024).toFixed(1) + ' KB';

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
    el.pill.lastElementChild.textContent = s.roblox ? (s.instances > 1 ? `Roblox x${s.instances}` : 'Roblox running') : 'Roblox idle';

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

    system.render(s);
    flags.renderFoot();
    live.renderStatus();
    if (view === 'profiles') profiles.render();
  }

  function clearLog() {
    el.log.textContent = '';
    el.log.appendChild(make('li', 'empty', 'No activity yet'));
  }

  function addLog(kind, msg) {
    const empty = el.log.querySelector('.empty');
    if (empty) empty.remove();
    const li = make('li', kind);
    li.append(make('time', '', new Date().toTimeString().slice(0, 8)), make('i'), make('span', '', msg));
    el.log.prepend(li);
    while (el.log.children.length > 200) el.log.lastElementChild.remove();
    if (view !== 'cap' || kind === 'err') toast(kind, msg);
  }

  function applyCap(v) {
    if (v === null || v === undefined) return;
    touched = false;
    syncNext = true;
    send('apply:' + v);
  }

  function apply() {
    const v = parse();
    if (v === null || el.apply.disabled) return;
    applyCap(v);
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
    else if (e.key === 'Escape') el.input.blur();
  });
  el.input.addEventListener('wheel', e => {
    e.preventDefault();
    const step = e.shiftKey ? 10 : 1;
    setValue((parse() ?? value) + (e.deltaY < 0 ? step : -step), true);
  }, { passive: false });
  el.range.addEventListener('input', () => setValue(+el.range.value, true));
  el.hintAction.addEventListener('click', () => { if (el.hintAction.dataset.v) setValue(+el.hintAction.dataset.v, true); });

  const resync = cmd => () => { syncNext = true; touched = false; el.input.blur(); send(cmd); };
  el.apply.addEventListener('click', apply);
  el.revert.addEventListener('click', () => { if (state && state.hasKey) { touched = false; setValue(state.fps, false); } });
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

  const system = (() => {
    const seg = $('prioSeg');
    seg.addEventListener('click', e => {
      const b = e.target.closest('button');
      if (b) send('priority:' + b.dataset.v);
    });
    $('tglFso').addEventListener('change', e => send('fso:' + (e.target.checked ? 1 : 0)));
    $('tglGpu').addEventListener('change', e => send('gpu:' + (e.target.checked ? 1 : 0)));
    $('planSelect').addEventListener('change', e => send('plan:' + e.target.value));
    $('overlaySelect').addEventListener('change', e => send('overlay:' + e.target.value));
    let planKey = '';

    function render(s) {
      seg.querySelectorAll('button').forEach(b => b.classList.toggle('active', +b.dataset.v === s.priority));
      $('prioTag').textContent = s.roblox ? `${s.instances} running` : 'not running';
      $('prioNote').textContent = s.priority === 2
        ? 'High gives Roblox priority over almost everything, including audio and voice apps. Use Above normal if you hear crackling.'
        : 'Gives Roblox more CPU time than background apps. Re-applied every time Roblox starts.';

      $('tglFso').checked = s.fso;
      $('fsoTag').textContent = s.exes ? `${s.fsoCount}/${s.exes} builds` : 'no builds found';
      $('tglGpu').checked = s.gpu;
      $('gpuTag').textContent = s.exes ? `${s.gpuCount}/${s.exes} builds` : 'no builds found';
      $('gpuHigh').textContent = s.gpuHigh || '-';
      $('gpuLow').textContent = s.gpuLow || '-';
      $('gpuHigh').title = s.gpuHigh || '';
      $('gpuLow').title = s.gpuLow || '';
      $('gpuNote').textContent = s.gpus > 1
        ? 'Multiple GPUs detected. This makes Windows run Roblox on the high-performance one. Takes effect on next launch.'
        : 'Only one GPU detected, so this has no effect on this PC.';

      const ps = $('planSelect');
      const key = s.plans.map(p => p.id + p.name).join('|');
      if (key !== planKey) {
        planKey = key;
        ps.textContent = '';
        s.plans.forEach(p => { const o = make('option', '', p.name); o.value = p.id; ps.appendChild(o); });
      }
      ps.value = s.plan;
      const showOverlay = s.overlay !== 'none' && s.planKind === 'balanced';
      $('overlayRow').style.display = showOverlay ? '' : 'none';
      if (showOverlay) $('overlaySelect').value = ['best', 'balanced', 'efficiency'].includes(s.overlay) ? s.overlay : 'balanced';

      const pb = $('powerBadge');
      const good = s.planKind === 'high' || s.planKind === 'ultimate' || (s.planKind === 'balanced' && s.overlay === 'best');
      const bad = s.planKind === 'saver' || s.overlay === 'efficiency';
      pb.className = 'badge ' + (good ? 'ok' : bad ? 'err' : 'warn');
      pb.textContent = good ? 'Optimal' : bad ? 'Limiting FPS' : 'Can be faster';
      const hasHigh = s.plans.some(p => p.kind === 'high' || p.kind === 'ultimate');
      $('powerNote').textContent = good
        ? 'Your CPU and GPU can boost fully while gaming.'
        : bad
          ? 'Power saving lowers CPU and GPU clocks, which directly cuts FPS. Switch while gaming.'
          : showOverlay
            ? 'Set Power mode to Best performance for higher sustained clocks while gaming.'
            : hasHigh ? 'Switch to High performance for higher sustained clocks while gaming.' : 'Higher performance plans keep clocks up while gaming.';
    }
    return { render };
  })();

  const flags = (() => {
    const select = $('flagSelect'), list = $('flagList');
    const valueInput = $('flagValue'), boolSelect = $('flagBool');
    let items = [];
    let saved = '{}';

    const groups = new Map();
    FLAGS.forEach(f => {
      if (!groups.has(f.g)) {
        const og = document.createElement('optgroup');
        og.label = f.g;
        groups.set(f.g, og);
        select.appendChild(og);
      }
      const o = make('option', '', f.n);
      o.value = f.n;
      groups.get(f.g).appendChild(o);
    });

    function showInfo() {
      const f = FLAG_INFO.get(select.value);
      if (!f) return;
      $('flagName').textContent = f.n;
      $('flagDesc').textContent = f.d;
      $('flagType').textContent = f.t === 'bool' ? 'boolean' : 'integer';
      const a = $('flagAllow');
      a.textContent = f.x ? 'ignored by Roblox' : 'allowlisted';
      a.className = 'chip-tag ' + (f.x ? 'no' : 'ok');
      const row = valueInput.parentElement;
      row.classList.toggle('bool', f.t === 'bool');
      if (f.t === 'bool') boolSelect.value = f.v; else valueInput.value = f.v;
    }
    select.addEventListener('change', showInfo);

    const json = () => {
      const o = {};
      items.forEach(i => { o[i.n] = i.v; });
      return JSON.stringify(o, null, 2);
    };

    function upsert(n, v) {
      n = n.trim();
      v = String(v).trim();
      if (!/^[A-Za-z][A-Za-z0-9_]*$/.test(n)) { toast('err', 'Flag names use letters, digits and underscores'); return false; }
      if (!v) { toast('err', 'Enter a value'); return false; }
      const f = FLAG_INFO.get(n);
      if (f && f.t === 'int' && !/^-?\d+$/.test(v)) { toast('err', `${n} needs a whole number`); return false; }
      const i = items.find(x => x.n === n);
      if (i) i.v = v; else items.push({ n, v });
      renderList();
      return true;
    }

    function renderList() {
      list.textContent = '';
      if (!items.length) list.appendChild(make('li', 'empty', 'No flags. Pick one on the left and add it.'));
      items.forEach(item => {
        const li = make('li', isIgnored(item.n) ? 'ignored' : '');
        li.title = isIgnored(item.n) ? 'Not on the Roblox allowlist, ignored by the client' : 'Allowlisted';
        const inp = make('input', 'field');
        inp.value = item.v;
        inp.spellcheck = false;
        inp.addEventListener('input', () => { item.v = inp.value.trim(); renderFoot(); });
        const rm = make('button', 'rm');
        rm.title = 'Remove';
        rm.innerHTML = '<svg viewBox="0 0 10 10"><path d="M2 2l6 6M8 2l-6 6"/></svg>';
        rm.addEventListener('click', () => { items = items.filter(x => x !== item); renderList(); });
        li.append(make('i'), make('span', 'fname', item.n), inp, rm);
        list.appendChild(li);
      });
      $('flagCount').textContent = items.length;
      renderFoot();
    }

    function renderFoot() {
      const dirty = json() !== saved;
      const b = $('flagBadge');
      b.className = 'badge ' + (dirty ? 'warn' : 'ok');
      b.textContent = dirty ? 'Unsaved' : 'Synced';
      $('btnFlagApply').disabled = !dirty || items.some(i => !i.v);
      $('btnFlagClear').disabled = !items.length;
      $('flagDirs').textContent = state ? `${state.versions} Roblox build(s) installed` : '-';
    }

    $('btnFlagAdd').addEventListener('click', () => {
      const f = FLAG_INFO.get(select.value);
      if (f && upsert(f.n, f.t === 'bool' ? boolSelect.value : valueInput.value) && f.x) toast('warn', `${f.n} is not allowlisted and will be ignored`);
    });
    $('btnCustomAdd').addEventListener('click', () => {
      if (upsert($('customName').value, $('customValue').value)) { $('customName').value = ''; $('customValue').value = ''; }
    });
    $('btnFlagClear').addEventListener('click', () => { items = []; renderList(); });
    $('btnFlagApply').addEventListener('click', () => { saved = json(); send('fflags:' + saved); renderFoot(); });

    function load(data) {
      let o = {};
      try { o = JSON.parse(data || '{}'); } catch (e) { o = {}; }
      if (!o || typeof o !== 'object' || Array.isArray(o)) o = {};
      items = Object.keys(o).map(n => ({ n, v: String(o[n]) }));
      saved = json();
      renderList();
    }

    function set(obj) {
      items = Object.keys(obj).map(n => ({ n, v: String(obj[n]) }));
      saved = json();
      send('fflags:' + saved);
      renderList();
    }

    const current = () => { const o = {}; items.forEach(i => { o[i.n] = i.v; }); return o; };

    showInfo();
    renderList();
    return { load, set, current, renderFoot };
  })();

  const profiles = (() => {
    const grid = $('profGrid');
    let list = [];
    const DEFAULTS = [
      { name: 'Competitive', fps: MAX, priority: 1, fso: true, gpu: true, flags: null },
      { name: 'Smooth', fps: 'hz', priority: 1, fso: true, gpu: true, flags: null },
      { name: 'Battery saver', fps: 60, priority: 0, fso: false, gpu: false, flags: null }
    ];

    const persist = () => send('profiles:' + JSON.stringify(list));
    const fpsOf = p => p.fps === 'hz' ? ((state && state.hz) || 60) : p.fps;

    function load(data) {
      let parsed = null;
      try { parsed = JSON.parse(data); } catch (e) { parsed = null; }
      list = Array.isArray(parsed) ? parsed.filter(p => p && typeof p.name === 'string') : DEFAULTS.map(p => ({ ...p }));
      if (!Array.isArray(parsed)) persist();
      render();
    }

    function matches(p) {
      if (!state || !state.hasKey) return false;
      return fpsOf(p) === state.fps && p.priority === state.priority && p.fso === state.fso && p.gpu === state.gpu &&
        (p.flags === null || JSON.stringify(p.flags) === JSON.stringify(flags.current()));
    }

    function render() {
      grid.textContent = '';
      if (!list.length) {
        grid.appendChild(make('div', 'note', 'No profiles yet. Save your current setup above.'));
        return;
      }
      list.forEach((p, idx) => {
        const card = make('section', 'panel prof' + (matches(p) ? ' current' : ''));
        const head = make('div', 'prof-head');
        head.append(make('span', 'prof-name', p.name));
        if (matches(p)) head.append(make('span', 'badge ok', 'Active'));
        const fps = make('div', 'prof-fps', p.fps === 'hz' ? 'Display' : p.fps >= MAX ? 'Max' : String(p.fps));
        fps.appendChild(make('small', '', p.fps === 'hz' ? `${fpsOf(p)} FPS` : 'FPS'));
        const tags = make('div', 'prof-chips');
        tags.append(
          make('span', 'chip-tag', PRIORITY[p.priority] || 'Normal'),
          make('span', 'chip-tag', p.fso ? 'FSO off' : 'FSO default'),
          make('span', 'chip-tag', p.gpu ? 'High-perf GPU' : 'GPU default'),
          make('span', 'chip-tag', p.flags ? `${Object.keys(p.flags).length} flags` : 'flags unchanged')
        );
        const row = make('div', 'btn-row');
        const ap = make('button', 'btn sm primary', 'Apply');
        ap.addEventListener('click', () => applyProfile(p));
        const del = make('button', 'btn sm ghost', 'Delete');
        del.addEventListener('click', () => { list.splice(idx, 1); persist(); render(); });
        row.append(del, ap);
        card.append(head, fps, tags, row);
        grid.appendChild(card);
      });
    }

    function applyProfile(p) {
      if (!state || !state.found) { toast('err', 'Roblox settings file not found'); return; }
      applyCap(fpsOf(p));
      send('priority:' + (p.priority | 0));
      send('fso:' + (p.fso ? 1 : 0));
      send('gpu:' + (p.gpu ? 1 : 0));
      if (p.flags) flags.set(p.flags);
      toast('ok', `Profile "${p.name}" applied`);
    }

    $('btnProfSave').addEventListener('click', () => {
      const name = $('profName').value.trim();
      if (!name) { toast('err', 'Enter a profile name'); return; }
      const fps = parse() ?? (state && state.hasKey ? state.fps : value);
      const entry = { name, fps, priority: state ? state.priority : 0, fso: !!(state && state.fso), gpu: !!(state && state.gpu), flags: flags.current() };
      const i = list.findIndex(p => p.name.toLowerCase() === name.toLowerCase());
      if (i >= 0) list[i] = entry; else list.push(entry);
      persist();
      $('profName').value = '';
      render();
      toast('ok', `Profile "${name}" saved`);
    });

    return { load, render };
  })();

  const live = (() => {
    const canvas = $('graph'), ctx = canvas.getContext('2d');
    const empty = $('graphEmpty'), msg = $('graphMsg'), btn = $('graphBtn');
    let fts = [], ts = [], clock = 0, lastData = 0, lastStats = 0, raf = 0, pid = 0, stats = null;
    let w = 0, h = 0, dpr = 1;

    $('tglLive').addEventListener('change', e => send('live:' + (e.target.checked ? 1 : 0)));
    btn.addEventListener('click', () => send(btn.dataset.cmd));

    function resize() {
      const r = canvas.getBoundingClientRect();
      dpr = window.devicePixelRatio || 1;
      w = Math.max(1, Math.round(r.width));
      h = Math.max(1, Math.round(r.height));
      canvas.width = Math.round(w * dpr);
      canvas.height = Math.round(h * dpr);
      schedule();
    }
    new ResizeObserver(resize).observe(canvas);

    function push(d) {
      if (d.pid !== pid) { fts = []; ts = []; clock = 0; pid = d.pid; }
      for (const f of d.ft) { clock += f; fts.push(f); ts.push(clock); }
      const cut = clock - WINDOW_MS;
      let i = 0;
      while (i < ts.length && ts[i] < cut) i++;
      if (i) { fts.splice(0, i); ts.splice(0, i); }
      lastData = performance.now();
      if (lastData - lastStats > 250) { computeStats(); lastStats = lastData; }
      renderStatus();
      schedule();
    }

    function computeStats() {
      const n = fts.length;
      if (!n) { stats = null; return; }
      let sum = 0, max = 0, oneSec = 0, cnt = 0;
      for (let i = n - 1; i >= 0; i--) {
        if (oneSec < 1000) { oneSec += fts[i]; cnt++; }
        sum += fts[i];
        if (fts[i] > max) max = fts[i];
      }
      const sorted = Float32Array.from(fts).sort();
      const median = sorted[n >> 1];
      const worst = p => {
        const k = Math.max(1, Math.ceil(n * p));
        let s = 0;
        for (let i = n - k; i < n; i++) s += sorted[i];
        return 1000 / (s / k);
      };
      let stutter = 0;
      for (const f of fts) if (f > Math.max(median * 2.5, median + 8)) stutter++;
      stats = { fps: cnt / (oneSec / 1000), avg: sum / n, low1: worst(0.01), low01: worst(0.001), max, stutter, median };
      const t = (id, v, unit) => { const e = $(id); e.textContent = v; if (unit) e.appendChild(make('small', '', unit)); };
      t('tFps', Math.round(stats.fps));
      t('tAvg', stats.avg.toFixed(2), 'ms');
      t('tLow1', Math.round(stats.low1), 'fps');
      t('tLow01', Math.round(stats.low01), 'fps');
      t('tMax', stats.max.toFixed(1), 'ms');
      t('tStutter', String(stats.stutter));
      if (view === 'cap' && !touched) render();
    }

    const fresh = () => performance.now() - lastData < 2000;
    const recentFps = () => (fresh() && stats ? stats.fps : 0);

    function niceMax(v) {
      const steps = [5, 10, 20, 25, 40, 50, 100, 200, 250, 500, 1000, 2000, 5000];
      return steps.find(s => s >= v) || 5000;
    }

    function schedule() {
      if (!raf && view === 'live') raf = requestAnimationFrame(draw);
    }

    function draw() {
      raf = 0;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, w, h);
      const padL = 44, padR = 12, padT = 12, padB = 22;
      const gw = w - padL - padR, gh = h - padT - padB;
      const capMs = state && state.hasKey && state.fps < MAX ? 1000 / state.fps : 0;
      let p99 = 0;
      if (fts.length) {
        const s = Float32Array.from(fts).sort();
        p99 = s[Math.floor((s.length - 1) * 0.99)];
      }
      const ymax = niceMax(Math.max(8, p99 * 1.4, capMs * 1.5, stats ? stats.avg * 2 : 0));
      const y = v => padT + gh - Math.min(v, ymax) / ymax * gh;

      ctx.font = '10.5px "Cascadia Mono", Consolas, monospace';
      ctx.textBaseline = 'middle';
      ctx.textAlign = 'right';
      ctx.lineWidth = 1;
      for (let i = 0; i <= 4; i++) {
        const v = ymax * i / 4;
        const yy = Math.round(y(v)) + 0.5;
        ctx.strokeStyle = i === 0 ? '#30333a' : '#1f2125';
        ctx.beginPath(); ctx.moveTo(padL, yy); ctx.lineTo(w - padR, yy); ctx.stroke();
        ctx.fillStyle = '#5d626b';
        ctx.fillText(v % 1 ? v.toFixed(1) : String(v), padL - 8, yy);
      }
      ctx.textAlign = 'center';
      ctx.textBaseline = 'top';
      for (let s = 0; s <= 10; s += 2) {
        const xx = padL + gw * (1 - s / 10);
        ctx.fillText(s === 0 ? 'now' : `-${s}s`, xx, h - padB + 6);
      }

      if (capMs && capMs < ymax) {
        const yy = Math.round(y(capMs)) + 0.5;
        ctx.setLineDash([4, 4]);
        ctx.strokeStyle = '#5d626b';
        ctx.beginPath(); ctx.moveTo(padL, yy); ctx.lineTo(w - padR, yy); ctx.stroke();
        ctx.setLineDash([]);
        ctx.textAlign = 'right';
        ctx.textBaseline = 'bottom';
        ctx.fillStyle = '#8b9099';
        ctx.fillText(`cap ${capMs.toFixed(2)} ms`, w - padR - 4, yy - 3);
      }

      if (fts.length > 1) {
        ctx.save();
        ctx.beginPath();
        ctx.rect(padL, padT - 1, gw, gh + 2);
        ctx.clip();
        ctx.strokeStyle = '#3d8bfd';
        ctx.lineWidth = 1.25;
        ctx.lineJoin = 'round';
        ctx.beginPath();
        const end = clock;
        for (let i = 0; i < fts.length; i++) {
          const xx = padL + gw * (1 - (end - ts[i]) / WINDOW_MS);
          const yy = y(fts[i]);
          if (i === 0) ctx.moveTo(xx, yy); else ctx.lineTo(xx, yy);
        }
        ctx.stroke();
        if (stats) {
          ctx.fillStyle = '#f85149';
          const lim = Math.max(stats.median * 2.5, stats.median + 8);
          for (let i = 0; i < fts.length; i++) {
            if (fts[i] > lim) {
              const xx = padL + gw * (1 - (end - ts[i]) / WINDOW_MS);
              ctx.fillRect(xx - 1.5, y(fts[i]) - 1.5, 3, 3);
            }
          }
        }
        ctx.restore();
      }
    }

    function overlay(text, label, cmd) {
      msg.textContent = text;
      btn.textContent = label || '';
      btn.dataset.cmd = cmd || '';
      empty.classList.toggle('hide', !text);
    }

    function renderStatus() {
      if (!state) return;
      $('tglLive').checked = state.liveEnabled;
      const b = $('liveBadge');
      const running = state.live === 'running';
      const ok = running && fresh();
      b.className = 'badge ' + (ok ? 'live' : state.live === 'denied' || state.live === 'failed' ? 'err' : running ? 'warn' : '');
      b.textContent = ok ? 'Live' : state.live === 'denied' ? 'Needs admin' : state.live === 'failed' ? 'Error' : running ? 'Waiting' : 'Off';
      $('liveMeta').textContent = ok && pid ? `PID ${pid}` : '';

      if (!state.liveEnabled) overlay('Live monitoring is turned off.', 'Turn on', 'live:1');
      else if (state.live === 'denied' || (!state.elevated && state.live !== 'running')) overlay('Reading frame timing from Windows needs administrator rights.', 'Restart as administrator', 'elevate');
      else if (state.live === 'failed') overlay('The Windows frame event session could not start.', 'Retry', 'live:1');
      else if (!state.roblox) overlay('Start Roblox to see live frame times.');
      else if (!ok) overlay('Waiting for frames. Roblox must render with Direct3D 11; Vulkan and OpenGL cannot be measured.');
      else overlay('');
    }

    setInterval(() => { if (state && state.live === 'running') renderStatus(); }, 1000);
    return { push, resize, recentFps, renderStatus };
  })();

  document.addEventListener('keydown', e => {
    if (view !== 'cap') return;
    if (e.target.closest && e.target.closest('select, .field')) return;
    if (e.key === 'Enter' && !e.repeat) { e.preventDefault(); apply(); }
    else if (e.ctrlKey && e.key.toLowerCase() === 's') { e.preventDefault(); apply(); }
  });
  document.addEventListener('keydown', e => {
    if (e.ctrlKey && e.key >= '1' && e.key <= '5') {
      e.preventDefault();
      setView(['cap', 'live', 'system', 'flags', 'profiles'][+e.key - 1]);
    }
  });
  document.addEventListener('contextmenu', e => e.preventDefault());
  document.addEventListener('dragstart', e => e.preventDefault());

  if (host) {
    host.addEventListener('message', e => {
      const d = e.data;
      if (!d || typeof d !== 'object') return;
      switch (d.t) {
        case 'state': onState(d); break;
        case 'log': addLog(d.k, d.m); break;
        case 'frames': live.push(d); break;
        case 'fflags': flags.load(d.data); break;
        case 'profiles': profiles.load(d.data); break;
        case 'focus': document.body.classList.toggle('blur', !d.v); break;
        case 'win':
          document.body.classList.toggle('max', !!d.max);
          $('btnMax').title = d.max ? 'Restore' : 'Maximize';
          break;
      }
    });
  }

  buildStatic();
  render();
  send('ready');
})();
