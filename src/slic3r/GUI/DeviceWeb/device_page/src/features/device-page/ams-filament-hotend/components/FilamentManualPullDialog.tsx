import { useTranslation } from 'react-i18next';
import type { FilamentManualPullItem, FilamentManualPullStatus } from '../types';
import manualPullHotendDone from '../assets/manual_pull_hotend_done.svg';
import manualPullHotendPending from '../assets/manual_pull_hotend_pending.svg';
import manualPullStatusDone from '../assets/manual_pull_status_done.svg';
import manualPullStatusPending from '../assets/manual_pull_status_pending.svg';

function itemKey(item: FilamentManualPullItem, index: number) {
  return `${item.ams_id}-${item.slot_id}-${item.display_index}-${index}`;
}

function stateLabel(t: ReturnType<typeof useTranslation>['t'], state: FilamentManualPullItem['state']) {
  if (state === 'success') return t('Removed');
  if (state === 'failed') return t('Failed');
  return t('Not removed');
}

function hotendImageFor(state: FilamentManualPullItem['state']) {
  return state === 'success' ? manualPullHotendDone : manualPullHotendPending;
}

function statusImageFor(state: FilamentManualPullItem['state']) {
  return state === 'success' ? manualPullStatusDone : manualPullStatusPending;
}

export function FilamentManualPullBanner({
  status,
  onOpen,
}: {
  status?: FilamentManualPullStatus;
  onOpen: () => void;
}) {
  const { t } = useTranslation();
  const items = status?.items ?? [];
  if (!items.length) return null;

  const title = status?.title || t('Filament not removed');
  return (
    <button
      type="button"
      className="filament-manual-pull-banner"
      data-testid="ams-filament-manual-pull-banner"
      onClick={onOpen}
    >
      <span className="filament-manual-pull-warning" aria-hidden="true">!</span>
      <span className="filament-manual-pull-banner-text">{title}</span>
      <span className="filament-manual-pull-chevron" aria-hidden="true">›</span>
    </button>
  );
}

export function FilamentManualPullDialog({
  open,
  embedded,
  status,
  onClose,
  onExit,
}: {
  open: boolean;
  embedded?: boolean;
  status?: FilamentManualPullStatus;
  onClose: () => void;
  onExit?: () => void;
}) {
  const { t } = useTranslation();
  if (!open || !status) return null;

  const items = status.items ?? [];
  const title = t('Tip');
  const completedCount = items.filter((item) => item.state === 'success').length;
  const canClose = status.can_close !== false;

  const dialog = (
      <section
        className={`filament-manual-pull-dialog${embedded ? ' embedded' : ''}`}
        role="dialog"
        aria-modal="true"
        aria-label={title}
        data-testid="ams-filament-manual-pull-dialog"
      >
        <header className="filament-manual-pull-header">
          <div className="filament-manual-pull-window-dots" aria-hidden="true">
            <span className="red" />
            <span />
            <span />
          </div>
          <h3>{title}</h3>
          <div className="filament-manual-pull-window-spacer" aria-hidden="true" />
        </header>

        <div className="filament-manual-pull-body">
          <p className="filament-manual-pull-message">
            {t('Please pull out all selected filaments for this unload.')}
          </p>
          <p className="filament-manual-pull-progress">
            {t('Completed: {{done}}/{{total}}', { done: completedCount, total: items.length })}
          </p>

          <div className="filament-manual-pull-status-card">
            <div className="filament-manual-pull-status-list">
              {items.map((item, index) => (
                <div className="filament-manual-pull-status-item" key={itemKey(item, index)}>
                  <div className="filament-manual-pull-slot-index">{item.display_index}</div>
                  <img
                    className="filament-manual-pull-nozzle-img"
                    src={hotendImageFor(item.state)}
                    alt=""
                    aria-hidden="true"
                  />
                  <img
                    className="filament-manual-pull-result-img"
                    src={statusImageFor(item.state)}
                    alt={stateLabel(t, item.state)}
                    title={stateLabel(t, item.state)}
                    data-testid={`ams-filament-manual-pull-result-${item.display_index}`}
                  />
                </div>
              ))}
            </div>
          </div>
        </div>

        <footer className="filament-manual-pull-actions">
          <button type="button" className="filament-manual-pull-exit" onClick={onExit}>
            {t('Exit')}
          </button>
          {canClose && (
            <button type="button" className="filament-manual-pull-confirm" onClick={onClose}>
              {t('Close')}
            </button>
          )}
        </footer>
      </section>
  );
  return embedded ? dialog : (
    <div className="filament-manual-pull-backdrop" role="presentation">
      {dialog}
    </div>
  );
}
