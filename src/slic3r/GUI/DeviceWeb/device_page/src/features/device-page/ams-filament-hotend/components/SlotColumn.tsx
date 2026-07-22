import type { AmsFilamentHotendSlotViewModel, FilamentPageMode } from '../types';
import { FilamentCard } from './FilamentCard';
import { FeedLine } from './FeedLine';
import { Nozzle } from './Nozzle';

export function SlotColumn({
  slot,
  lineHeight,
  mode,
  batchChecked,
  onToggleSelect,
  onAction,
}: {
  slot: AmsFilamentHotendSlotViewModel;
  lineHeight: number;
  mode: FilamentPageMode;
  batchChecked: boolean;
  onToggleSelect: (slot: AmsFilamentHotendSlotViewModel) => void;
  onAction: (action: string, slot: AmsFilamentHotendSlotViewModel) => void;
}) {
  return (
    <div
      className="slot-column"
      data-tier={slot.is_top_tier ? 'top' : 'bottom'}
    >
      <FilamentCard
        slot={slot}
        mode={mode}
        batchChecked={batchChecked}
        onToggleSelect={onToggleSelect}
        onAction={onAction}
      />
      <FeedLine
        height={lineHeight}
        state={slot.filament_line.state}
        color={slot.filament_line.filament_color || slot.filament_card.color}
        hasFilamentInNozzle={slot.filament_line.has_filament}
      />
      <Nozzle
        nozzleState={slot.filament_nozzle.state}
        displayIndex={slot.display_index}
        color={slot.filament_nozzle.filament_color || slot.filament_card.color}
        hasFilamentInNozzle={slot.filament_nozzle.has_filament}
        onExtruder={slot.filament_nozzle.on_extruder}
        toolKind={slot.filament_nozzle.tool_kind}
        solid={slot.filament_line.state === 'loaded'}
      />
    </div>
  );
}
