import type { SlotLayout } from './geometry';
import type { AmsListUnit, ExtruderView, UnitView } from './types';

// C++ resolves the unit layout while building the hub, so the page no longer
// sniffs machine types here: it only renames the hub kind to its road layout.
export function unitSlotLayout(unit: UnitView): SlotLayout {
  if (unit.hub.kind === 'cross4') return 'lite_cross';
  if (unit.hub.kind === 'passthrough') return 'single_ht';
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
