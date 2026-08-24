import type { SlotLayout } from './geometry';
import type { AmsListUnit, ExtruderView, UnitView } from './types';

export function unitSlotLayout(unit: UnitView, dataUnits: AmsListUnit[]): SlotLayout {
  const data = dataUnits.find((item) => item.ams_id === unit.ams_id);
  if (data?.is_ams_lite_mixed || unit.ams_type_name === 'AMS_LITE') return 'lite_cross';
  if (unit.ams_type_name === 'N3S' || unit.slot_count === 1) return 'single_ht';
  return 'row4';
}

export function isLiteMachine(dataUnits: AmsListUnit[]): boolean {
  return dataUnits.some((unit) => unit.is_ams_lite_mixed || unit.ams_type_name === 'AMS_LITE');
}

export function extruderIconName(extruders: ExtruderView[], extruder: ExtruderView): string {
  if (extruder.icon) return extruder.icon;
  if (extruders.length <= 1) return 'single_nozzle_xp';
  return extruder.id === 1 ? 'left_nozzle' : 'right_nozzle';
}

export function extrudersLeftToRight(extruders: ExtruderView[]): ExtruderView[] {
  return [...extruders].sort((a, b) => b.id - a.id);
}
