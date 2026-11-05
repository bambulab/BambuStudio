import { useTranslation } from 'react-i18next';
import type { AmsFilamentHotendSlotViewModel } from '../types';

export function CardText({ slot }: { slot: AmsFilamentHotendSlotViewModel }) {
  const { t } = useTranslation();
  const card = slot.filament_card;
  const isEmpty = card.state === 'Empty';
  const isAwaitingTpu = card.state === 'AwaitingTpuRead';
  const isReading = card.state === 'Reading';
  const isUnknown = card.state === 'Unknown';

  // Loading / Unloading slots intentionally fall through to the default
  // branch: the tray still holds known filament during a change, and the
  // user expects to see the C++ controlled card text (e.g. "PLA Basic") under the spool —
  // the spinning indicator on the card head already conveys "in transit".
  const primary = isEmpty
    ? t('Empty')
    : isAwaitingTpu
    ? (card.text || t('TPU pending read'))
    : isReading
    ? '--'
    : isUnknown
    ? '?'
    : card.text || card.name || card.type || '';

  return (
    <div className="card-text" data-card-state={card.state}>
      <div className="card-name-row">
        <span
          className={`card-name${isEmpty || isAwaitingTpu ? ' placeholder' : ''}${isUnknown ? ' card-name-unknown' : ''}`}
          data-testid={`ams-hotend-slot-name-${slot.display_index}`}
          title={card.name || primary}
        >
          {primary}
        </span>
      </div>
    </div>
  );
}
