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
  const $ = (sel, root = document) => root.querySelector(sel);
  function h(tag, cls, ...kids) {
    const e = document.createElement(tag); if (cls) e.className = cls;
    for (const k of kids) if (k != null) e.append(k.nodeType ? k : document.createTextNode(String(k)));
    return e;
  }
  const app = { sim: null, dx: null, mode: null, realAccess: null, state: null, voices: null, scenes: null, sceneRev: null, sceneBank: 0, sceneSel: 0,
                soundBank: 0, tab: 'device', sig: '', controls: [], connecting: false, locked: false, errors: 0,
                voiceSeq: 0, voiceState: null, voiceBanks: 0, voiceError: '', autoReread: false };
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
  const HOLD = 1500;
  // ... and once the hold has run out the control is painted again from the newest state, so what the unit really holds shows
  // (a refused or clamped value, a change made on the panel meanwhile).
  let resyncTimer = null;
  function scheduleResync() { clearTimeout(resyncTimer); resyncTimer = setTimeout(() => { if (app.dx && app.state) renderCurrent(); }, HOLD + 80); }
  const touch = el => { el._edit = Date.now(); scheduleResync(); };
  const recent = el => el._edit && Date.now() - el._edit < HOLD;
  let releaseSlider = null;
  for (const type of ['pointerup', 'pointercancel']) window.addEventListener(type, () => { if (releaseSlider) { releaseSlider(); releaseSlider = null; scheduleResync(); } });
  /** opts.snap = { at, within }: a drag that ends within `within` steps of `at` lands on it, and a double-click goes there (the centre
   *  of pan, BRIGHT and the octave shift). The keyboard is not snapped, so the arrow keys can still step off the centre. */
  function slider(min, max, fmt, onSet, cls, opts = {}) {
    const wrap = h('label', 'ctl ' + (cls || '')), input = h('input'), out = h('span', 'val'), track = h('span', 'track');
    input.type = 'range'; input.min = min; input.max = max; input.step = 1; track.append(input);
    if (opts.snap) { const tick = h('i', 'tick'); tick.style.setProperty('--at', (opts.snap.at - min) / (max - min)); track.append(tick); }
    wrap.append(track, out);
    const send = throttled(onSet); let busy = false;
    const apply = () => { touch(input); out.textContent = fmt(+input.value); send(+input.value); };
    input.addEventListener('input', () => {
      if (opts.snap && busy) input.value = DxControls.snap(+input.value, opts.snap.at, opts.snap.within);
      apply();
    });
    if (opts.snap) input.addEventListener('dblclick', () => { input.value = opts.snap.at; apply(); });
    input.addEventListener('pointerdown', () => { busy = true; releaseSlider = () => { busy = false; }; });
    return { el: wrap, input, set(v, label) { if (busy || recent(input)) return; input.value = v; out.textContent = label != null ? label : fmt(v); } };
  }
  function select(options, onSet, cls) {
    const s = h('select', cls); options.forEach(([value, label]) => s.append(new Option(label, value)));
    s.addEventListener('change', () => { touch(s); onSet(+s.value); s.blur(); });
    return { el: s, node: s, set(v) { if (!recent(s)) s.value = v; } };
  }
  function field(label, node, cls) { const f = h(node.matches && node.matches('.keyrange, .dual-wrap, .seg') ? 'div' : 'label', 'field' + (cls ? ' ' + cls : '')); f.append(h('span', null, label), node); return f; }
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
    updatePresetBar();
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
      app.dx = dx; app.mode = mode; app.sig = ''; app.voices = null; app.voiceState = null; app.autoReread = false; app.scenes = null; app.sceneRev = null; app.errors = 0; app.state = null;
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
  /** Read the voice presets (the names of the 128 library voices) from the unit, so the voice lists show what the unit holds.
   *  force: forget what was read before (the library may have changed under us). */
  async function loadVoices(force) {
    const dx = app.dx; if (!dx) return;
    const seq = ++app.voiceSeq; if (force) dx.forgetVoiceNames();
    app.voiceState = 'loading'; app.voiceBanks = 0; updatePresetBar();
    try {
      const names = [];
      for (let b = 0; b < 4; b++) { names.push(...await dx.voiceNames(b)); if (app.dx !== dx || seq !== app.voiceSeq) return; app.voiceBanks = b + 1; updatePresetBar(); }
      const changed = !app.voices || app.voices.join('\n') !== names.join('\n');
      app.voices = names; app.voiceState = 'ok'; updatePresetBar();
      if (changed) { app.sig = ''; renderCurrent(); }                         // rebuild the lists only when a name is different
      if (app.state) checkLibrary(app.state);
    } catch (error) {
      if (app.dx !== dx || seq !== app.voiceSeq) return;
      app.voiceState = 'error'; app.voiceError = error.message || String(error); updatePresetBar();
      toast('Could not read the voice presets: ' + app.voiceError, true);
    }
  }
  function updatePresetBar() {
    const bar = $('#presetbar'); if (!bar) return;
    bar.hidden = !app.dx;
    const where = app.mode === 'unit' ? 'the FM-1' : 'the simulator', status = $('#preset-status');
    status.className = app.voiceState === 'error' ? 'err' : '';
    status.textContent = !app.dx ? '' : app.voiceState === 'loading' ? 'Reading the voice presets from ' + where + '… ' + app.voiceBanks + ' of 4 banks'
      : app.voiceState === 'error' ? 'Could not read the presets (' + app.voiceError + '). ' + (app.voices ? 'The lists below may be out of date: press Reload presets.' : 'Voices show their program numbers until this works.')
      : app.voices ? 'Presets: the ' + app.voices.length + ' voices of banks A to D, as ' + where + ' holds them.' : '';
    $('#preset-reload').disabled = !app.dx || app.voiceState === 'loading';
  }
  /** The layer's engine plays a voice other than the library voice of its program (edited, or sent as DX7 SysEx). */
  function voiceDiffers(x, lib) {
    const name = (x.voice || '').trim();
    return !!(x.up && name && typeof lib === 'string' && lib.trim() !== name);
  }
  /** After the names were read: when a part shows a voice the library does not list, they may be out of date (the library changed while
   *  the page was open): read them again, once per connection. Never from a poll, so it cannot start while the user is working. */
  function checkLibrary(s) {
    if (!app.voices || app.autoReread || app.voiceState !== 'ok') return;
    if (s.engines.some(x => x.owner < 8 && voiceDiffers(x, app.voices[x.program]))) { app.autoReread = true; loadVoices(true); }
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
      const name = h('input', 'name'); name.maxLength = 12; name.placeholder = 'Part name'; name.title = 'The unit shows the first 10 letters; without a name it shows the voice of the main layer'; name.setAttribute('aria-label', 'Name of part ' + (p + 1));
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
  /** The voice list of a layer: the unit's own 128 presets in four banks, named as the unit's library names them. */
  function voiceSelect(onSet) {
    const s = h('select', 'voice');
    for (let b = 0; b < 4; b++) {
      const group = h('optgroup'); group.label = 'Bank ' + 'ABCD'[b];
      for (let i = 0; i < 32; i++) group.append(new Option(voiceLabel(b * 32 + i), b * 32 + i));
      s.append(group);
    }
    s.addEventListener('change', () => { touch(s); onSet(+s.value); s.blur(); });
    return { el: s, node: s, set(v) { if (!recent(s)) s.value = v; } };
  }
  const shiftText = v => (v > 0 ? '+' : '') + v + ' oct';
  function buildLayer(e, index, color, part) {
    const set = (f, v) => act(() => app.dx.setLayer(e, f, v)), box = h('div', 'layer');
    const chip = h('span', 'chip small', index + 1); chip.style.background = color;
    const voice = voiceSelect(v => act(() => app.dx.setLayer(e, 14, v), refresh));
    voice.node.setAttribute('aria-label', 'Voice of part ' + (part.index + 1) + ' layer ' + (index + 1));
    const odd = h('span', 'voice-odd'); odd.hidden = true;
    const mute = h('button', 'toggle', 'Mute'), solo = h('button', 'toggle', 'Solo'), remove = h('button', 'warn', 'Remove');
    mute.onclick = () => act(() => app.dx.setLayer(e, 12, app.state.engines[e].muted ? 0 : 1), refresh);
    solo.onclick = () => act(() => app.dx.setLayer(e, 13, app.state.engines[e].solo ? 0 : 1), refresh);
    remove.onclick = () => act(() => app.dx.partOp(2, part.index, index), refresh);
    const row1 = h('div', 'row', chip, voice.el, odd, mute, solo, remove);
    const centre = { snap: { at: 0, within: 3 } };
    const level = slider(0, 127, v => v, v => set(0, v)), pan = slider(-50, 50, v => (v ? (v < 0 ? 'L' : 'R') + Math.abs(v) : 'C'), v => set(1, v + 50), '', centre),
      bright = slider(-50, 50, v => (v > 0 ? '+' : '') + v, v => set(2, v + 50), '', centre), rev = slider(0, 15, v => v, v => set(3, v)), dly = slider(0, 15, v => v, v => set(4, v));
    const row2 = h('div', 'row sliders', field('Level', level.el), field('Pan', pan.el), field('BRIGHT', bright.el), field('Reverb', rev.el), field('Delay', dly.el));
    const where = 'part ' + (part.index + 1) + ' layer ' + (index + 1);
    const keys = DxControls.keyRange((end, n) => set(end === 'lo' ? 5 : 6, n), color, where);
    const shift = slider(-4, 4, shiftText, v => set(7, v * 12 + 64), '', { snap: { at: 0, within: 0 } });
    const fold = select(FOLD.map((n, i) => [i, n]), v => set(8, v));
    const vel = DxControls.dualSlider(1, 127, (end, v) => set(end === 'lo' ? 9 : 10, v), ['Lowest', 'Highest'], where);
    const poly = DxControls.segmented(Array.from({ length: 8 }, (_, i) => [i + 1, String(i + 1)]), v => set(11, v), 'Polyphony in voices, ' + where);
    const row3 = h('div', 'row', field('Octave shift', shift.el), field('Fold', fold.el), field('Velocity range', vel.el), field('Polyphony (voices)', poly.el));
    const row4 = h('div', 'row', field('Key range', keys.el, 'full'));
    box.append(row1, row2, row4, row3);
    app.controls.push(st => {
      const x = st.engines[e], lib = app.voices && app.voices[x.program];
      voice.set(x.program); level.set(x.level); pan.set(x.pan); bright.set(x.bright); rev.set(x.reverb); dly.set(x.delay);
      keys.set(x.low, x.high); shift.set(Math.round(x.shift / 12), x.shift % 12 ? (x.shift > 0 ? '+' : '') + x.shift + ' semitones' : undefined); fold.set(x.fold);
      poly.set(x.poly); vel.set(Math.max(1, x.velLow), x.velHigh || 127);
      const differs = voiceDiffers(x, lib); odd.hidden = !differs;
      if (differs) { odd.textContent = 'Sounding as \u201c' + x.voice + '\u201d'; odd.title = 'The voice in this layer is not the library voice ' + voiceLabel(x.program) + ': it was edited or loaded as DX7 SysEx.'; }
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
      const dx = app.dx;
      try { await dx.importRom(+target.node.value, bytes); toast('Imported and read back'); }
      finally { app.autoReread = false; soundsUI.key = ''; loadVoices(true); fillSounds(); }   // the names changed (or may have, if the read-back failed)
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
      ['Configure', 'Eight parts, each on one MIDI channel with one to eight layers. A layer is one DX7 voice with its own key range, velocity range, octave shift, polyphony, level, pan, BRIGHT and sends. The voice lists hold the presets read from the unit (Reload presets reads them again). Pan, BRIGHT and the octave shift snap to the centre (double-click goes there); on the keyboard, click or drag to set the lowest or highest key (Reset selects the whole keyboard); the velocity window has a handle at each end. Choose Play this part to make it the part the keyboard plays and the DX7 editors edit.'],
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
    DxControls.onTouch = scheduleResync;
    $('#preset-reload').onclick = () => { app.autoReread = true; loadVoices(true); };
    $('#btn-reset').onclick = () => { if (confirm('Erase the simulator\'s saved scenes and settings and start again?')) app.sim.reset(); };
    const wasm = Uint8Array.from(atob(DX_SIMULATOR.data), c => c.charCodeAt(0));
    app.sim = new SimPanel($('#sim-host'), wasm);
    app.sim.on((what, value) => { if (what === 'audio') $('#sound-note').textContent = 'Sound is on.'; if (what === 'audio-error') toast('Sound could not start: ' + value.message, true); });
    bindKeyboard(); help();
    $('#sound-note').textContent = 'Click the panel or a key to switch the sound on.';
    document.addEventListener('pointerdown', () => app.sim.startAudio(), { once: true });
    const first = location.hash.slice(1);                      // a link can open a tab: #device, #configure, #sounds ...
    showTab(document.querySelector('#tabs button[data-tab="' + first + '"]') ? first : 'device'); setStatus();
    useSimulator();
    app.release = release; app.setLocked = on => { app.locked = on; setStatus(); };
    globalThis.DxApp = app;
  }
  document.addEventListener('DOMContentLoaded', start);
})();
