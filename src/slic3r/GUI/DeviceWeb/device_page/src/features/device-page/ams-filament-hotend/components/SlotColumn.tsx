import type { AmsFilamentHotendSlotViewModel } from '../types';
import { FilamentCard } from './FilamentCard';
import { FeedLine } from './FeedLine';
import { Nozzle } from './Nozzle';

export function SlotColumn({
  slot,
  lineHeight,
  globalBusy,
  onAction,
}: {
  slot: AmsFilamentHotendSlotViewModel;
  lineHeight: number;
  globalBusy?: boolean;
  onAction: (action: string, slot: AmsFilamentHotendSlotViewModel) => void;
}) {
  return (
    <div
      className="slot-column"
      data-tier={slot.is_top_tier ? 'top' : 'bottom'}
      data-in-dryer={slot.in_dryer ? 'true' : 'false'}
    >
      <FilamentCard slot={slot} globalBusy={globalBusy} onAction={onAction} />
      <FeedLine height={lineHeight} state={slot.line_state} />
      <Nozzle nozzleState={slot.nozzle_state} displayIndex={slot.display_index} />
    </div>
  );
}
