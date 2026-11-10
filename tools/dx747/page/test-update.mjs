// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
//
// The Update tab's logic (web/page/dx747-update.js), run against a fake page and a fake updater: no unit, no firmware. What it pins is what keeps a wrong
// firmware off a unit and what the user is told:
//   - an SMK-37 page is for ONE model; in update mode, where the unit cannot say which model it is, nothing is written until the user has been asked to read the
//     label on the unit, and the answer is passed on as install({confirmedModel}); the fake updater refuses an update-mode install without it, as the SDK does
//   - cancelling that question writes nothing
//   - outside update mode no extra question is asked, and the FM-1 page behaves as it always did (no model, no confirmedModel)
//   - the restore asks about V16 and the model on an SMK-37 page, and the SDK's Chinese messages reach the user in English
// The real updater and the real loader can only be tested on hardware (docs/ON-DEVICE-CHECKLIST-SMK37.md).
import { readFileSync } from 'node:fs';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
let checks = 0, failures = 0;
function check(ok, what) { ++checks; if (!ok) { ++failures; console.error('FAIL: ' + what); } }
const eq = (got, want, what) => check(JSON.stringify(got) === JSON.stringify(want), what + ': wanted ' + JSON.stringify(want) + ', got ' + JSON.stringify(got));

/** One fake page + updater, then the real dx747-update.js loaded into it. */
function page({ model, mode, answers }) {
  const els = new Map(), log = { confirms: [], installs: [], statuses: [] };
  const el = (id) => { if (!els.has(id)) els.set(id, { id, textContent: '', disabled: false, hidden: false, checked: false, value: 0, onclick: null, onchange: null, querySelector: () => ({ textContent: '' }) }); return els.get(id); };
  const listeners = {};
  const pkgMeta = (kind) => ({ kind, product: model ? `SMK-37 ${model === 'pro' ? 'Pro' : 'Elite'}_${kind === 'stock' ? '016' : '8747747'}` : (kind === 'stock' ? 'FM-1_015' : 'FM-1_8747747'),
    model: model || undefined, release_version: kind === 'stock' ? (model ? 'V16' : 'V15') : 'V01', build_id: 'ab'.repeat(32), bytes: 1000 });
  class Updater {
    constructor(access, pkg, status) { this.pkg = pkg; this.status = status; this.identity = pkg.kind === 'stock' ? 'unit' : 'unit'; }
    async connect() {
      this.mode = mode; this.identity = mode === 'loader' ? 'ota-' + this.pkg.product : (model ? `SMK-37 ${model === 'pro' ? 'Pro' : 'Elite'}_8000001` : 'FM-1_8000001');
      this.status('准备升级程序', 0);                                         // one of the SDK's Chinese messages
      return mode === 'loader' ? { resume: true, modelUnverified: !!model, identity: this.identity } : { identity: this.identity };
    }
    async install(options = {}) {
      log.installs.push({ force: options.force, confirmedModel: options.confirmedModel, kind: this.pkg.kind });
      if (this.mode === 'loader' && this.pkg.model && options.confirmedModel !== this.pkg.model) throw new Error('升级模式无法自动识别机型，请先核对机身型号');   // the SDK's own refusal
    }
    async disconnect() {}
    get link() { return { lost: false }; }
  }
  const context = {
    document: { getElementById: el, addEventListener: (type, fn) => { listeners[type] = fn; }, querySelector: () => null },
    window: { isSecureContext: true, addEventListener() {}, DxApp: { device: model ? 'SMK-37' : 'FM-1', release: async () => {}, setLocked() {} } },
    crypto: { subtle: {} }, confirm: (message) => { log.confirms.push(message); return answers.length ? answers.shift() : true; },
    DxDevice: { requestUnitAccess: async () => ({ sysexEnabled: true }) },
    FM1Web: { prepare: async (meta) => ({ ...meta }), Updater }, setTimeout, clearTimeout, AbortController, console,
    URLSearchParams, location: { search: '', hash: '' }, navigator: { language: 'en-US', languages: ['en-US'] }, localStorage: { getItem: () => null, setItem() {} },
    FM1_PACKAGE: { ...pkgMeta('open'), stock: pkgMeta('stock') },
  };
  context.window.DxApp = context.window.DxApp; context.globalThis = context; context.DxApp = context.window.DxApp;
  if (!model) { delete context.FM1_PACKAGE.model; delete context.FM1_PACKAGE.stock.model; }
  vm.createContext(context);
  // the page's translation of the SDK's messages: I18n.js in a context of its own (it scans the page when there is one), handed to the page
  const i18n = { URLSearchParams, location: { search: '', hash: '' }, navigator: { language: 'en-US', languages: ['en-US'] }, localStorage: { getItem: () => null, setItem() {} }, console };
  i18n.globalThis = i18n; vm.createContext(i18n);
  vm.runInContext(readFileSync(join(root, 'targets/smk37/sdk/I18n.js'), 'utf8'), i18n, { filename: 'I18n.js' });
  context.FM1I18n = i18n.FM1I18n;
  vm.runInContext(readFileSync(join(root, 'web/page/dx747-update.js'), 'utf8'), context, { filename: 'dx747-update.js' });
  return { el, log, context, start: async () => { listeners.DOMContentLoaded(); await new Promise((r) => setTimeout(r, 20)); } };
}
const tick = () => new Promise((r) => setTimeout(r, 30));
async function run(opts, steps) {
  const p = page(opts); await p.start();
  p.el('up-backup').checked = true;
  for (const [button, check_] of steps) { await p.el(button).onclick(); await tick(); if (check_) check_(p); }
  return p;
}

