import { useTranslation } from 'react-i18next';
import type { TFunction } from 'i18next';
import type { HotendRackStatus } from '../types';
import { HotendRackProgressVisual } from './HotendRackProgressVisual';

function progressTitle(status: HotendRackStatus, t: TFunction) {
  const rawIndex = status.target_display_index || status.display_index;
  const total = status.total || 0;
  const n = rawIndex > 0 && (total <= 0 || rawIndex <= total) ? rawIndex : 0;
  if (status.operation === 'read_all') {
    return status.message || t('Reading ({{current}} / {{total}})', { current: status.current, total: status.total });
  }
  if (status.operation === 'take' || status.operation === 'exchange') {
    return n ? t('Hotend {{n}} taking', { n }) : t('Taking hotend');
  }
  if (status.operation === 'place') {
    return n ? t('Hotend {{n}} placing', { n }) : t('Placing hotend');
  }
  if (status.operation === 'read_single') {
    return status.message || (n ? t('Reading hotend {{n}}', { n }) : t('Reading hotend'));
  }
  return status.message || t('Hotend rack is busy');
}

function stepLabel(step: { id: number; label: string }, t: TFunction) {
  switch (step.id) {
    case 1: return t('Homing PrintTool Rack');
    case 2: return t('Homing Toolhead');
    case 3: return t('Calibrating Hotend Rack');
    case 4: return t('Waiting for hotend to heat');
    case 5: return t('Cutting Filament');
    case 6: return t('Unlocking Hotend');
    case 7: return t('Lifting Hotend Rack');
    case 8: return t('Nesting Tool');
    case 9: return t('Fetching Tool');
    case 10: return t('Locking Tool');
    case 11: return t('Moving PrintTool Rack');
    case 12: return t('Refresh Tool Info');
    default: return step.label ? t(step.label) : t('Step {{n}}', { n: step.id });
  }
}

export function HotendRackProgressPanel({
  status,
  onAction,
}: {
  status?: HotendRackStatus;
  onAction?: (action: string) => void;
}) {
  const { t } = useTranslation();
  if (!status?.active) return null;

  const steps = status.steps ?? [];

  return (
    <section className="hotend-rack-progress" data-testid="ams-hotend-rack-progress">
      <div className="hotend-rack-progress-bar" data-testid="ams-hotend-reading-status">
        <span className="hotend-rack-status-spinner" aria-hidden="true" />
        <span>{progressTitle(status, t)}</span>
      </div>

      <div className="hotend-rack-progress-detail">
        <div className="hotend-rack-progress-steps">
          {steps.map((step, index) => (
            <div key={`${step.id}-${index}`} className={`hotend-rack-progress-step ${step.state}`}>
              <span className="hotend-rack-progress-mark">
                {step.state === 'done' ? '✓' : step.state === 'failed' ? '×' : index + 1}
              </span>
              <span className="hotend-rack-progress-label">{stepLabel(step, t)}</span>
            </div>
          ))}
        </div>
        <div className="hotend-rack-progress-visual" aria-hidden="true">
          <HotendRackProgressVisual operation={status.operation} />
        </div>
      </div>

      <div className="hotend-rack-progress-actions">
        {status.can_retry && (
          <button
            type="button"
            className="primary"
            onClick={() => onAction?.('retry_hotend_operation')}
          >
            {t('Retry')}
          </button>
        )}
        <button
          type="button"
          disabled={!status.can_stop}
          onClick={() => onAction?.('stop_hotend_operation')}
        >
          {t('Stop')}
        </button>
      </div>
    </section>
  );
}
