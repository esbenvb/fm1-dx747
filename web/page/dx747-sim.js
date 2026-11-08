/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The simulator panel in the page: the screen, the 14 buttons, 7 encoders, master knob and 27 keys of the FM-1 around a
 * DxCore, with the clock and the sound. The clock follows the wall clock; once the browser lets the page make sound (after a
 * click) the audio blocks go to an AudioWorklet. The flash image is kept in localStorage so that scenes survive a reload. */
(() => {
  'use strict';
  const BUTTONS = ['OCT-', 'OCT+', 'FX', 'SEL', 'ENV', 'LFO', 'EDIT', 'GLO', 'HOME', 'SAVE', 'ARP', 'SEQ', 'PLAY', 'REC'];
  const ENCODERS = ['SELECT', 'ALGO', 'K1', 'K2', 'K3', 'K4', 'PRESETS'];
  const WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26];      // key indices of the 16 white keys, left to right
  const STORE = 'dx747.simulator.flash.v1';
  const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };

  const WORKLET = `class DxOut extends AudioWorkletProcessor {
    constructor() { super(); this.queue = []; this.at = 0; this.played = 0; this.sent = 0;
      this.port.onmessage = e => { this.queue.push(e.data); };
    }
    process(inputs, outputs) {
      const out = outputs[0], left = out[0], right = out[1] || out[0];
      let block = this.queue[0];
      for (let i = 0; i < left.length; i++) {
        if (!block) { left[i] = 0; right[i] = 0; continue; }
        left[i] = block[this.at * 2]; right[i] = block[this.at * 2 + 1]; this.at++; this.played++;
        if (this.at * 2 >= block.length) { this.queue.shift(); block = this.queue[0]; this.at = 0; }
      }
      if (++this.sent % 8 === 0) this.port.postMessage(this.played);
      return true;
    }
  }
  registerProcessor('dx-out', DxOut);`;

  class SimPanel {
    /** container: where the panel is built; wasm: the module bytes. */
    constructor(container, wasm) {
      this.container = container; this.wasm = wasm; this.listeners = [];
      let flash = null;
      try { const saved = localStorage.getItem(STORE); if (saved) flash = Uint8Array.from(atob(saved), c => c.charCodeAt(0)); } catch (e) { /* storage may be blocked */ }
      this.core = new DxCore(wasm, flash);
      if (!flash) { this.core.prepareStore(); this.core.advance(400); }   // a fresh simulator has its scene store set up already
      // The SMK-37's own SDK script (sdk/WebUpdate.js of that tree) only takes ports whose name says smk37, the FM-1's only fm1.
      this.access = new DxDevice.VirtualMidiAccess(this.core, this.core.smk37 ? 'SMK-37 DX747 (simulator)' : undefined);
      this.audio = null; this.lastRevision = -1; this.lastFlash = this.core.flashRevision; this.savedAt = 0;
      this.last = performance.now(); this.debt = 0; this.queued = 0; this.produced = 0;
      this.build();
      this.timer = setInterval(() => this.tick(), 8);
    }
    on(fn) { this.listeners.push(fn); }
    // ---- clock, screen and sound -----------------------------------------------------------------
    tick() {
      const now = performance.now();
      this.debt += Math.min(now - this.last, 100); this.last = now;
      const blockMs = this.core.blockFrames * 1000 / this.core.sampleRate;
      for (let n = 0; this.debt >= blockMs && n < 40; n++) {
        const block = this.core.render(); this.debt -= blockMs;
        if (this.audio && this.produced - this.audio.played < 8192) { this.audio.push(block); this.produced += this.core.blockFrames; }
        else if (this.audio) this.produced = Math.max(this.produced, this.audio.played);
      }
      if (this.core.frameRevision !== this.lastRevision) { this.lastRevision = this.core.frameRevision; this.drawScreen(); this.drawLamps(); }
      if (this.core.flashRevision !== this.lastFlash && now - this.savedAt > 2000) this.save();
    }
    save() {
      this.lastFlash = this.core.flashRevision; this.savedAt = performance.now();
      try { let s = ''; const f = this.core.flash(); for (let i = 0; i < f.length; i += 8192) s += String.fromCharCode(...f.subarray(i, i + 8192)); localStorage.setItem(STORE, btoa(s)); } catch (e) { /* quota or blocked: the simulator still works */ }
    }
    reset() { try { localStorage.removeItem(STORE); } catch (e) { /* ignore */ } location.reload(); }
    /** Switch the sound on (needs a user gesture). Safe to call from every click: it starts once. */
    startAudio() {
      if (!this.audioStart) this.audioStart = this.openAudio();
      return this.audioStart;
    }
    async openAudio() {
      try {
        const ctx = new AudioContext({ sampleRate: this.core.sampleRate, latencyHint: 'interactive' });
        let sink;
        try {
          const url = URL.createObjectURL(new Blob([WORKLET], { type: 'text/javascript' }));
          try { await ctx.audioWorklet.addModule(url); } finally { URL.revokeObjectURL(url); }
          const node = new AudioWorkletNode(ctx, 'dx-out', { outputChannelCount: [2] });
          sink = { ctx, node, played: 0, push(block) { const copy = block.slice(); node.port.postMessage(copy, [copy.buffer]); } };
          node.port.onmessage = e => { sink.played = e.data; };
          node.connect(ctx.destination);
        } catch (workletError) { sink = this.scriptSink(ctx); }      // a page opened from disk cannot load a worklet from a blob: use the older node
        await ctx.resume();
        this.audio = sink; this.produced = 0;
        this.listeners.forEach(fn => fn('audio', true));
        return true;
      } catch (error) { this.audioStart = null; this.listeners.forEach(fn => fn('audio-error', error)); return false; }
    }
    /** The fallback sink: a ScriptProcessorNode that plays the same blocks, on the main thread. */
    scriptSink(ctx) {
      const queue = []; let at = 0;
      const sink = { ctx, played: 0, push(block) { queue.push(block.slice()); } };
      const node = ctx.createScriptProcessor(1024, 0, 2);
      node.onaudioprocess = e => {
        const left = e.outputBuffer.getChannelData(0), right = e.outputBuffer.getChannelData(1);
        for (let i = 0; i < left.length; i++) {
          const b = queue[0];
          if (!b) { left[i] = right[i] = 0; continue; }
          left[i] = b[at * 2]; right[i] = b[at * 2 + 1]; at++; sink.played++;
          if (at * 2 >= b.length) { queue.shift(); at = 0; }
        }
      };
      node.connect(ctx.destination); sink.node = node;
      return sink;
    }
    drawScreen() {
      const px = this.core.pixels(), img = this.image.data;
      for (let i = 0, o = 0; i < px.length; i++, o += 4) {
        const v = px[i];
        img[o] = ((v >> 11) & 31) * 255 / 31; img[o + 1] = ((v >> 5) & 63) * 255 / 63; img[o + 2] = (v & 31) * 255 / 31; img[o + 3] = 255;
      }
      this.ctx2d.putImageData(this.image, 0, 0);
    }
    drawLamps() {
      const l = this.core.leds();
      this.buttons.forEach((b, i) => { b.style.setProperty('--lamp', l[i] / 8); b.classList.toggle('lit', l[i] > 0); });
      this.keys.forEach((k, i) => { k.style.setProperty('--lamp', l[14 + i] / 8); k.classList.toggle('lit', l[14 + i] > 0); });
    }
    // ---- the panel ------------------------------------------------------------------------------------
    build() {
      const root = this.container; root.textContent = ''; root.classList.add('simpanel');
      const top = el('div', 'sim-top');
      const screen = el('div', 'sim-screen'); const canvas = el('canvas'); canvas.width = canvas.height = 240;
      this.ctx2d = canvas.getContext('2d'); this.image = this.ctx2d.createImageData(240, 240); screen.append(canvas);
      const controls = el('div', 'sim-controls');
      const enc = el('div', 'sim-encoders');
      ENCODERS.forEach((name, i) => {
        const box = el('div', 'sim-encoder'); const minus = el('button', 'sim-step', '−'), plus = el('button', 'sim-step', '+'), label = el('span', null, name);
        minus.setAttribute('aria-label', name + ' down'); plus.setAttribute('aria-label', name + ' up');
        minus.onclick = () => this.core.encoder(i, -1); plus.onclick = () => this.core.encoder(i, 1);
        box.addEventListener('wheel', e => { e.preventDefault(); this.core.encoder(i, e.deltaY < 0 ? 1 : -1); }, { passive: false });
        box.append(label, el('div', 'sim-pm')); box.lastChild.append(minus, plus); enc.append(box);
      });
      const knob = el('label', 'sim-knob', 'MASTER '); const range = el('input'); range.type = 'range'; range.min = 0; range.max = 1023; range.value = 900;
      range.oninput = () => this.core.knob(+range.value); this.core.knob(900); knob.append(range);
      const bar = el('div', 'sim-buttons'); this.buttons = BUTTONS.map((name, i) => {
        const b = el('button', 'sim-button', name); this.hold(b, down => this.core.button(i, down)); bar.append(b); return b;
      });
      controls.append(enc, knob, bar); top.append(screen, controls);
      const keys = el('div', 'sim-keys'); this.keys = [];
      for (let k = 0; k < 27; k++) {
        const white = WHITE.includes(k), b = el('button', 'sim-key ' + (white ? 'white' : 'black'));
        this.hold(b, down => this.core.key(k, down)); this.keys.push(b);
      }
      WHITE.forEach(k => keys.append(this.keys[k]));
      WHITE.forEach((k, i) => { this.keys[k].style.left = (i * 100 / 16) + '%'; });
      for (let k = 0; k < 27; k++) if (!WHITE.includes(k)) {            // a black key sits between the white keys on either side of it
        const left = WHITE.filter(w => w < k).length;
        this.keys[k].style.left = (left * 100 / 16 - 1.9) + '%'; keys.append(this.keys[k]);
      }
      root.append(top, keys);
    }
    hold(node, fn) {
      let down = false;
      const press = e => { e.preventDefault(); if (!down) { down = true; node.classList.add('held'); fn(true); this.startAudio(); } };
      const lift = () => { if (down) { down = false; node.classList.remove('held'); fn(false); } };
      node.addEventListener('pointerdown', press); node.addEventListener('pointerup', lift);
      node.addEventListener('pointerleave', lift); node.addEventListener('pointercancel', lift);
    }
  }
  /* ---- the SMK-37 -----------------------------------------------------------------------------------------------
   * The same core, clock, sound and flash, with the SMK-37's panel around it, laid out and labelled as the drawing in the SMK-37 manual
   * has it: the wheels, the screen with KNOB BANK / FADER BANK, < >, OCTAVE- OCTAVE+ under it, eight knobs K1..K8 above 16 pads (the
   * transport symbols printed under the lower row), four faders with twelve buttons under them, and 37 keys. The code is this project's.
   * The core takes the board's events: smk(kind, id, value). Everything on the panel is called by what is printed on it (DxCore.SMK37.BUTTONS,
   * ENCODERS): under every button, knob and fader is a short caption with what DX747 does with it, and a tooltip with the whole story. The
   * lamps are the firmware's own (sim_smk_lamps), the two banks' included (lit for parts 5 to 8). */
  /* What DX747 does with each of its buttons (the index is its number in DxCore.SMK37.VIRTUAL), said in the labels printed on the unit: printed(v) is
   * the label of the button that is DX747's button v, and the texts name the other buttons, knobs and screens the same way. */
  const printed = v => { const S = DxCore.SMK37, i = S.BUTTON_MAP.indexOf(v); return i >= 0 ? S.BUTTONS[i] : '?'; };
  const SMK_SHORT = ['oct / page -', 'oct / page +', 'FX', 'Mix', 'add layer', 'remove layer', 'Edit', 'System', 'Parts', 'Save', 'LEARN', 'Part', 'Scenes', 'MIDI in', 'back', 'forward'];
  const SMK_LONG = [
    () => printed(0) + ': Parts, Part, Mix, MIDI in: the keys one octave down. Edit pages, FX, Master filter: previous page. Details: previous part. Scenes: 16 scenes back. System: previous row. Pressed while ' + printed(12) + ' is held: previous scene, on any screen.',
    () => printed(1) + ': Parts, Part, Mix, MIDI in: the keys one octave up. Edit pages, FX, Master filter: next page. Details: next part. Scenes: 16 scenes on. System: next row. Pressed while ' + printed(12) + ' is held: next scene, on any screen.',
    () => printed(2) + ': the shared reverb and delay; pressed again, the master filter.',
    () => printed(3) + ': the Mix screen: levels, meters, pan and sends of all parts; mute and solo.',
    () => printed(4) + ': add a layer to the selected part; in an empty slot, a new part (Parts, Part, Details, edit pages, Mix).',
    () => printed(5) + ': remove the framed layer; the last layer takes the part with it (Parts, Part, Details, edit pages, Mix).',
    () => printed(6) + ': the edit pages of the framed layer (Sound, Range, Velocity, Sends), one page on at each press.',
    () => printed(7) + ': the System screen: Bluetooth, button light, scene channel, master control channel.',
    () => printed(8) + ': the Parts screen; pressed again, the selected part in Details. Held, then ' + printed(12) + ': all notes off. Cancels naming a scene.',
    () => printed(9) + ': save the setup as a scene; it asks for a name first, and pressing ' + printed(9) + ' again stores it.',
    () => printed(10) + ': LEARN: play the lowest and the highest key to set a layer\'s key range (Part and Range screens).',
    () => printed(11) + ': the Part screen: the layers of the selected part.',
    () => printed(12) + ': the Scenes screen. Held, then ' + printed(0) + ' or ' + printed(1) + ': previous or next scene on any screen.',
    () => printed(13) + ': the MIDI in screen: the latest messages, with their port and channel.',
    () => printed(14) + ': one step back: the part (Parts, Mix, FX), the layer (Part, Details, edit pages), the stored scene (Scenes), the value of the row (System), the cursor (naming a scene).',
    () => printed(15) + ': one step forward: the part (Parts, Mix, FX), the layer (Part, Details, edit pages), the stored scene (Scenes), the value of the row (System), the cursor (naming a scene).',
  ];
  /** The short caption under physical button id. */
  function smkButtonCaption(id) {
    const v = DxCore.SMK37.BUTTON_MAP[id];
    if (id === 0) return 'knob bank';
    if (id === 1) return 'fader bank';
    return v === 255 ? 'unused' : SMK_SHORT[v];
  }
  /** What DX747 does with physical button id, in full (the tooltip). */
  function smkButtonDetail(id) {
    const S = DxCore.SMK37, v = S.BUTTON_MAP[id];
    if (id === 0) return S.BUTTONS[0] + ': on Mix, K1 to K4 pan parts 1 to 4, or parts 5 to 8. The lamp is lit for parts 5 to 8.';
    if (id === 1) return S.BUTTONS[1] + ': where the faders are part levels, FADER1 to FADER4 are parts 1 to 4, or parts 5 to 8. The lamp is lit for parts 5 to 8.';
    if (v === 255) return S.BUTTONS[id] + ': DX747 does not use it yet.';
    return SMK_LONG[v]();
  }
  class Smk37Panel extends SimPanel {
    build() {
      const S = DxCore.SMK37, K = S.KIND, core = this.core;
      if (!core.smk37) throw new Error('This core is not the SMK-37 build');
      const root = this.container; root.textContent = ''; root.classList.add('simpanel', 'smkpanel', 'captions');
      const slider = (cls, label, caption, detail, min, max, value, kind, id, spring) => {
        const box = el('label', 'smk-slider ' + cls), input = el('input'); input.type = 'range'; input.min = min; input.max = max; input.value = value;
        input.setAttribute('aria-label', label); input.title = label + ': ' + detail;
        input.oninput = () => { core.smk(kind, id, +input.value); this.startAudio(); };
        if (spring != null) for (const type of ['pointerup', 'pointercancel', 'blur']) input.addEventListener(type, () => { input.value = spring; core.smk(kind, id, spring); });
        box.append(el('b', null, label), input, el('small', 'smk-cap', caption)); return box;
      };
      const wheels = el('div', 'smk-wheels');
      wheels.append(slider('wheel', 'PITCH', 'pitch bend', 'pitch bend of the selected part (springs back to the middle)', 0, 127, 64, K.FADER, 4, 64),
        slider('wheel', 'MOD', 'mod (CC1)', 'modulation (CC1, vibrato) of the selected part', 0, 127, 0, K.FADER, 5));
      const screen = el('div', 'sim-screen'); const canvas = el('canvas'); canvas.width = canvas.height = 240;
      this.ctx2d = canvas.getContext('2d'); this.image = this.ctx2d.createImageData(240, 240); screen.append(canvas);
      const button = id => {
        const b = el('button', 'sim-button smk-b'); b.append(el('span', null, S.BUTTONS[id]), el('small', 'smk-cap', smkButtonCaption(id)));
        b.title = S.BUTTONS[id] + ' → ' + smkButtonDetail(id); b.dataset.id = id;
        this.holdButton(b, down => { core.smk(K.BUTTON, id, down ? 1 : 0); if (down && id < 2) this.drawLamps(); });
        return b;
      };
      this.buttons = S.BUTTONS.map((_, id) => button(id));
      const left = el('div', 'smk-bl'); for (let id = 0; id < 6; id++) left.append(this.buttons[id]);
      const buttons = el('div', 'smk-br'); for (let id = 6; id < 18; id++) buttons.append(this.buttons[id]);
      // eight knobs, as on the unit: a round knob that turns under the mouse (drag up or down, scroll, click), with - and + under it
      const KNOB_CAPTION = ['per screen', 'per screen', 'per screen', 'per screen', 'part', 'layer', 'voice', 'master level'];
      const KNOB_K = 'one of the four knobs that change with the screen. On Mix: the pan of a part, parts 1 to 4 or 5 to 8 (' + S.BUTTONS[0] + ')';
      const KNOB_DETAIL = [KNOB_K, KNOB_K, KNOB_K, KNOB_K, 'the part (Scenes: the scene cursor, System: the row)', 'the layer inside the part (Mix: the channel of the part)', 'the voice (1 to 128) of the framed layer (System: the value)', 'the master level of everything you hear'];
      const knob = i => {
        const name = S.ENCODERS[i];
        const box = el('div', 'smk-knobbox'), dial = el('button', 'smk-knob'), minus = el('button', 'sim-step', '−'), plus = el('button', 'sim-step', '+');
        dial.append(el('i')); dial.setAttribute('aria-label', name + ': drag, scroll or click'); box.title = name + ' → ' + KNOB_DETAIL[i];
        minus.setAttribute('aria-label', name + ' down'); plus.setAttribute('aria-label', name + ' up');
        let angle = 0, lastY = 0, moved = false;
        const turn = d => { angle += d * 18; dial.style.setProperty('--angle', angle + 'deg'); core.smk(K.ENCODER, i, d); };
        minus.onclick = () => turn(-1); plus.onclick = () => turn(1);
        box.addEventListener('wheel', e => {          // as M-VAVE's own page: leave horizontal swipes and pinch-zoom to the browser
          if (!e.deltaY || e.ctrlKey || e.metaKey || Math.abs(e.deltaX) > Math.abs(e.deltaY)) return;
          e.preventDefault(); turn(e.deltaY < 0 ? 1 : -1);
        }, { passive: false });
        dial.addEventListener('pointerdown', e => { if (e.button !== 0) return; e.preventDefault(); lastY = e.clientY; moved = false; dial.setPointerCapture(e.pointerId); this.startAudio(); });
        dial.addEventListener('pointermove', e => {
          if (!dial.hasPointerCapture(e.pointerId)) return;
          const d = Math.trunc((lastY - e.clientY) / 6); if (d) { moved = true; lastY -= d * 6; turn(Math.max(-20, Math.min(20, d))); }
        });
        dial.addEventListener('pointerup', e => { if (dial.hasPointerCapture(e.pointerId) && !moved) turn(e.shiftKey ? -1 : 1); });
        dial.addEventListener('keydown', e => {
          if (e.key === 'ArrowUp' || e.key === 'ArrowRight') { e.preventDefault(); turn(1); } else if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') { e.preventDefault(); turn(-1); }
        });
        const pm = el('div', 'sim-pm'); pm.append(minus, plus);
        box.append(el('b', 'smk-k', name), dial, pm, el('small', 'smk-cap', KNOB_CAPTION[i]));
        return box;
      };
      const knobs = el('div', 'smk-knobs'); for (let i = 0; i < S.ENCODERS.length; i++) knobs.append(knob(i));
      const pads = el('div', 'smk-pads'); this.pads = [];
      for (let i = 0; i < S.PADS; i++) {
        const b = el('button', 'smk-pad', String(i + 1).padStart(2, '0')); b.title = 'Pad ' + (i + 1) + ': ' + (i < 8
          ? 'recalls scene ' + (i + 1) + ' of the bank on screen. On Mix: mutes part ' + (i + 1) + '. On Part, Details and the edit pages: picks layer ' + (i + 1) + ' of the selected part.'
          : 'recalls scene ' + (i + 1) + ' of the bank on screen. On Mix: solos part ' + (i - 7) + '. On Part, Details and the edit pages: picks part ' + (i - 7) + '.');
        this.hold(b, down => core.smk(K.PAD, i, down ? 1 : 0)); this.pads.push(b); pads.append(b);
      }
      const icons = el('div', 'smk-icons');            // printed under the lower row of pads; DX747 does not use them
      for (const icon of S.PAD_ICONS) { const s = el('span', null, icon); s.title = 'Printed on the unit; DX747 does not use it'; icons.append(s); }
      const faders = el('div', 'smk-faders');
      const FADER_DETAIL = [
        'Parts: the master filter cutoff. Master filter: its type. Details and FX: the reverb decay. Everywhere else: the level of part 1 (part 5 after FADER BANK).',
        'Parts: the master filter resonance. Master filter: its cutoff. Details and FX: the delay time. Everywhere else: the level of part 2 (part 6 after FADER BANK).',
        'Parts: the master filter type. Master filter: its resonance. Details and FX: the delay feedback. Everywhere else: the level of part 3 (part 7 after FADER BANK).',
        'Parts and Master filter: not used. Details and FX: the effects return. Everywhere else: the level of part 4 (part 8 after FADER BANK).',
      ].map(t => t + ' It acts as soon as you move it, wherever it was left (no pickup).');
      for (let i = 0; i < 4; i++) faders.append(slider('fader', 'FADER' + (i + 1), 'by screen', FADER_DETAIL[i], 0, 127, 127, K.FADER, i));
      for (let i = 0; i < 4; i++) core.smk(K.FADER, i, 127);          // the first readings, as the scanner reports them at power-on: a part at 127 is picked up at once
      const top = el('div', 'smk-top');
      const mid = el('div', 'smk-mid'); mid.append(knobs, pads, icons);
      const col = el('div', 'smk-left'); col.append(screen, left);
      const right = el('div', 'smk-right'); right.append(faders, buttons);
      top.append(wheels, col, mid, right);
      const keys = el('div', 'smk-keys'); this.keys = [];
      const whites = Array.from({ length: S.KEYS }, (_, k) => k).filter(k => ![1, 3, 6, 8, 10].includes(k % 12));
      for (let k = 0; k < S.KEYS; k++) {
        const white = whites.indexOf(k) >= 0, b = el('button', 'sim-key ' + (white ? 'white' : 'black'));
        const center = white ? whites.indexOf(k) + 0.5 : whites.filter(w => w < k).length;
        b.style.left = (center * 100 / whites.length) + '%'; b.style.transform = 'translateX(-50%)';
        b.title = 'Key ' + (k + 1) + ': MIDI note ' + (S.FIRST_NOTE + k) + ' at octave 0, 12 more for each OCTAVE+ step. The lower on the key you press, the harder it is struck.';
        this.holdKey(b, k); this.keys.push(b); keys.append(b);
      }
      const foot = el('div', 'smk-foot');
      const pedal = el('button', null, 'Sustain pedal (hold)'); this.hold(pedal, down => core.smk(K.PEDAL, 0, down ? 127 : 0));
      const caps = el('label', 'check'), box = el('input'); box.type = 'checkbox'; box.checked = true;
      box.onchange = () => root.classList.toggle('captions', box.checked);
      caps.append(box, ' Show what each button, knob and fader does in DX747');
      foot.append(pedal, caps);
      // the panel has a natural width (the unit's proportions); in a narrower window it is scaled to fit, so that no control is cut off.
      // The scaling changes the panel's height, so it is done a frame later, and a growth of under 2% is ignored: with a scroll bar that
      // takes room, the height decides whether there is a bar, and the bar decides the width.
      const fit = el('div', 'smk-fit'); fit.append(top, keys, foot); root.append(fit);
      let zoom = 1, pending = 0;
      const refit = () => {
        pending = 0;
        const want = Math.min(1, Math.max(0.3, (root.clientWidth - 36) / 1240));
        if (want < zoom || want > zoom * 1.02) { zoom = want; fit.style.zoom = String(zoom); }
      };
      const schedule = () => { if (!pending) pending = requestAnimationFrame(refit); };
      refit();
      if (typeof ResizeObserver === 'function') new ResizeObserver(schedule).observe(root); else window.addEventListener('resize', schedule);
    }
    /** As SimPanel.hold, for the main mouse button only (a right click must not press anything). */
    hold(node, fn) {
      let down = false;
      node.addEventListener('pointerdown', e => { if (e.button !== 0) return; e.preventDefault(); if (!down) { down = true; node.classList.add('held'); fn(true); this.startAudio(); } });
      const lift = () => { if (down) { down = false; node.classList.remove('held'); fn(false); } };
      node.addEventListener('pointerup', lift); node.addEventListener('pointerleave', lift); node.addEventListener('pointercancel', lift);
    }
    /** A button the firmware reads by polling (its control task looks at the panel every few milliseconds): a press that is over before the
     *  next look would be lost, as it would on the unit, so the release waits until the button has been down for MIN_DOWN ms. */
    holdButton(node, fn) {
      const MIN_DOWN = 50; let down = false, at = 0, release = null;
      const flush = () => { if (release) { clearTimeout(release); release = null; fn(false); node.classList.remove('held'); } };
      node.addEventListener('pointerdown', e => {
        if (e.button !== 0) return; e.preventDefault(); flush(); if (down) return;
        down = true; at = performance.now(); node.classList.add('held'); fn(true); this.startAudio();
      });
      const lift = () => {
        if (!down) return; down = false;
        const wait = Math.max(0, MIN_DOWN - (performance.now() - at));
        release = setTimeout(() => { release = null; fn(false); node.classList.remove('held'); }, wait);
      };
      node.addEventListener('pointerup', lift); node.addEventListener('pointerleave', lift); node.addEventListener('pointercancel', lift);
    }
    /** A key struck at a velocity from where it is pressed: the top of the key is soft, the bottom hard. */
    holdKey(node, key) {
      const K = DxCore.SMK37.KIND; let down = false;
      node.addEventListener('pointerdown', e => {
        if (e.button !== 0) return; e.preventDefault(); if (down) return; down = true; node.classList.add('held');
        const r = node.getBoundingClientRect(), y = r.height ? Math.min(1, Math.max(0, (e.clientY - r.top) / r.height)) : 0.7;
        this.core.smk(K.KEY, key, Math.round(24 + y * 103)); this.startAudio();
      });
      const lift = () => { if (down) { down = false; node.classList.remove('held'); this.core.smk(K.KEY, key, 0); } };
      node.addEventListener('pointerup', lift); node.addEventListener('pointerleave', lift); node.addEventListener('pointercancel', lift);
    }
    tick() { super.tick(); this.drawPads(); }
    drawPads() {
      this.pads.forEach((b, i) => {
        const rgb = this.core.padColor(i); if (b.dataset.rgb === String(rgb)) return;
        b.dataset.rgb = rgb; b.style.setProperty('--pad', '#' + rgb.toString(16).padStart(6, '0'));
      });
    }
    /** The lamps of the 14 virtual buttons, shown on the physical buttons that carry them (panel.c: ENV and LFO share one), and the
     *  FADER BANK lamp, lit for parts 5 to 8. */
    drawLamps() {
      const lamps = this.core.smkLamps();
      this.buttons.forEach((b, p) => {
        b.style.setProperty('--lamp', lamps[p] / 8); b.classList.toggle('lit', lamps[p] > 0);
      });
    }
  }
  Smk37Panel.caption = smkButtonCaption; Smk37Panel.detail = smkButtonDetail;
  globalThis.SimPanel = SimPanel;
  globalThis.Smk37Panel = Smk37Panel;
})();
