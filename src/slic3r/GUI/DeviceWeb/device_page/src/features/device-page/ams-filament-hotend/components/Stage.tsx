import type { AmsFilamentHotendDryerZone, AmsFilamentHotendSlotViewModel } from '../types';
import { SlotColumn } from './SlotColumn';
import { DryerChamberDecoration, findDryerRuns } from './DryerChamber';

// Top feed lines cross the lower row before reaching the shared nozzle row.
export const TOP_LINE_HEIGHT = 167;
export const BOTTOM_LINE_HEIGHT = 26;

export function Stage({
  topSlots,
  bottomSlots,
  amsDryZones,
  hotendMode,
  onAction,
}: {
  topSlots: AmsFilamentHotendSlotViewModel[];
  bottomSlots: AmsFilamentHotendSlotViewModel[];
  amsDryZones: AmsFilamentHotendDryerZone[];
  hotendMode: boolean;
  onAction: (action: string, slot: AmsFilamentHotendSlotViewModel) => void;
}) {
  // Dryer chambers are physically attached to the top tier in the current
  // hardware layout; only top slots can fall under a chamber decoration.
  const dryerRuns = findDryerRuns(topSlots, amsDryZones);
  const allSlots = [...topSlots, ...bottomSlots];
  const globalBusy = allSlots.some((slot) =>
    slot.filament_line.state === 'loading' || slot.filament_line.state === 'unloading' || !!slot.is_running
  );

  return (
    <section className={`stage${hotendMode ? ' hotend-mode' : ''}`}>
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
            globalBusy={globalBusy}
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
            globalBusy={globalBusy}
            onAction={onAction}
          />
        ))}
      </div>
    </section>
  );
}
