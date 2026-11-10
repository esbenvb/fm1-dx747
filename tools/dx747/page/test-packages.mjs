// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
//
// The firmware INSIDE a built workbench page, checked the way the page checks it before it writes anything to a unit: the package JSON is read out of the
// page and given to the SDK's own FM1Web.prepare (hash, identity, model, family, loader), and so is the original firmware it offers as the way back. It also
// checks what must never go wrong in a page for testers: a Pro page holds the Pro firmware and the Pro V16 and nothing of the Elite (and the other way round),
// the SMK-37 SDK's terms are in the page's notices, and a preview carries its label and a source address. No unit and no firmware build is needed, only pages.
//
//   node tools/dx747/page/test-packages.mjs [--fm1 PAGE] [--smk37-pro PAGE] [--smk37-elite PAGE] [--label TEXT]
// --label TEXT: the pages must be labelled as previews with exactly TEXT (omit it for pages that must not be).
import { existsSync, readFileSync } from 'node:fs';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
let checks = 0, failures = 0;
function check(ok, what) { ++checks; if (!ok) { ++failures; console.error('FAIL: ' + what); } }
const eq = (got, want, what) => check(JSON.stringify(got) === JSON.stringify(want), what + ': wanted ' + JSON.stringify(want) + ', got ' + JSON.stringify(got));

const args = process.argv.slice(2), opt = (name) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : undefined; };
const pages = [['fm1', opt('--fm1')], ['pro', opt('--smk37-pro')], ['elite', opt('--smk37-elite')]].filter(([, p]) => p);
const label = opt('--label');
if (!pages.length) { console.error('usage: test-packages.mjs [--fm1 PAGE] [--smk37-pro PAGE] [--smk37-elite PAGE] [--label TEXT]'); process.exit(2); }

/** The SDK's WebUpdate.js of one device, in a context of its own: FM1Web.prepare is the page's own validation. */
function sdk(dir) {
  const context = { crypto: globalThis.crypto, TextDecoder, TextEncoder, URL, setTimeout, clearTimeout, console, atob: globalThis.atob, btoa: globalThis.btoa, AbortController, fetch: undefined };
  context.globalThis = context; vm.createContext(context);
  for (const f of ['I18n.js', 'WebUpdate.js']) vm.runInContext(readFileSync(join(root, dir, f), 'utf8'), context, { filename: join(dir, f) });
  return context.FM1Web;
}
const HASH = { proV16: '2d98ce72530e71d617384963423820536f094501299e2cfec0a8d3c3fb6bcfb0', eliteV16: 'b2ee38abfef3ae922e4eecdb8a551e14345300ae6f270265feacd33b6ef99cb6', fm1V15: 'db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a' };

for (const [kind, file] of pages) {
  const name = kind === 'fm1' ? 'FM-1 page' : 'SMK-37 ' + kind + ' page';
  if (!existsSync(file)) { check(false, name + ': ' + file + ' does not exist'); continue; }
  const html = readFileSync(file, 'utf8');
  const at = html.indexOf('const FM1_PACKAGE = '), end = at < 0 ? -1 : html.indexOf(';</script>', at);
  const json = at < 0 || end < 0 ? '' : html.slice(at + 'const FM1_PACKAGE = '.length, end);
  check(json !== '' && json !== 'null', name + ' has firmware inside');
  if (json === '' || json === 'null') continue;
  const meta = JSON.parse(json);
  const FM1Web = sdk(kind === 'fm1' ? 'sdk' : 'targets/smk37/sdk');

  // the firmware: the SDK's own check (hash, identity shape, loader, model against family)
  let pkg = null, stock = null;
  try { pkg = await FM1Web.prepare(meta); } catch (e) { check(false, name + ': the firmware fails the page\'s own check: ' + e.message); }
  check(!!pkg, name + ': FM1Web.prepare accepts the firmware');
  try { stock = await FM1Web.prepare(meta.stock); } catch (e) { check(false, name + ': the way-back firmware fails the page\'s own check: ' + e.message); }
  check(!!stock, name + ': FM1Web.prepare accepts the original firmware offered as the way back');
  eq(meta.hardware_validated, false, name + ' says its firmware is not validated on a unit');
  eq(meta.release_version, 'V01', name + ' release');

  if (kind === 'fm1') {
    eq([meta.product, meta.family, meta.stock.product, meta.stock.sha256], ['FM-1_8747747', 7, 'FM-1_015', HASH.fm1V15], name + ': identity, family and the V15 way back');
    check(meta.model === undefined, name + ' has no SMK-37 model');
    check(!html.includes(HASH.proV16) && !html.includes(HASH.eliteV16), name + ' holds no SMK-37 firmware');
    check(!html.includes('===== THIRD_PARTY.smk37_open.md ====='), name + ' does not carry the SMK-37 SDK notice (it has no SMK-37 SDK in it)');
  } else {
    const [Name, family, mine, other] = kind === 'pro' ? ['Pro', 8, HASH.proV16, HASH.eliteV16] : ['Elite', 9, HASH.eliteV16, HASH.proV16];
    eq([meta.model, meta.family, meta.product, meta.stock.model, meta.stock.family, meta.stock.product, meta.stock.sha256],
      [kind, family, 'SMK-37 ' + Name + '_8747747', kind, family, 'SMK-37 ' + Name + '_016', mine], name + ': model, family, identity and the V16 way back all belong to the ' + Name);
    eq([pkg && pkg.model, stock && stock.model], [kind, kind], name + ': the prepared packages are the ' + Name);
    check(html.includes(mine), name + ' holds the ' + Name + ' V16');
    // (the SDK script lists both models' V16 hashes: what must be absent is the other model's firmware itself)
    const otherFile = join(root, 'targets/smk37/sdk/firmware', kind === 'pro' ? 'SMK37-Elite-V16.fwsc' : 'SMK37-Pro-V16.fwsc');
    check(!html.includes(readFileSync(otherFile).subarray(0, 600).toString('base64')), name + ' holds none of the other model\'s V16 firmware (' + other.slice(0, 8) + ')');
    check(!html.includes('SMK-37 ' + (kind === 'pro' ? 'Elite' : 'Pro') + '_8747747') && !html.includes('SMK-37 ' + (kind === 'pro' ? 'Elite' : 'Pro') + '_016'), name + ' names no identity of the other model');
    check(html.includes('===== THIRD_PARTY.smk37_open.md =====') && html.includes('===== LICENSE.smk37_open.md ====='), name + ' carries M-VAVE\'s SMK-37 SDK terms in its notices');
    check(html.includes('M-VAVE SMK37 Pro and Elite devices'), name + ': the notice text of the SDK grant is in the page');
  }
  // what a page for testers must carry
  const src = /<a href="(https:\/\/github\.com\/[^"]+\/archive\/[0-9a-f]{40}\.zip)"/.exec(html);
  check(!!src, name + ' has a source address (an archive of one commit)');
  if (label !== undefined) {
    check(html.includes('const DX_LABEL = ' + JSON.stringify(label) + ';'), name + ' carries the preview label ' + JSON.stringify(label));
    check(html.includes('<meta name="robots" content="noindex">'), name + ' asks search engines not to index it');
    check(html.includes('<title>DX747 workbench (preview)</title>'), name + ' has a preview title');
  } else check(html.includes('const DX_LABEL = null;'), name + ' is not labelled as a preview');
  check(/id="device"/.test(html), name + ' has the Device menu');
  check(!/Unexpanded|\/\*DX_[A-Z_]+\*\//.test(html), name + ' has no placeholder left');
}

console.log('test-packages: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
