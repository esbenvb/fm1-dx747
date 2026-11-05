/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The test keyboard: an on-screen piano that is also played from the computer keyboard (two rows, like a tracker), with a
 * choice of MIDI channel and output, velocity, octave, sustain, panic, MIDI-in routing from a hardware keyboard, and a monitor.
 * It sends plain MIDI to whichever output is chosen (the simulator in the page, the unit, or any Web MIDI port). */
(() => {
  'use strict';
  const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };
  // computer key -> semitones above the keyboard's base note (the lower row starts at the base C, the upper row an octave higher)
  const MAP = {
    z: 0, s: 1, x: 2, d: 3, c: 4, v: 5, g: 6, b: 7, h: 8, n: 9, j: 10, m: 11, ',': 12, l: 13, '.': 14, ';': 15, '/': 16,
    q: 12, 2: 13, w: 14, 3: 15, e: 16, r: 17, 5: 18, t: 19, 6: 20, y: 21, 7: 22, u: 23, i: 24, 9: 25, o: 26, 0: 27, p: 28,
  };
  const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const noteName = n => NAMES[n % 12] + (Math.floor(n / 12) - 1);
  const BLACK = [1, 3, 6, 8, 10];

  class KeyboardTab {
    /** host: the container; api: { outputs(): [{name, send(bytes)}], inputs(): [{name, port}], selectedChannel(): 1..16 or 0, active(): bool, enableMidi(): Promise } */
    constructor(host, api) {
      this.host = host; this.api = api; this.octave = 3; this.velocity = 100; this.held = new Map(); this.sustain = false; this.log = [];
      this.channel = 'follow'; this.output = 0; this.routed = new Map();
      this.build();
      window.addEventListener('keydown', e => this.key(e, true));
      window.addEventListener('keyup', e => this.key(e, false));
      window.addEventListener('blur', () => this.releaseAll());
    }
    get base() { return (this.octave + 1) * 12; }
    channelNumber() { return this.channel === 'follow' ? (this.api.selectedChannel() || 1) : +this.channel; }
    // ---- sending ---------------------------------------------------------------------------------------
    send(bytes) {
      const outs = this.api.outputs(), out = outs[Math.min(this.output, outs.length - 1)];
      if (!out) { this.note('No output: start the simulator or connect the unit'); return false; }
      try { out.send(bytes); } catch (error) { this.note('Send failed: ' + error.message); return false; }
      this.monitor(out.name, bytes);
      return true;
    }
    noteOn(note, channel = this.channelNumber(), velocity = this.velocity) {
      if (note < 0 || note > 127) return;
      const key = note; if (this.held.has(key)) this.noteOff(note);
      if (this.send([0x90 | (channel - 1), note, velocity])) this.held.set(key, channel);
      this.paint();
    }
    noteOff(note) {
      const ch = this.held.get(note); if (ch == null) return;
      this.held.delete(note); this.send([0x80 | (ch - 1), note, 0]); this.paint();
    }
    releaseAll() { for (const n of [...this.held.keys()]) this.noteOff(n); }
    panic() {
      this.held.clear(); this.send([0xff]);
      for (let ch = 0; ch < 16; ch++) { this.send([0xb0 | ch, 123, 0]); this.send([0xb0 | ch, 120, 0]); this.send([0xb0 | ch, 64, 0]); }
      this.sustainBox.checked = this.sustain = false; this.paint();
    }
    setSustain(on) { this.sustain = on; this.send([0xb0 | (this.channelNumber() - 1), 64, on ? 127 : 0]); }
    // ---- the computer keyboard ---------------------------------------------------------------------------
    key(e, down) {
      if (!this.api.active() || e.ctrlKey || e.metaKey || e.altKey) return;
      const t = e.target; if (t && /^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName) && t.type !== 'range' && t.type !== 'checkbox') return;
      if (down && (e.key === 'ArrowUp' || e.key === 'ArrowDown')) { e.preventDefault(); this.setOctave(this.octave + (e.key === 'ArrowUp' ? 1 : -1)); return; }
      if (down && e.key === ' ') { e.preventDefault(); this.sustainBox.checked = !this.sustainBox.checked; this.setSustain(this.sustainBox.checked); return; }
      const semi = MAP[e.key.toLowerCase()]; if (semi == null) return;
      e.preventDefault();
      const note = this.base + semi;
      if (down) { if (!e.repeat && !this.held.has(note)) this.noteOn(note); } else this.noteOff(note);
    }
    setOctave(n) { this.releaseAll(); this.octave = Math.max(0, Math.min(8, n)); this.octLabel.textContent = 'C' + this.octave; this.drawKeys(); }
    // ---- the page ----------------------------------------------------------------------------------------
    build() {
      const root = this.host; root.textContent = '';
      const bar = el('div', 'kb-bar');
      const chan = el('select'); chan.append(new Option('Selected part\'s channel', 'follow'));
      for (let c = 1; c <= 16; c++) chan.append(new Option('Channel ' + c, c));
      chan.onchange = () => { this.releaseAll(); this.channel = chan.value; };
      this.outSelect = el('select'); this.outSelect.onchange = () => { this.releaseAll(); this.output = +this.outSelect.value; };
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
      const hint = el('p', 'hint', 'Play from the computer keyboard: the lower row Z S X D C V G B H N J M is the octave shown, the row Q 2 W 3 E R 5 T 6 Y 7 U the octave above. Up and Down arrows change the octave, Space is the sustain pedal. Click the keys to play with the mouse.');
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
        if (note % 12 === 0) k.append(el('i', null, noteName(note)));
        if (!black) { k.style.left = (w * 100 / whites) + '%'; k.style.width = (100 / whites) + '%'; w++; } else { k.style.left = (w * 100 / whites - 0.9) + '%'; }
        let on = false;
        k.addEventListener('pointerdown', e => { e.preventDefault(); on = true; this.noteOn(note); });
        const lift = () => { if (on) { on = false; this.noteOff(note); } };
        k.addEventListener('pointerup', lift); k.addEventListener('pointerleave', lift);
        this.cells.set(note, k); keys.append(k);
      }
      [...this.cells.entries()].filter(([n]) => BLACK.includes(n % 12)).forEach(([, k]) => keys.append(k));
      this.paint();
    }
    paint() { if (this.cells) for (const [n, k] of this.cells) k.classList.toggle('on', this.held.has(n)); }
    refreshOutputs() {
      const outs = this.api.outputs(); this.outSelect.textContent = '';
      outs.forEach((o, i) => this.outSelect.append(new Option(o.name, i)));
      if (!outs.length) this.outSelect.append(new Option('(none yet)', 0));
      this.output = Math.min(this.output, Math.max(0, outs.length - 1)); this.outSelect.value = this.output;
    }
    refreshInputs() {
      const box = this.inputBox; box.textContent = '';
      const inputs = this.api.inputs();
      if (!inputs.length) {
        const b = el('button', null, 'Use a MIDI keyboard or another MIDI port…'); b.onclick = async () => { try { await this.api.enableMidi(); } catch (error) { this.note(error.message); } };
        box.append(el('h3', null, 'MIDI keyboards'), b); return;
      }
      box.append(el('h3', null, 'MIDI keyboards: play them through the output chosen above'));
      for (const inp of inputs) {
        const lab = el('label', 'check'); const cb = el('input'); cb.type = 'checkbox'; cb.checked = this.routed.has(inp.port.id);
        cb.onchange = () => this.route(inp.port, cb.checked); lab.append(cb, ' ' + inp.name); box.append(lab);
      }
    }
    route(port, on) {
      if (!on) { const fn = this.routed.get(port.id); if (fn) port.removeEventListener('midimessage', fn); this.routed.delete(port.id); return; }
      Promise.resolve(port.open && port.open()).catch(() => {});
      const fn = e => {
        const d = e.data; if (!d || d.length < 1 || d[0] >= 0xf0) return;
        const bytes = Array.from(d);
        if (this.channel !== 'follow') bytes[0] = (bytes[0] & 0xf0) | (this.channelNumber() - 1);
        this.send(bytes);
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
