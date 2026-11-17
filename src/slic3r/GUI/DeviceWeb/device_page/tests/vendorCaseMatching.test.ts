import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { resolveTrayPreset } from '../src/features/filament-manager/buildSpoolFromTray.ts';
import type { PresetVendor } from '../src/features/filament-manager/types.ts';

// A tray's vendor spelling must not hide the preset's material types.
const presets: PresetVendor[] = [{ name: 'Elegoo', types: [
  { name: 'ASA-CF', series: ['Basic'], items: [] },
] }];
for (const brand of ['Elegoo', 'ELEGOO', 'elegoo']) {
  const tray = { slot_id: '0', is_exists: true, sub_brands: brand, fila_type: 'ASA-CF Basic' };
  const before = JSON.stringify({ presets, tray });
  const result = resolveTrayPreset(tray, presets);
  assert.equal(result.brandName, brand);
  assert.equal(result.typeName, 'ASA-CF');
  assert.equal(result.seriesName, 'Basic');
  assert.equal(JSON.stringify({ presets, tray }), before);
}

// Matching by the device-reported ID continues to take precedence over spelling.
const byId: PresetVendor[] = [{ name: 'Elegoo', types: [
  { name: 'ASA-CF', series: ['Basic'], items: [
    { filament_id: 'Pold-id', setting_id: 'Sold-id', series: 'Basic', name: 'ELEGOO ASA-CF Basic' },
  ] },
] }];
const result = resolveTrayPreset({
  slot_id: '0', is_exists: true, sub_brands: 'ELEGOO', setting_id: 'Pold-id',
}, byId);
assert.equal(result.brandName, 'Elegoo');
assert.equal(result.filamentName, 'ASA-CF Basic');
assert.equal(byId[0].types[0].items![0].filament_id, 'Pold-id');
assert.equal(byId[0].types[0].items![0].name, 'ELEGOO ASA-CF Basic');

// The creation wizard uses the same comparison policy without changing the
// spelling of a prefilled vendor or adding a second capitalization variant.
const context = vm.createContext({ Set });
vm.runInContext(fs.readFileSync(path.resolve(
  process.cwd(), '../../../../../resources/web/filament_create/vendor_names.js',
), 'utf8'), context);
const wizardVendors = context.FilamentVendorNames;
assert(wizardVendors.same('Elegoo', 'ELEGOO'));
assert(wizardVendors.same('eSUN', 'ESUN'));
assert(!wizardVendors.same('Elegoo', 'eSUN'));
assert.deepEqual(Array.from(wizardVendors.unique(['Elegoo', 'ELEGOO', 'elegoo', 'eSUN'])), ['Elegoo', 'eSUN']);
console.log('vendorCaseMatching.test ok');
