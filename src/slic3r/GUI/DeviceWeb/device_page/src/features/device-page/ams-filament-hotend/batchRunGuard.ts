import type { AmsFilamentHotendSlotViewModel, FilamentPageMode } from './types';

export type BatchFilamentEditPayload = {
  intent: 'edit' | 'run';
  mode: FilamentPageMode;
  slots: Array<{
    addr_id: number;
    ams_id: string;
    slot_id: string;
    display_index: number;
  }>;
};

const EDIT_BEFORE_RUN_MODES: ReadonlySet<FilamentPageMode> = new Set([
  'batch_ams_load',
  'batch_ext_load',
]);

export function shouldOpenBatchFilamentEditBeforeRun(
  mode: FilamentPageMode,
  slots: AmsFilamentHotendSlotViewModel[],
) {
  return EDIT_BEFORE_RUN_MODES.has(mode)
    && slots.some((slot) => slot.filament_card.state === 'Unknown');
}

export function buildBatchFilamentEditPayload(
  mode: FilamentPageMode,
  slots: AmsFilamentHotendSlotViewModel[],
  intent: 'edit' | 'run' = 'run',
): BatchFilamentEditPayload {
  return {
    intent,
    mode,
    slots: slots.map((slot) => ({
      addr_id: slot.addr_id,
      ams_id: slot.ams_id,
      slot_id: slot.slot_id,
      display_index: slot.display_index,
    })),
  };
}
