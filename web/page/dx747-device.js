/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* Where the configurator's MIDI goes. A "MIDI access" is the object navigator.requestMIDIAccess() returns; the page works with
 * two of them and does not care which: the real one (the FM-1 on USB) and a virtual one whose single input and output are the
 * simulator in the page. The virtual one implements just what sdk/WebUpdate.js and the page use of a port: name, state, open,
 * close, send, and 'midimessage' events. */
(() => {
  'use strict';

  class VirtualPort {
    constructor(name, type, owner) {
      this.id = name + ':' + type; this.name = name; this.manufacturer = 'DX747'; this.type = type;
      this.state = 'connected'; this.connection = 'closed'; this.owner = owner; this.listeners = new Set();
    }
    open() { this.connection = 'open'; return Promise.resolve(this); }
    close() { this.connection = 'closed'; return Promise.resolve(this); }
    addEventListener(type, fn) { if (type === 'midimessage') this.listeners.add(fn); }
    removeEventListener(type, fn) { if (type === 'midimessage') this.listeners.delete(fn); }
    deliver(bytes) { for (const fn of [...this.listeners]) fn({ type: 'midimessage', data: bytes, target: this }); }
    send(bytes) { this.owner.fromPage(Uint8Array.from(bytes)); }
  }

  /** The simulator as MIDI ports. Everything the page sends goes into the core as USB MIDI; whatever the unit answers comes out of the input. */
  class VirtualMidiAccess {
    /** advanceMs: let the unit's clock run this long after every message (tests, where nothing else moves time). */
    constructor(core, name = 'FM-1 DX747 (simulator)', advanceMs = 0) {
      this.core = core; this.sysexEnabled = true; this.advanceMs = advanceMs;
      this.input = new VirtualPort(name, 'input', this);
      this.output = new VirtualPort(name, 'output', this);
      this.inputs = new Map([[this.input.id, this.input]]);
      this.outputs = new Map([[this.output.id, this.output]]);
      this.onstatechange = null; this.tap = null;
    }
    fromPage(bytes) {
      if (this.tap) this.tap('out', bytes);
      const refused = this.core.midiIn(bytes, DxCore.SOURCE.USB);
      // the replies exist already (the core answers while it takes the message); hand them over like a port would, a moment later
      const replies = this.core.midiOut();
      if (this.advanceMs) this.core.advance(this.advanceMs);
      if (refused && !replies.length) return;
      for (const r of replies) setTimeout(() => { if (this.tap) this.tap('in', r.bytes); this.input.deliver(r.bytes); }, 0);
    }
    addEventListener() {}
    removeEventListener() {}
  }

  /** The real unit: ask the browser for MIDI with SysEx. Resolves with a MIDIAccess or throws a readable error. */
  async function requestUnitAccess() {
    if (!globalThis.isSecureContext) throw new Error('Web MIDI needs a secure page: open this file from https:// or localhost, or from disk in Chrome.');
    if (!navigator.requestMIDIAccess) throw new Error('This browser has no Web MIDI. Use Chrome or Edge on a computer.');
    try { return await navigator.requestMIDIAccess({ sysex: true }); }
    catch (error) { throw new Error('MIDI access was refused (' + (error.message || error.name) + '). Allow MIDI with SysEx for this page and close other MIDI software.'); }
  }

  globalThis.DxDevice = { VirtualMidiAccess, VirtualPort, requestUnitAccess };
})();
