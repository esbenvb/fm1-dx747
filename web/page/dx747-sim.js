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
   * The same core, clock, sound and flash, with the SMK-37's panel around it: 18 buttons, 8 encoders, 4 faders, 2 wheels, a pedal,
   * 16 pads with RGB lights and 37 keys with velocity. The layout (counts, labels, which buttons sit left and right of the screen)
   * follows M-VAVE's own simulator in the SMK-37 SDK; the code is this project's. The core takes the board's events: smk(kind, id, value).
   * Every control carries a caption with what DX747 does with it (from the tables in DxCore.SMK37, so a label that disagrees with its
   * function, like MIX opening System, shows). */
  const SCREEN_OF = { SEL: 'Mix', GLO: 'System', HOME: 'Parts, then Details', EDIT: 'Edit pages', SEQ: 'Part', REC: 'MIDI in', PLAY: 'Scenes', FX: 'FX, then Master filter',
    SAVE: 'save a scene', ARP: 'LEARN', ENV: 'add a layer', LFO: 'remove a layer', 'OCT-': 'octave, page back', 'OCT+': 'octave, page on' };
  /** What DX747 does with physical button id (text for the caption and the tooltip). */
  function smkButtonCaption(id) {
    const S = DxCore.SMK37, v = S.BUTTON_MAP[id];
    if (id === S.SHIFT) return 'with ENV: LFO (click to hold)';
    if (id === 1) return 'faders: parts 1-4 / 5-8';
    if (id === S.ENV) return 'ENV: add a layer; with SHIFT: LFO, remove one';
    if (v === 255) return 'unused';
    const name = S.VIRTUAL[v];
    return name + ': ' + SCREEN_OF[name];
  }
  class Smk37Panel extends SimPanel {
    build() {
      const S = DxCore.SMK37, K = S.KIND, core = this.core;
      if (!core.smk37) throw new Error('This core is not the SMK-37 build');
      const root = this.container; root.textContent = ''; root.classList.add('simpanel', 'smkpanel', 'captions');
      const slider = (cls, label, caption, min, max, value, kind, id, spring) => {
        const box = el('label', 'smk-slider ' + cls), input = el('input'); input.type = 'range'; input.min = min; input.max = max; input.value = value;
        input.setAttribute('aria-label', label); input.title = label + ': ' + caption;
        input.oninput = () => { core.smk(kind, id, +input.value); this.startAudio(); };
        if (spring != null) for (const type of ['pointerup', 'pointercancel', 'blur']) input.addEventListener(type, () => { input.value = spring; core.smk(kind, id, spring); });
        box.append(input, el('b', null, label), el('small', 'smk-cap', caption)); return box;
      };
      const wheels = el('div', 'smk-wheels');
      wheels.append(slider('wheel', 'PITCH', 'pitch bend', 0, 127, 64, K.FADER, 4, 64), slider('wheel', 'MOD', 'CC1', 0, 127, 0, K.FADER, 5));
      const screen = el('div', 'sim-screen'); const canvas = el('canvas'); canvas.width = canvas.height = 240;
      this.ctx2d = canvas.getContext('2d'); this.image = this.ctx2d.createImageData(240, 240); screen.append(canvas);
      const button = id => {
        const b = el('button', 'sim-button smk-b'); b.append(el('span', null, S.BUTTONS[id]), el('small', 'smk-cap', smkButtonCaption(id)));
        b.title = S.BUTTONS[id] + ' → ' + smkButtonCaption(id); b.dataset.id = id;
        if (id === S.SHIFT) {                        // a mouse cannot hold SHIFT while it presses ENV: this one latches
          let on = false;
          b.addEventListener('pointerdown', e => { e.preventDefault(); on = !on; b.classList.toggle('held', on); core.smk(K.BUTTON, id, on ? 1 : 0); this.startAudio(); });
        } else this.holdButton(b, down => core.smk(K.BUTTON, id, down ? 1 : 0));
        return b;
      };
      this.buttons = S.BUTTONS.map((_, id) => button(id));
      const left = el('div', 'smk-bl'); for (let id = 0; id < 6; id++) left.append(this.buttons[id]);
      const right = el('div', 'smk-br'); for (let id = 6; id < 18; id++) right.append(this.buttons[id]);
      const enc = el('div', 'smk-enc');
      S.ENCODERS.forEach((name, i) => {
        const box = el('div', 'sim-encoder'), minus = el('button', 'sim-step', '−'), plus = el('button', 'sim-step', '+');
        const caption = i < 7 ? S.VIRTUAL_ENCODERS[S.ENCODER_MAP[i]] : 'master level';
        minus.setAttribute('aria-label', name + ' down'); plus.setAttribute('aria-label', name + ' up'); box.title = name + ' → ' + caption;
        minus.onclick = () => core.smk(K.ENCODER, i, -1); plus.onclick = () => core.smk(K.ENCODER, i, 1);
        box.addEventListener('wheel', e => { e.preventDefault(); core.smk(K.ENCODER, i, e.deltaY < 0 ? 1 : -1); }, { passive: false });
        box.append(el('span', null, name), el('div', 'sim-pm'), el('small', 'smk-cap', caption)); box.children[1].append(minus, plus); enc.append(box);
      });
      const pads = el('div', 'smk-pads'); this.pads = [];
      for (let i = 0; i < S.PADS; i++) {
        const b = el('button', 'smk-pad', String(i + 1).padStart(2, '0')); b.title = 'Pad ' + (i + 1);
        this.hold(b, down => core.smk(K.PAD, i, down ? 1 : 0)); this.pads.push(b); pads.append(b);
      }
      const faders = el('div', 'smk-faders');
      for (let i = 0; i < 4; i++) faders.append(slider('fader', 'F' + (i + 1), 'part level', 0, 127, 127, K.FADER, i));
      for (let i = 0; i < 4; i++) core.smk(K.FADER, i, 127);          // the first readings, as the scanner reports them at power-on: a part at 127 is picked up at once
      const top = el('div', 'smk-top');
      const mid = el('div', 'smk-mid'); mid.append(enc, pads, right);
      const col = el('div', 'smk-left'); col.append(screen, left);
      top.append(wheels, col, mid, faders);
      const keys = el('div', 'smk-keys'); this.keys = [];
      const whites = Array.from({ length: S.KEYS }, (_, k) => k).filter(k => ![1, 3, 6, 8, 10].includes(k % 12));
      for (let k = 0; k < S.KEYS; k++) {
        const white = whites.indexOf(k) >= 0, b = el('button', 'sim-key ' + (white ? 'white' : 'black'));
        const center = white ? whites.indexOf(k) + 0.5 : whites.filter(w => w < k).length;
        b.style.left = (center * 100 / whites.length) + '%'; b.style.transform = 'translateX(-50%)';
        b.title = 'MIDI note ' + (S.FIRST_NOTE + k) + ' (the lower on the key, the harder)';
        if (k % 12 === 0) b.append(el('span', 'smk-c', 'C' + (3 + Math.floor(k / 12))));
        this.holdKey(b, k); this.keys.push(b); keys.append(b);
      }
      const foot = el('div', 'smk-foot');
      const pedal = el('button', null, 'Sustain pedal (hold)'); this.hold(pedal, down => core.smk(K.PEDAL, 0, down ? 127 : 0));
      const caps = el('label', 'check'), box = el('input'); box.type = 'checkbox'; box.checked = true;
      box.onchange = () => root.classList.toggle('captions', box.checked);
      caps.append(box, ' Show what each control does in DX747');
      foot.append(pedal, caps);
      root.append(top, keys, foot);
    }
    /** A button the firmware reads by polling (its control task looks at the panel every few milliseconds): a press that is over before the
     *  next look would be lost, as it would on the unit, so the release waits until the button has been down for MIN_DOWN ms. */
    holdButton(node, fn) {
      const MIN_DOWN = 50; let down = false, at = 0, release = null;
      const flush = () => { if (release) { clearTimeout(release); release = null; fn(false); node.classList.remove('held'); } };
      node.addEventListener('pointerdown', e => {
        e.preventDefault(); flush(); if (down) return;
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
        e.preventDefault(); if (down) return; down = true; node.classList.add('held');
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
    /** The lamps of the 14 virtual buttons, shown on the physical buttons that carry them (panel.c: ENV and LFO share one). */
    drawLamps() {
      const l = this.core.leds(), S = DxCore.SMK37;
      this.buttons.forEach((b, p) => {
        let level = 0; S.LAMP.forEach((physical, v) => { if (physical === p && l[v] > level) level = l[v]; });
        b.style.setProperty('--lamp', level / 8); b.classList.toggle('lit', level > 0);
      });
    }
  }
  globalThis.SimPanel = SimPanel;
  globalThis.Smk37Panel = Smk37Panel;
})();
