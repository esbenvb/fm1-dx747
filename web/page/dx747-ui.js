/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The DX747 workbench: tabs, connection (the simulator in the page or the unit over Web MIDI) and the configurator for parts, layers,
 * sounds, scenes, filter, effects and settings. It talks to the unit through Dx747 (dx747-client.js, docs/CONFIG-PROTOCOL.md),
 * which polls the unit, so a change made on the panel shows here and a change made here shows on the panel. */
(() => {
  'use strict';
  const COLORS = ['#ff5a5a', '#ffa23a', '#ffe34a', '#5cf06a', '#2de0d0', '#5aa9ff', '#b68cff', '#ff7ad2'];
  const FOLD = ['Drop', 'Clip', 'Wrap'], FILTER = ['Off', 'Low pass', 'High pass', 'Band pass'];
  const noteName = n => Dx747.noteName(n);
  const $ = (sel, root = document) => root.querySelector(sel);
  function h(tag, cls, ...kids) {
    const e = document.createElement(tag); if (cls) e.className = cls;
    for (const k of kids) if (k != null) e.append(k.nodeType ? k : document.createTextNode(String(k)));
    return e;
  }
  const app = { sim: null, dx: null, mode: null, realAccess: null, state: null, voices: null, scenes: null, sceneBank: 0, sceneSel: 0, soundBank: 0, tab: 'device', sig: '', controls: [] };

  // ---- small things -------------------------------------------------------------------------------
  let toastTimer = null;
  function toast(text, bad = false) {
    const t = $('#toast'); t.textContent = text; t.className = 'show' + (bad ? ' bad' : '');
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
  function slider(min, max, fmt, onSet, cls) {
    const wrap = h('label', 'ctl ' + (cls || '')), input = h('input'), out = h('span', 'val');
    input.type = 'range'; input.min = min; input.max = max; input.step = 1; wrap.append(input, out);
    const send = throttled(onSet); let busy = false;
    input.addEventListener('input', () => { out.textContent = fmt(+input.value); send(+input.value); });
    input.addEventListener('pointerdown', () => { busy = true; });
    window.addEventListener('pointerup', () => { busy = false; });
    return { el: wrap, set(v) { if (busy) return; input.value = v; out.textContent = fmt(v); } };
  }
  function select(options, onSet, cls) {
    const s = h('select', cls); options.forEach(([value, label]) => s.append(new Option(label, value)));
    s.addEventListener('change', () => onSet(+s.value));
    return { el: s, set(v) { if (document.activeElement !== s) s.value = v; }, node: s };
  }
  function field(label, node) { const f = h('label', 'field'); f.append(h('span', null, label), node); return f; }
  const NOTE_OPTIONS = Array.from({ length: 128 }, (_, n) => [n, noteName(n) + '  (' + n + ')']);
  const PART_COLOR = p => COLORS[p % 8];

  // ---- connection --------------------------------------------------------------------------------------
  function setStatus() {
    const s = $('#conn'), dx = app.dx;
    if (dx && dx.connected) { s.textContent = (app.mode === 'simulator' ? 'Simulator' : 'FM-1') + ' connected  ·  DX747 ' + dx.hello.release + '  ·  protocol ' + dx.hello.protocol; s.className = 'pill ok'; }
    else { s.textContent = 'Not connected'; s.className = 'pill'; }
    $('#btn-sim').classList.toggle('active', app.mode === 'simulator' && !!(dx && dx.connected));
    $('#btn-unit').classList.toggle('active', app.mode === 'unit' && !!(dx && dx.connected));
  }
  async function connect(access, mode) {
    if (app.dx) { app.dx.stopPolling(); await app.dx.disconnect().catch(() => {}); }
    const dx = new Dx747(access);
    try { await dx.connect(); } catch (error) { app.dx = null; app.mode = null; setStatus(); throw error; }
    app.dx = dx; app.mode = mode; app.sig = ''; app.voices = null; app.scenes = null;
    setStatus();
    dx.startPolling(onState, error => { toast('Lost contact with the unit: ' + error.message, true); setStatus(); }, 400);
    if (app.kb) { app.kb.refreshOutputs(); app.kb.refreshInputs(); }
    loadVoices();
  }
  async function useSimulator() { await act(() => connect(app.sim.access, 'simulator')); }
  async function useUnit() {
    await act(async () => {
      app.realAccess = await DxDevice.requestUnitAccess();
      app.realAccess.onstatechange = () => { if (app.kb) { app.kb.refreshOutputs(); app.kb.refreshInputs(); } };
      await connect(app.realAccess, 'unit');
      toast('Connected to the FM-1');
    });
  }
  async function loadVoices() {
    const dx = app.dx; if (!dx) return;
    try {
      const names = [];
      for (let b = 0; b < 4; b++) { names.push(...await dx.voiceNames(b)); if (app.dx !== dx) return; app.voices = names.slice(); if (app.state) { app.sig = ''; renderCurrent(); } }
    } catch (error) { toast('Could not read the voice library: ' + error.message, true); }
  }
  const voiceLabel = p => 'ABCD'[p >> 5] + String((p & 31) + 1).padStart(2, '0') + '  ' + ((app.voices && app.voices[p]) || 'program ' + (p + 1));

  // ---- state in, drawing out -------------------------------------------------------------------------
  function onState(s) { app.state = s; renderCurrent(); }
  function renderCurrent() {
    if (!app.state) return;
    ({ configure: renderConfigure, master: renderMaster, scenes: renderScenes, sounds: renderSounds, keyboard: () => {} }[app.tab] || (() => {}))();
  }
  function showTab(name) {
    app.tab = name;
    document.querySelectorAll('#tabs button').forEach(b => b.classList.toggle('active', b.dataset.tab === name));
    document.querySelectorAll('main > section').forEach(s => { s.hidden = s.id !== 'tab-' + name; });
    if (name === 'scenes') loadScenes(); if (name === 'keyboard' && app.kb) { app.kb.refreshOutputs(); app.kb.refreshInputs(); }
    renderCurrent();
  }

  // ---- Configure: parts and layers -----------------------------------------------------------------------
  const layerFields = [];
  function renderConfigure() {
    const root = $('#parts'), s = app.state;
    if (!s) return;
    const sig = JSON.stringify([s.parts.map(p => [p.used, p.layers.map(l => l.engine)]), !!app.voices]);
    if (sig !== app.sig) { app.sig = sig; buildParts(root, s); }
    app.controls.forEach(c => c(s));
  }
  function buildParts(root, s) {
    root.textContent = ''; app.controls = [];
    const dx = app.dx;
    for (const part of s.parts) {
      const p = part.index, color = PART_COLOR(p), card = h('section', 'part');
      card.style.setProperty('--c', color);
      const head = h('div', 'part-head'), chip = h('span', 'chip', p + 1); chip.style.background = color; head.append(chip);
      if (!part.used) {
        head.append(h('b', null, 'Empty slot'));
        const add = h('button', null, 'Add a part here'); add.onclick = () => act(() => dx.partOp(0, p), refresh); head.append(add);
        card.append(head); root.append(card); continue;
      }
      const chan = select(Array.from({ length: 16 }, (_, i) => [i + 1, 'Channel ' + (i + 1)]), v => act(() => dx.setPart(p, 1, v), refresh));
      const name = h('input', 'name'); name.maxLength = 12; name.placeholder = 'Part name';
      name.addEventListener('change', () => act(() => dx.partName(p, name.value.toUpperCase()), refresh));
      const level = slider(0, 127, v => v, v => act(() => dx.setPart(p, 0, v)), 'wide');
      const play = h('button', null, 'Play this part'); play.onclick = () => act(() => dx.partOp(3, p), refresh);
      const addLayer = h('button', null, 'Add layer'); addLayer.onclick = () => act(() => dx.partOp(1, p), refresh);
      const removePart = h('button', 'warn', 'Remove part');
      removePart.onclick = () => act(async () => { for (let i = app.state.parts[p].layers.length - 1; i >= 0; i--) await dx.partOp(2, p, i); }, refresh);
      head.append(name, field('Channel', chan.el), field('Level', level.el), play, addLayer, removePart);
      const lane = h('div', 'lane'); card.append(head, lane);
      app.controls.push(st => {
        const pt = st.parts[p]; chan.set(pt.channel); level.set(pt.level);
        if (document.activeElement !== name) name.value = pt.name;
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
    const dx = app.dx, set = (f, v) => act(() => dx.setLayer(e, f, v)), box = h('div', 'layer');
    const chip = h('span', 'chip small', index + 1); chip.style.background = color;
    const voice = select(Array.from({ length: 128 }, (_, n) => [n, voiceLabel(n)]), v => act(() => dx.setLayer(e, 14, v), refresh), 'voice');
    const mute = h('button', 'toggle', 'Mute'), solo = h('button', 'toggle', 'Solo'), remove = h('button', 'warn', 'Remove');
    mute.onclick = () => act(() => dx.setLayer(e, 12, app.state.engines[e].muted ? 0 : 1), refresh);
    solo.onclick = () => act(() => dx.setLayer(e, 13, app.state.engines[e].solo ? 0 : 1), refresh);
    remove.onclick = () => act(() => dx.partOp(2, part.index, index), refresh);
    const row1 = h('div', 'row', chip, voice.el, mute, solo, remove);
    const level = slider(0, 127, v => v, v => set(0, v)), pan = slider(-50, 50, v => (v ? (v < 0 ? 'L' : 'R') + Math.abs(v) : 'C'), v => set(1, v + 50)),
      bright = slider(-50, 50, v => (v > 0 ? '+' : '') + v, v => set(2, v + 50)), rev = slider(0, 15, v => v, v => set(3, v)), dly = slider(0, 15, v => v, v => set(4, v));
    const row2 = h('div', 'row sliders', field('Level', level.el), field('Pan', pan.el), field('BRIGHT', bright.el), field('Reverb', rev.el), field('Delay', dly.el));
    const lo = select(NOTE_OPTIONS, v => set(5, v)), hi = select(NOTE_OPTIONS, v => set(6, v));
    const shift = select(Array.from({ length: 9 }, (_, i) => [i - 4, (i - 4 > 0 ? '+' : '') + (i - 4) + ' octave' + (Math.abs(i - 4) === 1 ? '' : 's')]), v => set(7, v * 12 + 64));
    const fold = select(FOLD.map((n, i) => [i, n]), v => set(8, v)), poly = select(Array.from({ length: 8 }, (_, i) => [i + 1, i + 1 + ' voice' + (i ? 's' : '')]), v => set(11, v));
    const vlo = h('input', 'num'), vhi = h('input', 'num');
    for (const [inp, f] of [[vlo, 9], [vhi, 10]]) { inp.type = 'number'; inp.min = 1; inp.max = 127; inp.addEventListener('change', () => set(f, Math.max(1, Math.min(127, +inp.value || 1)))); }
    const row3 = h('div', 'row', field('Lowest key', lo.el), field('Highest key', hi.el), field('Shift', shift.el), field('Fold', fold.el), field('Velocity', h('span', 'pair', vlo, '–', vhi)), field('Polyphony', poly.el));
    box.append(row1, row2, row3);
    app.controls.push(st => {
      const x = st.engines[e];
      voice.set(x.program); level.set(x.level); pan.set(x.pan); bright.set(x.bright); rev.set(x.reverb); dly.set(x.delay);
      lo.set(x.low); hi.set(x.high); shift.set(Math.round(x.shift / 12)); fold.set(x.fold); poly.set(x.poly);
      if (document.activeElement !== vlo) vlo.value = x.velLow; if (document.activeElement !== vhi) vhi.value = x.velHigh;
      mute.classList.toggle('on', x.muted); solo.classList.toggle('on', x.solo); box.classList.toggle('silent', !x.audible);
    });
    return box;
  }

  // ---- Master and FX ---------------------------------------------------------------------------------------
  let masterBuilt = false;
  function renderMaster() {
    if (!masterBuilt) { buildMaster(); masterBuilt = true; }
    const s = app.state; masterSet.forEach(f => f(s));
  }
  const masterSet = [];
  function buildMaster() {
    const dx = app.dx, root = $('#master-body'); root.textContent = '';
    const type = select(FILTER.map((n, i) => [i, n]), v => act(() => dx.setMaster(0, v), refresh));
    const cut = slider(0, 1000, v => { const hz = Math.round(20 * Math.pow(1000, v / 1000)); return hz >= 1000 ? (hz / 1000).toFixed(1) + ' kHz' : hz + ' Hz'; }, v => act(() => dx.setMaster(1, Math.round(20 * Math.pow(1000, v / 1000)))), 'wide');
    const res = slider(0, 127, v => v, v => act(() => dx.setMaster(2, v)), 'wide');
    root.append(h('h3', null, 'Master filter (all parts)'), h('div', 'row', field('Type', type.el), field('Cutoff', cut.el), field('Resonance', res.el)));
    const fx = [['Reverb decay', 3, 'decay'], ['Delay time', 4, 'delay'], ['Delay feedback', 5, 'feedback'], ['Effects return', 6, 'ret']].map(([label, f, key]) => {
      const c = slider(0, 255, v => v, v => act(() => dx.setMaster(f, v)), 'wide'); masterSet.push(s => c.set(s.effects[key])); return field(label, c.el);
    });
    root.append(h('h3', null, 'Shared reverb and delay'), h('div', 'row', ...fx), h('p', 'hint', 'Each layer\'s own reverb and delay amounts are on the Configure tab. The delay time also has synchronised settings in its upper values; the panel\'s FX screen shows them by name.'));
    const chans = [[0, 'Off'], ...Array.from({ length: 16 }, (_, i) => [i + 1, 'Channel ' + (i + 1)])];
    const sc = select(chans, v => act(() => dx.setSetting(0, v), refresh)), mc = select(chans, v => act(() => dx.setSetting(1, v), refresh));
    const light = slider(0, 8, v => v, v => act(() => dx.setSetting(2, v)));
    const bt = h('input'); bt.type = 'checkbox'; bt.onchange = () => act(() => dx.setSetting(3, bt.checked ? 1 : 0), refresh);
    root.append(h('h3', null, 'Unit settings'), h('div', 'row', field('Scene channel (program change recalls a scene)', sc.el), field('Master control channel (CC74 cutoff, CC71 resonance, CC70 type)', mc.el), field('Button light', light.el), field('Bluetooth audio in', bt)));
    masterSet.push(s => { type.set(s.filter.type); cut.set(Math.round(1000 * Math.log(Math.max(20, s.filter.hz) / 20) / Math.log(1000))); res.set(s.filter.resonance);
      sc.set(s.settings.sceneChannel); mc.set(s.settings.masterChannel); light.set(s.settings.backlight); bt.checked = s.settings.bluetoothAudio; });
  }

  // ---- Scenes ----------------------------------------------------------------------------------------------------
  async function loadScenes() {
    if (!app.dx || !app.dx.connected) return;
    try { app.scenes = await app.dx.scenes(); renderScenes(); } catch (error) { toast('Could not read the scenes: ' + error.message, true); }
  }
  function renderScenes() {
    const root = $('#scenes-body'); if (!root) return;
    const s = app.state, list = app.scenes; root.textContent = '';
    const banks = h('div', 'banks'); for (let b = 0; b < 4; b++) { const k = h('button', b === app.sceneBank ? 'active' : '', 'Bank ' + 'ABCD'[b]); k.onclick = () => { app.sceneBank = b; renderScenes(); }; banks.append(k); }
    const grid = h('div', 'scene-grid');
    for (let i = 0; i < 16; i++) {
      const idx = app.sceneBank * 16 + i, sc = list && list[idx], card = h('button', 'scene' + (idx === app.sceneSel ? ' sel' : '') + (s && s.scene === idx ? ' current' : ''));
      card.append(h('b', null, idx + 1), h('span', null, sc && sc.name ? sc.name : (list ? '—' : '…')));
      const dots = h('div', 'dots'); for (let p = 0; p < 8; p++) { const d = h('i'); if (sc && (sc.parts >> p) & 1) d.style.background = COLORS[p]; dots.append(d); } card.append(dots);
      card.onclick = () => { app.sceneSel = idx; renderScenes(); }; grid.append(card);
    }
    const sc = list && list[app.sceneSel], exists = !!(sc && sc.name.trim());
    const name = h('input', 'name'); name.maxLength = 12; name.value = exists ? sc.name : 'SCENE ' + String(app.sceneSel + 1).padStart(2, '0'); name.placeholder = 'Scene name';
    const dx = app.dx, done = async () => { await loadScenes(); await refresh(); };
    const refused = 'The unit refused this. If the scene store is not set up yet, press SAVE on the unit (or the simulator panel) once and confirm; names may only use letters, digits and punctuation.';
    const mk = (label, cls, fn) => { const b = h('button', cls, label); b.onclick = () => act(fn, done, refused); return b; };
    const bar = h('div', 'row', h('b', null, 'Scene ' + (app.sceneSel + 1)), name,
      mk('Recall', '', () => dx.sceneRecall(app.sceneSel)), mk('Save the current sound here', '', () => dx.sceneSave(app.sceneSel, name.value.toUpperCase())),
      mk('Rename', '', () => dx.sceneRename(app.sceneSel, name.value.toUpperCase())), mk('Delete', 'warn', () => dx.sceneDelete(app.sceneSel)));
    root.append(banks, grid, bar, h('p', 'hint', 'A scene holds all eight parts: their channels, layers, voices, ranges, levels, pan, sends, the filter and the effects. If saving is refused the scene store is not set up yet: press SAVE on the unit (or on the simulator panel) once and confirm. Saving writes flash, so give it a moment between scenes.'));
  }

  // ---- Sounds ------------------------------------------------------------------------------------------------------------
  function framedEngine() {
    const s = app.state, p = s && s.parts[s.selected]; if (!p || !p.used) return -1;
    const l = p.layers[Math.min(p.selectedLayer, p.layers.length - 1)]; return l ? l.engine : -1;
  }
  async function renderSounds() {
    const root = $('#sounds-body'); if (!root || !app.dx) return;
    const bank = app.soundBank, names = await app.dx.voiceNames(bank).catch(() => null);
    if (app.tab !== 'sounds') return;
    root.textContent = '';
    const banks = h('div', 'banks'); for (let b = 0; b < 4; b++) { const k = h('button', b === bank ? 'active' : '', 'Bank ' + 'ABCD'[b]); k.onclick = () => { app.soundBank = b; renderSounds(); }; banks.append(k); }
    const e = framedEngine(), cur = e >= 0 ? app.state.engines[e].program : -1;
    const grid = h('div', 'voice-grid');
    (names || []).forEach((n, i) => {
      const p = bank * 32 + i, b = h('button', 'voice' + (p === cur ? ' sel' : ''), h('i', null, String(p + 1)), ' ' + (n || '—'));
      b.onclick = () => { if (e < 0) { toast('Select a part first (Configure tab: Play this part)', true); return; } act(() => app.dx.setLayer(e, 14, p), refresh); };
      grid.append(b);
    });
    const file = h('input'); file.type = 'file'; file.accept = '.syx';
    const target = select([[0, 'Bank A'], [1, 'Bank B'], [2, 'Bank C'], [3, 'Bank D']], () => {}); target.node.value = 3;
    const go = h('button', null, 'Import into the unit');
    go.onclick = () => act(async () => {
      if (!file.files[0]) throw new Error('Choose a 32-voice DX7 bank file (.syx) first');
      const bytes = new Uint8Array(await file.files[0].arrayBuffer());
      if (!confirm('Replace bank ' + 'ABCD'[+target.node.value] + ' of the voice library with this file?')) return;
      await app.dx.importRom(+target.node.value, bytes); toast('Imported and read back');
    }, () => { app.soundBank = +target.node.value; return renderSounds(); });
    root.append(banks, h('p', 'hint', e >= 0 ? 'Click a voice to give it to the selected part\'s framed layer (part ' + (app.state.selected + 1) + ').' : 'Select a part on the Configure tab to choose its voice here.'), grid,
      h('h3', null, 'Import a DX7 ROM or bank file'), h('div', 'row', file, field('Into', target.el), go));
  }

  // ---- start ------------------------------------------------------------------------------------------------------------------------
  function help() {
    $('#tab-help').append(...[
      ['What this is', 'The workbench for DX747, the firmware that makes the FM-1 an eight-part multitimbral DX7 module. Use it with the simulator in this page or with the unit over USB. Everything you see is the unit\'s own state: changes made here appear on the panel and the other way round.'],
      ['Connect', 'Simulator: starts with the page; click anywhere to start its sound. FM-1: plug it in by USB, close other MIDI programs, press Connect FM-1 and allow MIDI with SysEx (Chrome or Edge on a computer).'],
      ['Configure', 'Eight parts, each on one MIDI channel with one to eight layers. A layer is one DX7 voice with its own key range, velocity range, octave shift, level, pan, BRIGHT and sends. Choose Play this part to make it the part the keyboard plays and the DX7 editors edit.'],
      ['Sounds, Scenes, Master', 'Sounds: the 128 voices in four banks, and DX7 ROM import. Scenes: 64 stored setups with names; a program change on the scene channel recalls one. Master: the filter, the shared reverb and delay and the unit\'s settings.'],
      ['Keyboard', 'Play any part from the computer keyboard or the on-screen piano, with a channel and output of your choice. Use it to try layers and splits.'],
    ].flatMap(([t, p]) => [h('h3', null, t), h('p', null, p)]));
  }
  function bindKeyboard() {
    app.kb = new KeyboardTab($('#kb-host'), {
      outputs() {
        const out = [];
        if (app.sim) out.push({ name: 'Simulator (in this page)', send: b => app.sim.access.output.send(b) });
        if (app.realAccess) for (const p of app.realAccess.outputs.values()) if (p.state === 'connected') out.push({ name: p.name, send: b => { p.open && p.open(); p.send(b); } });
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
    const note = $('#sound-note'); note.textContent = 'Click the panel or a key to switch the sound on.';
    document.addEventListener('pointerdown', () => app.sim.startAudio(), { once: true });
    showTab('device'); setStatus();
    useSimulator();
    globalThis.DxApp = app;
  }
  document.addEventListener('DOMContentLoaded', start);
})();
