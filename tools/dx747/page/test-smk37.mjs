#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
//
// The SMK-37 panel of the workbench page (web/page/dx747-sim.js, Smk37Panel) and the tables it is drawn from (DxCore.SMK37 in
// web/page/dx747-core.js).
//
//   1. The tables against the C they mirror (targets/smk37/app/smk37_controls.c, targets/smk37/board/panel.c, app/dx747/ui_data.inc)
//      and against M-VAVE's own labels (targets/smk37/sdk/Simulator.js): a change in one without the other fails here.
//   2. With the SMK-37 browser core (./build.sh --web --target smk37-pro, or --wasm FILE): the physical buttons, pads, faders and keys
//      through the same events the page sends, against the real firmware in WebAssembly. Without it only part 1 runs.
//
//   node tools/dx747/page/test-smk37.mjs [--wasm FILE]
import { existsSync, readFileSync } from 'node:fs';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const text = (f) => readFileSync(join(root, f), 'utf8');
// the SMK-37 tree's own sdk/ scripts, as the page built for it carries them (tools/dx747/target.mjs lays targets/smk37/sdk over sdk/)
for (const f of ['targets/smk37/sdk/I18n.js', 'targets/smk37/sdk/WebUpdate.js', 'web/page/dx747-core.js', 'web/page/dx747-device.js', 'web/page/dx747-client.js'])
  vm.runInThisContext(text(f), { filename: f });
const { DxCore, DxDevice, Dx747 } = globalThis;
const S = DxCore.SMK37;

let checks = 0, failures = 0;
function check(ok, what) { ++checks; if (!ok) { ++failures; console.error('FAIL: ' + what); } }
const eq = (got, want, what) => check(JSON.stringify(got) === JSON.stringify(want), what + ': wanted ' + JSON.stringify(want) + ', got ' + JSON.stringify(got));
const numbers = (source, pattern, what) => {
  const m = source.match(pattern); if (!m) { check(false, what + ': not found'); return []; }
  return m[1].split(',').map((n) => Number(n.trim())).filter((n) => !Number.isNaN(n));
};
const strings = (source, pattern, what) => {
  const m = source.match(pattern); if (!m) { check(false, what + ': not found'); return []; }
  return [...m[1].matchAll(/'([^']*)'/g)].map((x) => x[1]);
};

