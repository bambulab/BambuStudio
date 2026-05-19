// STUDIO-18344: regression tests for the AMS tray -> Spool payload helper.
//
// How to run (the device_page tests have no Vitest/Jest runner; they are
// plain Node scripts that use `node:assert/strict`):
//
//   # vendorOptions.test.ts / customColorSelection.test.ts only carry
//   # `import type` so they run directly through node's strip-types path:
//     node --experimental-strip-types --no-warnings tests/vendorOptions.test.ts
//
//   # This file imports runtime helpers (constants / colors) so the
//   # relative imports must be resolved by a bundler before node can
//   # execute the test. Bundle through the project's existing esbuild
//   # (already a transitive dep of vite), then run the bundle:
//     node node_modules/.pnpm/esbuild@0.25.5/node_modules/esbuild/bin/esbuild \
//          tests/buildSpoolFromTray.test.ts \
//          --bundle --platform=node --format=esm \
//          --external:node:* --outfile=.tmp-test.mjs && \
//     node .tmp-test.mjs && rm .tmp-test.mjs
//
// Scenarios covered:
//   1. RFID tray with a known setting_id -> reverse-lookup populates
//      brand / material / series / setting_id.
//   2. Non-RFID tray (no setting_id, no tag_uid) -> falls back to
//      sub_brands + fila_type string-matching and the payload is created
//      as a brand new spool (no existingSpoolId).
//   3. Multi-hex tray -> colors[] is forwarded and color_type stays in
//      multicolor / gradient mode. Single-hex tray emits colors=[] and
//      color_type=2 to honour the Form invariant.
//   4. tag_uid hits an existing spool -> existingSpoolId is set and
//      partitionTraysForBatchCreate routes it into the updates[] bucket
//      with the spool_id stamped on.

import assert from 'node:assert/strict';
import {
  buildSpoolFromTray,
  partitionTraysForBatchCreate,
  resolveTrayPreset,
} from '../src/features/filament-manager/buildSpoolFromTray.ts';
import type {
  AmsTray, AmsUnit, PresetVendor, Spool,
} from '../src/features/filament-manager/types.ts';

const presets: PresetVendor[] = [
  {
    name: 'Bambu Lab',
    types: [
      {
        name: 'PLA',
        series: ['Basic'],
        items: [
          { series: 'Basic', filament_id: 'GFA00', setting_id: 'GFA00', name: 'Bambu PLA Basic' },
          // Decoy user-derived preset that shares filament_id. Must NOT
          // be matched (STUDIO-18110 / STUDIO-18117 guard).
          { series: 'Basic', filament_id: 'GFA00', setting_id: 'user-GFA00', name: 'User PLA Basic', is_user: true },
        ],
      },
      {
        name: 'PETG',
        series: ['HF'],
        items: [
          { series: 'HF', filament_id: 'GFG02', setting_id: 'GFG02', name: 'Bambu PETG HF' },
        ],
      },
    ],
  },
];

const unit: AmsUnit = {
  ams_id: '0',
  ams_type: 0,
  trays: [],
};

const rfidTray: AmsTray = {
  slot_id: '0',
  is_exists: true,
  tag_uid: 'TAG-ABCDEF',
  setting_id: 'GFA00',
  fila_type: 'PLA',
  sub_brands: 'Bambu Lab',
  color: '#FF8800',
  weight: '1000',
  remain: 60,
  diameter: 1.75,
};

// Third-party tray: no setting_id, no sub_brands, fila_type matches a
// preset entry so material_type can still be auto-resolved. tag_uid is
// the all-zero placeholder firmware writes for non-RFID slots, which
// must NOT count as a match against any existing spool.
const thirdPartyTray: AmsTray = {
  slot_id: '1',
  is_exists: true,
  tag_uid: '0000000000000000',
  setting_id: '',
  fila_type: 'PETG HF',
  sub_brands: '',
  color: '#003355',
  weight: '750',
  remain: 100,
  diameter: '1.75',
};

const multiHexTray: AmsTray = {
  slot_id: '2',
  is_exists: true,
  tag_uid: 'TAG-MULTI01',
  setting_id: 'GFA00',
  fila_type: 'PLA',
  sub_brands: 'Bambu Lab',
  color: 'AABBCCFF',
  colors: ['#FF0000', '0000FF'],
  color_type: 1,
  weight: 1000,
  remain: 100,
  diameter: 1.75,
};

const existingSpool: Spool = {
  spool_id: 'sp-existing-1',
  brand: 'Bambu Lab',
  material_type: 'PLA',
  series: 'Bambu PLA Basic',
  color_code: '#FF8800',
  color_name: '',
  diameter: 1.75,
  initial_weight: 1000,
  spool_weight: 0,
  remain_percent: 80,
  status: 'active',
  favorite: false,
  entry_method: 'ams_sync',
  note: '',
  tag_uid: 'TAG-ABCDEF',
  setting_id: 'GFA00',
};

const otherSpool: Spool = {
  ...existingSpool,
  spool_id: 'sp-other',
  tag_uid: 'TAG-OTHER',
};

