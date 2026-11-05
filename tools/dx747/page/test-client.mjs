#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
//
// Runs the configurator's own code (web/page/dx747-core.js, dx747-device.js, dx747-client.js, on sdk/WebUpdate.js) against the
// real firmware compiled to WebAssembly, through the same virtual MIDI port the page uses for its simulator. What the page
// needs from the unit is tested end to end: hello, the state image, every setter, the scene commands, the voice library and the
// error messages. No browser is needed.
//
//   node tools/dx747/page/test-client.mjs [--wasm FILE]     (builds the browser core with tools/dx747/sim/build.sh when no file is given)
import { execFileSync } from 'node:child_process';
import { readFileSync, mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
const wasmArg = process.argv.indexOf('--wasm');
let wasmPath = wasmArg > 0 ? process.argv[wasmArg + 1] : null;
if (!wasmPath) {
  const out = mkdtempSync(join(tmpdir(), 'dx747-core-'));
  wasmPath = execFileSync(join(root, 'tools/dx747/sim/build.sh'), ['-o', out, root], { encoding: 'utf8' }).trim().split('\n').pop();
}
for (const f of ['sdk/WebUpdate.js', 'web/page/dx747-core.js', 'web/page/dx747-device.js', 'web/page/dx747-client.js'])
  vm.runInThisContext(readFileSync(join(root, f), 'utf8'), { filename: f });
const { DxCore, DxDevice, Dx747 } = globalThis;

let checks = 0, failures = 0;
function check(ok, what) { ++checks; if (!ok) { ++failures; console.error('FAIL: ' + what); } }
const eq = (got, want, what) => check(got === want, what + ': wanted ' + JSON.stringify(want) + ', got ' + JSON.stringify(got));
async function refused(promise, pattern, what) {
  try { await promise; check(false, what + ': should have been refused'); }
  catch (error) { check(pattern.test(error.message), what + ': message "' + error.message + '" does not match ' + pattern); }
}

const core = new DxCore(readFileSync(wasmPath));
core.prepareStore();                          // a unit whose scene store has been set up
core.advance(400);
const access = new DxDevice.VirtualMidiAccess(core, 'FM-1 DX747 (simulator)', 6);
const dx = new Dx747(access);

// ---- hello and the state image --------------------------------------------------------------
const hello = await dx.connect();
eq(hello.protocol, 1, 'protocol'); eq(hello.parts, 8, 'parts'); eq(hello.layers, 8, 'layers'); eq(hello.scenes, 64, 'scenes');
eq(hello.enginesUp, 255, 'engines up'); eq(hello.release, 'V01', 'release');
let s = await dx.readState();
eq(s.selected, 0, 'selected part'); eq(s.scene, 64, 'no current scene');
for (let p = 0; p < 8; p++) { eq(s.parts[p].channel, p + 1, 'part ' + p + ' channel'); eq(s.parts[p].layers.length, 1, 'part ' + p + ' layers'); eq(s.parts[p].layers[0].engine, p, 'part ' + p + ' engine'); }
eq(s.engines[0].voice, 'PIANO 1', 'voice name'); eq(s.engines[0].low, 0, 'low'); eq(s.engines[0].high, 127, 'high'); eq(s.engines[0].shift, 0, 'shift');
eq(s.engines[0].velLow, 1, 'velocity low'); eq(s.parts[0].level, 127, 'part level');
const crc0 = (await dx.crc()).crc;

// ---- layer settings ---------------------------------------------------------------------------
await dx.setLayer(0, 5, 60); await dx.setLayer(0, 6, 72); await dx.setLayer(0, 7, 76);
await dx.setLayer(0, 9, 20); await dx.setLayer(0, 10, 100); await dx.setLayer(0, 0, 64);
await dx.setLayer(0, 1, 70); await dx.setLayer(0, 2, 60); await dx.setLayer(0, 3, 9); await dx.setLayer(0, 4, 4); await dx.setLayer(0, 11, 4); await dx.setLayer(0, 8, 2);
s = await dx.readState();
const e0 = s.engines[0];
eq(e0.low, 60, 'low 60'); eq(e0.high, 72, 'high 72'); eq(e0.shift, 12, 'shift +12'); eq(e0.velLow, 20, 'vel low'); eq(e0.velHigh, 100, 'vel high');
eq(e0.level, 64, 'level'); eq(e0.pan, 20, 'pan'); eq(e0.bright, 10, 'bright'); eq(e0.reverb, 9, 'reverb'); eq(e0.delay, 4, 'delay'); eq(e0.poly, 4, 'poly'); eq(e0.fold, 2, 'fold');
check((await dx.crc()).crc !== crc0, 'the CRC changes with the state');
await dx.setLayer(0, 12, 1); s = await dx.readState(); check(s.engines[0].muted, 'muted'); await dx.setLayer(0, 12, 0);
await dx.setLayer(0, 13, 1); s = await dx.readState(); check(s.engines[0].solo && !s.engines[1].audible, 'solo silences the others'); await dx.setLayer(0, 13, 0);
await dx.setLayer(0, 14, 4); core.advance(100); s = await dx.readState(); eq(s.engines[0].program, 4, 'program'); eq(s.engines[0].voice, 'PIANO 2', 'voice name after the program change');
await dx.setLayer(0, 14, 0); core.advance(100);
await refused(dx.setLayer(0, 5, 128), /refused/, 'key out of range'); await refused(dx.setLayer(9, 0, 1), /refused/, 'engine out of range');
await refused(dx.setLayer(0, 15, 1), /refused/, 'unknown field');

// ---- parts ---------------------------------------------------------------------------------------
await dx.setPart(0, 0, 80); await dx.setPart(0, 1, 10);
s = await dx.readState(); eq(s.parts[0].level, 80, 'part level'); eq(s.parts[0].channel, 10, 'part channel');
await refused(dx.setPart(1, 1, 3), /refused/, 'a channel another part has'); await dx.setPart(0, 1, 1);
await refused(dx.partOp(1, 0), /No free engine/, 'a layer with no engine free');
eq(await dx.partOp(2, 7, 0), 7, 'removing part 8 frees engine 8');
eq(await dx.partOp(1, 0), 7, 'a second layer on part 1');
s = await dx.readState(); eq(s.parts[0].layers.length, 2, 'two layers'); eq(s.parts[7].used, false, 'part 8 gone');
await dx.partOp(4, 0, 1); s = await dx.readState(); eq(s.parts[0].selectedLayer, 1, 'framed layer');
await dx.partOp(2, 0, 1); await dx.partOp(0, 7); s = await dx.readState(); eq(s.parts[0].layers.length, 1, 'one layer again'); eq(s.parts[7].channel, 8, 'part 8 back');
await dx.partOp(3, 2); s = await dx.readState(); eq(s.selected, 2, 'selected part');  await dx.partOp(3, 0);
await refused(dx.partOp(0, 0), /No free part slot/, 'a part in an occupied slot');
await dx.partName(0, 'BASS LINE'); s = await dx.readState(); eq(s.parts[0].name, 'BASS LINE', 'part name');

// ---- filter, effects, settings -----------------------------------------------------------------
await dx.setMaster(0, 2); await dx.setMaster(1, 1500); await dx.setMaster(2, 40); await dx.setMaster(3, 50);
s = await dx.readState(); eq(s.filter.type, 2, 'filter type'); eq(s.filter.hz, 1500, 'cutoff'); eq(s.filter.resonance, 40, 'resonance'); eq(s.effects.decay, 50, 'decay');
await refused(dx.setMaster(1, 10), /refused/, 'cutoff below 20 Hz');
await dx.setSetting(0, 5); await dx.setSetting(1, 10); await dx.setSetting(2, 6);
s = await dx.readState(); eq(s.settings.sceneChannel, 5, 'scene channel'); eq(s.settings.masterChannel, 10, 'master channel'); eq(s.settings.backlight, 6, 'button light');
await dx.setSetting(0, 0); await dx.setSetting(1, 0);

// ---- scenes --------------------------------------------------------------------------------------
await dx.sceneSave(0, 'CFG TEST'); s = await dx.readState(); eq(s.scene, 0, 'current scene after a save');
let list = await dx.scenes(); eq(list[0].name, 'CFG TEST', 'scene name in the list'); eq(list[0].parts, 255, 'parts in the scene'); eq(list[1].name, '', 'an empty slot');
await refused(dx.sceneSave(1, 'TOO SOON'), /busy/, 'a second flash write inside 1.5 s');
core.advance(1600);
await dx.sceneRename(0, 'RENAMED'); list = await dx.scenes(); eq(list[0].name, 'RENAMED', 'renamed');
core.advance(1600);
await dx.setLayer(0, 0, 32); await dx.sceneRecall(0); core.advance(300); s = await dx.readState(); eq(s.engines[0].level, 64, 'a recall restores the level');
await refused(dx.sceneRecall(1), /empty/, 'recalling an empty slot');
core.advance(1600);
await dx.sceneDelete(0); list = await dx.scenes(); eq(list[0].name, '', 'deleted');

// ---- the voice library and ROM import ----------------------------------------------------------
const names = await dx.voiceNames(0);
eq(names.length, 32, 'bank names'); eq(names[0], 'PIANO 1', 'first voice of bank A');
const raw = await dx.run(() => dx.lib.readBank(0));
const file = new Uint8Array(4104); file.set([0xf0, 0x43, 0x00, 0x09, 0x20, 0x00]); file.set(raw, 6);
let sum = 0; for (const v of raw) sum = (sum + v) & 127; file[4102] = (128 - sum) & 127; file[4103] = 0xf7;
await dx.importRom(3, file);
const imported = await dx.voiceNames(3); check(imported.join('|') === names.join('|'), 'bank D holds the imported names');
file[100] ^= 1;
try { dx.importRom(3, file); check(false, 'a corrupt bank file must be refused'); } catch (error) { check(/checksum/.test(error.message), 'checksum message'); }

await dx.disconnect();
console.log(`${checks} checks, ${failures} failures`);
process.exit(failures ? 1 : 0);
