import assert from 'node:assert/strict';
import { buildVendorOptions, sameVendor, uniqueVendorNames, matchingVendorItems } from '../src/features/filament-manager/vendorOptions.ts';
import type { PresetVendor, Spool } from '../src/features/filament-manager/types.ts';

const presets: PresetVendor[] = [
  { name: 'Bambu Lab', types: [] },
  { name: 'Generic', types: [] },
];

const spools: Spool[] = [
  {
    spool_id: 'custom-1',
    brand: 'HATCHBOX',
    material_type: 'PP',
    series: 'PP Basic',
    color_code: '#FF8AC6',
    color_name: '',
    diameter: 1.75,
    initial_weight: 200,
    spool_weight: 0,
    remain_percent: 100,
    status: 'active',
    favorite: false,
    entry_method: 'manual',
    note: '',
  },
];

const vendors = buildVendorOptions(presets, ['Overture'], spools);

assert.deepEqual(vendors, ['Bambu Lab', 'Generic', 'HATCHBOX', 'Overture']);

// A vendor supplied by presets, cloud config, or a spool has one identity.
// Keep the first spelling for display, and leave all source records intact.
const mixedPresets: PresetVendor[] = [
  { name: 'Elegoo', types: [{ name: 'ASA-CF', series: ['Basic'], items: [
    { series: 'Basic', filament_id: 'Poriginal', setting_id: 'original', name: 'Elegoo ASA-CF Basic' },
  ] }] },
  { name: 'ELEGOO', types: [{ name: 'PETG', series: ['Rapid'], items: [
    { series: 'Rapid', filament_id: 'Puppercase', setting_id: 'uppercase', name: 'ELEGOO PETG Rapid' },
  ] }] },
];
const mixedSpools = [{ ...spools[0], brand: 'elegoo' }];
const before = JSON.stringify({ mixedPresets, mixedSpools });
assert.deepEqual(buildVendorOptions(mixedPresets, ['ELEGOO', 'Overture', 'OVERTURE'], mixedSpools), ['Elegoo', 'Overture']);
assert.deepEqual(uniqueVendorNames(['eSUN', 'ESUN', 'eSun', 'SUNLU']), ['eSUN', 'SUNLU']);
assert(sameVendor('Elegoo', 'ELEGOO'));
assert(sameVendor('Overture', 'OVERTURE'));
assert(!sameVendor('Elegoo', 'eSUN'));
// Both vendor buckets still participate when a brand is selected.
const ids = mixedPresets.filter((vendor) => sameVendor(vendor.name, 'elegoo'))
  .flatMap((vendor) => vendor.types.flatMap((type) => type.items ?? []))
  .map((item) => item.filament_id);
assert.deepEqual(ids, ['Poriginal', 'Puppercase']);
assert.equal(JSON.stringify({ mixedPresets, mixedSpools }), before);
const matches = matchingVendorItems(mixedPresets, 'ELEGOO', (vendor) => vendor.name);
assert.equal(matches.length, 2);
assert.equal(matches[0].types[0].items![0].filament_id, 'Puppercase');
assert.equal(matches[1].types[0].items![0].filament_id, 'Poriginal');
assert.equal(matchingVendorItems(mixedPresets, 'elegoo', (vendor) => vendor.name).length, 2);
assert.equal(matchingVendorItems(mixedPresets, 'eSUN', (vendor) => vendor.name).length, 0);
console.log('vendorOptions.test ok');