// SMK-37 Pro page, a unit in update mode: asked to read the label, and the answer is passed on
let p = await run({ model: 'pro', mode: 'loader', answers: [true] }, [['up-connect'], ['up-install']]);
eq(p.log.installs.length, 1, 'update mode, label confirmed: one install');
eq(p.log.installs[0].confirmedModel, 'pro', 'update mode, label confirmed: the updater is told the model');
check(p.log.confirms.length === 1 && /update mode/.test(p.log.confirms[0]) && /SMK-37 Pro/.test(p.log.confirms[0]) && /label/.test(p.log.confirms[0]), 'the question names update mode, the model and the label: ' + p.log.confirms[0]);
eq(p.el('up-error').textContent, '', 'a confirmed install shows no error');
check(/update mode/.test(p.el('up-status').textContent) || p.el('up-status').textContent.length > 0, 'the status shows something');

// ... cancelled: nothing is written
p = await run({ model: 'pro', mode: 'loader', answers: [false] }, [['up-connect'], ['up-install']]);
eq(p.log.installs.length, 0, 'update mode, question refused: nothing is installed');

// the Elite page asks about the Elite
p = await run({ model: 'elite', mode: 'loader', answers: [true] }, [['up-connect'], ['up-install']]);
eq(p.log.installs.map((i) => i.confirmedModel), ['elite'], 'Elite page passes elite');
check(/SMK-37 Elite/.test(p.log.confirms[0]) && !/SMK-37 Pro/.test(p.log.confirms[0]), 'the Elite page never mentions the Pro in its question');

// a unit running a normal firmware (not update mode): no extra question
p = await run({ model: 'pro', mode: 'tagged', answers: [] }, [['up-connect'], ['up-install']]);
eq([p.log.installs.length, p.log.installs[0] && p.log.installs[0].confirmedModel, p.log.confirms.length], [1, undefined, 0], 'normal mode: installs without a model question and without confirmedModel');

// the way back, update mode: V16 and the model are named in the first question, the label in the second, then the model is passed on
p = await run({ model: 'pro', mode: 'loader', answers: [true, true] }, [['up-restore-connect'], ['up-restore']]);
check(p.log.confirms.length === 2 && /V16/.test(p.log.confirms[0]) && /SMK-37 Pro/.test(p.log.confirms[0]) && /label/.test(p.log.confirms[1]), 'restore asks about V16 and the Pro, then about the label: ' + JSON.stringify(p.log.confirms));
eq(p.log.installs.map((i) => [i.kind, i.confirmedModel]), [['stock', 'pro']], 'restore in update mode passes the model');
p = await run({ model: 'pro', mode: 'loader', answers: [true, false] }, [['up-restore-connect'], ['up-restore']]);
eq(p.log.installs.length, 0, 'restore: refusing the label question writes nothing');
p = await run({ model: 'pro', mode: 'tagged', answers: [false] }, [['up-restore-connect'], ['up-restore']]);
eq(p.log.installs.length, 0, 'restore: refusing the first question writes nothing');

// the SDK's Chinese message reaches the user in English, and the page's own errors are English
p = await run({ model: 'pro', mode: 'tagged', answers: [] }, [['up-connect']]);
check(!/[一-鿿]/.test(p.el('up-status').textContent + p.el('up-error').textContent), 'no Chinese reaches the page: ' + p.el('up-status').textContent);

// the Install button stays off until the backup box is ticked and a unit is connected (a guard of the FM-1 page too)
p = page({ model: 'pro', mode: 'tagged', answers: [] }); await p.start(); await p.el('up-connect').onclick(); await tick();
eq(p.el('up-install').disabled, true, 'Install is disabled while the backup box is not ticked');
p.el('up-backup').checked = true; p.el('up-backup').onchange(); eq(p.el('up-install').disabled, false, 'Install is enabled once it is');

// the FM-1 page: no model, nothing new: its update-mode install has no confirmedModel and no model question
p = await run({ model: undefined, mode: 'loader', answers: [] }, [['up-connect'], ['up-install']]);
eq([p.log.installs.length, p.log.installs[0] && p.log.installs[0].confirmedModel, p.log.confirms.length], [1, undefined, 0], 'FM-1 page: installs as before, no model question');
p = await run({ model: undefined, mode: 'tagged', answers: [true] }, [['up-restore-connect'], ['up-restore']]);
check(p.log.confirms.length === 1 && /V15/.test(p.log.confirms[0]) && !/SMK/.test(p.log.confirms[0]), 'FM-1 page: the restore still talks about V15: ' + p.log.confirms[0]);

console.log('test-update: ' + checks + ' checks, ' + failures + ' failed');
process.exit(failures ? 1 : 0);
