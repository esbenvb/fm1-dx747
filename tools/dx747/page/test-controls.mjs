#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
//
// The rules of the Configure tab's own controls (web/page/dx747-controls.js): the key-range keyboard's geometry and what a click does,
// the snapping of pan/BRIGHT/octave shift, the handles of the velocity window. The page code itself is checked in a browser; the rules
// are plain functions, so they are checked here.
//
//   node tools/dx747/page/test-controls.mjs
import { readFileSync } from 'node:fs';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../..');
vm.runInThisContext(readFileSync(join(root, 'web/page/dx747-controls.js'), 'utf8'), { filename: 'web/page/dx747-controls.js' });
const C = globalThis.DxControls;

let checks = 0, failures = 0;
function check(ok, what) { ++checks; if (!ok) { ++failures; console.error('FAIL: ' + what); } }
const eq = (got, want, what) => check(JSON.stringify(got) === JSON.stringify(want), what + ': wanted ' + JSON.stringify(want) + ', got ' + JSON.stringify(got));

// ---- names and the keyboard's shape -----------------------------------------------------------------------------------
eq(C.noteName(60), 'C4', 'middle C'); eq(C.noteName(0), 'C-1', 'lowest key'); eq(C.noteName(127), 'G9', 'highest key'); eq(C.noteName(61), 'C#4', 'a black key');
const blacks = Array.from({ length: 128 }, (_, n) => n).filter(C.isBlack);
eq(blacks.length, 53, 'black keys in 0..127'); eq(C.KEYS - blacks.length, 75, 'white keys in 0..127'); eq(C.WHITES, 75, 'the keyboard is 75 white keys wide');
eq([1, 3, 6, 8, 10].every(n => C.isBlack(n)) && [0, 2, 4, 5, 7, 9, 11].every(n => !C.isBlack(n)), true, 'black key positions in an octave');
eq(C.whitesBefore(60), 35, 'white keys under middle C'); eq(C.whitesBefore(127), 74, 'index of the last white key');
let edge = 0;
for (let n = 0; n < 128; n++) {
  if (C.isBlack(n)) continue;
  const b = C.keyBox(n);
  check(Math.abs(b.left - edge) < 1e-9, 'white key ' + n + ' starts where the last one ended'); edge = b.left + b.width;
}
check(Math.abs(edge - 100) < 1e-9, 'the white keys fill the width exactly');
for (let n = 0; n < 128; n++) {                                       // a black key straddles the border of the two white keys it sits between
  if (!C.isBlack(n)) continue;
  const b = C.keyBox(n), a = C.keyBox(n - 1), c = C.keyBox(n + 1);
  check(Math.abs(b.left + b.width / 2 - (a.left + a.width)) < 1e-9 && Math.abs((a.left + a.width) - c.left) < 1e-9, 'black key ' + n + ' is centred on the border of its white neighbours');
  check(b.width < a.width, 'black key ' + n + ' is narrower than a white key');
}
// every white key's centre maps back to it, and the ends clamp
for (let n = 0; n < 128; n++) {
  if (C.isBlack(n)) continue;
  const b = C.keyBox(n); eq(C.noteAtX(b.left + b.width / 2, 0, 100), n, 'noteAtX at the centre of key ' + n);
}
eq(C.noteAtX(-50, 0, 100), 0, 'left of the keyboard is the lowest key'); eq(C.noteAtX(500, 0, 100), 127, 'right of the keyboard is the highest key');
eq(C.noteAtX(10, 10, 840), 0, 'the left edge of an offset keyboard'); eq(C.noteAtX(849.9, 10, 840), 127, 'the right edge of an offset keyboard');

// ---- snapping ---------------------------------------------------------------------------------------------------------
eq(C.snap(2, 0, 3), 0, 'near the centre snaps'); eq(C.snap(-3, 0, 3), 0, 'the edge of the snap zone snaps'); eq(C.snap(4, 0, 3), 4, 'outside the zone stays');
eq(C.snap(-4, 0, 3), -4, 'negative outside the zone stays'); eq(C.snap(1, 0, 0), 1, 'a zero zone snaps only to the centre itself'); eq(C.snap(0, 0, 0), 0, 'the centre stays');
eq(C.snap(52, 50, 3), 50, 'the centre may be anywhere on the scale');

