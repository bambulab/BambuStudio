import type { ConnectedSlotsColumn } from './types';

function sameColor(a?: string, b?: string): boolean {
  if (!a || !b) return false;
  return a.trim().toLowerCase() === b.trim().toLowerCase();
}

/**
 * Slot whose feed line is painted solid, or null when the column is idle. The
 * Schema carries no explicit "this tray feeds the hotend" marker yet, so match
 * the line colour and fall back to the last tray.
 */
export function resolveFeedTraySlotId(column: ConnectedSlotsColumn): string | null {
  const trays = column.unit?.trays ?? [];
  if (trays.length === 0 || !column.line.has_filament) return null;
  for (let i = trays.length - 1; i >= 0; i -= 1) {
    if (sameColor(trays[i].color, column.line.filament_color)) return trays[i].slot_id;
  }
  return trays[trays.length - 1].slot_id;
}
