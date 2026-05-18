import type { AmsFilamentHotendSlotViewModel } from '../types';
import { IndexPill } from './IndexPill';
import { MenuButton } from './MenuButton';
import { SpoolDisc } from './SpoolDisc';
import { CardText } from './CardText';

export function FilamentCard({
  slot,
  onAction,
}: {
  slot: AmsFilamentHotendSlotViewModel;
  onAction: (action: string, slot: AmsFilamentHotendSlotViewModel) => void;
}) {
  const cls = ['filament-card'];
  if (slot.selected) cls.push('selected');
  if (slot.is_running) cls.push('running');
  if (!slot.enabled) cls.push('disabled');

  return (
    <div
      className={cls.join(' ')}
      data-testid={`ams-hotend-slot-${slot.display_index}`}
      data-card-state={slot.card_state}
      data-line-state={slot.line_state}
    >
      <div className="card-spool-wrap">
        <div className="card-head">
          <IndexPill slot={slot} />
          <MenuButton slot={slot} onAction={onAction} />
        </div>
        <SpoolDisc slot={slot} />
      </div>
      <CardText slot={slot} />
    </div>
  );
}
