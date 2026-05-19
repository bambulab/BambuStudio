import type { AmsFilamentHotendSlotViewModel } from '../types';

export function CardText({ slot }: { slot: AmsFilamentHotendSlotViewModel }) {
  const isEmpty = slot.card_state === 'Empty';
  const isUnknown = slot.card_state === 'Unknown';
  const isTransporting = slot.card_state === 'Loading' || slot.card_state === 'Unloading';
  const showStatusLine = Boolean(slot.status_text);

  const primary = isEmpty
    ? 'Empty'
    : isUnknown
    ? '?'
    : isTransporting
    ? '--'
    : slot.filament_type || slot.filament_name || '';

  return (
    <div className="card-text" data-card-state={slot.card_state}>
      <div className="card-name-row">
        <span
          className={`card-name${isEmpty ? ' placeholder' : ''}${isUnknown ? ' card-name-unknown' : ''}`}
          data-testid={`ams-hotend-slot-name-${slot.display_index}`}
          title={slot.filament_name || primary}
        >
          {primary}
        </span>
      </div>
      {showStatusLine && (
        <div
          className={`card-status status-${slot.card_state.toLowerCase()}`}
          data-testid={`ams-hotend-slot-status-${slot.display_index}`}
        >
          {slot.status_text}
        </div>
      )}
    </div>
  );
}
