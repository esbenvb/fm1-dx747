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
    /** The 18 button lamps by printed button (DxCore.SMK37.BUTTONS order), 0 dark or 8 lit, as the firmware lights them through smk37_led(). */
    smkLamps() { return new Uint8Array(this.c.memory.buffer, this.c.sim_smk_lamps(), 18).slice(); }
  }
  /** Whether the compiled module (bytes) is the SMK-37 build, without starting it. */
  DxCore.hasSmk37 = wasm => WebAssembly.Module.exports(new WebAssembly.Module(wasm)).some(e => e.name === 'sim_smk');
  /* The SMK-37's panel, as DX747's controls layer has it. The tables mirror targets/smk37/app/smk37_controls.c (button_label, button_map,
   * encoder_map) and the names in M-VAVE's own simulator (targets/smk37/sdk/Simulator.js: SDK_BUTTONS, SDK_ENCODERS);
   * tools/dx747/page/test-smk37.mjs fails if they drift from those files.
   * What is printed on the unit (BUTTONS, ENCODERS, PAD_ICONS) is read off the drawing of the panel in the SMK-37 manual: the buttons
   * of each group in the order of the board's ids (left group 0..5, right group 6..17, row by row), eight knobs K1..K8 above the pads. */
  DxCore.SMK37 = {
    KIND: { BUTTON: 0, KEY: 1, PRESSURE: 2, PAD: 3, ENCODER: 4, COMBINATION: 5, PEDAL: 6, FADER: 7 },
    KEYS: 37, PADS: 16, FIRST_NOTE: 48,
    /** The 18 physical buttons, 0..17, with the label printed on the SMK-37 (the manual's drawing). */
    BUTTONS: ['KNOB BANK', 'FADER BANK', '<', '>', 'OCTAVE-', 'OCTAVE+', 'ARP', 'NOTE REPEAT', 'SCALE', 'CHORD', 'GLOBE', 'BT', 'PATCH', 'PARA', 'FX', 'SEQ', 'SEQ PLAY', 'SEQ REC'],
    /** The names M-VAVE's own simulator gives the same 18 buttons (the stock firmware's functions). */
    SDK_BUTTONS: ['SHIFT', 'FADER BANK', 'HOME', 'SAVE', 'OCT -', 'OCT +', 'ARP', 'PAD / STEP', 'SCL', 'ENV / LFO', 'MIX', 'BT', 'HOME / SRC', 'EDIT', 'FX', 'SEQ', 'PLAY', 'REC'],
    /** What is printed under the lower row of pads, left to right. */
    PAD_ICONS: ['▶', 'II', '●', '«', '»', '◁', '▷', 'UNDO'],
    /** DX747's own button names, the order of the virtual panel (BTN_* in app/dx747/ui_data.inc). Only the FM-1 uses these as the names of
     *  controls; on the SMK-37 every button is called by what is printed on it. */
    VIRTUAL: ['OCT-', 'OCT+', 'FX', 'SEL', 'ENV', 'LFO', 'EDIT', 'GLO', 'HOME', 'SAVE', 'ARP', 'SEQ', 'PLAY', 'REC', 'BACK', 'FWD'],
    /** Printed button -> DX747's button (255: none). Each of the 16 is on exactly one printed button; KNOB BANK has no function yet and FADER
     *  BANK is handled by the controls layer itself. */
    BUTTON_MAP: [255, 255, 14, 15, 0, 1, 10, 11, 4, 5, 7, 13, 8, 6, 2, 3, 12, 9],
    /** The printed buttons that, held at power-on, start safe boot: add layer and remove layer (SCALE and CHORD, left and right). */
    SAFE_BOOT: [8, 9],
    /** The 8 knobs, 0..7, left to right, as printed on the unit, and the DX747 control each one is (encoder_map; the last is the master level). */
    ENCODERS: ['K1', 'K2', 'K3', 'K4', 'K5', 'K6', 'K7', 'K8'],
    /** The names M-VAVE's simulator gives the same eight encoders: only four of them are K knobs there. */
    SDK_ENCODERS: ['K1', 'K2', 'K3', 'K4', 'SELECT', 'PAGE', 'PRESET', 'MASTER'],
    ENCODER_MAP: [2, 3, 4, 5, 0, 1, 6],
    VIRTUAL_ENCODERS: ['SELECT', 'ALGO', 'K1', 'K2', 'K3', 'K4', 'PRESETS'],
  };
  DxCore.SOURCE = SOURCE;
  globalThis.DxCore = DxCore;
})();
