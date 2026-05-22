import type { AmsFilamentHotendSlotViewModel } from '../types';

export function CardText({ slot }: { slot: AmsFilamentHotendSlotViewModel }) {
  const card = slot.filament_card;
  const isEmpty = card.state === 'Empty';
  const isUnknown = card.state === 'Unknown';
  const isTransporting = card.state === 'Loading' || card.state === 'Unloading';
  const showStatusLine = Boolean(card.status_text);

  const primary = isEmpty
    ? 'Empty'
    : isUnknown
    ? '?'
    : isTransporting
    ? '--'
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
      {showStatusLine && (
        <div
          className={`card-status status-${card.state.toLowerCase()}`}
          data-testid={`ams-hotend-slot-status-${slot.display_index}`}
        >
          {card.status_text}
        </div>
      )}
    </div>
  );
}
