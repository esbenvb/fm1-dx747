#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
//
// Dependency-free Node harness for the FM1 WASM simulator (tools/dx747/sim/build.sh).
// Loads simulator.wasm, presses buttons, keys and encoders through sim_control,
// renders audio blocks so the firmware's control loop runs, reads the 240x240
// RGB565 frame buffer (sim_pixels) and writes PNGs. Node >= 18, no packages.
//
// Library:
//   import { Sim } from './shots.mjs';
//   const sim = await Sim.load('simulator.wasm');
//   sim.boot({ clean: true });             // sample bank wiped, see bootClean()
//   sim.press('HOME'); sim.whiteKey(3); sim.encoder('K1', 2);
//   sim.png('screen.png');
//
// CLI (stock baseline screens, or any scenario file exporting `default(sim, shot)`):
//   node shots.mjs --wasm <simulator.wasm> --out <dir> [--scale 2] [--keep-samples] [--scenario file.mjs]
//
// Time: one sim_render() is one 128-frame block (2.9025 ms at 44.1 kHz). The
// firmware polls its control loop every 10 ms of simulated time and the screen
// compositor ticks every 33 ms, so advance() is how the UI "runs".
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { deflateSync } from 'node:zlib';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

// ---- hardware vocabulary (sim_control kinds: 0 button, 1 key, 2 encoder, 3 knob) ----
// Button order is fixed by the sim_control index (app/screen.c enum; SEL is `SCL` there).
export const BUTTONS = ['OCT-', 'OCT+', 'FX', 'SEL', 'ENV', 'LFO', 'EDIT', 'GLO', 'HOME', 'SAVE', 'ARP', 'SEQ', 'PLAY', 'REC'];
// The keyboard has 27 keys (F3..G5); these are the white ones, in order. Key indices
// not listed here are the black keys, which the stock UI uses as edit shortcuts.
export const WHITE_KEYS = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26];
// Encoder indices as app/screen.c reads them: 0 SELECT, 1 ALGORITHM, 2..5 K1..K4, 6 PRESETS.
export const ENCODERS = { SELECT: 0, ALGO: 1, K1: 2, K2: 3, K3: 4, K4: 5, PRESETS: 6 };
// Stock page ids (enum in app/screen.c); sim_open() takes these. PROJECT (8) is not openable.
export const PAGES = {
  OVERVIEW: 0, STEPS: 1, FM_EDIT: 2, FM_ENV: 3, FM_LFO: 4, TONE_EDIT: 5, MIX: 6, BLE: 7, PROJECT: 8, NAME: 9,
  EFFECTS: 10, SOURCE: 11, ACTIONS: 12, STEP_EDIT: 13, RHYTHM: 14, FM_QUICK: 15, PRESET_BROWSER: 16, ARP_EDIT: 17, STARTUP: 18,
};
// Flash layout (app/instrument.h). Project slots occupy 0..SAMPLE_OFFSET; sim_boot(1)
// then copies the factory sample bank over SAMPLE_OFFSET.. up to the library sectors.
export const FLASH = { BYTES: 536576, SAMPLE_OFFSET: 69632, LIBRARY_OFFSET: 536576 - 4096 - 4 * 4096, TUNING_OFFSET: 536576 - 4096 };

export const SCREEN = { width: 240, height: 240 };
const BLOCK_FRAMES = 128;
const SAMPLE_RATE = 44100;

export class Sim {
  static async load(wasmPath) {
    const module = await WebAssembly.compile(readFileSync(wasmPath));
    const instance = await WebAssembly.instantiate(module, {}); // the reactor imports nothing
    instance.exports._initialize();
    return new Sim(instance.exports);
  }

  constructor(exports) {
    this.c = exports;
    this.elapsedBlocks = 0;
    this.remainder = 0;
  }

  // Memory can grow (and detach old views), so views are created per use.
  u8(pointer, length) { return new Uint8Array(this.c.memory.buffer, pointer, length); }
  u16(pointer, length) { return new Uint16Array(this.c.memory.buffer, pointer, length); }
  cstring(pointer) {
    const bytes = new Uint8Array(this.c.memory.buffer);
    let end = pointer;
    while (bytes[end]) end++;
    return Buffer.from(bytes.subarray(pointer, end)).toString('latin1');
  }

