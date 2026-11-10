#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
//
// The SMK-37 panel of the workbench page (web/page/dx747-sim.js, Smk37Panel), the tables it is drawn from (DxCore.SMK37 in
// web/page/dx747-core.js), and what the SMK-37 screens say.
//
//   1. The tables against the C they mirror (targets/smk37/app/smk37_controls.c, app/dx747/ui_data.inc) and against M-VAVE's own names
//      (targets/smk37/sdk/Simulator.js): a change in one without the other fails here. The captions and the screens' texts must call every control
//      by what is printed on the unit, never by the FM-1's names (HOME, SAVE, ENV, LFO, SELECT, ALGO, PRESETS ...), and every tip line must fit
//      the screen once the names in braces are the printed labels.
//   2. With the SMK-37 browser core (./build.sh --web --target smk37-pro, or --wasm FILE): the printed buttons, knobs, pads, faders and keys
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
vm.runInThisContext(text('web/page/dx747-sim.js'), { filename: 'web/page/dx747-sim.js' });
const { DxCore, DxDevice, Dx747, Smk37Panel } = globalThis;
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
const cstrings = (source, pattern, what) => {
  const m = source.match(pattern); if (!m) { check(false, what + ': not found'); return []; }
  return [...m[1].matchAll(/"([^"]*)"/g)].map((x) => x[1]);
};

