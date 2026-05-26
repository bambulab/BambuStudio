import type { AmsFilamentHotendSlotViewModel } from '../types';

export function CardText({ slot }: { slot: AmsFilamentHotendSlotViewModel }) {
  const card = slot.filament_card;
  const isEmpty = card.state === 'Empty';
  const isUnknown = card.state === 'Unknown';

  // Loading / Unloading slots intentionally fall through to the default
  // branch: the tray still holds known filament during a change, and the
  // user expects to see the filament type (e.g. "PLA") under the spool —
  // the spinning indicator on the card head already conveys "in transit".
  const primary = isEmpty
    ? 'Empty'
    : isUnknown
    ? '?'
    : card.type || card.name || '';

  return (
    <div className="card-text" data-card-state={card.state}>
      <div className="card-name-row">
        <span
          className={`card-name${isEmpty ? ' placeholder' : ''}${isUnknown ? ' card-name-unknown' : ''}`}
          data-testid={`ams-hotend-slot-name-${slot.display_index}`}
          title={card.name || primary}
        >
          {primary}
        </span>
      </div>
    </div>
  );
}
