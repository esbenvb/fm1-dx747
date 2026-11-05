/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The Update tab: install the DX747 firmware that is inside this page, or put the original M-VAVE V15 firmware back. It drives
 * FM1Web.Updater (sdk/WebUpdate.js), the same updater the stock page uses; only the page around it is DX747's. The package and
 * the original firmware are embedded in the page (FM1_PACKAGE) and checked against their hashes before anything is written. */
(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const u = { dx: null, pkg: null, stock: null, updater: null, kind: null, busy: false, connected: false, same: false, migration: false, abort: null };
  const text = (id, value) => { const n = $(id); if (n) n.textContent = value; };
  const status = (message, progress) => { text('up-status', message); if (progress !== undefined) $('up-progress').value = progress; };
  const fail = error => { text('up-error', error.message || String(error)); };

  function enable() {
    const locked = u.busy;
    $('up-connect').disabled = locked || !u.pkg;
    $('up-restore-connect').disabled = locked || !u.stock;
    $('up-install').disabled = locked || !u.connected || u.same || u.kind !== 'open' || !$('up-backup').checked;
    $('up-restore').disabled = locked || !u.connected || u.kind !== 'stock' || !$('up-backup').checked;
    $('up-force-note').hidden = !(u.migration && u.kind === 'open');
  }
  async function disconnect() { if (u.updater) await u.updater.disconnect().catch(() => {}); u.updater = null; u.connected = false; u.same = false; u.migration = false; text('up-identity', ''); }

  async function connect(kind) {
    if (u.busy) return;
    u.busy = true; u.kind = kind; text('up-error', ''); enable();
    const controller = new AbortController(), timer = setTimeout(() => controller.abort(new Error('Connecting took too long: check the USB cable and the MIDI permission, close other MIDI programs and try again.')), 15000);
    try {
      if (window.DxApp && DxApp.dx) { DxApp.dx.stopPolling(); await DxApp.dx.disconnect().catch(() => {}); }   // the configurator must let go of the ports
      await disconnect();
      status('Asking the browser for MIDI access…', 0);
      const access = await DxDevice.requestUnitAccess();
      if (!access.sysexEnabled) throw new Error('Allow this page to use MIDI with SysEx.');
      u.updater = new FM1Web.Updater(access, kind === 'stock' ? u.stock : u.pkg, status);
      const device = await u.updater.connect({ force: kind === 'stock' || $('up-force').checked, signal: controller.signal });
      u.connected = true; u.same = !!device.same; u.migration = !!device.migration;
      text('up-identity', device.identity || '');
      status(kind === 'stock' ? 'Connected. The original firmware can be restored.' : u.same ? 'This unit already runs this version.' : u.migration ? 'Connected. The unit runs another firmware: tick "Force the update" below and connect again.' : device.resume ? 'Connected in update mode: ready to continue.' : 'Connected. Tick the backup box and press Install.');
    } catch (error) {
      fail(error); status('Not connected');
      if (error.identity) text('up-identity', error.identity);
      if (error.code === 'FIRMWARE_MISMATCH') u.migration = true;
      const d = u.updater && u.updater.diagnostics;
      if (d) { const box = $('up-details'); box.hidden = false; box.querySelector('pre').textContent = JSON.stringify(d, null, 1); }
    } finally { clearTimeout(timer); u.busy = false; enable(); }
  }

  async function install() {
    if (u.busy || !u.connected) return;
    const stock = u.kind === 'stock', force = stock || $('up-force').checked;
    if (stock && !confirm('Put the original FM-1 V15 firmware and its 128 factory voices back? The four voice banks are overwritten, DX747 and its saved scenes are removed.\nUnit: ' + u.updater.identity)) return;
    if (!stock && force && !confirm('Force the update? The unit runs another firmware (' + u.updater.identity + '). A forced update can leave a unit that does not start; only go on if this is an FM-1 and you can restore it with the programmer.')) return;
    u.busy = true; enable(); text('up-error', '');
    let done = false;
    try { await u.updater.install({ force: stock ? true : force }); done = true; }
    catch (error) { fail(error); status('The update did not finish. Keep this page open, check the cable and try again.'); }
    finally {
      u.busy = false; u.connected = false;
      if (done) { text('up-identity', (stock ? u.stock : u.pkg).product); status(stock ? 'The original V15 firmware is back. Switch voices to hear them.' : 'DX747 is installed. The unit restarts: press Connect FM-1 at the top to configure it.', 100); }
      enable();
    }
  }

  async function init() {
    if (typeof FM1_PACKAGE === 'undefined' || !FM1_PACKAGE) { text('up-intro', 'This page was built without firmware, so it cannot update a unit.'); status('No firmware inside'); return; }
    try {
      status('Checking the firmware inside this page…');
      const [pkg, stock] = await Promise.all([FM1Web.prepare(FM1_PACKAGE), FM1_PACKAGE.stock ? FM1Web.prepare(FM1_PACKAGE.stock) : Promise.resolve(null)]);
      u.pkg = pkg; u.stock = stock;
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