const spools: Spool[] = [existingSpool, otherSpool];

// ---- 1. RFID tray + setting_id resolves the preset ----

const r1 = resolveTrayPreset(rfidTray, presets);
assert.equal(r1.brandName, 'Bambu Lab', 'brand resolved from setting_id');
assert.equal(r1.typeName, 'PLA');
assert.equal(r1.filamentName, 'PLA Basic', 'filamentName stripped of vendor prefix');
assert.equal(r1.matchedSettingId, 'GFA00');
// Decoy `is_user` preset must not be hit, even though it shares filament_id.
assert.notEqual(r1.filamentName, 'User PLA Basic');

const built1 = buildSpoolFromTray({
  tray: rfidTray,
  unit,
  devId: 'dev-1',
  presets,
  spools,
});
assert.equal(built1.payload.brand, 'Bambu Lab');
assert.equal(built1.payload.material_type, 'PLA');
assert.equal(built1.payload.series, 'PLA Basic');
assert.equal(built1.payload.color_code, '#FF8800');
assert.equal(built1.payload.tag_uid, 'TAG-ABCDEF');
assert.equal(built1.payload.setting_id, 'GFA00');
assert.equal(built1.payload.entry_method, 'ams_sync');
assert.equal(built1.payload.initial_weight, 1000);
assert.equal(built1.payload.net_weight, 600, 'current net = initial * remain%');
assert.equal(built1.payload.remain_percent, 60);
assert.equal(built1.payload.diameter, 1.75);
assert.equal(built1.payload.bound_ams_id, '0');
assert.equal(built1.payload.bound_dev_id, 'dev-1');
assert.deepEqual(built1.payload.colors, []);
assert.equal(built1.payload.color_type, 2);
assert.equal(built1.existingSpoolId, 'sp-existing-1',
  'tag_uid match flips the entry into the updates bucket');

// ---- 2. Third-party tray, no setting_id, fallback path ----

const built2 = buildSpoolFromTray({
  tray: thirdPartyTray,
  unit,
  devId: 'dev-1',
  presets,
  spools,
});
// Empty tag_uid (all zeros) must not collide with any existing spool.
assert.equal(built2.existingSpoolId, '', 'all-zero tag_uid never matches existing spools');
assert.equal(built2.payload.brand, '',
  'with no sub_brands and no preset match the brand stays empty');
assert.equal(built2.payload.material_type, 'PETG',
  'fila_type prefix matches the preset type');
assert.equal(built2.payload.series, 'PETG HF',
  'remainder after the type prefix becomes the series');
assert.equal(built2.payload.color_code, '#003355');
assert.equal(built2.payload.initial_weight, 750);
assert.equal(built2.payload.remain_percent, 100);
assert.equal(built2.payload.entry_method, 'ams_sync');
assert.equal(built2.payload.diameter, 1.75);

// ---- 3. Multi-hex tray keeps colors[] and color_type ----

const built3 = buildSpoolFromTray({
  tray: multiHexTray,
  unit,
  devId: 'dev-1',
  presets,
  spools,
});
assert.deepEqual(built3.payload.colors, ['#FF0000', '#0000FF'],
  'colors[] entries get prefixed with # and trimmed to 7 chars');
assert.equal(built3.payload.color_type, 1, 'multicolor type preserved');
assert.equal(built3.payload.color_code, '#AABBCC',
  'tray.color normalized: alpha byte stripped, # prefix added');
assert.equal(built3.isMultiHex, true);

// Single-hex tray honours the form invariant.
assert.equal(built1.isMultiHex, false);
assert.deepEqual(built1.payload.colors, []);
assert.equal(built1.payload.color_type, 2);

// ---- 4. partitionTraysForBatchCreate routes the right buckets ----

const { creates, updates } = partitionTraysForBatchCreate([built1, built2, built3]);
assert.equal(creates.length, 2, 'thirdParty + multiHex (no tag_uid match) go to creates');
assert.equal(updates.length, 1, 'RFID hit on existing spool goes to updates');
assert.equal(updates[0].spool_id, 'sp-existing-1');
assert.equal(updates[0].brand, 'Bambu Lab',
  'update payload carries the same AMS-derived fields as a create');
assert.ok(!('spool_id' in creates[0]) || !creates[0].spool_id,
  'create payloads must not carry a spool_id');

// ---- 5. Empty AMS tray (is_exists === false) is still buildable, but
// callers (UI / handleSubmit) filter on `is_exists` before calling. We
// still ensure the helper does not crash on minimal input. ----

const minimal: AmsTray = { slot_id: '3', is_exists: true };
const builtMin = buildSpoolFromTray({
  tray: minimal,
  unit,
  devId: 'dev-1',
  presets: [],
  spools: [],
});
assert.equal(builtMin.payload.entry_method, 'ams_sync');
assert.equal(builtMin.payload.initial_weight, 1000, 'defaults to 1000g when tray.weight missing');
assert.equal(builtMin.payload.color_code, '#888888', 'falls back to neutral grey hex');

console.log('buildSpoolFromTray.test ok');