  // sim_boot flags: bit0 clear flash (and copy the factory sample bank), bit1 play the startup animation.
  boot({ clear = false, startup = false, clean = false } = {}) {
    if (clean) return this.bootClean({ startup });
    this.c.sim_boot((clear ? 1 : 0) | (startup ? 2 : 0));
    this.elapsedBlocks = 0;
    this.remainder = 0;
  }

  // Fresh device, but WITHOUT the stock sample bank. sim_boot(1) erases flash and copies the
  // factory sample bank over offset 69,632; on a DX747 build that region is scene storage, so
  // this erases everything from there to the end and boots again from that flash (sim_boot(0)
  // keeps flash and re-runs init, so samples/library/storage see the erased region).
  bootClean({ startup = false } = {}) {
    this.c.sim_boot(1);
    this.eraseFlash(FLASH.SAMPLE_OFFSET, FLASH.BYTES - FLASH.SAMPLE_OFFSET);
    this.c.sim_boot(startup ? 2 : 0);
    this.elapsedBlocks = 0;
    this.remainder = 0;
  }

  flash() { return this.u8(this.c.sim_flash(), FLASH.BYTES); }
  eraseFlash(offset, length) { this.flash().fill(0xff, offset, offset + length); }
  loadFlash(bytes) {
    if (bytes.length !== FLASH.BYTES) throw new Error(`flash image must be ${FLASH.BYTES} bytes`);
    this.flash().set(bytes);
  }
  factorySampleBytes() { return this.c.sim_factory_size(); }
  // True when every byte from the sample bank start to the DX7 library sectors is erased (0xFF).
  sampleRegionBlank() {
    return this.flash().subarray(FLASH.SAMPLE_OFFSET, FLASH.LIBRARY_OFFSET).every((b) => b === 0xff);
  }

  // ---- time ----
  // Render enough 128-frame blocks to cover `ms` of simulated time.
  advance(ms) {
    const exact = (ms * SAMPLE_RATE) / 1000 / BLOCK_FRAMES + this.remainder;
    const blocks = Math.floor(exact);
    this.remainder = exact - blocks;
    for (let i = 0; i < blocks; i++) this.c.sim_render();
    this.elapsedBlocks += blocks;
    return blocks;
  }
  get nowMs() { return (this.elapsedBlocks * BLOCK_FRAMES * 1000) / SAMPLE_RATE; }
  // Run one block and return the stereo float output (copied).
  renderBlock() {
    const pointer = this.c.sim_render();
    this.elapsedBlocks++;
    return new Float32Array(this.c.memory.buffer, pointer, BLOCK_FRAMES * 2).slice();
  }

  // ---- input ----
  button(name) {
    const index = typeof name === 'number' ? name : BUTTONS.indexOf(name);
    if (index < 0 || index > 13) throw new Error(`unknown button ${name}`);
    return index;
  }
  buttonDown(name) { this.c.sim_control(0, this.button(name), 1); }
  buttonUp(name) { this.c.sim_control(0, this.button(name), 0); }
  // A short press, then let the UI settle. Hold longer (e.g. 700 ms) for long-press actions.
  press(name, { holdMs = 60, settleMs = 80 } = {}) {
    this.buttonDown(name); this.advance(holdMs);
    this.buttonUp(name); this.advance(settleMs);
  }
  keyDown(index) { if (index < 0 || index > 26) throw new Error(`key ${index} out of range`); this.c.sim_control(1, index, 1); }
  keyUp(index) { this.c.sim_control(1, index, 0); }
  tapKey(index, { holdMs = 60, settleMs = 80 } = {}) {
    this.keyDown(index); this.advance(holdMs);
    this.keyUp(index); this.advance(settleMs);
  }
  // White key n = 1..16, the way the stock UI numbers them (step 1..16).
  whiteKey(n, opts) {
    if (n < 1 || n > 16) throw new Error(`white key ${n} out of range 1..16`);
    this.tapKey(WHITE_KEYS[n - 1], opts);
  }
  encoder(name, detents, { settleMs = 80 } = {}) {
    const index = typeof name === 'number' ? name : ENCODERS[name];
    if (index === undefined || index < 0 || index > 6) throw new Error(`unknown encoder ${name}`);
    // sim_control accepts -127..127 detents per call.
    this.c.sim_control(2, index, detents);
    this.advance(settleMs);
  }
  knob(value) { this.c.sim_control(3, 0, value); } // 0..1023, master volume ADC
  releaseAll() { this.c.sim_release(); }
  // Jump straight to a stock page (see PAGES); returns the firmware's status (0 = ok, -1 = refused).
  open(page) {
    const target = typeof page === 'number' ? page : PAGES[page];
    const result = this.c.sim_open(target);
    this.advance(80);
    return result;
  }

