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
      this.access = new DxDevice.VirtualMidiAccess(this.core);
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
        if (this.audio && this.produced - this.audio.played < 8192) { const copy = block.slice(); this.audio.node.port.postMessage(copy, [copy.buffer]); this.produced += this.core.blockFrames; }
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
    async startAudio() {
      if (this.audio) { await this.audio.ctx.resume(); return true; }
      try {
        const ctx = new AudioContext({ sampleRate: this.core.sampleRate, latencyHint: 'interactive' });
        const url = URL.createObjectURL(new Blob([WORKLET], { type: 'text/javascript' }));
        await ctx.audioWorklet.addModule(url); URL.revokeObjectURL(url);
        const node = new AudioWorkletNode(ctx, 'dx-out', { outputChannelCount: [2] });
        this.audio = { ctx, node, played: 0 }; this.produced = 0;
        node.port.onmessage = e => { this.audio.played = e.data; };
        node.connect(ctx.destination); await ctx.resume();
        this.listeners.forEach(fn => fn('audio', true));
        return true;
      } catch (error) { this.listeners.forEach(fn => fn('audio-error', error)); return false; }
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
  globalThis.SimPanel = SimPanel;
})();
