#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
//
// Assemble the DX747 workbench page (OUTDIR/DX747-Workbench.html): one file with the simulator, the configurator, the test
// keyboard and the firmware updater.
//
// The stock FM-1 page is built from sdk/Update.html.in by tools/portable/page.mjs; sdk/ is pinned (sdk/manifest.json) and DX747
// never edits it. This page is built from web/page/ instead and reuses sdk/WebUpdate.js (the workbench protocol: framing,
// tokens, retries, the voice library) and sdk/I18n.js unchanged. The package JSON, the notices and the source hash are made
// the way tools/portable/page.mjs makes them (which is a port of tools/Build-Web.ps1): keep the two in step.
//
//   node tools/dx747/workbench.mjs --root SRC --wasm simulator.wasm --source-url https://.../archive/<commit>.zip -o OUTDIR \
//        [--fwsc FILE.fwsc --identity build-identity.json] [--switch JSON] [--label TEXT]
//   node tools/dx747/workbench.mjs --root SRC --wasm simulator.wasm --preview -o OUTDIR
//   SMK-37: SRC is the tree tools/dx747/target.mjs assembles; with --fwsc add --model pro|elite. One page carries ONE model's firmware and M-VAVE's original
//   V16 for that model (the model is fixed by the page, never chosen in it). --label TEXT marks a preview build: a banner with TEXT, the title, and noindex.
//
// --preview (instead of --source-url) is the local preview, like tools/Build-Web.ps1 -LocalPreview: no firmware, no source address.
// With --fwsc and --identity the page also carries the firmware (and the original V15 recovery firmware, from sdk/firmware) for
// its Update tab, exactly as the stock page does. Without them the Update tab says the page has no firmware. ./build.sh runs
// this after the browser core is built (--web: without firmware, --package: with). Node 18+, no packages.
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { dirname, join, relative, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

const fail = (message) => { throw new Error(message); };
const sha256 = (data) => createHash('sha256').update(data).digest('hex');
const shaFile = (path) => sha256(readFileSync(path));
const readText = (path) => readFileSync(path, 'utf8').replace(/^﻿/, '');
const b64 = (data) => Buffer.from(data).toString('base64');
const compact = (value) => JSON.stringify(value);

/** System.Net.WebUtility.HtmlEncode: & < > " ' and U+00A0..U+00FF, and non-BMP characters, numerically. */
function htmlEncode(text) {
  let out = '';
  for (const ch of text) {
    const c = ch.codePointAt(0);
    if (ch === '&') out += '&amp;';
    else if (ch === '<') out += '&lt;';
    else if (ch === '>') out += '&gt;';
    else if (ch === '"') out += '&quot;';
    else if (ch === "'") out += '&#39;';
    else if ((c >= 160 && c <= 255) || c >= 0x10000) out += `&#${c};`;
    else out += ch;
  }
  return out;
}
function walk(dir) {
  const files = [];
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const full = join(dir, entry.name);
    if (entry.isDirectory()) files.push(...walk(full));
    else if (entry.isFile()) files.push(full);
  }
  return files;
}
/** The hash tools/Build-Web.ps1 gives the browser sources and records in the page (as tools/portable/page.mjs computes it). */
function sourceHash(root) {
  const inputs = new Map();
  for (const top of ['app', 'web', 'third_party/synth']) {
    for (const file of walk(join(root, top))) {
      const rel = relative(root, file).split(sep).join('/');
      const ext = file.slice(file.lastIndexOf('.'));
      if (!rel.startsWith('app/board/') && ['.c', '.h', '.inc'].includes(ext)) inputs.set(rel, shaFile(file));
    }
  }
  const extra = ['tools/Build-Web.ps1', ...readdirSync(join(root, 'sdk/include')).filter((n) => n.endsWith('.h')).map((n) => `sdk/include/${n}`)];
  for (const rel of extra) inputs.set(rel, shaFile(join(root, rel)));
  const names = [...inputs.keys()].sort();
  return sha256(names.map((n) => `${n}:${inputs.get(n)}`).join('\n'));
}