// ---- key range: which end a click moves, and what follows ------------------------------------------------------------------
eq(C.setEnd(0, 127, 'lo', 48), [48, 127], 'lowest inside the range'); eq(C.setEnd(48, 127, 'hi', 72), [48, 72], 'highest inside the range');
eq(C.setEnd(48, 72, 'lo', 84), [84, 84], 'lowest above the highest pulls the highest up'); eq(C.setEnd(48, 72, 'hi', 30), [30, 30], 'highest below the lowest pulls the lowest down');
eq(C.setEnd(48, 72, 'lo', 72), [72, 72], 'lowest on the highest key'); eq(C.setEnd(48, 72, 'hi', 127), [48, 127], 'highest to the top');
eq(C.setEnd(48, 72, 'lo', 0), [0, 72], 'lowest to the bottom');
eq(C.otherEnd('lo'), 'hi', 'the switch moves from lowest'); eq(C.otherEnd('hi'), 'lo', 'the switch moves from highest');
// a drag is computed from the range at the press, so going back undoes the push
{ const from = [48, 72]; eq(C.setEnd(...from, 'lo', 100), [100, 100], 'drag far'); eq(C.setEnd(...from, 'lo', 60), [60, 72], 'drag back undoes the push'); }

// ---- velocity window ---------------------------------------------------------------------------------------------------------
eq(C.limitEnd(1, 127, 'lo', 40, 1, 127), [40, 127], 'lowest handle'); eq(C.limitEnd(40, 127, 'hi', 100, 1, 127), [40, 100], 'highest handle');
eq(C.limitEnd(40, 100, 'lo', 110, 1, 127), [100, 100], 'the lowest stops at the highest'); eq(C.limitEnd(40, 100, 'hi', 10, 1, 127), [40, 40], 'the highest stops at the lowest');
eq(C.limitEnd(40, 100, 'lo', -5, 1, 127), [1, 100], 'clamped to 1'); eq(C.limitEnd(40, 100, 'hi', 500, 1, 127), [40, 127], 'clamped to 127');
// a press at pixel px; the handles' centres are at pixels cl and ch, a handle is 8 px to each side
eq(C.grab(100, 200, 105, 8), { end: 'lo', onHandle: true, offset: 5 }, 'a press on the lowest handle keeps its offset'); eq(C.grab(100, 200, 193, 8), { end: 'hi', onHandle: true, offset: -7 }, 'a press on the highest handle');
eq(C.grab(100, 200, 140, 8), { end: 'lo', onHandle: false, offset: 0 }, 'a press on the bare track takes the nearer handle'); eq(C.grab(100, 200, 170, 8), { end: 'hi', onHandle: false, offset: 0 }, 'nearer the highest');
eq(C.grab(100, 200, 150, 8).end, null, 'halfway: the drag decides'); eq(C.grab(100, 100, 100, 8), { end: null, onHandle: true, offset: 0 }, 'on handles that sit together the drag decides');
eq(C.grab(100, 108, 106, 8).end, 'hi', 'overlapping handles: the nearer centre wins, not the one on top'); eq(C.grab(100, 108, 101, 8).end, 'lo', 'overlapping handles: the lower one can be picked');
eq(C.sideEnd(40, 40, 10), 'lo', 'a click below handles that sit together takes the lowest'); eq(C.sideEnd(40, 40, 100), 'hi', 'a click above takes the highest'); eq(C.sideEnd(40, 40, 40), null, 'a click on them decides nothing');
eq(C.sideEnd(40, 80, 60), null, 'a click between two distinct handles is left to the nearer-handle rule');
eq(C.endByDrag(40, 40, 30), 'lo', 'dragging down from together takes the lowest'); eq(C.endByDrag(40, 40, 50), 'hi', 'dragging up from together takes the highest'); eq(C.endByDrag(40, 40, 40), null, 'no move yet');
eq(C.endByDrag(1, 127, 20), 'lo', 'halfway drag toward the lowest'); eq(C.endByDrag(1, 127, 100), 'hi', 'halfway drag toward the highest');
eq(C.valueAt(0, 0, 100, 1, 127), 1, 'track start'); eq(C.valueAt(100, 0, 100, 1, 127), 127, 'track end'); eq(C.valueAt(50, 0, 100, 1, 127), 64, 'track middle');
eq(C.valueAt(-20, 0, 100, 1, 127), 1, 'before the track'); eq(C.valueAt(200, 0, 100, 1, 127), 127, 'past the track'); eq(C.valueAt(5, 0, 0, 1, 127) >= 1, true, 'a zero-width track does not divide by zero');

check(C.segmented === undefined && C.keyRange === undefined && C.dualSlider === undefined, 'the builders are not offered where there is no document');
console.log(`${checks} checks, ${failures} failures`);
process.exit(failures ? 1 : 0);
