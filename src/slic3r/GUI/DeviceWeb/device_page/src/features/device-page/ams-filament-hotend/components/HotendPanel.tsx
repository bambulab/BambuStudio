import type { AmsFilamentHotendSlotViewModel } from '../types';
import { HotendCard } from './HotendCard';

export function HotendPanel({
  slots,
  onAction,
}: {
  slots: AmsFilamentHotendSlotViewModel[];
  onAction: (action: string, slot?: AmsFilamentHotendSlotViewModel) => void;
}) {
  const ordered = [...slots].sort((a, b) => a.display_index - b.display_index);

  return (
    <section className="hotend-panel" data-testid="ams-hotend-hotend-panel">
      <div className="hotend-strip">
        {ordered.map((slot) => (
          <HotendCard
            key={slot.display_index}
            slot={slot}
            onAction={onAction}
          />
        ))}
      </div>
    </section>
  );
}