// ---- 1. the tables ----------------------------------------------------------------------------------------------------------
const controls = text('targets/smk37/app/smk37_controls.c');
eq(S.BUTTON_MAP, numbers(controls, /button_map\[18\]\s*=\s*\{([^}]*)\}/, 'button_map'), 'BUTTON_MAP is button_map of smk37_controls.c');
eq(S.ENCODER_MAP, numbers(controls, /encoder_map\[7\]\s*=\s*\{([^}]*)\}/, 'encoder_map'), 'ENCODER_MAP is encoder_map of smk37_controls.c');
eq(S.BUTTONS, cstrings(controls, /button_label\[18\]\s*=\s*\{([^}]*)\}/, 'button_label'), 'BUTTONS are the button_label names of smk37_controls.c');
eq(S.ENCODERS, cstrings(controls, /encoder_label\[8\]\s*=\s*\{([^}]*)\}/, 'encoder_label'), 'ENCODERS are the encoder_label names of smk37_controls.c');
const chord = controls.match(/SCALE_BUTTON = (\d+), CHORD_BUTTON = (\d+)/);
eq(chord ? [Number(chord[1]), Number(chord[2])] : null, S.SAFE_BOOT, 'safe boot is the two buttons the controls layer says');
check(S.SAFE_BOOT.every((id) => [5, 4].includes(S.BUTTON_MAP[id])), 'those two are add layer and remove layer, the FM-1\'s ENV and LFO chord');
check(/smk37_control_event\(SMK37_BUTTON, 8, 1\); smk37_control_event\(SMK37_BUTTON, 9, 1\);/.test(text('web/engine/Simulator.c')), 'the simulator holds the same two buttons for safe boot');
check(!/shifted|SHIFT_BUTTON|enum \{ SHIFT/.test(controls), 'the controls layer has no SHIFT');
const kindEnum = text('targets/smk37/board/smk37_controls.h').match(/enum \{\s*(SMK37_BUTTON[^}]*)\}/);
const kinds = kindEnum ? [...kindEnum[1].matchAll(/SMK37_([A-Z]+)/g)].map((m) => m[1]) : [];
eq(Object.keys(S.KIND).map((k) => S.KIND[k] + ':' + k), kinds.map((k, i) => i + ':' + k), 'event kinds are the enum of smk37_controls.h');
const ids = text('app/dx747/ui_data.inc').match(/enum \{ (BTN_OCT_DOWN[\s\S]*?)\};/);
const virtual = ids ? ids[1].replace(/^\s*#.*$/gm, '').replace(/\/\*[\s\S]*?\*\//g, '').split(',').map((n) => n.trim()).filter(Boolean)
  .map((n) => n.replace('BTN_OCT_DOWN', 'OCT-').replace('BTN_OCT_UP', 'OCT+').replace('BTN_', '')) : [];
eq(S.VIRTUAL, virtual, 'VIRTUAL is the BTN_ enum of ui_data.inc');
const sim = text('web/page/dx747-sim.js');
eq(S.VIRTUAL_ENCODERS, strings(sim, /const ENCODERS = \[([^\]]*)\]/, 'FM-1 encoders'), 'VIRTUAL_ENCODERS are the FM-1 panel\'s encoders');
const stock = text('targets/smk37/sdk/Simulator.js');
eq(S.SDK_BUTTONS, strings(stock, /const labels=\[([^\]]*)\]/, 'M-VAVE button labels'), 'SDK_BUTTONS are the labels in M-VAVE\'s simulator');
eq(S.SDK_ENCODERS, strings(stock, /const encoders=\[([^\]]*)\]/, 'M-VAVE encoder labels'), 'SDK_ENCODERS are the names in M-VAVE\'s simulator');
check(S.BUTTONS.length === 18 && S.SDK_BUTTONS.length === 18 && S.BUTTON_MAP.length === 18, '18 buttons, named twice');
// what is printed on the unit (the manual's drawing): eight knobs K1..K8, and the buttons in the board's order. The names printed and
// the names in M-VAVE's simulator agree on five of the 18 positions; that, and FADER BANK and OCTAVE- / OCTAVE+ sitting in the places
// the board's ids 1, 4 and 5 give them, is what the order rests on.
eq(S.ENCODERS, ['K1', 'K2', 'K3', 'K4', 'K5', 'K6', 'K7', 'K8'], 'eight knobs K1 to K8 are printed on the unit');
eq(S.SDK_ENCODERS.length, 8, 'M-VAVE\'s simulator names the same eight encoders');
eq(S.BUTTONS.filter((name, i) => name === S.SDK_BUTTONS[i]), ['FADER BANK', 'ARP', 'BT', 'FX', 'SEQ'], 'the printed names that M-VAVE\'s simulator has at the same position');
eq([S.BUTTONS[4], S.BUTTONS[5], S.BUTTONS[14], S.BUTTONS[15]], ['OCTAVE-', 'OCTAVE+', 'FX', 'SEQ'], 'OCTAVE-, OCTAVE+, FX and SEQ are where the board\'s ids put OCT -, OCT +, FX and SEQ');
eq(S.PAD_ICONS.length, 8, 'eight symbols are printed under the lower row of pads');
// every button of DX747's is on exactly one printed button: no two buttons do the same, none is missing
eq(S.VIRTUAL.map((_, v) => S.BUTTON_MAP.filter((x) => x === v).length), S.VIRTUAL.map(() => 1), 'each of DX747\'s 16 buttons is on exactly one printed button');
eq(S.BUTTONS.filter((_, i) => S.BUTTON_MAP[i] === 255), ['KNOB BANK', 'FADER BANK'], 'the printed buttons DX747 does not read as a button: the two banks');
eq([S.BUTTONS[S.BUTTON_MAP.indexOf(4)], S.BUTTONS[S.BUTTON_MAP.indexOf(5)]], ['SCALE', 'CHORD'], 'the left button, SCALE, adds a layer and the right one, CHORD, removes it, as ENV and LFO on the FM-1');
check(S.BUTTON_MAP.indexOf(4) < S.BUTTON_MAP.indexOf(5) && S.BUTTON_MAP.indexOf(5) - S.BUTTON_MAP.indexOf(4) === 1, 'add layer is immediately left of remove layer');
eq([2, 3].map((id) => S.VIRTUAL[S.BUTTON_MAP[id]]), ['BACK', 'FWD'], '< and > are back and forward');
eq([4, 5].map((id) => S.VIRTUAL[S.BUTTON_MAP[id]]), ['OCT-', 'OCT+'], 'OCTAVE- and OCTAVE+ are the octave buttons');