// ---- 1. the tables ----------------------------------------------------------------------------------------------------------
const controls = text('targets/smk37/app/smk37_controls.c'), panel = text('targets/smk37/board/panel.c');
eq(S.BUTTON_MAP, numbers(controls, /button_map\[18\]\s*=\s*\{([^}]*)\}/, 'button_map'), 'BUTTON_MAP is button_map of smk37_controls.c');
eq(S.ENCODER_MAP, numbers(controls, /encoder_map\[7\]\s*=\s*\{([^}]*)\}/, 'encoder_map'), 'ENCODER_MAP is encoder_map of smk37_controls.c');
eq(S.LAMP, numbers(panel, /physical\[14\]\s*=\s*\{([^}]*)\}/, 'lamp table'), 'LAMP is the lamp table of panel.c');
check(new RegExp('SHIFT = ' + S.SHIFT + ', FADER_BANK = 1, ENV_BUTTON = ' + S.ENV + '\\b').test(controls), 'SHIFT, FADER BANK and ENV are buttons ' + S.SHIFT + ', 1 and ' + S.ENV + ' in the controls layer');
const kindEnum = text('targets/smk37/board/smk37_controls.h').match(/enum \{\s*(SMK37_BUTTON[^}]*)\}/);
const kinds = kindEnum ? [...kindEnum[1].matchAll(/SMK37_([A-Z]+)/g)].map((m) => m[1]) : [];
eq(Object.keys(S.KIND).map((k) => S.KIND[k] + ':' + k), kinds.map((k, i) => i + ':' + k), 'event kinds are the enum of smk37_controls.h');
const ids = text('app/dx747/ui_data.inc').match(/enum \{ (BTN_OCT_DOWN[^}]*) \}/);
eq(S.VIRTUAL, ids ? ids[1].split(',').map((n) => n.trim().replace('BTN_OCT_DOWN', 'OCT-').replace('BTN_OCT_UP', 'OCT+').replace('BTN_', '')) : [], 'VIRTUAL is the BTN_ enum of ui_data.inc');
const sim = text('web/page/dx747-sim.js');
eq(S.VIRTUAL_ENCODERS, strings(sim, /const ENCODERS = \[([^\]]*)\]/, 'FM-1 encoders'), 'VIRTUAL_ENCODERS are the FM-1 panel\'s encoders');
const stock = text('targets/smk37/sdk/Simulator.js');
eq(S.BUTTONS, strings(stock, /const labels=\[([^\]]*)\]/, 'M-VAVE button labels'), 'BUTTONS are the labels in M-VAVE\'s simulator');
eq(S.ENCODERS, strings(stock, /const encoders=\[([^\]]*)\]/, 'M-VAVE encoder labels'), 'ENCODERS are the labels in M-VAVE\'s simulator');
eq(numbers(stock, /const led=\[([^\]]*)\]/, 'M-VAVE led map'), S.BUTTON_MAP, 'M-VAVE\'s own button-to-lamp numbers are BUTTON_MAP');
check(S.BUTTONS.length === 18 && S.BUTTON_MAP.length === 18, '18 buttons');
check(S.BUTTON_MAP.every((v) => v === 255 || (v >= 0 && v < 14)), 'every button is none or one of the 14 virtual buttons');
const unused = S.BUTTONS.filter((_, i) => S.BUTTON_MAP[i] === 255 && i > 1);
eq(unused, ['PAD / STEP', 'BT'], 'the two buttons DX747 gives no function (besides SHIFT and FADER BANK)');
eq(S.BUTTONS.filter((_, i) => S.BUTTON_MAP[i] === 8), ['HOME', 'HOME / SRC'], 'two buttons are HOME');
eq([S.BUTTONS[8], S.VIRTUAL[S.BUTTON_MAP[8]], S.BUTTONS[10], S.VIRTUAL[S.BUTTON_MAP[10]]], ['SCL', 'SEL', 'MIX', 'GLO'], 'SCL is SEL and MIX is GLO');

