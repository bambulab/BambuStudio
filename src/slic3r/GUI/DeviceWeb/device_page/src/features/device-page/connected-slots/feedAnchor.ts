import type { ConnectedSlotsColumn } from './types';

function sameColor(a?: string, b?: string): boolean {
  if (!a || !b) return false;
  return a.trim().toLowerCase() === b.trim().toLowerCase();
}

/**
 * Slot the column's feed line starts from. The Schema carries no explicit
 * "this tray feeds the hotend" marker yet, so match the line colour and fall
 * back to the last tray, which is where Figma anchors an idle column.
 */
export function resolveFeedTraySlotId(column: ConnectedSlotsColumn): string | null {
  const trays = column.unit?.trays ?? [];
  if (trays.length === 0) return null;
  if (column.line.has_filament) {
    for (let i = trays.length - 1; i >= 0; i -= 1) {
      if (sameColor(trays[i].color, column.line.filament_color)) return trays[i].slot_id;
    }
  }
  return trays[trays.length - 1].slot_id;
}
