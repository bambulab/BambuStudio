// Run: node tests/filamentSelectChipPaging.test.ts
import assert from 'node:assert/strict';
import {
  chipWindowGeometry,
  chipsWithResults,
  isRowVisible,
  normalizeQuery,
  pageChips,
} from '../src/features/device-page/filament-select/chipPaging.ts';
import type { FilamentSelectChip, FilamentSelectRow } from '../src/features/device-page/filament-select/types.ts';

function chip(key: string, kind: FilamentSelectChip['kind'] = 'brand'): FilamentSelectChip {
  return { key, kind, label: key };
}

function row(rowKey: string, chipKey: string, searchKey: string): FilamentSelectRow {
  return {
    row_key: rowKey,
    chip_key: chipKey,
    search_key: searchKey,
    name: searchKey,
    weight_g: 0,
    swatch: { color_type: 2, colors: [], color_code: '#000000' },
    show_bbl_logo: false,
    tail: { kind: 'none', slot_name: '' },
    selectable: true,
  };
}

// ---- query normalisation (native Lower().Trim(false).Trim(true)) ----
assert.equal(normalizeQuery('  PLA Basic '), 'pla basic');
assert.equal(normalizeQuery(''), '');

// ---- row filtering ----
const rows = [
  row('r0', 'brand:A', 'a pla red'),
  row('r1', 'brand:B', 'b petg blue'),
  row('r2', 'unsupported', 'c pla green'),
];
assert.equal(isRowVisible(rows[0], 'all', ''), true);
assert.equal(isRowVisible(rows[0], '', ''), true, 'no chip yet behaves like All');
assert.equal(isRowVisible(rows[1], 'brand:A', ''), false);
assert.equal(isRowVisible(rows[0], 'brand:A', 'pla'), true);
assert.equal(isRowVisible(rows[0], 'brand:A', 'petg'), false);
assert.equal(isRowVisible(rows[2], 'all', 'pla'), true);

// ---- chips hidden while searching: every chip but All needs a hit ----
const chips = [chip('all', 'all'), chip('brand:A'), chip('brand:B'), chip('unsupported', 'unsupported')];
assert.deepEqual(chipsWithResults(chips, rows, ''), [true, true, true, true]);
assert.deepEqual(chipsWithResults(chips, rows, 'pla'), [true, true, false, true]);
assert.deepEqual(chipsWithResults(chips, rows, 'zzz'), [true, false, false, false]);

// ---- paging ----
// All = 40, five brand chips of 50 each; 40 + 2 * 50 = 140 fits exactly two.
const widths = [40, 50, 50, 50, 50, 50];
const all = [true, true, true, true, true, true];

let p = pageChips({ widths, allIndex: 0, hasResults: all, offset: 0, available: 140 });
assert.deepEqual(p.visible, [true, true, true, false, false, false]);
assert.equal(p.canLeft, false);
assert.equal(p.canRight, true);

p = pageChips({ widths, allIndex: 0, hasResults: all, offset: 3, available: 140 });
assert.deepEqual(p.visible, [true, false, false, false, true, true], 'All stays pinned');
assert.equal(p.canLeft, true);
assert.equal(p.canRight, false);

// Offset past the end is clamped to the last surviving chip.
p = pageChips({ widths, allIndex: 0, hasResults: all, offset: 99, available: 140 });
assert.equal(p.offset, 4);
assert.deepEqual(p.visible, [true, false, false, false, false, true]);

// A chip wider than the space is still shown alone (native: at least one).
p = pageChips({ widths: [40, 500], allIndex: 0, hasResults: [true, true], offset: 0, available: 100 });
assert.deepEqual(p.visible, [true, true]);
assert.equal(p.canRight, false);

// Chips without search hits are skipped; offset counts surviving chips only.
p = pageChips({
  widths,
  allIndex: 0,
  hasResults: [true, false, true, false, true, true],
  offset: 1,
  available: 140,
});
assert.deepEqual(p.visible, [true, false, false, false, true, true]);
assert.equal(p.canLeft, true);
assert.equal(p.canRight, false);

// Nothing survives the search: only All, arrows off, offset back to 0.
p = pageChips({ widths, allIndex: 0, hasResults: [true, false, false, false, false, false], offset: 2, available: 140 });
assert.deepEqual(p.visible, [true, false, false, false, false, false]);
assert.equal(p.offset, 0);
assert.equal(p.canLeft, false);
assert.equal(p.canRight, false);

// Not laid out yet: everything visible, arrows off.
p = pageChips({ widths, allIndex: 0, hasResults: all, offset: 0, available: 0 });
assert.deepEqual(p.visible, all);
assert.equal(p.canRight, false);

// ---- sliding window geometry ----
// Surviving chips and the window come with the paging result.
p = pageChips({ widths, allIndex: 0, hasResults: all, offset: 2, available: 140 });
assert.deepEqual(p.surviving, [1, 2, 3, 4, 5]);
assert.equal(p.last, 3);
assert.equal(p.measured, true);
// Two chips paged out on the left (2 * 50), two whole chips in the window.
assert.deepEqual(chipWindowGeometry(widths, p.surviving, p.offset, p.last), { trackShift: 100, windowWidth: 100 });

// Skipped (no search hit) chips are not on the track, so they take no space.
p = pageChips({
  widths: [40, 50, 30, 70, 50, 50],
  allIndex: 0,
  hasResults: [true, true, false, true, true, true],
  offset: 1,
  available: 160,
});
assert.deepEqual(p.surviving, [1, 3, 4, 5]);
assert.deepEqual(chipWindowGeometry([40, 50, 30, 70, 50, 50], p.surviving, p.offset, p.last), {
  trackShift: 50,
  windowWidth: 120,
});

// First page: nothing shifted.
p = pageChips({ widths, allIndex: 0, hasResults: all, offset: 0, available: 140 });
assert.deepEqual(chipWindowGeometry(widths, p.surviving, p.offset, p.last), { trackShift: 0, windowWidth: 100 });

// Not laid out yet: every surviving chip is on the track, flagged unmeasured.
p = pageChips({ widths, allIndex: 0, hasResults: all, offset: 3, available: 0 });
assert.equal(p.measured, false);
assert.deepEqual(p.surviving, [1, 2, 3, 4, 5]);
assert.equal(p.last, 4);

// Nothing survives: empty window.
p = pageChips({ widths, allIndex: 0, hasResults: [true, false, false, false, false, false], offset: 0, available: 140 });
assert.deepEqual(chipWindowGeometry(widths, p.surviving, p.offset, p.last), { trackShift: 0, windowWidth: 0 });

console.log('filamentSelectChipPaging: ok');
