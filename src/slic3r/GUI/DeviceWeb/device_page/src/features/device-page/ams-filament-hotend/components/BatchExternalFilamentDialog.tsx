import { useTranslation } from 'react-i18next';
import type { AmsFilamentHotendSlotViewModel, FilamentPageMode } from '../types';

export function BatchExternalFilamentDialog({
  open,
  embedded,
  intent,
  mode,
  slots,
  onClose,
  onEditSlot,
  onConfirmRun,
}: {
  open: boolean;
  embedded?: boolean;
  intent: 'edit' | 'run';
  mode?: FilamentPageMode;
  slots: AmsFilamentHotendSlotViewModel[];
  onClose: () => void;
  onEditSlot: (slot: AmsFilamentHotendSlotViewModel) => void;
  onConfirmRun: () => void;
}) {
  const { t } = useTranslation();
  if (!open) return null;

  const hasUnknown = slots.some((slot) => slot.filament_card.state === 'Unknown');
  const title = t('Edit Filament');
  const confirmLabel = intent === 'run'
    ? (mode === 'batch_ams_load' ? t('Load', { context: 'AmsHotendWebRackA' }) : t('Load'))
    : t('Confirm');

  const dialog = (
      <section
        className={`batch-ext-dialog${embedded ? ' embedded' : ''}`}
        role="dialog"
        aria-modal="true"
        aria-label={title}
        data-testid="ams-hotend-batch-ext-dialog"
      >
        <header className="batch-ext-dialog-titlebar">
          <span className="batch-ext-dialog-dot red" />
          <span className="batch-ext-dialog-dot" />
          <span className="batch-ext-dialog-dot" />
          <h3>{title}</h3>
        </header>

        {hasUnknown && (
          <div className="batch-ext-dialog-warning" data-testid="ams-hotend-batch-ext-warning">
            <span className="batch-ext-dialog-warning-icon">!</span>
            <span>{t('Please finish editing filament information before starting filament load.')}</span>
            <button type="button" aria-label={t('Close')} onClick={onClose}>×</button>
          </div>
        )}

        <div className="batch-ext-dialog-table" role="table">
          <div className="batch-ext-dialog-row header" role="row">
            <span>{t('Slot')}</span>
            <span>{t('Filament Type')}</span>
            <span>{t('Color')}</span>
            <span>{t('Edit Filament')}</span>
          </div>
          {slots.map((slot) => {
            const card = slot.filament_card;
            const unknown = card.state === 'Unknown';
            const typeText = unknown ? t('Please select') : (card.name || card.type || t('Unknown'));
            return (
              <div className="batch-ext-dialog-row" role="row" key={`${slot.ams_id}-${slot.slot_id}`}>
                <span>{slot.display_index}</span>
                <span
                  className={`batch-ext-select${unknown ? ' placeholder' : ''}`}
                  title={typeText}
                >
                  <span>{typeText}</span>
                </span>
                <span
                  className={`batch-ext-color${unknown ? ' unknown' : ''}`}
                  aria-label={t('Color')}
                  style={!unknown ? { ['--ext-color' as never]: card.color } : undefined}
                >
                  {unknown ? '?' : ''}
                </span>
                <button
                  type="button"
                  className="batch-ext-row-edit"
                  onClick={() => onEditSlot(slot)}
                >
                  {t('Edit')}
                </button>
              </div>
            );
          })}
        </div>

        <footer className="batch-ext-dialog-actions">
          <button type="button" className="batch-ext-cancel" onClick={onClose}>
            {t('Close')}
          </button>
          <button
            type="button"
            className="batch-ext-confirm"
            disabled={intent === 'run' && hasUnknown}
            onClick={intent === 'run' ? onConfirmRun : onClose}
          >
            {confirmLabel}
          </button>
        </footer>
      </section>
  );
  return embedded ? dialog : (
    <div className="batch-ext-dialog-backdrop" role="presentation">
      {dialog}
    </div>
  );
}