  // ---- state ----
  // First bytes of sim_state(): [0] selected track, [1] page id (PAGES), [2] ...
  state() { return this.u8(this.c.sim_state(), 225).slice(); }
  page() { return this.state()[1]; }
  pageName() { const p = this.page(); return Object.keys(PAGES).find((k) => PAGES[k] === p) ?? String(p); }
  track() { return this.state()[0]; }
  // 41 LED bytes: 0..13 buttons, 14..40 keys (sim_state()[168..208]).
  leds() { return this.state().subarray(168, 209).slice(); }
  // Text knobs/labels the firmware publishes for the four K tiles: [[label, value] x4].
  knobs() {
    const pointer = this.c.sim_knobs();
    const bytes = this.u8(pointer, 128);
    const text = (from) => { let s = ''; for (let i = from; i < from + 16 && bytes[i]; i++) s += String.fromCharCode(bytes[i]); return s; };
    return [0, 1, 2, 3].map((i) => [text(i * 32), text(i * 32 + 16)]);
  }
  // MIDI in from a port: 0 DIN/TRS, 1 USB, 2 BLE (sim_midi_from). Returns 0 on success.
  midiIn(bytes, source = 1) {
    this.u8(this.c.sim_file(), bytes.length).set(bytes);
    const result = this.c.sim_midi_from(source, bytes.length);
    this.advance(20);
    return result;
  }
  // Captured MIDI output [{port, bytes}], needs midiCapture(true) first.
  midiCapture(enable) { this.c.sim_midi_capture(enable ? 1 : 0); }
  midiOut() {
    const out = [];
    for (let code = this.c.sim_midi_output(); code; code = this.c.sim_midi_output()) {
      out.push({ port: code >> 8, bytes: Array.from(this.u8(this.c.sim_file(), code & 255)) });
    }
    return out;
  }

  // ---- pixels ----
  // The raw 240x240 native RGB565 frame (copied).
  pixels() { return this.u16(this.c.sim_pixels(), SCREEN.width * SCREEN.height).slice(); }
  rgb() { return rgb565ToRgb(this.pixels()); }
  png(path, { scale = 1 } = {}) {
    mkdirSync(dirname(resolve(path)), { recursive: true });
    const png = encodePng(this.rgb(), SCREEN.width, SCREEN.height, scale);
    writeFileSync(path, png);
    return png.length;
  }
}

// Same expansion as the browser simulator (sdk/Simulator.js): 5/6-bit channels scaled to 8 bits.
export function rgb565ToRgb(pixels) {
  const rgb = new Uint8Array(pixels.length * 3);
  for (let i = 0; i < pixels.length; i++) {
    const v = pixels[i];
    rgb[i * 3] = Math.round(((v >> 11) * 255) / 31);
    rgb[i * 3 + 1] = Math.round((((v >> 5) & 63) * 255) / 63);
    rgb[i * 3 + 2] = Math.round(((v & 31) * 255) / 31);
  }
  return rgb;
}

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c >>> 0;
  }
  return table;
})();
function crc32(bytes) {
  let c = 0xffffffff;
  for (const b of bytes) c = CRC_TABLE[(c ^ b) & 255] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const out = Buffer.alloc(12 + data.length);
  out.writeUInt32BE(data.length, 0);
  out.write(type, 4, 'ascii');
  out.set(data, 8);
  out.writeUInt32BE(crc32(out.subarray(4, 8 + data.length)), 8 + data.length);
  return out;
}
// 8-bit RGB PNG, nearest-neighbour integer upscale.
export function encodePng(rgb, width, height, scale = 1) {
  const w = width * scale, h = height * scale;
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) {
    const row = y * (w * 3 + 1);
    raw[row] = 0; // filter: none
    const source = Math.floor(y / scale) * width * 3;
    for (let x = 0; x < w; x++) {
      const from = source + Math.floor(x / scale) * 3;
      const to = row + 1 + x * 3;
      raw[to] = rgb[from]; raw[to + 1] = rgb[from + 1]; raw[to + 2] = rgb[from + 2];
    }
  }
  const header = Buffer.alloc(13);
  header.writeUInt32BE(w, 0); header.writeUInt32BE(h, 4);
  header[8] = 8; header[9] = 2; // 8-bit, truecolor
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', header), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0)),
  ]);
}

