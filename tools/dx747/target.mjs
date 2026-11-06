#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
//
// Assemble the source tree of a DX747 build for another device: this repository's files (the working tree, tracked and not ignored),
// without the FM-1's sdk/ and app/board/, with the target's own SDK, board layer and application files laid over them. The build
// scripts (tools/docker, tools/portable) then run on that tree exactly as they do on the repository itself.
//
//   node tools/dx747/target.mjs --target smk37-pro --out build/smk37-pro/src
//
// targets/smk37/sdk is M-VAVE's smk37_open SDK, unmodified (it has its own manifest.json: the build checks every file against it),
// targets/smk37/board its open board layer (key, pad, fader and encoder scanner, RGB LEDs, display, audio), targets/smk37/app DX747's
// own files for that device (version.h, the controls layer). The FM-1 SDK and its sealed engine are never put in an SMK-37 tree:
// its licence is for FM-1 devices only (THIRD_PARTY.md); the SMK-37 SDK has its own grant (targets/smk37/THIRD_PARTY.smk37_open.md).
import { execFileSync } from 'node:child_process';
import { cpSync, existsSync, mkdirSync, readdirSync, rmSync, copyFileSync, statSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const args = process.argv.slice(2);
const opt = (name) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : null; };
const target = opt('--target'), out = opt('--out');
const TARGETS = { 'smk37-pro': 'smk37', 'smk37-elite': 'smk37' };
if (!TARGETS[target] || !out) { console.error('usage: target.mjs --target smk37-pro|smk37-elite --out DIR'); process.exit(2); }
const dir = join(root, 'targets', TARGETS[target]);
const dest = resolve(out);
if (!dest.includes('/build/') && !process.env.FM1_TARGET_ANYWHERE) { console.error(`error: refusing to empty ${dest}: it is not inside a build/ folder`); process.exit(1); }

const skip = (rel) => ['sdk/', 'targets/', 'app/board/', 'docs/', '.github/', '.claude/', 'build/', '.git/'].some((p) => rel.startsWith(p));
const list = execFileSync('git', ['ls-files', '-z', '--cached', '--others', '--exclude-standard'], { cwd: root, maxBuffer: 1 << 28 })
  .toString('utf8').split('\0').filter(Boolean);
rmSync(dest, { recursive: true, force: true });
let n = 0;
for (const rel of new Set(list)) {
  if (skip(rel)) continue;
  const from = join(root, rel);
  if (!existsSync(from) || !statSync(from).isFile()) continue;            // deleted in the working tree
  const to = join(dest, rel);
  mkdirSync(dirname(to), { recursive: true });
  copyFileSync(from, to); ++n;
}
// The target's layers, in this order: its SDK, its board layer, its application files.
cpSync(join(dir, 'sdk'), join(dest, 'sdk'), { recursive: true });
mkdirSync(join(dest, 'app/board'), { recursive: true });
for (const f of readdirSync(join(dir, 'board'))) copyFileSync(join(dir, 'board', f), join(dest, 'app/board', f));
if (existsSync(join(dir, 'app'))) cpSync(join(dir, 'app'), join(dest, 'app'), { recursive: true });
console.log(`${target}: ${n} files of this repository, the ${TARGETS[target]} SDK, board layer and application files -> ${dest}`);
