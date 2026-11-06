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
    // ---- the SMK-37 flavour (a core built with DX_SMK37 exports sim_smk and sim_pad_color) ------------------------
    /** Whether the module is the SMK-37 build: it takes the board's events instead of the FM-1's panel. */
    get smk37() { return typeof this.c.sim_smk === 'function'; }
    /** One event as the board's scanner reports it: kind is one of DxCore.SMK37.KIND, id the control, value its state (see smk37_controls.h). */
    smk(kind, id, value) { this.c.sim_smk(kind, id, value | 0); }
    /** The colour (0xRRGGBB) the firmware gives pad 0..15. */
    padColor(pad) { return this.c.sim_pad_color(pad) >>> 0; }
  }
  /** Whether the compiled module (bytes) is the SMK-37 build, without starting it. */
  DxCore.hasSmk37 = wasm => WebAssembly.Module.exports(new WebAssembly.Module(wasm)).some(e => e.name === 'sim_smk');
  /* The SMK-37's panel, as the board layer and DX747's controls layer have it. The tables mirror targets/smk37/app/smk37_controls.c
   * (button_map, encoder_map), targets/smk37/board/panel.c (the lamps) and M-VAVE's own labels (targets/smk37/sdk/Simulator.js), and
   * tools/dx747/page/test-smk37.mjs fails if they drift from the C. */
  DxCore.SMK37 = {
    KIND: { BUTTON: 0, KEY: 1, PRESSURE: 2, PAD: 3, ENCODER: 4, COMBINATION: 5, PEDAL: 6, FADER: 7 },
    KEYS: 37, PADS: 16, FIRST_NOTE: 48,
    /** The 18 physical buttons, 0..17, with the label printed on the SMK-37. */
    BUTTONS: ['SHIFT', 'FADER BANK', 'HOME', 'SAVE', 'OCT -', 'OCT +', 'ARP', 'PAD / STEP', 'SCL', 'ENV / LFO', 'MIX', 'BT', 'HOME / SRC', 'EDIT', 'FX', 'SEQ', 'PLAY', 'REC'],
    /** DX747's button names, the order of the virtual panel (app/dx747/ui_data.inc). */
    VIRTUAL: ['OCT-', 'OCT+', 'FX', 'SEL', 'ENV', 'LFO', 'EDIT', 'GLO', 'HOME', 'SAVE', 'ARP', 'SEQ', 'PLAY', 'REC'],
    /** Physical button -> virtual button (255: none). SHIFT and FADER BANK are handled by the controls layer itself. */
    BUTTON_MAP: [255, 255, 8, 9, 0, 1, 10, 255, 3, 4, 7, 255, 8, 6, 2, 11, 12, 13],
    /** Virtual button -> the physical lamp that shows it (ENV and LFO share one). */
    LAMP: [4, 5, 14, 8, 9, 9, 13, 10, 12, 3, 6, 15, 16, 17],
    /** The 8 encoders, 0..7, with their labels, and the DX747 control each one is (encoder_map; the last is the master level). */
    ENCODERS: ['K1', 'K2', 'K3', 'K4', 'SELECT', 'PAGE', 'PRESET', 'MASTER'],
    ENCODER_MAP: [2, 3, 4, 5, 0, 1, 6],
    VIRTUAL_ENCODERS: ['SELECT', 'ALGO', 'K1', 'K2', 'K3', 'K4', 'PRESETS'],
    SHIFT: 0, ENV: 9,
  };
  DxCore.SOURCE = SOURCE;
  globalThis.DxCore = DxCore;
})();
