/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The DX747 simulator core in the page: the firmware compiled to WebAssembly (web/engine/Simulator.c), driven from the main
 * thread. It owns the panel, the sound engines and the flash; this wrapper moves the clock forward, carries the panel
 * controls and MIDI in, and reads the screen and the audio blocks out. No DOM here, so Node can run it too (the page
 * tests do). Time only moves when advance() or render() is called. */
(() => {
  'use strict';
  const FLASH = 536576, PIXELS = 57600, BUTTONS = 14, KEYS = 27, ENCODERS = 7;
  const SOURCE = { DIN: 0, USB: 1, BLE: 2 };

  class DxCore {
    /** wasm: the module bytes; flash: an optional saved 536,576-byte flash image (omit for a factory-fresh unit). */
    constructor(wasm, flash) {
      this.c = new WebAssembly.Instance(new WebAssembly.Module(wasm), {}).exports;
      const c = this.c;
      c._initialize();
      const saved = flash && flash.length === FLASH;
      if (saved) new Uint8Array(c.memory.buffer, c.sim_flash(), FLASH).set(flash);
      c.sim_boot(saved ? 0 : 1);
      c.sim_midi_capture(1);
      this.blockFrames = c.sim_block_frames();      // 128 stereo frames at 44.1 kHz per block
      this.sampleRate = 44100;
      this.blocks = 0;
    }
    /** Erase the scene store and its staging area, as a unit whose store has been set up from the panel. */
    prepareStore() {
      new Uint8Array(this.c.memory.buffer, this.c.sim_flash() + 65536, 335872 - 65536).fill(255);
    }
    // ---- time and audio -----------------------------------------------------------------------
    /** Render one block; returns the interleaved stereo float samples (a view, valid until the next call). */
    render() {
      const at = this.c.sim_render();
      this.blocks++;
      return new Float32Array(this.c.memory.buffer, at, this.blockFrames * 2);
    }
    /** Advance the unit by about ms milliseconds without keeping the audio. */
    advance(ms) {
      const n = Math.max(1, Math.round(ms * this.sampleRate / 1000 / this.blockFrames));
      for (let i = 0; i < n; i++) this.c.sim_render();
      this.blocks += n;
    }
    get elapsedMs() { return this.blocks * this.blockFrames * 1000 / this.sampleRate; }
    // ---- the panel ----------------------------------------------------------------------------
    button(index, down) { if (index >= 0 && index < BUTTONS) this.c.sim_control(0, index, down ? 1 : 0); }
    key(index, down) { if (index >= 0 && index < KEYS) this.c.sim_control(1, index, down ? 1 : 0); }
    encoder(index, detents) { if (index >= 0 && index < ENCODERS) this.c.sim_control(2, index, Math.max(-127, Math.min(127, detents | 0))); }
    knob(value) { this.c.sim_control(3, 0, Math.max(0, Math.min(1023, value | 0))); }
    release() { this.c.sim_release(); }
    // ---- MIDI ---------------------------------------------------------------------------------
    /** Deliver a MIDI message (a channel message of up to 3 bytes, or one SysEx frame) as if it came in on a port. Returns true if refused. */
    midiIn(bytes, source = SOURCE.USB) {
      if (!bytes.length || bytes.length > 1024) return true;
      new Uint8Array(this.c.memory.buffer, this.c.sim_file(), bytes.length).set(bytes);
      return this.c.sim_midi_from(source, bytes.length) !== 0;
    }
    /** Everything the unit has sent since the last call: [{port, bytes}]. */
    midiOut() {
      const out = [];
      for (let v; (v = this.c.sim_midi_output()) !== 0;) {
        const n = v & 255;
        out.push({ port: v >> 8, bytes: new Uint8Array(this.c.memory.buffer, this.c.sim_file(), n).slice() });
      }
      return out;
    }
    // ---- what the panel shows -----------------------------------------------------------------
    get frameRevision() { return this.c.sim_frame_revision(); }
    /** The 240 x 240 screen as RGB565 (a live view of the core's memory: copy it to keep it). */
    pixels() { return new Uint16Array(this.c.memory.buffer, this.c.sim_pixels(), PIXELS); }
    /** The 41 lamps, brightness 0..8: 14 buttons, then the 27 keys. */
    leds() { return new Uint8Array(this.c.memory.buffer, this.c.sim_state(), 225).slice(168, 209); }
    /** The DX747 screen id (1 parts, 2 part, 3 sound, ..., 8 mix, 14 details). */
    screen() { return new Uint8Array(this.c.memory.buffer, this.c.sim_state(), 225)[1]; }
    flash() { return new Uint8Array(this.c.memory.buffer, this.c.sim_flash(), FLASH); }
    get flashRevision() { return this.c.sim_flash_revision(); }
  }
  DxCore.SOURCE = SOURCE;
  globalThis.DxCore = DxCore;
})();