// ---------------------------------------------------------------- CLI
// Stock-firmware baseline: a fresh device, each page reached the way a player would
// (panel buttons, second presses), then sim_open() for pages with no direct button.
// File names are NN-<how reached>-<page the firmware reports>.
export async function stockScenario(sim, shot) {
  sim.bootClean({ startup: true });
  shot('00-startup-t0');
  sim.advance(300);
  shot('01-startup-t300');
  sim.advance(2500);
  sim.boot({ clean: true });
  sim.advance(200);
  shot('02-boot-overview');

  let n = 3;
  const reached = (how) => `${String(n++).padStart(2, '0')}-${how}`;
  // Every shot starts from a fresh boot so state (loaded presets, tempo, transport) never leaks between them.
  const fresh = () => { sim.boot({ clean: true }); sim.advance(200); };
  // One press each from HOME, then a second press of the same button.
  for (const button of ['SEQ', 'EDIT', 'ENV', 'LFO', 'FX', 'SEL', 'GLO', 'SAVE', 'ARP', 'PLAY', 'REC']) {
    fresh();
    sim.press('HOME');
    sim.press(button);
    shot(reached(`${button.toLowerCase()}`));
    sim.press(button);
    shot(reached(`${button.toLowerCase()}-again`));
  }
  fresh();
  sim.press('HOME');
  sim.press('HOME');
  shot(reached('home-again'));

  // Encoder turns on the quick edit page, so changed values show.
  fresh();
  sim.press('HOME');
  sim.press('EDIT');
  sim.encoder('K1', 3);
  sim.encoder('K4', -5);
  shot(reached('edit-k1+3-k4-5'));

  // Pages with no direct button (or only through sub-menus), straight through sim_open().
  for (const name of ['STEPS', 'FM_EDIT', 'FM_ENV', 'FM_LFO', 'TONE_EDIT', 'MIX', 'BLE', 'NAME', 'EFFECTS', 'SOURCE',
    'ACTIONS', 'STEP_EDIT', 'RHYTHM', 'FM_QUICK', 'PRESET_BROWSER', 'ARP_EDIT']) {
    fresh();
    const result = sim.open(name);
    shot(`open-${String(PAGES[name]).padStart(2, '0')}-${name.toLowerCase().replaceAll('_', '-')}${result ? '-refused' : ''}`);
  }
}

async function main(argv) {
  const args = { scale: 2 };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--wasm') args.wasm = argv[++i];
    else if (a === '--out') args.out = argv[++i];
    else if (a === '--scale') args.scale = Number(argv[++i]);
    else if (a === '--scenario') args.scenario = argv[++i];
    else { console.error(`unknown argument ${a}`); process.exit(2); }
  }
  if (!args.wasm || !args.out || !(args.scale >= 1)) {
    console.error('usage: shots.mjs --wasm <simulator.wasm> --out <dir> [--scale N] [--scenario file.mjs]');
    process.exit(2);
  }
  const sim = await Sim.load(args.wasm);
  const scenario = args.scenario ? (await import(pathToFileURL(resolve(args.scenario)).href)).default : stockScenario;
  const index = [];
  const shot = (name) => {
    // Name the file after how the page was reached plus the page the firmware reports (once).
    const page = sim.pageName().toLowerCase().replaceAll('_', '-');
    const file = `${name.endsWith(page) ? name : `${name}-${page}`}.png`;
    const bytes = sim.png(join(args.out, file), { scale: args.scale });
    const leds = Array.from(sim.leds()).map((v, i) => (v ? i : -1)).filter((i) => i >= 0);
    index.push({ file, page: sim.pageName(), track: sim.track() + 1, knobs: sim.knobs(), litLeds: leds, atMs: Math.round(sim.nowMs) });
    console.log(`${file}  ${bytes} B`);
  };
  await scenario(sim, shot);
  writeFileSync(join(args.out, 'index.json'), JSON.stringify({ scale: args.scale, shots: index }, null, 1));
  console.log(`${index.length} screenshots in ${args.out}; sample region erased: ${sim.sampleRegionBlank()}`);
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) await main(process.argv.slice(2));
