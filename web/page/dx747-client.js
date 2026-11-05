/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The configurator's side of docs/CONFIG-PROTOCOL.md. It sits on sdk/WebUpdate.js (FM1Web.Library): that class already does the
 * workbench framing, the tokens, the retries and the voice banks; this one adds the DX747 commands (23 to 31), the state image
 * and polling. Calls are queued one after another because the unit answers one request at a time. */
(() => {
  'use strict';
  const NOTE = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const noteName = n => NOTE[n % 12] + (Math.floor(n / 12) - 1);        // 60 is C4, as on the panel
  const text = (b, at, n) => String.fromCharCode(...b.subarray(at, at + n)).replace(/[^\x20-\x7e]/g, ' ').replace(/\s+$/, '');   // as the unit shows names: anything unprintable is a space
  const pad12 = s => Array.from({ length: 12 }, (_, i) => (s[i] && s.charCodeAt(i) >= 32 && s.charCodeAt(i) <= 126) ? s.charCodeAt(i) : 32);
  const REASON = { 1: 'That slot is empty', 67: 'The unit is busy writing flash: wait a moment and try again', 71: 'The unit refused that value or that action',
                   72: 'The unit is out of memory', 95: 'No free part slot', 96: 'No free engine: remove a layer first', 97: 'No free MIDI channel', 98: 'The unit refused that change' };

  /** The SDK's own messages are Chinese: put them in English (sdk/I18n.js has the table when the page carries it). */
  function translate(error) {
    if (error && typeof error.message === 'string' && globalThis.FM1I18n) error.message = FM1I18n.t(error.message, 'en');
    return error;
  }
  const checkRom = file => {
    if (!(file instanceof Uint8Array) || file.length !== 4104 || file[0] !== 0xf0 || file[1] !== 0x43 || file[3] !== 0x09 || file[4] !== 0x20 || file[5] !== 0x00 || file[4103] !== 0xf7)
      throw new Error('That is not a 32-voice DX7 bank file (4,104 bytes, F0 43 0n 09 20 00 ... F7)');
    let sum = 0; for (let i = 6; i < 4102; i++) sum = (sum + file[i]) & 127;
    if (((128 - sum) & 127) !== file[4102]) throw new Error('The bank file fails its checksum');
    return file.slice(6, 4102);
  };

  /** The 346-byte state image as an object (layout: docs/CONFIG-PROTOCOL.md). */
  function parseState(b) {
    if (b.length !== 346 || b[0] !== 1) throw new Error('The unit sent a state image this page does not understand (version ' + b[0] + ')');
    const s = {
      selected: b[1], up: b[2], scene: b[3],
      settings: { sceneChannel: b[4], masterChannel: b[5], backlight: b[6], bluetoothAudio: !(b[7] & 1) },
      sceneRevision: b[344] | b[345] << 8,
      filter: { type: b[8], resonance: b[9], hz: b[10] | b[11] << 8 },
      effects: { decay: b[12], delay: b[13], feedback: b[14], ret: b[15] },        // delay: the unit's stored value (128..136 is a synchronised division)
      parts: [], engines: [],
    };
    for (let p = 0; p < 8; p++) {
      const at = 16 + 15 * p;
      s.parts.push({ index: p, channel: b[at], used: b[at] !== 0, level: b[at + 1], selectedLayer: b[at + 2], name: text(b, at + 3, 12), layers: [] });
    }
    for (let e = 0; e < 8; e++) {
      const at = 136 + 26 * e, f = b[at + 8];
      const x = { engine: e, owner: b[at], level: b[at + 1], pan: b[at + 2] - 50, expression: b[at + 3], bright: b[at + 4] - 50,
                  reverb: b[at + 5], delay: b[at + 6], program: b[at + 7], muted: !!(f & 1), solo: !!(f & 2), audible: !!(f & 4), up: !!(f & 8),
                  low: b[at + 9], high: b[at + 10], shift: b[at + 11] - 64, fold: b[at + 12], poly: b[at + 13], velLow: b[at + 14], velHigh: b[at + 15],
                  voice: text(b, at + 16, 10) };
      s.engines.push(x);
      if (x.owner < 8) s.parts[x.owner].layers.push(x);
    }
    return s;
  }

  class Dx747 {
    constructor(access) {
      this.access = access; this.lib = new FM1Web.Library(access); this.chain = Promise.resolve();
      this.state = null; this.lastCrc = null; this.timer = null; this.polling = false; this.bankNames = {}; this.libGen = 0;
    }
    run(fn) {
      const p = this.chain.then(fn, fn).catch(error => { throw translate(error); });
      this.chain = p.catch(() => {});
      return p;
    }
    async q(command, payload = [], emptyOK = false) {
      try { return await this.lib.query(command, payload, emptyOK); }
      catch (error) {
        if (error.deviceCode && REASON[error.deviceCode]) error.message = REASON[error.deviceCode];
        throw error;
      }
    }
    /** Connect to the one FM-1 (or the simulator) on the access and say hello. Resolves with the hello details. */
    connect() {
      return this.run(async () => {
        await this.lib.connect();
        let h;
        try { h = await this.q(23); }
        catch (error) { await this.lib.disconnect().catch(() => {}); throw new Error(error.deviceCode === 71 ? 'This unit runs firmware without the DX747 configuration commands. Update it (Update tab).' : error.message); }
        if (h.length !== 11 || h[0] !== 68 || h[1] !== 88) { await this.lib.disconnect().catch(() => {}); throw new Error('This is not a DX747 unit.'); }
        this.hello = { protocol: h[2], enginesUp: h[4], parts: h[5], layers: h[6], scenes: h[7], release: String.fromCharCode(h[8], h[9], h[10]) };
        this.state = null; this.lastCrc = null; this.forgetVoiceNames();
        return this.hello;
      });
    }
    disconnect() { this.stopPolling(); return this.run(() => this.lib.disconnect()); }
    get connected() { return !!(this.lib.link && !this.lib.link.lost); }
    // ---- reading ------------------------------------------------------------------------------
    crc() {
      return this.run(async () => {
        const d = await this.q(24);
        return { crc: (d[0] | d[1] << 8 | d[2] << 16 | d[3] << 24) >>> 0, size: d[4] | d[5] << 8 };
      });
    }
    readState() {
      return this.run(async () => {
        const head = await this.q(1, [5, 0]), size = head[0] | head[1] << 8;
        if (size !== 346) throw new Error('The unit announced a state image of ' + size + ' bytes');
        const bytes = new Uint8Array(size);
        try { for (let at = 0; at < size; at += 64) { const n = Math.min(64, size - at); bytes.set(await this.q(2, [at & 255, at >> 8, n]), at); } }
        finally { await this.q(6).catch(() => {}); }
        this.state = parseState(bytes);
        return this.state;
      });
    }
    /** Poll the unit: when the state image changes (a panel edit, MIDI, a scene), read it and call onState(state). */
    startPolling(onState, onError, ms = 400, onOk) {
      this.stopPolling(); this.polling = true;
      const tick = async () => {
        if (!this.polling) return;
        try {
          const h = await this.crc();
          if (h.crc !== this.lastCrc) { const s = await this.readState(); this.lastCrc = h.crc; onState(s); }
          if (onOk) onOk();
        } catch (error) { if (this.polling && onError) onError(error); }
        if (this.polling) this.timer = setTimeout(tick, ms);
      };
      tick();
    }
    stopPolling() { this.polling = false; clearTimeout(this.timer); this.timer = null; }
    /** Read the state again now and report it. */
    async refresh(onState) { const s = await this.readState(); this.lastCrc = null; if (onState) onState(s); return s; }
    // ---- changing -----------------------------------------------------------------------------
    setLayer(engine, field, value) { return this.run(() => this.q(25, [engine, field, value])); }
    setPart(part, field, value) { return this.run(() => this.q(26, [part, field, value])); }
    partOp(op, a, b = 0) { return this.run(() => this.q(27, [op, a, b])).then(d => d[0]); }
    setMaster(field, value) { return this.run(() => this.q(28, [field, value & 255, value >> 8])); }
    setSetting(field, value) { return this.run(() => this.q(29, [field, value])); }
    partName(part, name) { return this.run(() => this.q(31, [part, ...pad12(name)])); }
    // ---- scenes -------------------------------------------------------------------------------
    scenes() {
      return this.run(async () => {
        const out = [];
        for (let first = 0; first < 64; first += 4) {
          const d = await this.q(30, [0, first]);
          for (let i = 0; i < 4; i++) out.push({ index: first + i, parts: d[13 * i], name: text(d, 13 * i + 1, 12) });
        }
        return out;
      });
    }
    sceneRecall(index) { return this.run(() => this.q(30, [1, index])); }
    sceneSave(index, name) { return this.run(() => this.q(30, [2, index, ...pad12(name)])); }
    sceneRename(index, name) { return this.run(() => this.q(30, [3, index, ...pad12(name)])); }
    sceneDelete(index) { return this.run(() => this.q(30, [4, index])); }
    // ---- sounds -------------------------------------------------------------------------------
    /** Names of the 32 voices in library bank 0..3 (A to D). */
    async voiceNames(bank) {
      if (this.bankNames[bank]) return this.bankNames[bank];
      const gen = this.libGen, raw = await this.run(() => this.lib.readBank(bank));
      const names = Array.from({ length: 32 }, (_, v) => text(raw, v * 128 + 118, 10));
      if (gen === this.libGen) this.bankNames[bank] = names;                 // a read that began before the library changed must not be remembered
      return names;
    }
    /** Forget the names read so far: the next voiceNames reads the unit again. */
    forgetVoiceNames() { this.bankNames = {}; this.libGen++; }
    async allVoiceNames() { const out = []; for (let b = 0; b < 4; b++) out.push(...await this.voiceNames(b)); return out; }
    /** Import a 32-voice DX7 bank file (4,104 bytes of SysEx) into library bank 0..3; the library reads it back and checks it. */
    importRom(bank, file) {
      const raw = checkRom(file);
      this.forgetVoiceNames();
      return this.run(() => this.lib.writeBank(bank, raw)).finally(() => this.forgetVoiceNames());
    }
  }
  globalThis.Dx747 = Dx747;
  globalThis.Dx747.parseState = parseState;
  globalThis.Dx747.noteName = noteName;
  globalThis.Dx747.checkRom = checkRom;
})();
