/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The DX747 workbench: tabs, connection (the simulator in the page or the unit over Web MIDI) and the configurator for parts, layers,
 * sounds, scenes, filter, effects and settings. It talks to the unit through Dx747 (dx747-client.js, docs/CONFIG-PROTOCOL.md),
 * which polls the unit, so a change made on the panel shows here and a change made here shows on the panel. Every control reads
 * the current connection (app.dx) when it is used, never one captured when it was built. */
(() => {
  'use strict';
  const COLORS = ['#ff5a5a', '#ffa23a', '#ffe34a', '#5cf06a', '#2de0d0', '#5aa9ff', '#b68cff', '#ff7ad2'];
  const FOLD = ['Drop', 'Clip', 'Wrap'], FILTER = ['Off', 'Low pass', 'High pass', 'Band pass'];
  const SYNC = ['1/32T', '1/32', '1/16T', '1/16', '1/8T', '1/16D', '1/8', '1/8D', '1/4'];
  const noteName = n => Dx747.noteName(n);
  const $ = (sel, root = document) => root.querySelector(sel);
  function h(tag, cls, ...kids) {
    const e = document.createElement(tag); if (cls) e.className = cls;
    for (const k of kids) if (k != null) e.append(k.nodeType ? k : document.createTextNode(String(k)));
    return e;
  }
  const app = { sim: null, dx: null, mode: null, realAccess: null, state: null, voices: null, scenes: null, sceneRev: null, sceneBank: 0, sceneSel: 0,
                soundBank: 0, tab: 'device', sig: '', controls: [], connecting: false, locked: false, errors: 0 };
  const english = text => (globalThis.FM1I18n ? FM1I18n.t(String(text), 'en') : String(text));

  // ---- small things ---------------------------------------------------------------------------------------
  let toastTimer = null;
  function toast(text, bad = false) {
    const t = $('#toast'); t.textContent = english(text); t.className = 'show' + (bad ? ' bad' : '');
    clearTimeout(toastTimer); toastTimer = setTimeout(() => { t.className = ''; }, bad ? 6000 : 2500);
  }
  async function act(fn, then, hint) {
    try { const r = await fn(); if (then) await then(r); return r; }
    catch (error) { toast(hint && error.deviceCode === 71 ? hint : (error.message || String(error)), true); await refresh().catch(() => {}); }
  }
  async function refresh() { if (app.dx && app.dx.connected) await app.dx.refresh(onState); }
  function throttled(fn, ms = 70) {
    let t = null, pending;
    return v => { pending = v; if (!t) t = setTimeout(() => { t = null; fn(pending); }, ms); };
  }
  // A control the user just touched keeps what they set for a moment, so a poll of the unit does not undo it mid-adjustment.
  const touch = el => { el._edit = Date.now(); };
  const recent = el => el._edit && Date.now() - el._edit < 1500;
  let releaseSlider = null;
  for (const type of ['pointerup', 'pointercancel']) window.addEventListener(type, () => { if (releaseSlider) { releaseSlider(); releaseSlider = null; } });
  function slider(min, max, fmt, onSet, cls) {
    const wrap = h('label', 'ctl ' + (cls || '')), input = h('input'), out = h('span', 'val');
    input.type = 'range'; input.min = min; input.max = max; input.step = 1; wrap.append(input, out);
    const send = throttled(onSet); let busy = false;
    input.addEventListener('input', () => { touch(input); out.textContent = fmt(+input.value); send(+input.value); });
    input.addEventListener('pointerdown', () => { busy = true; releaseSlider = () => { busy = false; }; });
    return { el: wrap, input, set(v, label) { if (busy || recent(input)) return; input.value = v; out.textContent = label != null ? label : fmt(v); } };
  }
  function select(options, onSet, cls) {
    const s = h('select', cls); options.forEach(([value, label]) => s.append(new Option(label, value)));
    s.addEventListener('change', () => { touch(s); onSet(+s.value); s.blur(); });
    return { el: s, node: s, set(v) { if (!recent(s)) s.value = v; } };
  }
  function number(min, max, onSet) {
    const i = h('input', 'num'); i.type = 'number'; i.min = min; i.max = max;
    i.addEventListener('change', () => { touch(i); const v = Math.max(min, Math.min(max, Math.round(+i.value) || min)); i.value = v; onSet(v); });
    return { el: i, set(v) { if (!recent(i) && document.activeElement !== i) i.value = v; } };
  }
  function field(label, node) { const f = h('label', 'field'); f.append(h('span', null, label), node); return f; }
  const NOTE_OPTIONS = Array.from({ length: 128 }, (_, n) => [n, noteName(n) + '  (' + n + ')']);
  const PART_COLOR = p => COLORS[p % 8];
  const notConnected = root => { root.textContent = ''; root.append(h('p', 'hint', 'Not connected. Choose Simulator or Connect FM-1 (USB) at the top.')); app.sig = ''; };

  // ---- connection ----------------------------------------------------------------------------------------------
  function setStatus() {
    const s = $('#conn'), dx = app.dx;
    if (app.connecting) { s.textContent = 'Connecting…'; s.className = 'pill'; }
    else if (dx && dx.connected) { s.textContent = (app.mode === 'simulator' ? 'Simulator' : 'FM-1') + ' connected  ·  DX747 ' + dx.hello.release + '  ·  protocol ' + dx.hello.protocol; s.className = 'pill ok'; }
    else { s.textContent = 'Not connected'; s.className = 'pill'; }
    $('#btn-sim').classList.toggle('active', app.mode === 'simulator' && !!(dx && dx.connected));
    $('#btn-unit').classList.toggle('active', app.mode === 'unit' && !!(dx && dx.connected));
    $('#btn-sim').disabled = $('#btn-unit').disabled = app.connecting || app.locked;
  }
  /** Connect the configurator to a MIDI access (the simulator's or the unit's). A failed attempt leaves the working connection alone. */
  async function connect(access, mode) {
    if (app.connecting || app.locked) return;
    app.connecting = true; setStatus();
    try {
      const old = app.dx, same = !!old && old.access === access;     // the same ports cannot be shared: let go of them first
      if (same) { old.stopPolling(); await old.disconnect().catch(() => {}); app.dx = null; app.state = null; }
      const dx = new Dx747(access);
      try { await dx.connect(); }
      catch (error) { if (same) { app.mode = null; renderCurrent(); } throw error; }
      if (old && !same) { old.stopPolling(); await old.disconnect().catch(() => {}); }
      app.dx = dx; app.mode = mode; app.sig = ''; app.voices = null; app.scenes = null; app.sceneRev = null; app.errors = 0; app.state = null;
      dx.startPolling(onState, onPollError, 400, () => { app.errors = 0; });
      if (app.kb) { app.kb.refreshOutputs(mode === 'unit' ? 'unit' : 'sim'); app.kb.refreshInputs(); }
      loadVoices();
    } finally { app.connecting = false; setStatus(); }
  }
  function onPollError(error) {
    const dx = app.dx; app.errors++;
    if (!dx || dx.connected && app.errors < 3) { if (app.errors === 1) toast('Trouble talking to the unit: ' + error.message, true); return; }
    dx.stopPolling(); app.dx = null; app.state = null; app.mode = null;
    toast('Lost contact with the unit: ' + error.message + ' Press Connect again.', true);
    setStatus(); renderCurrent();
  }
  async function release() {                                  // the Update tab needs the ports
    if (app.dx) { app.dx.stopPolling(); await app.dx.disconnect().catch(() => {}); }
    app.dx = null; app.mode = null; app.state = null; app.sig = '';
    setStatus(); renderCurrent();
  }
  const useSimulator = () => act(() => connect(app.sim.access, 'simulator'));
  const useUnit = () => act(async () => {
    if (app.connecting || app.locked) return;
    app.realAccess = await DxDevice.requestUnitAccess();
    app.realAccess.onstatechange = () => { if (app.kb) { app.kb.refreshOutputs(); app.kb.refreshInputs(); } };
    await connect(app.realAccess, 'unit');
    toast('Connected to the FM-1');
  });
  async function loadVoices() {
    const dx = app.dx; if (!dx) return;
    try {
      const names = [];
      for (let b = 0; b < 4; b++) { names.push(...await dx.voiceNames(b)); if (app.dx !== dx) return; }
      app.voices = names; app.sig = ''; renderCurrent();
    } catch (error) { if (app.dx === dx) toast('Could not read the voice library: ' + error.message, true); }
  }
  const voiceLabel = p => 'ABCD'[p >> 5] + String((p & 31) + 1).padStart(2, '0') + '  ' + ((app.voices && app.voices[p]) || 'program ' + (p + 1));

  // ---- state in, drawing out ------------------------------------------------------------------------------------------
  function onState(s) { app.state = s; renderCurrent(); }
  function renderCurrent() {
    const fn = { configure: renderConfigure, master: renderMaster, scenes: renderScenes, sounds: renderSounds }[app.tab];
    if (fn) fn();
  }
  function showTab(name) {
    if (app.tab === 'keyboard' && name !== 'keyboard' && app.kb) app.kb.releaseAll();
    app.tab = name;
    document.querySelectorAll('#tabs button').forEach(b => b.classList.toggle('active', b.dataset.tab === name));
    document.querySelectorAll('main > section').forEach(s => { s.hidden = s.id !== 'tab-' + name; });
    if (name === 'keyboard' && app.kb) { app.kb.refreshOutputs(); app.kb.refreshInputs(); }
    renderCurrent();
  }

  // ---- Configure: parts and layers ------------------------------------------------------------------------------------------
  function renderConfigure() {
    const root = $('#parts'), s = app.state;
    if (!app.dx || !s) { if (!app.dx) notConnected(root); return; }
    const sig = JSON.stringify([s.parts.map(p => [p.used, p.layers.map(l => l.engine)]), !!app.voices]);
    if (sig !== app.sig) { app.sig = sig; buildParts(root, s); }
    app.controls.forEach(c => c(s));
  }
  function buildParts(root, s) {
    root.textContent = ''; app.controls = [];
    for (const part of s.parts) {
      const p = part.index, color = PART_COLOR(p), card = h('section', 'part');
      card.style.setProperty('--c', color);
      const head = h('div', 'part-head'), chip = h('span', 'chip', p + 1); chip.style.background = color; head.append(chip);
      if (!part.used) {
        head.append(h('b', null, 'Empty slot'));
        const add = h('button', null, 'Add a part here'); add.onclick = () => act(() => app.dx.partOp(0, p), refresh); head.append(add);
        card.append(head); root.append(card); continue;
      }
      const chan = select(Array.from({ length: 16 }, (_, i) => [i + 1, 'Channel ' + (i + 1)]), v => act(() => app.dx.setPart(p, 1, v), refresh));
      const name = h('input', 'name'); name.maxLength = 12; name.placeholder = 'Part name'; name.setAttribute('aria-label', 'Name of part ' + (p + 1));
      name.addEventListener('change', () => { touch(name); act(() => app.dx.partName(p, name.value.toUpperCase()), refresh); });
      const level = slider(0, 127, v => v, v => act(() => app.dx.setPart(p, 0, v)), 'wide');
      const play = h('button', null, 'Play this part'); play.onclick = () => act(() => app.dx.partOp(3, p), refresh);
      const addLayer = h('button', null, 'Add layer'); addLayer.onclick = () => act(() => app.dx.partOp(1, p), refresh);
      const removePart = h('button', 'warn', 'Remove part');
      removePart.onclick = () => act(async () => { for (let i = app.state.parts[p].layers.length - 1; i >= 0; i--) await app.dx.partOp(2, p, i); }, refresh);
      head.append(name, field('Channel', chan.el), field('Level', level.el), play, addLayer, removePart);
      const lane = h('div', 'lane'); card.append(head, lane);
      app.controls.push(st => {
        const pt = st.parts[p]; chan.set(pt.channel); level.set(pt.level);
        if (!recent(name) && document.activeElement !== name) name.value = pt.name;
        card.classList.toggle('selected', st.selected === p);
        lane.textContent = '';
        pt.layers.forEach((l, i) => { const b = h('i'); b.style.cssText = `left:${l.low / 128 * 100}%;width:${(l.high - l.low + 1) / 128 * 100}%;background:${color};top:${i * 7}px;opacity:${i % 2 ? .6 : 1}`; lane.append(b); });
        lane.style.height = Math.max(24, pt.layers.length * 7 + 6) + 'px';
      });
      part.layers.forEach((layer, i) => card.append(buildLayer(layer.engine, i, color, part)));
      root.append(card);
    }
  }
  function buildLayer(e, index, color, part) {
    const set = (f, v) => act(() => app.dx.setLayer(e, f, v)), box = h('div', 'layer');
    const chip = h('span', 'chip small', index + 1); chip.style.background = color;
    const voice = select(Array.from({ length: 128 }, (_, n) => [n, voiceLabel(n)]), v => act(() => app.dx.setLayer(e, 14, v), refresh), 'voice');
    voice.node.setAttribute('aria-label', 'Voice of layer ' + (index + 1));
    const mute = h('button', 'toggle', 'Mute'), solo = h('button', 'toggle', 'Solo'), remove = h('button', 'warn', 'Remove');
    mute.onclick = () => act(() => app.dx.setLayer(e, 12, app.state.engines[e].muted ? 0 : 1), refresh);
    solo.onclick = () => act(() => app.dx.setLayer(e, 13, app.state.engines[e].solo ? 0 : 1), refresh);
    remove.onclick = () => act(() => app.dx.partOp(2, part.index, index), refresh);
    const row1 = h('div', 'row', chip, voice.el, mute, solo, remove);
    const level = slider(0, 127, v => v, v => set(0, v)), pan = slider(-50, 50, v => (v ? (v < 0 ? 'L' : 'R') + Math.abs(v) : 'C'), v => set(1, v + 50)),
      bright = slider(-50, 50, v => (v > 0 ? '+' : '') + v, v => set(2, v + 50)), rev = slider(0, 15, v => v, v => set(3, v)), dly = slider(0, 15, v => v, v => set(4, v));
    const row2 = h('div', 'row sliders', field('Level', level.el), field('Pan', pan.el), field('BRIGHT', bright.el), field('Reverb', rev.el), field('Delay', dly.el));
    const lo = select(NOTE_OPTIONS, v => set(5, v)), hi = select(NOTE_OPTIONS, v => set(6, v));
    const shift = select(Array.from({ length: 9 }, (_, i) => [i - 4, (i - 4 > 0 ? '+' : '') + (i - 4) + ' octave' + (Math.abs(i - 4) === 1 ? '' : 's')]), v => set(7, v * 12 + 64));
    const fold = select(FOLD.map((n, i) => [i, n]), v => set(8, v)), poly = select(Array.from({ length: 8 }, (_, i) => [i + 1, i + 1 + ' voice' + (i ? 's' : '')]), v => set(11, v));
    const vlo = number(1, 127, v => set(9, v)), vhi = number(1, 127, v => set(10, v));
    const row3 = h('div', 'row', field('Lowest key', lo.el), field('Highest key', hi.el), field('Shift', shift.el), field('Fold', fold.el), field('Velocity', h('span', 'pair', vlo.el, '–', vhi.el)), field('Polyphony', poly.el));
    box.append(row1, row2, row3);
    app.controls.push(st => {
      const x = st.engines[e];
      voice.set(x.program); level.set(x.level); pan.set(x.pan); bright.set(x.bright); rev.set(x.reverb); dly.set(x.delay);
      lo.set(x.low); hi.set(x.high); shift.set(Math.round(x.shift / 12)); fold.set(x.fold); poly.set(x.poly); vlo.set(x.velLow); vhi.set(x.velHigh);
      mute.classList.toggle('on', x.muted); solo.classList.toggle('on', x.solo); box.classList.toggle('silent', !x.audible);
    });
    return box;
  }

  // ---- Master and FX ------------------------------------------------------------------------------------------------------------
  const masterSet = [];
  let masterBuilt = false;
  const delayMs = raw => (raw < 128 ? raw * 3 : raw <= 201 ? 360 + (raw - 137) * 10 : 1020 + (raw - 202) * 20);   // the unit's free delay times
  function renderMaster() {
    const root = $('#master-body');
    if (!app.dx || !app.state) { if (!app.dx) { masterBuilt = false; masterSet.length = 0; notConnected(root); } return; }
    if (!masterBuilt) { buildMaster(root); masterBuilt = true; }
    masterSet.forEach(f => f(app.state));
  }
  function buildMaster(root) {
    root.textContent = '';
    const type = select(FILTER.map((n, i) => [i, n]), v => act(() => app.dx.setMaster(0, v), refresh));
    const hzAt = v => Math.round(20 * Math.pow(1000, v / 1000)), hzText = hz => (hz >= 1000 ? (hz / 1000).toFixed(1) + ' kHz' : hz + ' Hz');
    const cut = slider(0, 1000, v => hzText(hzAt(v)), v => act(() => app.dx.setMaster(1, hzAt(v))), 'wide');
    const res = slider(0, 127, v => v, v => act(() => app.dx.setMaster(2, v)), 'wide');
    root.append(h('h3', null, 'Master filter (all parts)'), h('div', 'row', field('Type', type.el), field('Cutoff', cut.el), field('Resonance', res.el)));
    const decay = slider(0, 127, v => v, v => act(() => app.dx.setMaster(3, v)), 'wide');
    const mode = select([[0, 'Free time'], [1, 'Synchronised']], v => act(() => app.dx.setMaster(7, v), refresh));
    const freeTime = slider(0, 244, p => delayMs(p <= 127 ? p : p + 9) + ' ms', v => act(() => app.dx.setMaster(4, v)), 'wide');
    const division = select(SYNC.map((n, i) => [i, n]), v => act(() => app.dx.setMaster(8, v), refresh));
    const feedback = slider(0, 127, v => v, v => act(() => app.dx.setMaster(5, v)), 'wide');
    const ret = slider(0, 127, v => v, v => act(() => app.dx.setMaster(6, v)), 'wide');
    root.append(h('h3', null, 'Shared reverb and delay'),
      h('div', 'row', field('Reverb decay', decay.el), field('Effects return', ret.el)),
      h('div', 'row', field('Delay mode', mode.el), field('Delay time (free)', freeTime.el), field('Delay division (synchronised)', division.el), field('Delay feedback', feedback.el)),
      h('p', 'hint', 'Each layer\'s own reverb and delay amounts are on the Configure tab. In synchronised mode the delay follows the tempo of incoming MIDI clock.'));
    const chans = [[0, 'Off'], ...Array.from({ length: 16 }, (_, i) => [i + 1, 'Channel ' + (i + 1)])];
    const sc = select(chans, v => act(() => app.dx.setSetting(0, v), refresh)), mc = select(chans, v => act(() => app.dx.setSetting(1, v), refresh));
    const light = slider(0, 8, v => v, v => act(() => app.dx.setSetting(2, v)));
    const bt = h('input'); bt.type = 'checkbox'; bt.onchange = () => { touch(bt); act(() => app.dx.setSetting(3, bt.checked ? 1 : 0), refresh); };
    root.append(h('h3', null, 'Unit settings'), h('div', 'row', field('Scene channel (program change recalls a scene)', sc.el), field('Master control channel (CC74 cutoff, CC71 resonance, CC70 type)', mc.el), field('Button light', light.el), field('Bluetooth audio in', bt)));
    masterSet.length = 0;
    masterSet.push(s => {
      type.set(s.filter.type); cut.set(Math.round(1000 * Math.log(Math.max(20, s.filter.hz) / 20) / Math.log(1000)), hzText(s.filter.hz)); res.set(s.filter.resonance);
      const raw = s.effects.delay, synced = raw >= 128 && raw <= 136;
      decay.set(s.effects.decay); feedback.set(s.effects.feedback); ret.set(s.effects.ret);
      mode.set(synced ? 1 : 0); division.set(synced ? raw - 128 : 0); freeTime.set(synced ? 0 : raw > 136 ? raw - 9 : raw);
      freeTime.input.disabled = synced; division.node.disabled = !synced;
      sc.set(s.settings.sceneChannel); mc.set(s.settings.masterChannel); light.set(s.settings.backlight); if (!recent(bt)) bt.checked = s.settings.bluetoothAudio;
    });
  }

  // ---- Scenes ------------------------------------------------------------------------------------------------------------------------
  const scenesUI = { built: false };
  async function loadScenes() {
    const dx = app.dx; if (!dx || !dx.connected) return;
    try { const list = await dx.scenes(); if (app.dx !== dx) return; app.scenes = list; app.sceneRev = app.state ? app.state.sceneRevision : null; updateScenes(); } catch (error) { toast('Could not read the scenes: ' + error.message, true); }
  }
  function renderScenes() {
    const root = $('#scenes-body');
    if (!app.dx || !app.state) { if (!app.dx) { scenesUI.built = false; notConnected(root); } return; }
    if (!scenesUI.built) buildScenes(root);
    if (!app.scenes || app.sceneRev !== app.state.sceneRevision) { if (!scenesUI.loading) { scenesUI.loading = true; loadScenes().finally(() => { scenesUI.loading = false; }); } }
    updateScenes();
  }
  const sceneAt = i => app.scenes && app.scenes[i];
  function pickScene(idx) {
    app.sceneSel = idx; const sc = sceneAt(idx), exists = !!(sc && sc.name.trim());
    scenesUI.name.value = exists && sc.name !== '(NO NAME)' ? sc.name : 'SCENE ' + String(idx + 1).padStart(2, '0');
    updateScenes();
  }
  function buildScenes(root) {
    root.textContent = ''; scenesUI.built = true; scenesUI.cards = [];
    const banks = h('div', 'banks'); scenesUI.banks = [];
    for (let b = 0; b < 4; b++) { const k = h('button', '', 'Bank ' + 'ABCD'[b]); k.onclick = () => { app.sceneBank = b; updateScenes(); }; scenesUI.banks.push(k); banks.append(k); }
    const grid = h('div', 'scene-grid');
    for (let i = 0; i < 16; i++) {
      const card = h('button', 'scene'), num = h('b'), nm = h('span'), dots = h('div', 'dots');
      for (let p = 0; p < 8; p++) dots.append(h('i'));
      card.append(num, nm, dots); card.onclick = () => pickScene(app.sceneBank * 16 + i); scenesUI.cards.push({ card, num, nm, dots }); grid.append(card);
    }
    const name = h('input', 'name'); name.maxLength = 12; name.placeholder = 'Scene name'; name.setAttribute('aria-label', 'Scene name'); scenesUI.name = name; name.value = 'SCENE 01';
    const refused = 'The unit refused this. If the scene store is not set up yet, press SAVE on the unit (or the simulator panel) once and confirm; names may only use letters, digits and punctuation.';
    const done = async () => { await loadScenes(); await refresh(); };
    const mk = (label, cls, fn) => { const b = h('button', cls, label); b.onclick = () => act(() => fn(app.sceneSel, name.value.toUpperCase()), done, refused); return b; };
    scenesUI.title = h('b');
    const bar = h('div', 'row', scenesUI.title, name, mk('Recall', '', i => app.dx.sceneRecall(i)), mk('Save the current sound here', '', (i, n) => app.dx.sceneSave(i, n)),
      mk('Rename', '', (i, n) => app.dx.sceneRename(i, n)), mk('Delete', 'warn', i => app.dx.sceneDelete(i)));
    root.append(banks, grid, bar, h('p', 'hint', 'A scene holds all eight parts: their channels, layers, voices, ranges, levels, pan, sends, the filter and the effects. If saving is refused the scene store is not set up yet: press SAVE on the unit (or on the simulator panel) once and confirm. Saving writes flash, so give it a moment between scenes.'));
  }
  function updateScenes() {
    if (!scenesUI.built || !app.state) return;
    scenesUI.banks.forEach((k, b) => k.classList.toggle('active', b === app.sceneBank));
    scenesUI.cards.forEach((c, i) => {
      const idx = app.sceneBank * 16 + i, sc = sceneAt(idx);
      c.card.classList.toggle('sel', idx === app.sceneSel); c.card.classList.toggle('current', app.state.scene === idx);
      c.num.textContent = idx + 1; c.nm.textContent = sc && sc.name.trim() ? sc.name : (app.scenes ? '—' : '…');
      [...c.dots.children].forEach((d, p) => { d.style.background = sc && sc.name.trim() && (sc.parts >> p) & 1 ? COLORS[p] : ''; });
    });
    scenesUI.title.textContent = 'Scene ' + (app.sceneSel + 1);
  }

  // ---- Sounds ---------------------------------------------------------------------------------------------------------------------------
  const soundsUI = { built: false };
  function framedEngine() {
    const s = app.state, p = s && s.parts[s.selected]; if (!p || !p.used) return -1;
    const l = p.layers[Math.min(p.selectedLayer, p.layers.length - 1)]; return l ? l.engine : -1;
  }
  function renderSounds() {
    const root = $('#sounds-body');
    if (!app.dx || !app.state) { if (!app.dx) { soundsUI.built = false; notConnected(root); } return; }
    if (!soundsUI.built) buildSounds(root);
    fillSounds();
  }
  function buildSounds(root) {
    root.textContent = ''; soundsUI.built = true; soundsUI.banks = [];
    const banks = h('div', 'banks');
    for (let b = 0; b < 4; b++) { const k = h('button', '', 'Bank ' + 'ABCD'[b]); k.onclick = () => { app.soundBank = b; soundsUI.key = ''; fillSounds(); }; soundsUI.banks.push(k); banks.append(k); }
    soundsUI.note = h('p', 'hint'); soundsUI.grid = h('div', 'voice-grid');
    const file = h('input'); file.type = 'file'; file.accept = '.syx'; file.setAttribute('aria-label', 'DX7 bank file');
    const target = select([[0, 'Bank A'], [1, 'Bank B'], [2, 'Bank C'], [3, 'Bank D']], () => {}); target.node.value = 3;
    const go = h('button', null, 'Import into the unit');
    go.onclick = () => act(async () => {
      if (!file.files[0]) throw new Error('Choose a 32-voice DX7 bank file (.syx) first');
      const bytes = new Uint8Array(await file.files[0].arrayBuffer());
      Dx747.checkRom(bytes);                                                          // refuse a bad file before asking
      if (!confirm('Replace bank ' + 'ABCD'[+target.node.value] + ' of the voice library with this file?')) return;
      app.soundBank = +target.node.value;
      await app.dx.importRom(+target.node.value, bytes); toast('Imported and read back');
      app.voices = null; app.sig = ''; soundsUI.key = ''; loadVoices(); fillSounds();   // the names changed everywhere
    });
    root.append(banks, soundsUI.note, soundsUI.grid, h('h3', null, 'Import a DX7 ROM or bank file'), h('div', 'row', file, field('Into', target.el), go));
  }
  async function fillSounds() {
    const dx = app.dx; if (!dx || !app.state) return;
    const bank = app.soundBank, e = framedEngine(), cur = e >= 0 ? app.state.engines[e].program : -1;
    soundsUI.banks.forEach((k, b) => k.classList.toggle('active', b === bank));
    soundsUI.note.textContent = e >= 0 ? 'Click a voice to give it to the selected part\'s framed layer (part ' + (app.state.selected + 1) + ').' : 'Select a part on the Configure tab to choose its voice here.';
    const names = await dx.voiceNames(bank).catch(() => null);
    if (app.tab !== 'sounds' || app.dx !== dx || bank !== app.soundBank) return;
    const key = bank + '|' + cur + '|' + (names || []).join(',') + '|' + e;
    if (key === soundsUI.key) return;
    soundsUI.key = key; soundsUI.grid.textContent = '';
    (names || []).forEach((n, i) => {
      const p = bank * 32 + i, b = h('button', 'voice' + (p === cur ? ' sel' : ''), h('i', null, String(p + 1)), ' ' + (n || '—'));
      b.onclick = () => { if (framedEngine() < 0) { toast('Select a part first (Configure tab: Play this part)', true); return; } act(() => app.dx.setLayer(framedEngine(), 14, p), refresh); };
      soundsUI.grid.append(b);
    });
  }

  // ---- start ------------------------------------------------------------------------------------------------------------------------------------
  function help() {
    $('#tab-help').append(...[
      ['What this is', 'The workbench for DX747, the firmware that makes the FM-1 an eight-part multitimbral DX7 module. Use it with the simulator in this page or with the unit over USB. Everything you see is the unit\'s own state: changes made here appear on the panel and the other way round.'],
      ['Connect', 'Simulator: starts with the page; click anywhere to start its sound. FM-1: plug it in by USB, close other MIDI programs, press Connect FM-1 and allow MIDI with SysEx (Chrome or Edge on a computer).'],
      ['Configure', 'Eight parts, each on one MIDI channel with one to eight layers. A layer is one DX7 voice with its own key range, velocity range, octave shift, level, pan, BRIGHT and sends. Choose Play this part to make it the part the keyboard plays and the DX7 editors edit.'],
      ['Sounds, Scenes, Master', 'Sounds: the 128 voices in four banks, and DX7 ROM import. Scenes: 64 stored setups with names; a program change on the scene channel recalls one. Master: the filter, the shared reverb and delay and the unit\'s settings.'],
      ['Keyboard', 'Play any part from the computer keyboard or the on-screen piano, with a channel and output of your choice. Use it to try layers and splits.'],
      ['Update', 'Installs the DX747 firmware that is inside this page, or puts the original V15 firmware back. Read docs/FLASHING-AND-RECOVERY.md first.'],
    ].flatMap(([t, p]) => [h('h3', null, t), h('p', null, p)]));
  }
  function bindKeyboard() {
    app.kb = new KeyboardTab($('#kb-host'), {
      outputs() {
        const out = [];
        if (app.sim) out.push({ name: 'Simulator (in this page)', kind: 'sim', send: b => app.sim.access.output.send(b) });
        if (app.realAccess) for (const p of app.realAccess.outputs.values()) if (p.state === 'connected') out.push({ name: p.name, kind: /fm.?1/i.test(p.name || '') ? 'unit' : 'port', send: b => { p.open && p.open(); p.send(b); } });
        return out;
      },
      inputs() { return app.realAccess ? [...app.realAccess.inputs.values()].filter(p => p.state === 'connected').map(p => ({ name: p.name, port: p })) : []; },
      selectedChannel() { const s = app.state; return s && s.parts[s.selected] && s.parts[s.selected].used ? s.parts[s.selected].channel : 0; },
      active() { return app.tab === 'keyboard'; },
      async enableMidi() { app.realAccess = await DxDevice.requestUnitAccess(); app.kb.refreshOutputs(); app.kb.refreshInputs(); },
    });
  }
  function start() {
    if (!globalThis.FM1Web) { document.body.textContent = 'The workbench script failed to load.'; return; }
    document.querySelectorAll('#tabs button').forEach(b => { b.onclick = () => showTab(b.dataset.tab); });
    $('#btn-sim').onclick = useSimulator; $('#btn-unit').onclick = useUnit;
    $('#btn-reset').onclick = () => { if (confirm('Erase the simulator\'s saved scenes and settings and start again?')) app.sim.reset(); };
    const wasm = Uint8Array.from(atob(DX_SIMULATOR.data), c => c.charCodeAt(0));
    app.sim = new SimPanel($('#sim-host'), wasm);
    app.sim.on((what, value) => { if (what === 'audio') $('#sound-note').textContent = 'Sound is on.'; if (what === 'audio-error') toast('Sound could not start: ' + value.message, true); });
    bindKeyboard(); help();
    $('#sound-note').textContent = 'Click the panel or a key to switch the sound on.';
    document.addEventListener('pointerdown', () => app.sim.startAudio(), { once: true });
    showTab('device'); setStatus();
    useSimulator();
    app.release = release; app.setLocked = on => { app.locked = on; setStatus(); };
    globalThis.DxApp = app;
  }
  document.addEventListener('DOMContentLoaded', start);
})();