// the captions and tooltips under the buttons (Smk37Panel.caption and .detail): every button has them, they are in the printed names, and they
// never use the FM-1's names for a control
const FM1_NAMES = /\b(HOME|SAVE|ALGO|ENV|LFO|PRESETS|SELECT|SHIFT|GLO|SEL|OCT|FM-1)\b/;
for (let id = 0; id < 18; id++) {
  const v = S.BUTTON_MAP[id], cap = Smk37Panel.caption(id), detail = Smk37Panel.detail(id);
  check(typeof cap === 'string' && cap.length > 0 && typeof detail === 'string' && detail.length > 10, 'button ' + S.BUTTONS[id] + ' has a caption and a tooltip');
  check(detail.startsWith(S.BUTTONS[id] + ':'), 'the tooltip of ' + S.BUTTONS[id] + ' starts with its printed name: ' + detail.slice(0, 40));
  check(!FM1_NAMES.test(cap) && !FM1_NAMES.test(detail), 'the caption and tooltip of ' + S.BUTTONS[id] + ' use no FM-1 name: ' + (cap + ' / ' + detail).match(FM1_NAMES));
  check(cap !== 'unused', 'no printed button is left unused: ' + S.BUTTONS[id]);
}
eq(S.BUTTONS.map((_, id) => Smk37Panel.caption(id)),
  ['knob bank', 'fader bank', 'back', 'forward', 'oct / page -', 'oct / page +', 'LEARN', 'Part', 'add layer', 'remove layer', 'System', 'MIDI in', 'Parts', 'Edit', 'FX', 'Mix', 'Scenes', 'Save'],
  'the captions under the 18 buttons');
check(Smk37Panel.detail(12).includes('SEQ PLAY') && Smk37Panel.detail(16).includes('OCTAVE-'), 'the chords are named with the printed labels (PATCH then SEQ PLAY, SEQ PLAY then OCTAVE- or OCTAVE+)');

// what the screens say: C strings in app/dx747/ui_screens.inc and ui_input.inc, as the SMK-37 build has them. TXT(fm1, smk) is the FM-1's text and
// the SMK-37's; the names in braces become the printed labels (tip_label in ui_screens.inc: K knobs from encoder_map, buttons from button_map).
const LABEL = { OCT: 'OCTAVE', KNOBBANK: S.BUTTONS[0], FADERBANK: S.BUTTONS[1] };
for (const [token, v] of Object.entries({ FX: 2, SEL: 3, ENV: 4, LFO: 5, EDIT: 6, GLO: 7, HOME: 8, SAVE: 9, ARP: 10, SEQ: 11, PLAY: 12, REC: 13 })) LABEL[token] = S.BUTTONS[S.BUTTON_MAP.indexOf(v)];
for (const [token, v] of Object.entries({ SELECT: 0, ALGO: 1, K1: 2, K2: 3, K3: 4, K4: 5, PRESETS: 6 })) LABEL[token] = S.ENCODERS[S.ENCODER_MAP.indexOf(v)];
const expand = (s) => s.replace(/\{([A-Z0-9]+)\}/g, (_, name) => (name in LABEL ? LABEL[name] : name));
const ui = text('app/dx747/ui_screens.inc') + '\n' + text('app/dx747/ui_input.inc');
const tipTable = ui.match(/static const char \*tip_label[\s\S]*?\n\}\n/);                    // the token names themselves ("SELECT", "HOME", ...) are the FM-1's, by design
check(tipTable !== null, 'the token table of tip_label is found');
const smk = ui.replace(tipTable ? tipTable[0] : '', '').replace(/TXT\(\s*"(?:[^"\\]|\\.)*"\s*,\s*((?:"(?:[^"\\]|\\.)*"|[^()]*\([^()]*\)))\s*\)/g, '$1');     // the SMK-37's side of every TXT(...)
const literals = [...smk.matchAll(/"((?:[^"\\]|\\.)*)"/g)].map((m) => m[1]);
// 1. no FM-1 control name is left in a text outside braces, except where the word is about something else (a layer's shift, a stock-data save)
const ALLOWED = new Set(['BARS = KEYS  +N = SHIFT  V = VELOCITY', 'SAVE AS SCENE ', 'SAVE). IT WILL BE ERASED.', 'FIRST SAVE: FLASH SETUP', ' OCT)', ' OCT', 'SHIFT', 'SAVE FAILED']);
const stray = literals.filter((l) => /\b(ENV|LFO|HOME|SELECT|ALGO|PRESETS|SHIFT|GLO|SEL|SAVE|OCT)\b/.test(l.replace(/\{[A-Z0-9]+\}/g, '')) && !ALLOWED.has(l));
eq(stray, [], 'no FM-1 control name outside braces in the SMK-37 texts');
// 2. the pad texts say pads where the FM-1's say keys
check(literals.includes('PADS RECALL  {OCT} BANK  {SAVE} STORES') && literals.includes('PADS = MUTE / SOLO') && !literals.some((l) => /^(WHITE )?KEYS (RECALL|MUTE|PLAY)/.test(l)),
  'the Scenes and Mix screens say pads recall, and pads mute and solo, where the FM-1 says keys');
