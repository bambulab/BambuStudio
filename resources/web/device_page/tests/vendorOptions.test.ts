import assert from 'node:assert/strict';
import { buildVendorOptions } from '../src/features/filament/vendorOptions.ts';
import type { PresetVendor, Spool } from '../src/features/filament/types.ts';

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