const SCRIPTS = [['/*DX_I18N*/', 'sdk/I18n.js'], ['/*DX_WEBUPDATE*/', 'sdk/WebUpdate.js'], ['/*DX_CORE*/', 'web/page/dx747-core.js'],
  ['/*DX_DEVICE*/', 'web/page/dx747-device.js'], ['/*DX_CLIENT*/', 'web/page/dx747-client.js'], ['/*DX_CONTROLS*/', 'web/page/dx747-controls.js'],
  ['/*DX_SIM*/', 'web/page/dx747-sim.js'], ['/*DX_KEYBOARD*/', 'web/page/dx747-keyboard.js'], ['/*DX_UI*/', 'web/page/dx747-ui.js'],
  ['/*DX_UPDATE*/', 'web/page/dx747-update.js']];

/** The original M-VAVE recovery firmwares, by model, with the hashes sdk/Build.ps1 pins (the SMK-37 ones: targets/smk37/sdk/Build.ps1 -Package). */
const SMK_STOCK = {
  pro: { file: 'sdk/firmware/SMK37-Pro-V16.fwsc', hash: '2d98ce72530e71d617384963423820536f094501299e2cfec0a8d3c3fb6bcfb0', name: 'Pro', family: 8 },
  elite: { file: 'sdk/firmware/SMK37-Elite-V16.fwsc', hash: 'b2ee38abfef3ae922e4eecdb8a551e14345300ae6f270265feacd33b6ef99cb6', name: 'Elite', family: 9 },
};
const VENDOR_LOADER = 'ef1f2db89b011fa53e71df363f0122de0ea896618c53fb53e8da676322004541';

/** The firmware package and the original recovery firmware (the FM-1's V15, or the SMK-37's V16 for model pro or elite) as JSON, as Build.ps1 -Package embeds them. */
function packageJson(root, fwscPath, identityPath, release, product, model) {
  const identity = JSON.parse(readText(identityPath));
  if (!/^[0-9a-f]{64}$/.test(identity.build_id) || identity.release_version !== release) fail('Firmware and web release versions differ, or the source identifier is invalid.');
  const fwsc = readFileSync(fwscPath);
  let stock, family = 7, extra = {};
  if (model) {
    const m = SMK_STOCK[model] ?? fail('--model must be pro or elite.');
    const stockPath = join(root, m.file);
    if (!existsSync(stockPath) || shaFile(stockPath) !== m.hash) fail(`Matching SDK original V16 recovery firmware for the ${m.name} is missing or invalid.`);
    const stockBytes = readFileSync(stockPath);
    family = m.family; extra = { model };
    stock = { schema: 1, kind: 'stock', product: `SMK-37 ${m.name}_016`, model, profile: `SMK37 ${m.name} Original V16`, family, release_version: 'V16',
      build_id: m.hash, sha256: m.hash, bytes: stockBytes.length, loader_sha256: VENDOR_LOADER, data: b64(stockBytes) };
  } else {
    const stockPath = join(root, 'sdk/firmware/FM-1_015.fwsc');
    const stockHash = 'db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a';
    if (!existsSync(stockPath) || shaFile(stockPath) !== stockHash) fail('Matching SDK original V15 recovery firmware is missing or invalid.');
    const stockBytes = readFileSync(stockPath);
    stock = { schema: 1, kind: 'stock', product: 'FM-1_015', profile: 'M-VAVE Original V15', family: 7, release_version: 'V15',
      build_id: stockHash, sha256: stockHash, bytes: stockBytes.length, loader_sha256: VENDOR_LOADER, data: b64(stockBytes) };
  }
  return compact({ schema: 1, product, profile: identity.profile ?? 'dx7', family, release_version: release, build_id: identity.build_id,
    sha256: sha256(fwsc), bytes: fwsc.length, loader_sha256: shaFile(join(root, 'sdk/tools/pack/ota.bin')), hardware_validated: false, data: b64(fwsc), ...extra, stock });
}

