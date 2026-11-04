#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
//
// Runs the real workbench page code (sdk/WebUpdate.js, the Library class that the page's "Projects and
// voices" tab uses) against the real DX747 firmware in the native simulator, so that what the page needs
// from the unit is tested end to end: connect, the checks refreshLibrary makes before it unlocks the
// bank controls, reading all four DX7 banks, importing a 32-voice bank (the ROM import path) and
// reading it back, and a voice download and upload.
//
//   node tools/dx747/sim/page-flow.mjs [--keep]      (builds the native simulator first if it is stale)
//
// The simulator side is the harness command `wbserve` (tools/dx747/sim/native/harness.c): it reads one SysEx
// message per line as hex from stdin, delivers it as USB MIDI and prints the unit's reply. Fake MIDI
// ports here carry the page's bytes to it. The page's refreshLibrary lives in sdk/Update.html.in together
// with the DOM, so its checks are mirrored below; the page text is checked for the lines mirrored, so that
// a change to the page makes this test fail loudly rather than drift.
import { spawn, execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { createInterface } from 'node:readline';
import { tmpdir } from 'node:os';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
execFileSync(join(root, 'tools/dx747/sim/native/ensure-built.sh'), { stdio: 'inherit' });
const harness = join(root, 'tools/dx747/sim/native/dx747-sim');
const keep = process.argv.includes('--keep');

let failures = 0, checks = 0;
function check(ok, what) { ++checks; if (!ok) { ++failures; console.error('FAIL: ' + what); } }
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const hex = bytes => Array.from(bytes, x => x.toString(16).padStart(2, '0')).join('');

// ---- the page code -----------------------------------------------------------------------
vm.runInThisContext(readFileSync(join(root, 'sdk/WebUpdate.js'), 'utf8'), { filename: 'sdk/WebUpdate.js' });
const Web = globalThis.FM1Web;
const html = readFileSync(join(root, 'sdk/Update.html.in'), 'utf8');
for (const line of ['info[1]!==16', 'library.query(9,[slot],true)', 'await library.download(0)', 'FM1Web.projectVersion(working)',
                    'working.slice(3836,3848)', 'working.length!==(library.tuningBytes||(info[3]|info[4]<<8))']) {
  check(html.includes(line), 'sdk/Update.html.in no longer contains the mirrored line: ' + line);
}

// Mirrors refreshLibrary in sdk/Update.html.in (the part that can reject the unit).
async function refreshLibrary(library) {
  const info = await library.query(0);
  if (info.length !== 14 || info[1] !== 16 || info[2] >= 16 ||
      !((info[0] === 3 && info[3] === 208 && info[4] === 15) || (info[0] === 4 && info[3] === 224 && info[4] === 15)) ||
      info.slice(6).some(source => source > 5)) throw Error('device project library info invalid');
  const names = [];
  for (let slot = 0; slot < info[1]; slot++) names.push(String.fromCharCode(...await library.query(9, [slot], true)));
  const working = await library.download(0);
  if (working.length !== (library.tuningBytes || (info[3] | info[4] << 8))) throw Error('device file length invalid');
  Web.projectVersion(working);
  return { info, names, projectName: String.fromCharCode(...working.slice(3836, 3848)).trimEnd() };
}

// ---- the unit: a native simulator process behind fake MIDI ports ---------------------------
function startUnit(script) {
  const dir = mkdtempSync(join(tmpdir(), 'dx747-page-'));
  const file = join(dir, 'script.txt');
  writeFileSync(file, script + '\nwbserve\n');
  const child = spawn(harness, [dir, file], { stdio: ['pipe', 'pipe', 'inherit'] });
  const replies = [], waiting = [];
  createInterface({ input: child.stdout }).on('line', line => {
    if (!line.startsWith('R ')) return;
    const text = line.slice(2);
    if (waiting.length) waiting.shift()(text); else replies.push(text);
  });
  const ask = bytes => new Promise(resolveReply => {
    waiting.push(resolveReply);
    child.stdin.write(hex(bytes) + '\n');
  });
  const listeners = new Set();
  let chain = Promise.resolve();
  const input = {
    name: 'FM-1', manufacturer: 'M-VAVE', state: 'connected', connection: 'closed', type: 'input',
    addEventListener: (type, fn) => { if (type === 'midimessage') listeners.add(fn); },
    removeEventListener: (type, fn) => { if (type === 'midimessage') listeners.delete(fn); },
    open: async () => input, close: async () => input,
  };
  const output = {
    name: 'FM-1', manufacturer: 'M-VAVE', state: 'connected', connection: 'closed', type: 'output',
    open: async () => output, close: async () => output,
    send(bytes) {
      chain = chain.then(async () => {
        const reply = await ask(bytes);
        if (reply !== 'none') for (const fn of [...listeners]) fn({ data: Uint8Array.from(Buffer.from(reply, 'hex')) });
      });
    },
  };
  const access = { inputs: new Map([['in', input]]), outputs: new Map([['out', output]]) };
  const stop = async () => {
    child.stdin.write('\n'); child.stdin.end();
    await new Promise(r => child.on('close', r));
    if (!keep) rmSync(dir, { recursive: true, force: true });
  };
  return { access, stop };
}

function distinctBank(raw, tag) {
  const out = Uint8Array.from(raw);
  for (let k = 0; k < 32; k++) {
    const name = (tag + String(k + 1).padStart(2, '0')).padEnd(10, ' ');
    for (let i = 0; i < 10; i++) out[k * 128 + 118 + i] = name.charCodeAt(i);
  }
  return out;
}

async function scenario(title, script) {
  console.log('== ' + title);
  const unit = startUnit(script);
  const library = new Web.Library(unit.access);
  try {
    await library.connect();
    check(true, 'connect');
    const state = await refreshLibrary(library);
    check(state.names.length === 16, 'sixteen project slot names');
    check(state.info[0] === 4 && state.info.slice(6, 14).every(s => s === 0), 'all eight tracks report the DX7 source');
    check(library.programs === 128 && library.bankAvailable, 'the unit offers the 128-voice library to the page');

    const before = [];
    for (let g = 0; g < 4; g++) before.push(await library.readBank(g));
    check(before.every(b => b.length === 4096), 'four banks read');

    // The ROM import path: the page validates the bank, uploads it and compares the read-back.
    const rom = distinctBank(before[0], 'ROM');
    const saved = await library.writeBank(3, rom);
    check(same(saved, rom), 'bank D read back equals the imported bank');
    check(same(await library.readBank(3), rom), 'bank D still equals it on a second read');
    for (let g = 0; g < 3; g++) check(same(await library.readBank(g), before[g]), 'bank ' + 'ABC'[g] + ' unchanged by the import');
    const names = Web.readVoices(Web.bankFile(await library.readBank(3))).map(v => String.fromCharCode(...v.slice(145, 155)));
    check(names[0] === 'ROM01     ' && names[31] === 'ROM32     ', 'the imported voices carry their names');

    // Put the factory bank back, the way a user would.
    await library.writeBank(3, before[3]);
    check(same(await library.readBank(3), before[3]), 'bank D restored');

    // The page's "import track timbre" button must not be able to turn an engine into another source.
    for (const type of [1, 3, 5]) {
      const timbre = new Uint8Array(223); timbre[0] = type;
      let refused = false;
      try { await library.upload(2, 3, timbre); } catch { refused = true; }
      check(refused, 'a timbre of source ' + type + ' is refused');
    }
    const after = await library.query(0);
    check(after.slice(6, 14).every(s => s === 0), 'every track still reports the DX7 source after the refused timbres');

    // A single voice, as the page's voice editor moves it.
    const voice = await library.download(1, 0);
    check(voice.length === 156, 'voice 156 bytes');
    await library.upload(1, 0, voice);
    check(same(await library.download(1, 0), voice), 'voice upload of the downloaded voice is stable');
  } catch (error) {
    check(false, title + ': ' + (error && error.stack || error));
  } finally {
    await library.disconnect().catch(() => {});
    await unit.stop();
  }
}

await scenario('freshly installed over a stock sample bank', 'boot\nrun 400');
await scenario('scene flash already claimed (erased)', 'boot\nwipe\nrun 400');
await scenario('after a first scene has been saved', 'boot\nwipe\nrun 400\ntap btn 11\nwhite 2\ntap btn 9\ntap btn 9\nrun 200');

console.log(`${checks} checks, ${failures} failures`);
process.exit(failures ? 1 : 0);
