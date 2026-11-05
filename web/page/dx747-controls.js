/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The controls of the Configure tab that the browser does not have: a keyboard that picks a key range, a slider with two handles,
 * a segmented control. The rules they follow (snapping, which end a click moves, what a crossing does) are plain functions at the
 * top, so tools/dx747/page/test-controls.mjs can test them in Node; the builders below only put them on the page.
 *
 * Every control has set(...) for the unit's state, which is ignored for a moment after the user touched the control so that the
 * poll of the unit does not undo an adjustment in progress, and calls onSet(...) for each change the user makes. */
(() => {
  'use strict';
  // ---- the rules ----------------------------------------------------------------------------------------------------
  const NOTE = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const noteName = n => NOTE[n % 12] + (Math.floor(n / 12) - 1);          // 60 is C4, as on the panel
  const BLACK = [false, true, false, true, false, false, true, false, true, false, true, false];
  const WHITES_BELOW = [0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6];                // white keys under a key, within its octave
  const isBlack = n => BLACK[n % 12];
  const whitesBefore = n => Math.floor(n / 12) * 7 + WHITES_BELOW[n % 12];
  const KEYS = 128, WHITES = whitesBefore(KEYS - 1) + 1;                      // 75 white keys from C-1 to G9
  const WHITE_NOTES = []; for (let n = 0; n < KEYS; n++) if (!isBlack(n)) WHITE_NOTES.push(n);
  const BLACK_WIDTH = 0.6;                                                    // a black key, in white-key widths
  const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  /** Where a key goes on a keyboard of 75 white keys: left and width in percent. */
  function keyBox(n) {
    return isBlack(n) ? { left: (whitesBefore(n) - BLACK_WIDTH / 2) / WHITES * 100, width: BLACK_WIDTH / WHITES * 100 }
                      : { left: whitesBefore(n) / WHITES * 100, width: 100 / WHITES };
  }
  /** A slider that is dragged near its centre lands on it. */
  const snap = (v, at, within) => (Math.abs(v - at) <= within ? at : v);
  /** Key range: clicking or dragging to key n sets the chosen end; the other end follows when they would cross, as the unit does. */
  const setEnd = (lo, hi, end, n) => (end === 'lo' ? [n, Math.max(hi, n)] : [Math.min(lo, n), n]);
  /** Velocity range: a handle stops at the other one. */
  const limitEnd = (lo, hi, end, v, min, max) => { v = clamp(v, min, max); return end === 'lo' ? [Math.min(v, hi), hi] : [lo, Math.max(v, lo)]; };
  const otherEnd = end => (end === 'lo' ? 'hi' : 'lo');
  /** The value under pixel x of a track that starts at `left` and is `width` wide. */
  const valueAt = (x, left, width, min, max) => clamp(Math.round(min + (x - left) / (width || 1) * (max - min)), min, max);
  /** The white key under pixel x of a keyboard. */
  const noteAtX = (x, left, width) => WHITE_NOTES[clamp(Math.floor((x - left) / (width || 1) * WHITES), 0, WHITES - 1)];
  /** A press at pixel px on a two-handle slider whose handle centres are at cl and ch (pixels, handles `half` wide on each side):
   *  end: the handle it takes, the nearer one by pixels (null when they sit together or the press is halfway between them: the drag
   *  decides); onHandle: the press landed on a handle, so the value must not jump; offset: how far from the handle's centre. */
  function grab(cl, ch, px, half) {
    const dl = Math.abs(px - cl), dh = Math.abs(px - ch), end = dl < dh ? 'lo' : dh < dl ? 'hi' : null, d = end === 'hi' ? dh : dl;
    return { end, onHandle: d <= half, offset: d <= half ? px - (end === 'hi' ? ch : cl) : 0 };
  }
  /** A press on the bare track with no nearer handle (they sit together): the side of the pair it is on. */
  const sideEnd = (lo, hi, v) => (v < lo ? 'lo' : v > hi ? 'hi' : null);
  /** The handle a drag moves when it could not be told at the press: by the side the pointer goes to. */
  const endByDrag = (lo, hi, v) => (v === lo || v === hi ? null : v < lo ? 'lo' : v > hi ? 'hi' : (v - lo < hi - v ? 'lo' : 'hi'));

  // ---- the builders -------------------------------------------------------------------------------------------------
  function h(tag, cls, ...kids) {
    const e = document.createElement(tag); if (cls) e.className = cls;
    for (const k of kids) if (k != null) e.append(k.nodeType ? k : document.createTextNode(String(k)));
    return e;
  }
  const HOLD = 1500;                                                          // ms a touched control keeps what the user set
  let onTouch = null;                                                         // the page repaints from the unit's state when a hold has run out
  const stamp = () => { if (onTouch) onTouch(); return Date.now(); };
  function throttle(fn, ms = 70) {
    let t = null, pending;
    return v => { pending = v; if (!t) t = setTimeout(() => { t = null; fn(pending); }, ms); };
  }

  /** Buttons side by side, one chosen: options are [value, label]. set(v) follows the unit, choose(v) is the program's own move. */
  function segmented(options, onSet, label) {
    const el = h('div', 'seg'), buttons = [];
    let cur = null, edited = 0;
    el.setAttribute('role', 'radiogroup'); if (label) el.setAttribute('aria-label', label);
    const paint = () => buttons.forEach(([v, b], i) => {
      b.classList.toggle('on', v === cur); b.setAttribute('aria-checked', String(v === cur));
      b.tabIndex = v === cur || (cur === null && i === 0) ? 0 : -1;
    });
    const choose = (v, user) => { cur = v; if (user) edited = stamp(); paint(); if (user) onSet(v); };
    options.forEach(([v, text], i) => {
      const b = h('button', 'seg-b', text); b.type = 'button'; b.setAttribute('role', 'radio');
      b.onclick = () => choose(v, true);
      b.addEventListener('keydown', e => {
        const d = e.key === 'ArrowRight' || e.key === 'ArrowDown' ? 1 : e.key === 'ArrowLeft' || e.key === 'ArrowUp' ? -1 : 0;
        if (!d) return;
        e.preventDefault(); const to = buttons[clamp(i + d, 0, buttons.length - 1)]; to[1].focus(); choose(to[0], true);
      });
      buttons.push([v, b]); el.append(b);
    });
    paint();
    return { el, choose: v => choose(v, false), set(v) { if (Date.now() - edited >= HOLD && v !== cur) { cur = v; paint(); } }, get value() { return cur; } };
  }

  /** A keyboard from C-1 to G9 that picks the lowest and highest key. The chosen keys are lit in `color`.
   *  Click or drag: it sets the end named by the Lowest/Highest switch (and then the switch moves to the other end, so two clicks pick a
   *  range). The arrow keys move the chosen end (Shift: an octave), Enter switches ends. Reset selects the whole keyboard.
   *  onSet(end, note) is called with end 'lo' or 'hi'. */
  function keyRange(onSet, color, label = '') {
    const el = h('div', 'keyrange'); if (color) el.style.setProperty('--c', color);
    let lo = 0, hi = KEYS - 1, end = 'lo', dragging = false, edited = 0, from = null, last = -1;
    const sendLo = throttle(n => onSet('lo', n)), sendHi = throttle(n => onSet('hi', n));
    const seg = segmented([['lo', 'Lowest'], ['hi', 'Highest']], v => { end = v; paint(); }, 'Which end of the key range the next click sets' + (label ? ', ' + label : ''));
    seg.choose('lo');
    const read = h('span', 'kr-read'), reset = h('button', 'mini', 'Reset'); reset.type = 'button'; reset.title = 'Select the whole keyboard again';
    const piano = h('div', 'kr-piano'), keys = [];
    piano.tabIndex = 0; piano.setAttribute('role', 'group');
    for (let n = 0; n < KEYS; n++) {
      const box = keyBox(n), k = h('i', 'k ' + (isBlack(n) ? 'black' : 'white'));
      k.style.left = box.left + '%'; k.style.width = box.width + '%'; k.dataset.n = n;
      if (n % 12 === 0) k.append(h('b', null, noteName(n)));
      keys.push(k); piano.append(k);
    }
    const scroller = h('div', 'kr-scroll', piano);
    el.append(h('div', 'kr-head', seg.el, read, reset), scroller);
    function paint() {
      keys.forEach((k, n) => { k.classList.toggle('in', n >= lo && n <= hi); k.classList.toggle('elo', n === lo); k.classList.toggle('ehi', n === hi); });
      read.textContent = noteName(lo) + ' – ' + noteName(hi) + '  (' + lo + '–' + hi + ', ' + (hi - lo + 1) + ' key' + (hi > lo ? 's' : '') + ')';
      piano.setAttribute('aria-label', (label ? label + ': ' : '') + 'Key range ' + noteName(lo) + ' to ' + noteName(hi) + '. Arrow keys move the ' + (end === 'lo' ? 'lowest' : 'highest') + ' key, Enter switches ends.');
      reset.disabled = lo === 0 && hi === KEYS - 1;
    }
    function change(nl, nh) {
      const dl = nl !== lo, dh = nh !== hi; lo = nl; hi = nh; edited = stamp(); paint();
      if (dl) sendLo(lo); if (dh) sendHi(hi);
    }
    function noteFrom(e) {
      const r = piano.getBoundingClientRect(), t = document.elementFromPoint(e.clientX, e.clientY), k = t && t.closest ? t.closest('[data-n]') : null;
      return k && piano.contains(k) ? +k.dataset.n : noteAtX(e.clientX, r.left, r.width);
    }
    function reveal(n) {                                  // keep the key being set in view when the keyboard is wider than its box
      const box = keyBox(n), w = piano.offsetWidth, x = box.left / 100 * w, wd = box.width / 100 * w, pad = 28;
      if (x - pad < scroller.scrollLeft) scroller.scrollLeft = Math.max(0, x - pad);
      else if (x + wd + pad > scroller.scrollLeft + scroller.clientWidth) scroller.scrollLeft = x + wd + pad - scroller.clientWidth;
    }
    const go = n => { last = n; reveal(n); change(...setEnd(from[0], from[1], end, n)); };       // from the range at the press: dragging back undoes a push
    piano.addEventListener('pointerdown', e => {
      if (e.button) return;
      e.preventDefault(); piano.setPointerCapture(e.pointerId); piano.focus({ preventScroll: true });
      dragging = true; from = [lo, hi]; go(noteFrom(e));
    });
    piano.addEventListener('pointermove', e => { if (dragging) { const n = noteFrom(e); if (n !== last) go(n); } });
    const finish = e => {
      if (!dragging) return; dragging = false;
      try { piano.releasePointerCapture(e.pointerId); } catch (error) { /* already released */ }
      end = otherEnd(end); seg.choose(end); edited = stamp(); paint();
    };
    piano.addEventListener('pointerup', finish); piano.addEventListener('pointercancel', finish);
    piano.addEventListener('keydown', e => {
      const d = e.key === 'ArrowRight' || e.key === 'ArrowUp' ? 1 : e.key === 'ArrowLeft' || e.key === 'ArrowDown' ? -1 : 0, step = e.shiftKey ? 12 : 1;
      if (d) { e.preventDefault(); const cur = end === 'lo' ? lo : hi, n = clamp(cur + d * step, 0, KEYS - 1); change(...setEnd(lo, hi, end, n)); reveal(n); }
      else if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); end = otherEnd(end); seg.choose(end); paint(); }
    });
    reset.onclick = () => { end = 'lo'; seg.choose('lo'); change(0, KEYS - 1); };
    paint();
    return { el, set(nl, nh) { if (dragging || Date.now() - edited < HOLD || (nl === lo && nh === hi)) return; lo = nl; hi = nh; paint(); }, get range() { return [lo, hi]; } };
  }

  /** A slider with two handles (the velocity window). The handles cannot cross. Press a handle and drag it (the value does not jump
   *  to the pointer), click the track to move the nearer handle, arrow keys on a focused handle (PageUp/PageDown: 10), double-click or
   *  the Full button for the whole scale. onSet(end, value). */
  function dualSlider(min, max, onSet, names = ['Lowest', 'Highest'], label = '') {
    const el = h('div', 'dual-wrap'), area = h('div', 'dual'), fill = h('div', 'dual-fill');
    const thumbs = { lo: h('div', 'dual-thumb'), hi: h('div', 'dual-thumb') };
    let lo = min, hi = max, edited = 0, which = null, dragging = false, offset = 0;
    area.append(h('div', 'dual-track'), fill, thumbs.lo, thumbs.hi);
    const out = h('span', 'val dual-val'), full = h('button', 'mini', 'Full'); full.type = 'button'; full.title = 'The whole scale';
    el.append(area, out, full);
    const send = { lo: throttle(v => onSet('lo', v)), hi: throttle(v => onSet('hi', v)) };
    ['lo', 'hi'].forEach((end, i) => {
      const t = thumbs[end]; t.tabIndex = 0; t.setAttribute('role', 'slider'); t.setAttribute('aria-label', names[i] + ' velocity' + (label ? ', ' + label : ''));
      t.setAttribute('aria-valuemin', min); t.setAttribute('aria-valuemax', max);
      t.addEventListener('keydown', e => {
        const d = { ArrowRight: 1, ArrowUp: 1, ArrowLeft: -1, ArrowDown: -1, PageUp: 10, PageDown: -10 }[e.key];
        if (d === undefined && e.key !== 'Home' && e.key !== 'End') return;
        e.preventDefault(); move(end, e.key === 'Home' ? min : e.key === 'End' ? max : (end === 'lo' ? lo : hi) + d);
      });
    });
    function paint() {
      const pct = v => (v - min) / (max - min) * 100;
      thumbs.lo.style.left = pct(lo) + '%'; thumbs.hi.style.left = pct(hi) + '%'; fill.style.left = pct(lo) + '%'; fill.style.width = (pct(hi) - pct(lo)) + '%';
      thumbs.lo.setAttribute('aria-valuenow', lo); thumbs.hi.setAttribute('aria-valuenow', hi);
      thumbs.lo.setAttribute('aria-valuetext', String(lo)); thumbs.hi.setAttribute('aria-valuetext', String(hi));
      out.textContent = lo + ' – ' + hi; full.disabled = lo === min && hi === max;
    }
    function move(end, v) {
      const [nl, nh] = limitEnd(lo, hi, end, v, min, max), dl = nl !== lo, dh = nh !== hi;
      if (!dl && !dh) return;
      edited = stamp(); lo = nl; hi = nh; paint(); if (dl) send.lo(lo); if (dh) send.hi(hi);
    }
    const at = x => { const r = area.getBoundingClientRect(); return valueAt(x, r.left, r.width, min, max); };
    area.addEventListener('pointerdown', e => {
      if (e.button) return;
      e.preventDefault(); area.setPointerCapture(e.pointerId); dragging = true;
      const r = area.getBoundingClientRect(), centre = v => r.left + (v - min) / (max - min) * r.width;
      const g = grab(centre(lo), centre(hi), e.clientX, 8);
      if (g.onHandle) { which = g.end; offset = g.offset; }                      // a press on a handle only picks it up
      else { offset = 0; const v = at(e.clientX); which = g.end || sideEnd(lo, hi, v); }
      if (which) thumbs[which].focus({ preventScroll: true });
      if (!g.onHandle && which) move(which, at(e.clientX));
    });
    area.addEventListener('pointermove', e => {
      if (!dragging) return;
      const v = at(e.clientX - offset); if (!which) { which = endByDrag(lo, hi, v); if (!which) return; thumbs[which].focus({ preventScroll: true }); }
      move(which, v);
    });
    const finish = e => { if (!dragging) return; dragging = false; which = null; offset = 0; edited = stamp(); try { area.releasePointerCapture(e.pointerId); } catch (error) { /* released */ } };
    area.addEventListener('pointerup', finish); area.addEventListener('pointercancel', finish);
    const whole = () => { move('lo', min); move('hi', max); };
    area.addEventListener('dblclick', whole); full.onclick = whole;
    paint();
    return { el, set(nl, nh) { if (dragging || Date.now() - edited < HOLD || (nl === lo && nh === hi)) return; lo = nl; hi = nh; paint(); }, get range() { return [lo, hi]; } };
  }

  globalThis.DxControls = { noteName, isBlack, whitesBefore, keyBox, snap, setEnd, limitEnd, otherEnd, valueAt, noteAtX, grab, sideEnd, endByDrag, KEYS, WHITES,
                            set onTouch(fn) { onTouch = fn; },
                            segmented: typeof document === 'undefined' ? undefined : segmented, keyRange: typeof document === 'undefined' ? undefined : keyRange,
                            dualSlider: typeof document === 'undefined' ? undefined : dualSlider };
})();
