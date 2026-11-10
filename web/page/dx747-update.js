/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The Update tab: install the DX747 firmware that is inside this page, or put the original M-VAVE firmware back (the FM-1's V15, the SMK-37's V16). It
 * drives FM1Web.Updater (sdk/WebUpdate.js), the same updater the stock page uses; only the page around it is DX747's. The package and the original firmware
 * are embedded in the page (FM1_PACKAGE) and checked against their hashes before anything is written. An SMK-37 page carries ONE model (FM1_PACKAGE.model,
 * 'pro' or 'elite'): the page never offers a choice, and in update mode, where the unit cannot say which model it is, it asks the user to read the label
 * on the unit and passes that confirmation to the updater (install({confirmedModel})), which refuses without it. */
(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const u = { dx: null, pkg: null, stock: null, updater: null, kind: null, busy: false, connected: false, same: false, migration: false, abort: null };
  const text = (id, value) => { const n = $(id); if (n) n.textContent = value; };
  // the SMK-37 SDK's own messages are in Chinese: FM1I18n turns every message into English (English ones stay as they are)
  const en = message => (globalThis.FM1I18n ? FM1I18n.t(String(message), 'en') : String(message));
  const status = (message, progress) => { text('up-status', en(message)); if (progress !== undefined) $('up-progress').value = progress; };
  const fail = error => { text('up-error', en(error.message || String(error))); };
  /** The SMK-37 model this page is for ('Pro' or 'Elite'), or '' on the FM-1 page. */
  const modelName = () => (u.pkg && u.pkg.model ? (u.pkg.model === 'pro' ? 'Pro' : 'Elite') : '');

  const lock = on => { u.busy = on; if (window.DxApp && DxApp.setLocked) DxApp.setLocked(on); };
  function enable() {
    if (u.connected && !u.busy && u.updater && u.updater.link && u.updater.link.lost) { u.connected = false; u.same = false; u.migration = false; text('up-identity', ''); status('The unit was unplugged. Connect again.'); }
    const locked = u.busy;
    $('up-connect').disabled = locked || !u.pkg;
    $('up-restore-connect').disabled = locked || !u.stock;
    $('up-install').disabled = locked || !u.connected || u.same || u.migration || u.kind !== 'open' || !$('up-backup').checked;
    $('up-restore').disabled = locked || !u.connected || u.kind !== 'stock' || !$('up-backup').checked;
    $('up-force-note').hidden = !(u.migration && u.kind === 'open');
  }
  async function disconnect() { if (u.updater) await u.updater.disconnect().catch(() => {}); u.updater = null; u.connected = false; u.same = false; u.migration = false; text('up-identity', ''); }

  async function connect(kind) {
    if (u.busy) return;
    lock(true); u.kind = kind; text('up-error', ''); $('up-details').hidden = true; enable();
    const controller = new AbortController(), timer = setTimeout(() => controller.abort(new Error('Connecting took too long: check the USB cable and the MIDI permission, close other MIDI programs and try again.')), 15000);
    try {
      if (window.DxApp && DxApp.release) await DxApp.release();                       // the configurator must let go of the ports (its status shows it)
      await disconnect();
      status('Asking the browser for MIDI access…', 0);
      if (!window.isSecureContext || !crypto.subtle) throw new Error('Open this page from disk in Chrome or from an https:// or localhost address: the update needs a secure page.');
      const access = await DxDevice.requestUnitAccess();
      if (!access.sysexEnabled) throw new Error('Allow this page to use MIDI with SysEx.');
      access.onstatechange = () => enable();
      u.updater = new FM1Web.Updater(access, kind === 'stock' ? u.stock : u.pkg, status);
      const device = await u.updater.connect({ force: kind === 'stock' || $('up-force').checked, signal: controller.signal });
      u.connected = true; u.same = !!device.same; u.migration = !!device.migration;
      text('up-identity', device.identity || '');
      status(device.modelUnverified ? 'Connected in update mode. The page cannot tell which model this is: you will be asked to read the label on the unit before anything is written.' : kind === 'stock' ? 'Connected. The original firmware can be restored.' : u.same ? 'This unit already runs this version.' : u.migration ? 'Connected. The unit runs another firmware: tick "Force the update" below and connect again.' : device.resume ? 'Connected in update mode: ready to continue.' : 'Connected. Tick the backup box and press Install.');
    } catch (error) {
      fail(error); status('Not connected');
      if (error.identity) text('up-identity', error.identity);
      if (error.code === 'FIRMWARE_MISMATCH') u.migration = true;
      const d = u.updater && u.updater.diagnostics;
      if (d) { const box = $('up-details'); box.hidden = false; box.querySelector('pre').textContent = JSON.stringify(d, null, 1); }
    } finally { clearTimeout(timer); lock(false); enable(); }
  }

  /** The unit's name for the confirmations: the page says SMK-37 when it was built for the SMK-37 (dx747-ui.js sets DxApp.device). */
  const unit = () => (globalThis.DxApp && DxApp.device) || 'FM-1';
  async function install() {
    if (u.busy || !u.connected) return;
    const stock = u.kind === 'stock', force = stock || $('up-force').checked, model = modelName();
    if (stock && model && !confirm('Put M-VAVE\'s original SMK-37 ' + model + ' V16 firmware back? DX747 and its saved scenes are removed. Only go on if the label on the unit says SMK-37 ' + model + '.\nUnit: ' + u.updater.identity)) return;
    if (stock && !model && !confirm((u.updater.identity === 'FM-1_015' ? 'The unit already runs V15: this puts its 128 factory voices back. The four voice banks are overwritten.' : 'Put the original ' + unit() + ' V15 firmware and its 128 factory voices back? The four voice banks are overwritten, DX747 and its saved scenes are removed.') + '\nUnit: ' + u.updater.identity)) return;
    if (!stock && force && !confirm('Force the update? The unit runs another firmware (' + u.updater.identity + '). A forced update can leave a unit that does not start; only go on if this is an ' + unit() + ' and you can restore it with the programmer.')) return;
    // In update mode the unit cannot say which model it is (the SMK-37 SDK checks this itself): the user reads the label, and only then is the model passed on
    let confirmedModel;
    if (model && u.updater.mode === 'loader') {
      if (!confirm('The unit is in update mode, where this page cannot tell which model it is. This page is for the SMK-37 ' + model + ': go on only if the label on the unit says SMK-37 ' + model + '. A wrong model can leave a unit that does not start.')) return;
      confirmedModel = u.pkg.model;
    }
    lock(true); enable(); text('up-error', '');
    let done = false;
    try { await u.updater.install({ force: stock ? true : force, confirmedModel }); done = true; }
    catch (error) { fail(error); status('The update did not finish. Keep this page open, check the cable and try again.'); }
    finally {
      lock(false); u.connected = false;
      if (done) { text('up-identity', (stock ? u.stock : u.pkg).product); status(stock ? (model ? 'The original V16 firmware is back.' : 'The original V15 firmware is back. Switch voices to hear them.') : 'DX747 is installed. The unit restarts: press Connect ' + unit() + ' at the top to configure it.', 100); }
      enable();
    }
  }

  async function init() {
    if (typeof FM1_PACKAGE === 'undefined' || !FM1_PACKAGE) { text('up-intro', 'This page was built without firmware, so it cannot update a unit.'); status('No firmware inside'); return; }
    try {
      status('Checking the firmware inside this page…');
      const [pkg, stock] = await Promise.all([FM1Web.prepare(FM1_PACKAGE), FM1_PACKAGE.stock ? FM1Web.prepare(FM1_PACKAGE.stock) : Promise.resolve(null)]);
      u.pkg = pkg; u.stock = stock;
      if (pkg.model) {                                           // an SMK-37 page: its texts name the model, the V16 and what is not known yet
        const name = modelName();
        text('up-intro', 'Install the DX747 firmware that is inside this page on an SMK-37 ' + name + ', or put M-VAVE\'s original V16 firmware back. THIS FIRMWARE HAS NEVER RUN ON AN SMK-37: ' +
          'read docs/ON-DEVICE-CHECKLIST-SMK37.md first, have a hardware programmer and a backup of the unit\'s flash, and install M-VAVE\'s own firmware once to check that the way back works. ' +
          'This page is for the ' + name + ' only; check the label on the unit. While you update, the configurator lets go of the unit\'s MIDI ports.');
        text('up-restore-connect', 'Connect for the restore'); text('up-restore', 'Restore original V16');
        const hint = $('up-restore-hint'); if (hint) hint.textContent = 'Puts M-VAVE\'s original SMK-37 ' + name + ' V16 firmware back. DX747 and its saved scenes are removed.';
      }
      text('up-release', pkg.release_version || ''); text('up-product', pkg.product); text('up-build', pkg.build_id.slice(0, 16)); text('up-size', (pkg.bytes / 1024).toFixed(1) + ' KiB');
      status('The firmware checks out. Plug the unit in and press Connect.');
    } catch (error) { fail(error); status('The firmware inside this page failed its check; download the page again.'); }
    enable();
  }
  document.addEventListener('DOMContentLoaded', () => {
    $('up-connect').onclick = () => connect('open'); $('up-restore-connect').onclick = () => connect('stock');
    $('up-install').onclick = install; $('up-restore').onclick = install; $('up-backup').onchange = enable;
    window.addEventListener('beforeunload', e => { if (u.busy) { e.preventDefault(); e.returnValue = ''; } });
    init();
  });
})();
