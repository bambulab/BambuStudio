import type {
  AmsFilamentHotendSlotActionPayload,
  AmsFilamentHotendSlotViewModel,
  HotendRackStatus,
  HotendSlotViewModel,
} from '../types';
import { HotendCard } from './HotendCard';
import { HotendRackProgressPanel } from './HotendRackProgressPanel';

export function HotendPanel({
  slots,
  rackStatus,
  onAction,
}: {
  slots: HotendSlotViewModel[];
  rackStatus?: HotendRackStatus;
  onAction: (
    action: string,
    slot?: AmsFilamentHotendSlotViewModel | HotendSlotViewModel,
    payload?: AmsFilamentHotendSlotActionPayload,
  ) => void;
}) {
  const ordered = [...slots].sort((a, b) => a.display_index - b.display_index);
  const isRackBusy = !!rackStatus?.active;

  if (rackStatus?.active && rackStatus.mode === 'all') {
    return <HotendRackProgressPanel status={rackStatus} onAction={(action) => onAction(action)} />;
  }

  return (
    <section className="hotend-panel" data-testid="ams-hotend-hotend-panel">
      <div className="hotend-strip">
        {ordered.map((slot) => (
          <HotendCard
            key={slot.display_index}
            slot={slot}
            globalBusy={isRackBusy}
            onAction={onAction}
          />
        ))}
      </div>
      {rackStatus?.active && rackStatus.mode === 'single' && (
        <HotendRackProgressPanel status={rackStatus} onAction={(action) => onAction(action)} />
      )}
    </section>
  );
}
