import { useTranslation } from 'react-i18next';
import type {
  AmsFilamentHotendSlotActionPayload,
  AmsFilamentHotendSlotViewModel,
  HotendRackStatus,
  HotendSlotViewModel,
} from '../types';
import { HotendCard } from './HotendCard';

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
  const { t } = useTranslation();
  const ordered = [...slots].sort((a, b) => a.display_index - b.display_index);
  const isRackBusy = !!rackStatus?.active;

  if (rackStatus?.active && rackStatus.mode === 'all') {
    return (
      <section className="hotend-panel hotend-panel-reading" data-testid="ams-hotend-hotend-panel">
        <div className="hotend-read-all-center" data-testid="ams-hotend-read-all-progress">
          <span className="hotend-read-all-spinner" aria-hidden="true" />
          <div>{rackStatus.message || t('Reading ({{current}} / {{total}})', { current: rackStatus.current, total: rackStatus.total })}</div>
          <div className="hotend-read-all-tips" data-testid="ams-hotend-read-all-tips">
            {rackStatus.tips || t('Please wait...')}
          </div>
        </div>
      </section>
    );
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
        <div className="hotend-rack-status-bar" data-testid="ams-hotend-reading-status">
          <span className="hotend-rack-status-spinner" aria-hidden="true" />
          <span>{rackStatus.message || t('Reading hotend {{n}}', { n: rackStatus.display_index })}</span>
        </div>
      )}
    </section>
  );
}