// 3. every tip line fits the 240 pixel screen (6 pixels a character) once the names are the printed labels
const LIMIT = { '{K1} CURSOR {K2} LETTER {SAVE} STORES': 37, '{OCT} PAGE': 11 };
for (const l of literals.filter((x) => /\{[A-Z0-9]+\}/.test(x))) {
  const shown = expand(l).replace(/[{}]/g, '');
  check(shown.length <= (LIMIT[l] || 40), 'the tip line fits the screen (' + shown.length + ' characters): ' + shown);
}
eq(expand('{ALGO} LAYER {ENV} ADD {LFO} DEL {ARP} LEARN'), 'K6 LAYER SCALE ADD CHORD DEL ARP LEARN', 'the layer tip line reads in the printed labels');
eq(expand('{HOME}'), 'PATCH', 'HOME is PATCH');

// ---- 2. the firmware ---------------------------------------------------------------------------------------------------------
const wasmArg = process.argv.indexOf('--wasm');
const wasmPath = wasmArg > 0 ? process.argv[wasmArg + 1] : join(root, 'build/smk37-pro/web/simulator.wasm');
if (!existsSync(wasmPath) && wasmArg > 0) {
  check(false, 'the browser core given with --wasm does not exist: ' + wasmPath);
} else if (!existsSync(wasmPath)) {
  console.log('test-smk37: the SMK-37 browser core is not built (./build.sh --web --target smk37-pro): only the tables and the texts were checked');
} else {
  const bytes = readFileSync(wasmPath);
  check(DxCore.hasSmk37(bytes), 'the module exports sim_smk');
  const core = new DxCore(bytes);
  check(core.smk37, 'the core is the SMK-37 build');
  core.prepareStore(); core.advance(400);
  const K = S.KIND;
  const press = (kind, id) => { core.smk(kind, id, 1); core.advance(60); core.smk(kind, id, 0); core.advance(60); };
  const button = (label) => { const id = S.BUTTONS.indexOf(label); check(id >= 0, 'a button called ' + label); press(K.BUTTON, id); };
  const SCREEN = { BOOT: 0, PARTS: 1, PART: 2, MON: 7, MIX: 8, FX: 9, MASTER: 10, SCENES: 11, SYS: 12, SAFE: 13, DETAIL: 14 };
  const peak = () => { let p = 0; for (let b = 0; b < 40; b++) for (const v of core.render()) p = Math.max(p, Math.abs(v)); return p; };
  const access = new DxDevice.VirtualMidiAccess(core, 'SMK-37 DX747 (simulator)', 6);
  const dx = new Dx747(access);
  await dx.connect();
  const state = () => dx.readState();

  // the SMK-37 SDK's script refuses factory voice A23 (detune 15); the page still has to list the voices
  const names = await dx.voiceNames(0); eq([names.length, names[0], names[22]], [32, 'PIANO 1', 'SYN LEAD 6'], 'the voice names of bank A are read with the SMK-37 script');
  // a DX7 ROM with a detune of 15 (bank A's voice 23 is one) imports and reads back although the SMK-37 script's own upload refuses it
  {
    const rawA = await dx.rawBank(0), rom = new Uint8Array(4104); rom.set([0xf0, 0x43, 0x00, 0x09, 0x20, 0x00]); rom.set(rawA, 6);
    let sum = 0; for (const v of rawA) sum = (sum + v) & 127; rom[4102] = (128 - sum) & 127; rom[4103] = 0xf7;
    let refused = false; try { await dx.lib.upload(4, 3, rawA); } catch { refused = true; }
    check(refused, 'the SMK-37 script itself refuses a bank with a detune of 15 (the reason the page uploads banks itself)');
    await dx.importRom(3, rom);
    eq((await dx.voiceNames(3)).join('|'), names.join('|'), 'the ROM with a detune of 15 is in bank D');
    check((await dx.rawBank(3)).every((v, i) => v === rawA[i]), 'bank D reads back byte for byte');
  }
  eq(core.screen(), SCREEN.PARTS, 'a fresh unit has left the boot animation and shows Parts');
  // the screens the printed buttons open, and the lamp of the one that is open
  const lit = () => Array.from(core.smkLamps(), (l, i) => (l ? i : -1)).filter((i) => i >= 0);
  eq(lit(), [S.BUTTONS.indexOf('PATCH')], 'on Parts the lamp of PATCH is lit and no other');
  for (const [label, screen, what] of [['SEQ', SCREEN.MIX, 'Mix'], ['GLOBE', SCREEN.SYS, 'System'], ['NOTE REPEAT', SCREEN.PART, 'Part'], ['SEQ PLAY', SCREEN.SCENES, 'Scenes'],
    ['BT', SCREEN.MON, 'MIDI in'], ['FX', SCREEN.FX, 'FX'], ['FX', SCREEN.MASTER, 'Master filter (FX twice)'], ['PARA', 3, 'the edit pages'], ['PATCH', SCREEN.PARTS, 'Parts'], ['PATCH', SCREEN.DETAIL, 'Details (PATCH twice)']]) {
    button(label); eq(core.screen(), screen, label + ' opens ' + what);
    if (!['Master filter (FX twice)', 'Details (PATCH twice)'].includes(what)) eq(lit(), [S.BUTTONS.indexOf(label)], 'the lamp of ' + label + ' is the one that is lit');
  }
  button('PATCH');
  button('<'); button('>'); eq(core.screen(), SCREEN.PARTS, '< and > leave the screen as it is');
  // KNOB BANK moves the four knobs to parts 5 to 8 on Mix; it opens no screen
  button('KNOB BANK'); eq(core.screen(), SCREEN.PARTS, 'KNOB BANK opens no screen'); button('KNOB BANK');

  // pads: a fresh unit has no scenes, so every pad is dark on Parts; on Mix they are the parts' mute (top row) and solo (lower row), always
  const colours = () => Array.from({ length: S.PADS }, (_, i) => core.padColor(i));
  eq(colours(), [0x282828, ...Array(15).fill(0)], 'on Parts every pad is dark while no scene exists, except the cursor, on scene 1, which is dim white');
  button('SEQ');
  check(colours().every((c) => c !== 0 && c <= 0xffffff), 'on Mix every pad of a part that exists is lit: mute on the top row, solo on the lower');
  button('PATCH');

  // faders: on the screens where they are part levels (here Mix): the first readings only say where the faders stand, then every fader sets its part at once, then the fader bank
  button('SEQ');
  for (let i = 0; i < 4; i++) core.smk(K.FADER, i, 127);
  core.smk(K.FADER, 0, 64); core.advance(60);
  let s = await state(); eq(s.parts[0].level, 64, 'fader 1 sets the level of part 1 from where it was');
  press(K.BUTTON, 1); core.smk(K.FADER, 0, 100); core.advance(60);
  s = await state(); eq([s.parts[4].level, s.parts[0].level], [100, 64], 'after FADER BANK fader 1 sets part 5 at once, and part 1 keeps its level');
  for (let i = 1; i < 4; i++) { core.smk(K.FADER, i, 20 + i); core.advance(60); }
  s = await state(); eq(s.parts.slice(4).map((p) => p.level), [100, 21, 22, 23], 'in the fader bank all four faders set parts 5 to 8, from wherever they were left');
  check(core.smkLamps()[1] > 0, 'the lamp of FADER BANK is lit for parts 5 to 8');
  press(K.BUTTON, 1);
  check(core.smkLamps()[1] === 0, 'and dark again for parts 1 to 4');
  // a fader moved on another screen is not dead on this one: FADER2 was the filter's resonance on Parts, here it is the level of part 2 (a bug report: FADER2 and the bank did nothing)
  button('PATCH'); core.smk(K.FADER, 1, 70); core.advance(60); button('SEQ');
  core.smk(K.FADER, 1, 35); core.advance(60);
  s = await state(); eq(s.parts[1].level, 35, 'FADER2 sets part 2 on Mix after it was moved on Parts');
  core.smk(K.FADER, 3, 5); core.advance(60); s = await state(); eq(s.parts[3].level, 5, 'FADER4 sets part 4 as well');
  // on Mix < and > pick the part and OCTAVE+ leaves the part alone (it moves the keys)
  s = await state(); const mixSel = s.selected;
  button('>'); s = await state(); eq(s.selected, (mixSel + 1) % 8, '> selects the next part on Mix');
  button('<'); s = await state(); eq(s.selected, mixSel, '< selects the part before on Mix');
  button('OCTAVE+'); s = await state(); eq(s.selected, mixSel, 'OCTAVE+ does not change the part on Mix');
  button('OCTAVE-');
  button('PATCH');

  // keys: a key plays the selected part at the velocity it is struck with; released, it is silent
  check(peak() < 0.001, 'silence before a key');
  core.smk(K.KEY, 12, 100); core.advance(100); check(peak() > 0.01, 'a key sounds');
  core.smk(K.KEY, 12, 0); core.advance(1500); check(peak() < 0.001, 'a released key is silent again');

  // the eight knobs: K1 is the volume of the selected part on Parts, K5 picks the part, K7 the voice, K8 is the master level
  if (core.screen() !== SCREEN.PARTS) button('PATCH');          // PATCH on Parts would open Details, where K1 is a layer's level
  s = await state();
  const sel0 = s.selected, lvl0 = s.parts[sel0].level;
  core.smk(K.ENCODER, 0, -4); core.advance(60); s = await state(); check(s.parts[sel0].level < lvl0, 'K1 turns the level of the selected part down on Parts');
  core.smk(K.ENCODER, 4, 1); core.advance(60); s = await state(); eq(s.selected, sel0 + 1, 'K5 selects the next part');
  const voice = () => s.engines[s.parts[s.selected].layers[0].engine].voice;
  const voice0 = voice();
  core.smk(K.ENCODER, 6, 1); core.advance(100); s = await state(); check(voice() !== voice0, 'K7 chooses another voice for the framed layer: ' + voice0 + ' -> ' + voice());
  core.smk(K.ENCODER, 4, -1); core.advance(60); s = await state(); eq(s.selected, sel0, 'K5 turned back selects the part again');
  const loud = () => { core.smk(K.KEY, 12, 100); core.advance(100); const p = peak(); core.smk(K.KEY, 12, 0); core.advance(1500); return p; };
  const full = loud(); core.smk(K.ENCODER, 7, -40); const quiet = loud(); core.smk(K.ENCODER, 7, 40);
  check(quiet < full * 0.9, 'K8 is the master level: ' + full.toFixed(3) + ' -> ' + quiet.toFixed(3));

  // < and > on Parts: the part before and after
  button('>'); s = await state(); eq(s.selected, sel0 + 1, '> selects the next part on Parts');
  button('<'); s = await state(); eq(s.selected, sel0, '< selects the part before');

  // pads: on Mix after PARA the pad pairs are the mute and solo of each part; the pedal sustains a held note
  const engineOf = (part) => s.engines[s.parts[part].layers[0].engine];
  button('SEQ');
  core.smk(K.PAD, 0, 1); core.advance(60); core.smk(K.PAD, 0, 0); s = await state(); check(engineOf(0).muted, 'pad 1 mutes part 1 on Mix');
  core.smk(K.PAD, 0, 1); core.advance(60); core.smk(K.PAD, 0, 0); s = await state(); check(!engineOf(0).muted, 'pad 1 again lets part 1 play');
  core.smk(K.PAD, 9, 1); core.advance(60); core.smk(K.PAD, 9, 0); s = await state(); check(engineOf(1).solo, 'pad 10 (the lower row) solos part 2 on Mix');
  core.smk(K.PAD, 9, 1); core.advance(60); core.smk(K.PAD, 9, 0); s = await state(); check(!engineOf(1).solo, 'pad 10 again ends the solo');
  button('PATCH');
  s = await state(); eq(s.selected, 1, 'a pad on Mix also selects its part'); core.smk(K.ENCODER, 4, -1); core.advance(60); s = await state(); eq(s.selected, 0, 'K5 selects part 1 again');
  core.smk(K.KEY, 12, 100); core.advance(100); core.smk(K.PEDAL, 0, 127); core.smk(K.KEY, 12, 0); core.advance(400);
  check(peak() > 0.01, 'the pedal holds a note after the key is let go');
  core.smk(K.PEDAL, 0, 0); core.advance(1500); check(peak() < 0.001, 'letting go of the pedal ends the note');

  // CHORD removes the framed layer (the last layer takes the part with it), SCALE adds one, left adds and right removes: no SHIFT
  s = await state(); check(s.parts[0].used, 'part 1 exists');
  const layers = (st) => st.parts.reduce((n, p) => n + (p.used ? p.layers.length : 0), 0);
  const before = layers(s);
  button('CHORD');
  s = await state(); check(!s.parts[0].used, 'CHORD removes the last layer, and with it part 1'); eq(layers(s), before - 1, 'one layer fewer');
  button('SCALE'); s = await state(); eq(layers(s), before, 'SCALE adds a layer again');

  // SEQ REC saves a scene, and the stored scenes are the pads
  button('SEQ PLAY'); button('SEQ REC'); eq(core.screen(), SCREEN.SCENES, 'SEQ REC opens the scene name on the Scenes screen');
  button('PATCH'); s = await state(); eq(s.scene, 64, 'PATCH cancels: nothing was stored');
  button('SEQ REC'); button('SEQ REC'); s = await state(); eq(s.scene, 0, 'SEQ REC twice stores scene 1');
  eq(colours()[0], 0xffffff, 'pad 1 is bright white now: scene 1 exists, is in use and has the cursor');

  // safe boot: SCALE and CHORD held at power-on
  const safe = new DxCore(bytes);
  safe.c.sim_safe_boot(1); safe.c.sim_boot(1); safe.advance(400);
  eq(safe.screen(), SCREEN.SAFE, 'power on with SCALE and CHORD held starts safe boot');
}

console.log('test-smk37: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
