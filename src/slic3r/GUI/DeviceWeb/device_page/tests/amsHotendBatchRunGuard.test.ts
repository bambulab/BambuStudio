import assert from 'node:assert/strict';
import {
  buildBatchFilamentEditPayload,
  shouldOpenBatchFilamentEditBeforeRun,
} from '../src/features/device-page/ams-filament-hotend/batchRunGuard.ts';
import type { AmsFilamentHotendSlotViewModel } from '../src/features/device-page/ams-filament-hotend/types.ts';

function slot(state: AmsFilamentHotendSlotViewModel['filament_card']['state']): AmsFilamentHotendSlotViewModel {
  return {
    addr_id: 0,
    display_index: 1,
    ams_id: '0',
    slot_id: '1',
    is_top_tier: false,
    enabled: true,
    filament_card: {
      state,
      color: '#D9D9D9',
      text: '',
      name: '',
      type: '',
      status_text: '',
    },
    filament_line: { state: 'no_filament' },
    filament_nozzle: { state: 'Normal' },
  };
}

const selectedSlots = [slot('Unknown')];

assert.equal(shouldOpenBatchFilamentEditBeforeRun('batch_ams_load', selectedSlots), true);
assert.equal(shouldOpenBatchFilamentEditBeforeRun('batch_ext_load', selectedSlots), true);
assert.equal(shouldOpenBatchFilamentEditBeforeRun('batch_unload', selectedSlots), false);

assert.deepEqual(buildBatchFilamentEditPayload('batch_ams_load', selectedSlots), {
  intent: 'run',
  mode: 'batch_ams_load',
  slots: [
    {
      addr_id: 0,
      ams_id: '0',
      slot_id: '1',
      display_index: 1,
    },
  ],
});

console.log('amsHotendBatchRunGuard tests passed');