// ---- 2. the firmware ---------------------------------------------------------------------------------------------------------
const wasmArg = process.argv.indexOf('--wasm');
const wasmPath = wasmArg > 0 ? process.argv[wasmArg + 1] : join(root, 'build/smk37-pro/web/simulator.wasm');
if (!existsSync(wasmPath)) {
  console.log('test-smk37: the SMK-37 browser core is not built (./build.sh --web --target smk37-pro): only the tables were checked');
} else {
  const bytes = readFileSync(wasmPath);
  check(DxCore.hasSmk37(bytes), 'the module exports sim_smk');
  const core = new DxCore(bytes);
  check(core.smk37, 'the core is the SMK-37 build');
  core.prepareStore(); core.advance(400);
  const K = S.KIND;
  const press = (kind, id) => { core.smk(kind, id, 1); core.advance(60); core.smk(kind, id, 0); core.advance(60); };
  const button = (label) => { const id = S.BUTTONS.indexOf(label); check(id >= 0, 'a button called ' + label); press(K.BUTTON, id); };
  const SCREEN = { BOOT: 0, PARTS: 1, PART: 2, MON: 7, MIX: 8, FX: 9, MASTER: 10, SCENES: 11, SYS: 12, DETAIL: 14 };
  const peak = () => { let p = 0; for (let b = 0; b < 40; b++) for (const v of core.render()) p = Math.max(p, Math.abs(v)); return p; };
  const access = new DxDevice.VirtualMidiAccess(core, 'SMK-37 DX747 (simulator)', 6);
  const dx = new Dx747(access);
  await dx.connect();
  const state = () => dx.readState();

  // the SMK-37 SDK's script refuses factory voice A23 (detune 15); the page still has to list the voices
  const names = await dx.voiceNames(0); eq([names.length, names[0], names[22]], [32, 'PIANO 1', 'SYN LEAD 6'], 'the voice names of bank A are read with the SMK-37 script');
  eq(core.screen(), SCREEN.PARTS, 'a fresh unit has left the boot animation and shows Parts');
  // the screens the physical buttons open, by what is printed on them
  for (const [label, screen, what] of [['SCL', SCREEN.MIX, 'Mix'], ['MIX', SCREEN.SYS, 'System'], ['SEQ', SCREEN.PART, 'Part'], ['PLAY', SCREEN.SCENES, 'Scenes'],
    ['REC', SCREEN.MON, 'MIDI in'], ['FX', SCREEN.FX, 'FX'], ['HOME / SRC', SCREEN.PARTS, 'Parts'], ['HOME', SCREEN.DETAIL, 'Details (HOME twice)']]) {
    button(label); eq(core.screen(), screen, label + ' opens ' + what);
  }
  button('HOME'); button('PAD / STEP'); eq(core.screen(), SCREEN.PARTS, 'PAD / STEP does nothing'); button('BT'); eq(core.screen(), SCREEN.PARTS, 'BT does nothing');

  // pads: a fresh unit has no scenes, so every pad is the dark "empty" colour; on Mix after EDIT they are the parts' mute and solo pads
  const colours = () => Array.from({ length: S.PADS }, (_, i) => core.padColor(i));
  check(colours().every((c) => c === 0x040404), 'every pad is the dark colour while no scene exists');
  button('SCL'); button('EDIT');
  check(colours().every((c) => c !== 0x040404 && c <= 0xffffff), 'on Mix after EDIT every pad of a part that exists is lit in its colour');
  button('EDIT'); check(colours().every((c) => c === 0x040404), 'EDIT again: the pads are scenes again');
  button('HOME');

  // faders: the first readings, then pickup, then the fader bank
  for (let i = 0; i < 4; i++) core.smk(K.FADER, i, 127);
  core.smk(K.FADER, 0, 64); core.advance(60);
  let s = await state(); eq(s.parts[0].level, 64, 'fader 1 sets the level of part 1 from where it was');
  press(K.BUTTON, 1); core.smk(K.FADER, 0, 100); core.advance(60);
  s = await state(); eq([s.parts[4].level, s.parts[0].level], [127, 64], 'after FADER BANK fader 1 waits for part 5, and part 1 keeps its level');
  press(K.BUTTON, 1);

  // keys: a key plays the selected part at the velocity it is struck with; released, it is silent
  check(peak() < 0.001, 'silence before a key');
  core.smk(K.KEY, 12, 100); core.advance(100); check(peak() > 0.01, 'a key sounds');
  core.smk(K.KEY, 12, 0); core.advance(1500); check(peak() < 0.001, 'a released key is silent again');

  // SHIFT + ENV is LFO: remove the framed layer; the last layer takes the part with it. ENV alone adds one back.
  s = await state(); check(s.parts[0].used, 'part 1 exists');
  const layers = (st) => st.parts.reduce((n, p) => n + (p.used ? p.layers.length : 0), 0);
  const before = layers(s);
  core.smk(K.BUTTON, S.SHIFT, 1); press(K.BUTTON, S.ENV); core.smk(K.BUTTON, S.SHIFT, 0); core.advance(60);
  s = await state(); check(!s.parts[0].used, 'SHIFT + ENV removes the last layer, and with it part 1'); eq(layers(s), before - 1, 'one layer fewer');
  press(K.BUTTON, S.ENV); s = await state(); eq(layers(s), before, 'ENV alone adds a layer or a part again');
}

console.log('test-smk37: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
