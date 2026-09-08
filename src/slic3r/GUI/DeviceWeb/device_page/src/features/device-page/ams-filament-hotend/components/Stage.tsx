import type { CSSProperties } from 'react';
import type { AmsFilamentHotendDryerZone, AmsFilamentHotendSlotViewModel, FilamentPageMode } from '../types';
import { SlotColumn } from './SlotColumn';
import { DryerChamberDecoration, findDryerRuns } from './DryerChamber';

// Top feed lines cross the lower row before reaching the shared nozzle row.
export const TOP_LINE_HEIGHT = 167;
export const BOTTOM_LINE_HEIGHT = 26;

// Slot column width, kept in sync with `.slot-column` / `.filament-card`.
const COLUMN_WIDTH = 80;

// Horizontal distance between two neighbouring slots, counting both tiers as
// one evenly spaced sequence (bottom, top, bottom, ...). The outermost columns
// touch the stage edges, so the pitch depends on the total slot count: with 9
// slots it is 1/8 of the free width, with 5 slots 1/4 of it. Deriving it here
// keeps the tiers interleaved for any slot count instead of hard-coding the
// 9-slot case in CSS. cqw resolves against `.stage` (see the CSS).
export function stagePitchStyle(totalSlots: number): CSSProperties {
  const gaps = totalSlots - 1;
  return {
    ['--slot-pitch' as never]: gaps > 0 ? `calc((100cqw - ${COLUMN_WIDTH}px) / ${gaps})` : '0px',
  } as CSSProperties;
}

// Stable key for the local selection set. Composes ams_id + slot_id so two
// slots that share a display_index (rare but possible across AMS units) do
// not collide. Used by both <Stage> and <BatchModeFooter>.
export function selectionKey(slot: { ams_id: string; slot_id: string }): string {
  return `${slot.ams_id}::${slot.slot_id}`;
}

export function Stage({
  topSlots,
  bottomSlots,
  amsDryZones,
  hotendMode,
  mode,
  selectedKeys,
  onToggleSelect,
  onAction,
}: {
  topSlots: AmsFilamentHotendSlotViewModel[];
  bottomSlots: AmsFilamentHotendSlotViewModel[];
  amsDryZones: AmsFilamentHotendDryerZone[];
  hotendMode: boolean;
  mode: FilamentPageMode;
  selectedKeys: ReadonlySet<string>;
  onToggleSelect: (slot: AmsFilamentHotendSlotViewModel) => void;
  onAction: (action: string, slot: AmsFilamentHotendSlotViewModel) => void;
}) {
  // Dryer chambers are physically attached to the top tier in the current
  // hardware layout; only top slots can fall under a chamber decoration.
  const dryerRuns = findDryerRuns(topSlots, amsDryZones);

  return (
    <section
      className={`stage${hotendMode ? ' hotend-mode' : ''}${mode !== 'normal' ? ' batch-mode' : ''}`}
      style={stagePitchStyle(topSlots.length + bottomSlots.length)}
    >
      <div className="top-tier-row" data-testid="ams-hotend-top-tier">
        {dryerRuns.map((run) => (
          <DryerChamberDecoration
            key={`dryer-${run.start}-${run.end}`}
            startIndex={run.start}
            endIndex={run.end}
            totalSlots={topSlots.length}
            humidity={run.humidity}
            status={run.status}
            isDrying={run.isDrying}
          />
        ))}
        {topSlots.map((slot) => (
          <SlotColumn
            key={slot.display_index}
            slot={slot}
            lineHeight={TOP_LINE_HEIGHT}
            mode={mode}
            batchChecked={selectedKeys.has(selectionKey(slot))}
            onToggleSelect={onToggleSelect}
            onAction={onAction}
          />
        ))}
      </div>

      <div className="bottom-tier-row" data-testid="ams-hotend-bottom-tier">
        {bottomSlots.map((slot) => (
          <SlotColumn
            key={slot.display_index}
            slot={slot}
            lineHeight={BOTTOM_LINE_HEIGHT}
            mode={mode}
            batchChecked={selectedKeys.has(selectionKey(slot))}
            onToggleSelect={onToggleSelect}
            onAction={onAction}
          />
        ))}
      </div>
    </section>
  );
}
