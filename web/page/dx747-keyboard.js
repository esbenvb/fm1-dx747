/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The test keyboard: an on-screen piano that is also played from the computer keyboard (two rows, like a tracker, by key position
 * so it works on any layout), with a choice of MIDI channel and output, velocity, octave, sustain, panic, routing of a hardware
 * keyboard, and a monitor. It sends plain MIDI to whichever output is chosen (the simulator in the page, the unit, or any Web MIDI port). */
(() => {
  'use strict';
  const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };
  // physical key (KeyboardEvent.code) -> semitones above the base C: the lower row starts at the base C, the upper row an octave higher
  const MAP = {
    KeyZ: 0, KeyS: 1, KeyX: 2, KeyD: 3, KeyC: 4, KeyV: 5, KeyG: 6, KeyB: 7, KeyH: 8, KeyN: 9, KeyJ: 10, KeyM: 11, Comma: 12, KeyL: 13, Period: 14, Semicolon: 15, Slash: 16,
    KeyQ: 12, Digit2: 13, KeyW: 14, Digit3: 15, KeyE: 16, KeyR: 17, Digit5: 18, KeyT: 19, Digit6: 20, KeyY: 21, Digit7: 22, KeyU: 23, KeyI: 24, Digit9: 25, KeyO: 26, Digit0: 27, KeyP: 28,
  };
  const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const noteName = n => NAMES[n % 12] + (Math.floor(n / 12) - 1);
  const BLACK = [1, 3, 6, 8, 10];

  class KeyboardTab {
    /** host: the container; api: { outputs(): [{name, kind, send(bytes)}], inputs(): [{name, port}], selectedChannel(): 1..16 or 0, active(): bool, enableMidi(): Promise } */
    constructor(host, api) {
      this.host = host; this.api = api; this.octave = 3; this.velocity = 100; this.log = [];
      this.held = new Map();          // note -> {channel, output}: what was sent, so the note off goes to the same place
      this.keysDown = new Map();      // physical key -> note
      this.channel = 'follow'; this.output = 0; this.routed = new Map(); this.sustain = null; this.keepOwn = false;
      this.build();
      window.addEventListener('keydown', e => this.key(e, true));
      window.addEventListener('keyup', e => this.key(e, false));
      window.addEventListener('blur', () => this.releaseAll());
    }
    get base() { return (this.octave + 1) * 12; }
    channelNumber() { return this.channel === 'follow' ? (this.api.selectedChannel() || 1) : +this.channel; }
    currentOutput() { const outs = this.api.outputs(); return outs[Math.min(this.output, outs.length - 1)]; }
    // ---- sending ---------------------------------------------------------------------------------------
    sendTo(out, bytes) {
      if (!out) { this.note('No output: start the simulator or connect the unit'); return false; }
      try { out.send(bytes); } catch (error) { this.note('Send failed: ' + error.message); return false; }
      this.monitor(out.name, bytes);
      return true;
    }
    noteOn(note, channel = this.channelNumber(), velocity = this.velocity) {
      if (note < 0 || note > 127) return;
      if (this.held.has(note)) this.noteOff(note);
      const out = this.currentOutput();
      if (this.sendTo(out, [0x90 | (channel - 1), note, velocity])) this.held.set(note, { channel, out });
      this.paint();
    }
    noteOff(note) {
      const h = this.held.get(note); if (!h) return;
      this.held.delete(note); this.sendTo(h.out, [0x80 | (h.channel - 1), note, 0]); this.paint();
    }
    releaseAll() { this.keysDown.clear(); for (const n of [...this.held.keys()]) this.noteOff(n); }
    panic() {
      this.keysDown.clear(); this.held.clear();
      const out = this.currentOutput();
      if (!out) return;
      if (out.kind === 'unit' || out.kind === 'sim') this.sendTo(out, [0xff]);      // System Reset is the unit's own all-sound-off; not for other devices
      for (let ch = 0; ch < 16; ch++) for (const cc of [123, 120, 64]) this.sendTo(out, [0xb0 | ch, cc, 0]);
      this.sustain = null; this.sustainBox.checked = false; this.paint();
    }
    setSustain(on) {
      if (on) { const out = this.currentOutput(), channel = this.channelNumber(); if (this.sendTo(out, [0xb0 | (channel - 1), 64, 127])) this.sustain = { channel, out }; }
      else if (this.sustain) { this.sendTo(this.sustain.out, [0xb0 | (this.sustain.channel - 1), 64, 0]); this.sustain = null; }
    }
    // ---- the computer keyboard ---------------------------------------------------------------------------
    key(e, down) {
      if (!down) {                                                                   // a key that started a note always ends it
        const note = this.keysDown.get(e.code); if (note == null) return;
        this.keysDown.delete(e.code);
        if (![...this.keysDown.values()].includes(note)) this.noteOff(note);
        return;
      }
      if (!this.api.active() || e.ctrlKey || e.metaKey || e.altKey) return;
      const t = e.target;
      if (t && (t.isContentEditable || t.tagName === 'TEXTAREA' || (t.tagName === 'INPUT' && !['range', 'checkbox', 'button'].includes(t.type)))) return;   // typing a name is not playing
      if (e.code === 'ArrowUp' || e.code === 'ArrowDown') { e.preventDefault(); this.setOctave(this.octave + (e.code === 'ArrowUp' ? 1 : -1)); return; }
      if (e.code === 'Space') { e.preventDefault(); if (!e.repeat) { this.sustainBox.checked = !this.sustainBox.checked; this.setSustain(this.sustainBox.checked); } return; }
      const semi = MAP[e.code]; if (semi == null) return;
      e.preventDefault();
      if (e.repeat || this.keysDown.has(e.code)) return;
      const note = this.base + semi;
      this.keysDown.set(e.code, note);
      if (!this.held.has(note)) this.noteOn(note);
    }
    setOctave(n) { this.releaseAll(); this.octave = Math.max(0, Math.min(8, n)); this.octLabel.textContent = 'C' + this.octave; this.drawKeys(); }
    // ---- the page ----------------------------------------------------------------------------------------
    build() {
      const root = this.host; root.textContent = '';
      const bar = el('div', 'kb-bar');
      const chan = el('select'); chan.append(new Option('Selected part\'s channel', 'follow'));
      for (let c = 1; c <= 16; c++) chan.append(new Option('Channel ' + c, c));
      chan.onchange = () => { this.releaseAll(); this.channel = chan.value; chan.blur(); };
      this.outSelect = el('select'); this.outSelect.onchange = () => { this.releaseAll(); this.output = +this.outSelect.value; this.outSelect.blur(); };
      const vel = el('input'); vel.type = 'range'; vel.min = 1; vel.max = 127; vel.value = this.velocity;
      const velOut = el('span', 'val', String(this.velocity)); vel.oninput = () => { this.velocity = +vel.value; velOut.textContent = vel.value; };
      this.octLabel = el('b', null, 'C' + this.octave);
      const down = el('button', null, '−'), up = el('button', null, '+'); down.onclick = () => this.setOctave(this.octave - 1); up.onclick = () => this.setOctave(this.octave + 1);
      this.sustainBox = el('input'); this.sustainBox.type = 'checkbox'; this.sustainBox.onchange = () => this.setSustain(this.sustainBox.checked);
      const sustain = el('label', null, 'Sustain '); sustain.append(this.sustainBox);
      const panic = el('button', 'warn', 'Panic'); panic.onclick = () => this.panic();
      const field = (label, node) => { const f = el('label', 'field'); f.append(el('span', null, label), node); return f; };
      bar.append(field('Output', this.outSelect), field('Channel', chan), field('Velocity', vel), velOut, field('Octave', this.octLabel), down, up, sustain, panic);
      this.keys = el('div', 'kb-piano'); this.drawKeys();
      const hint = el('p', 'hint', 'Play from the computer keyboard: the lower row Z S X D C V G B H N J M is the octave shown, the row Q 2 W 3 E R 5 T 6 Y 7 U the octave above (by key position, whatever the layout). Up and Down arrows change the octave, Space is the sustain pedal. Click the keys to play with the mouse.');
      this.inputBox = el('div', 'kb-inputs');
      const monitor = el('div', 'kb-monitor'); this.monitorEl = monitor;
      root.append(bar, this.keys, hint, this.inputBox, el('h3', null, 'Sent'), monitor);
      this.refreshOutputs(); this.refreshInputs();
    }
    drawKeys() {
      const keys = this.keys; keys.textContent = ''; this.cells = new Map();
      const count = 37, first = this.base;                                  // three octaves and a note
      let whites = 0; for (let i = 0; i < count; i++) if (!BLACK.includes((first + i) % 12)) whites++;
      let w = 0;
      for (let i = 0; i < count; i++) {
        const note = first + i, black = BLACK.includes(note % 12), k = el('button', 'pk ' + (black ? 'black' : 'white'));
        k.setAttribute('aria-label', noteName(note));
        if (note % 12 === 0) k.append(el('i', null, noteName(note)));
        if (!black) { k.style.left = (w * 100 / whites) + '%'; k.style.width = (100 / whites) + '%'; w++; } else { k.style.left = (w * 100 / whites - 0.9) + '%'; }
        let on = false;
        k.addEventListener('pointerdown', e => { e.preventDefault(); on = true; this.noteOn(note); });
        const lift = () => { if (on) { on = false; this.noteOff(note); } };
        k.addEventListener('pointerup', lift); k.addEventListener('pointerleave', lift); k.addEventListener('pointercancel', lift);
        this.cells.set(note, k); keys.append(k);
      }
      [...this.cells.entries()].filter(([n]) => BLACK.includes(n % 12)).forEach(([, k]) => keys.append(k));
      this.paint();
    }
    paint() { if (this.cells) for (const [n, k] of this.cells) k.classList.toggle('on', this.held.has(n)); }
    /** prefer: 'unit' or 'sim' picks the first output of that kind (after a connect); otherwise the choice is kept. */
    refreshOutputs(prefer) {
      const outs = this.api.outputs(), was = this.currentOutput();
      this.outSelect.textContent = '';
      outs.forEach((o, i) => this.outSelect.append(new Option(o.name, i)));
      if (!outs.length) this.outSelect.append(new Option('(none yet)', 0));
      let index = was ? outs.findIndex(o => o.name === was.name) : -1;
      if (prefer) { const p = outs.findIndex(o => o.kind === prefer); if (p >= 0) index = p; }
      if (index < 0) index = 0;
      if (index !== this.output) this.releaseAll();
      this.output = index; this.outSelect.value = index;
    }
    refreshInputs() {
      const box = this.inputBox; box.textContent = '';
      const inputs = this.api.inputs();
      if (!inputs.length) {
        const b = el('button', null, 'Use a MIDI keyboard or another MIDI port…'); b.onclick = async () => { try { await this.api.enableMidi(); } catch (error) { this.note(error.message); } };
        box.append(el('h3', null, 'MIDI keyboards'), b); return;
      }
      box.append(el('h3', null, 'MIDI keyboards: play them through the output chosen above'));
      const keep = el('label', 'check'), kcb = el('input'); kcb.type = 'checkbox'; kcb.checked = this.keepOwn; kcb.onchange = () => { this.keepOwn = kcb.checked; };
      keep.append(kcb, ' Keep the keyboard\'s own MIDI channel (otherwise it plays the channel chosen above)'); box.append(keep);
      for (const inp of inputs) {
        const lab = el('label', 'check'); const cb = el('input'); cb.type = 'checkbox'; cb.checked = this.routed.has(inp.port.id);
        cb.onchange = () => this.route(inp.port, cb.checked); lab.append(cb, ' ' + inp.name); box.append(lab);
      }
    }
    route(port, on) {
      if (!on) { const fn = this.routed.get(port.id); if (fn) port.removeEventListener('midimessage', fn); this.routed.delete(port.id); return; }
      Promise.resolve(port.open && port.open()).catch(() => {});
      const fn = e => {
        const d = e.data; if (!d || d.length < 1 || d[0] >= 0xf0) return;     // only channel messages: never SysEx or system messages
        const bytes = Array.from(d);
        if (!this.keepOwn) bytes[0] = (bytes[0] & 0xf0) | (this.channelNumber() - 1);
        this.sendTo(this.currentOutput(), bytes);
      };
      port.addEventListener('midimessage', fn); this.routed.set(port.id, fn);
    }
    monitor(where, bytes) {
      const s = Array.from(bytes, b => b.toString(16).padStart(2, '0')).join(' ');
      let label = '';
      const st = bytes[0] & 0xf0, ch = (bytes[0] & 15) + 1;
      if (st === 0x90 && bytes[2]) label = 'note on ' + noteName(bytes[1]) + ' vel ' + bytes[2] + ' ch ' + ch;
      else if (st === 0x80 || st === 0x90) label = 'note off ' + noteName(bytes[1]) + ' ch ' + ch;
      else if (st === 0xb0) label = 'cc ' + bytes[1] + ' = ' + bytes[2] + ' ch ' + ch;
      else if (st === 0xc0) label = 'program ' + (bytes[1] + 1) + ' ch ' + ch;
      else if (st === 0xe0) label = 'bend ch ' + ch;
      this.log.unshift(where + ': ' + s + (label ? '   ' + label : '')); this.log.length = Math.min(this.log.length, 12);
      this.monitorEl.textContent = this.log.join('\n');
    }
    note(text) { this.log.unshift(text); this.log.length = Math.min(this.log.length, 12); this.monitorEl.textContent = this.log.join('\n'); }
  }
  globalThis.KeyboardTab = KeyboardTab;
})();