function parseArgs(argv) {
  const opts = { preview: false };
  const flags = new Set(['--root', '--wasm', '--source-url', '-o', '--fwsc', '--identity', '--switch', '--model', '--label']);
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--preview') opts.preview = true;
    else if (flags.has(argv[i])) opts[argv[i]] = argv[++i];
    else return null;
  }
  if (opts.preview) return ['--root', '--wasm', '-o'].every((k) => opts[k]) && !opts['--fwsc'] && !opts['--source-url'] ? opts : null;
  return ['--root', '--wasm', '--source-url', '-o'].every((k) => opts[k]) && !!opts['--fwsc'] === !!opts['--identity'] ? opts : null;
}

/** A label that marks a preview build (a banner, the title, noindex): short, and no character that could leave a script or an attribute. */
function previewLabel(text) {
  if (text === undefined) return null;
  if (!/^[A-Za-z0-9 ._:\/@#()+,-]{1,80}$/.test(text)) fail('--label wants 1 to 80 of: letters, digits, space and . _ : / @ # ( ) + , -');
  return text;
}

/** The device switch of the GitHub Pages site: {"self":KEY,KEY:ADDRESS,...} with KEY one of fm1, smk37, smk37-pro, smk37-elite and the addresses relative
 *  (the other pages of the same site); none: no switch. */
function deviceSwitch(json) {
  if (!json) return 'const DX_SWITCH = null;';
  const s = JSON.parse(json);
  const relative = /^(\.\.?\/)+([a-z0-9-]+\/)*$|^\.\/$/;
  const keys = Object.keys(s).filter((k) => k !== 'self');
  const known = ['fm1', 'smk37', 'smk37-pro', 'smk37-elite'];
  if (!keys.length || !keys.includes(s.self) || keys.some((k) => !known.includes(k) || !relative.test(s[k]))) fail('--switch wants {"self":KEY,KEY:ADDRESS,...} with KEY one of fm1, smk37, smk37-pro, smk37-elite, self one of them, and relative addresses such as ./ ../ ./smk37-pro/');
  return `const DX_SWITCH = ${compact(s)};`;
}

/** M-VAVE's terms for the SMK-37 SDK: the page must carry them (targets/smk37/THIRD_PARTY.smk37_open.md: "include this file"). Read from this repository, since
 *  tools/dx747/target.mjs leaves targets/ out of the SMK-37 build tree. */
function smk37Notices() {
  const here = resolve(dirname(fileURLToPath(import.meta.url)), '../../targets/smk37');
  return ['THIRD_PARTY.smk37_open.md', 'LICENSE.smk37_open.md'].map((name) => {
    const file = join(here, name);
    if (!existsSync(file)) fail(`${file} is missing: the SMK-37 page must carry M-VAVE's SDK terms.`);
    return `\n===== ${name} =====\n${readText(file)}`;
  });
}

function build(opts) {
  const root = resolve(opts['--root']);
  const url = opts['--source-url'] ?? '';
  if (!opts.preview) {
    let parsed;
    try { parsed = new URL(url); } catch { fail('The source URL must be an HTTPS URL without credentials.'); }
    if (parsed.protocol !== 'https:' || parsed.username || parsed.password || /[\r\n]/.test(url)) fail('The source URL must be an HTTPS URL without credentials.');
  }
  const wasm = readFileSync(opts['--wasm']);
  const versionH = readText(join(root, 'app/version.h'));
  const release = /^#define FM1_RELEASE_VERSION "(V[0-9]{2,3})"/m.exec(versionH)?.[1] ?? 'V00';
  const smk37 = /^#define FM1_UPDATE_PRODUCT_(PRO|ELITE) "/m.test(versionH);          // the SMK-37 tree (tools/dx747/target.mjs): one identity per model
  const model = opts['--model'];
  if (smk37 && opts['--fwsc'] && !['pro', 'elite'].includes(model)) fail('The SMK-37 page carries one model: pass --model pro or --model elite.');
  if (!smk37 && model) fail('--model is for the SMK-37 tree only.');
  const product = smk37
    ? (model ? new RegExp(`^#define FM1_UPDATE_PRODUCT_${model.toUpperCase()} "(SMK-37 ${model === 'pro' ? 'Pro' : 'Elite'}_8[0-9]{6})"[ \\t]*$`, 'm').exec(versionH)?.[1] : undefined)
    : /^#define FM1_UPDATE_PRODUCT "(FM-1_8[0-9]{6})"[ \t]*$/m.exec(versionH)?.[1];
  const label = previewLabel(opts['--label']);
  const simulation = compact({ schema: 1, release_version: release, sha256: sha256(wasm), source_sha256: sourceHash(root), data: b64(wasm) });
  const notices = [`APP: GPLv3 with the FM1 SDK linking exception; no warranty. Source: ${url}`];
  for (const name of ['THIRD_PARTY.md', 'LICENSE.md']) notices.push(`\n===== ${name} =====\n${readText(join(root, name))}`);
  if (smk37) notices.push(...smk37Notices());
  let page = readText(join(root, 'web/page/dx747.html.in'));
  if (label) page = page.replace('<title>DX747 workbench</title>', '<title>DX747 workbench (preview)</title>\n<meta name="robots" content="noindex">');
  if (opts.preview) page = page.replace(/<a\b[^>]*href="\/\*DX_SOURCE_URL\*\/"[^>]*>(.*?)<\/a>/g, '<span>(local preview, no source address)</span>');
  let payload = 'const FM1_PACKAGE = null;';
  if (opts['--fwsc']) {
    if (!product) fail(smk37 ? `Set FM1_UPDATE_PRODUCT_${model.toUpperCase()} in app/version.h.` : 'Set FM1_UPDATE_PRODUCT in app/version.h.');
    payload = `const FM1_PACKAGE = ${packageJson(root, opts['--fwsc'], opts['--identity'], release, product, smk37 ? model : undefined)};`;
  }
  const replacements = [['/*DX_PAYLOAD*/', payload], ['/*DX_SOURCE_URL*/', htmlEncode(url)], ['/*DX_NOTICES*/', htmlEncode(notices.join('\n'))],
    ['/*DX_SIMULATOR*/', `const DX_SIMULATOR = ${simulation};`], ['/*DX_SWITCH*/', deviceSwitch(opts['--switch'])],
    ['/*DX_LABEL*/', `const DX_LABEL = ${compact(label)};`]];
  for (const [key, path] of SCRIPTS) {
    const text = readText(join(root, path));
    if (/<\/script/i.test(text)) fail(`${path} contains a closing script tag`);
    replacements.push([key, text]);
  }
  for (const [key, value] of replacements) page = page.split(key).join(value);
  const left = /\/\*DX_[A-Z_]+\*\//.exec(page);
  if (left) fail(`Unexpanded placeholder: ${left[0]}`);
  mkdirSync(opts['-o'], { recursive: true });
  const out = join(opts['-o'], 'DX747-Workbench.html');
  writeFileSync(out, page, 'utf8');
  console.log(`DX747 workbench: ${out}`);
  console.log(`sha256 ${shaFile(out)}   (${statSync(out).size} bytes, simulator ${sha256(wasm).slice(0, 16)}...)`);
}

const opts = parseArgs(process.argv.slice(2));
if (!opts) { console.error('usage: workbench.mjs --root SRC --wasm simulator.wasm --source-url URL -o OUTDIR [--fwsc FILE.fwsc --identity build-identity.json [--model pro|elite]] [--switch JSON] [--label TEXT]'); process.exit(2); }
try { build(opts); } catch (error) { console.error(`error: ${error.message}`); process.exit(1); }
