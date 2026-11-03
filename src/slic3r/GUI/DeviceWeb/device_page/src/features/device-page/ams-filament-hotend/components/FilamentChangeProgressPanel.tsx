import { useTranslation } from 'react-i18next';
import type { TFunction } from 'i18next';
import type { FilamentChangeStatus } from '../types';
import { BatchProgressStrip } from './BatchProgressStrip';
import { Nozzle } from './Nozzle';

const STEP_IDLE = 0x00;
const STEP_PAUSE = 0x01;
const STEP_HEAT_NOZZLE = 0x02;
const STEP_CUT_FILAMENT = 0x03;
const STEP_PULL_CURR_FILAMENT = 0x04;
const STEP_PUSH_NEW_FILAMENT = 0x05;
const STEP_GRAB_NEW_FILAMENT = 0x06;
const STEP_PURGE_OLD_FILAMENT = 0x07;
const STEP_CHECK_POSITION = 0x08;
const STEP_SWITCH_EXTRUDER = 0x09;
const STEP_SWITCH_HOTEND = 0x0A;
const STEP_AMS_FILA_COOLING = 0x0B;
const STEP_PUSH_SWITCHER_FILA = 0x0C;
const STEP_PULL_SWITCHER_FILA = 0x0D;
const STEP_SWITCHER_SWITCH = 0x0E;
const STEP_BATCH_PULL_FILA = 0x0F;

const MANUAL_CARD_STEP_STYLES = new Set(['manual_load', 'maunal_unload']);

interface DisplayStep {
  id: number;
  label: string;
  step_style?: string;
  step_style_text?: string;
  step_text?: string;
}

// Literal t() calls so i18next-parser can extract keys. Copy matches
// C++ filament_step_label() / gettext catalog.
function filamentStepLabel(stepId: number, t: TFunction): string {
  switch (stepId) {
    case STEP_IDLE: return t('Idling...');
    case STEP_PAUSE: return t('Pause');
    case STEP_HEAT_NOZZLE: return t('Heat the nozzle');
    case STEP_CUT_FILAMENT: return t('Cut filament');
    case STEP_PULL_CURR_FILAMENT: return t('Pull back current filament');
    case STEP_PUSH_NEW_FILAMENT: return t('Push new filament into extruder');
    case STEP_GRAB_NEW_FILAMENT: return t('Grab new filament');
    case STEP_PURGE_OLD_FILAMENT: return t('Purge old filament');
    case STEP_CHECK_POSITION: return t('Check filament location');
    case STEP_SWITCH_EXTRUDER: return t('Switch extruder');
    case STEP_SWITCH_HOTEND: return t('Switch hotend');
    case STEP_AMS_FILA_COOLING: return t('Wait for AMS cooling');
    case STEP_PUSH_SWITCHER_FILA: return t('Switch current filament at Filament Track Switch');
    case STEP_PULL_SWITCHER_FILA: return t('Pull back current filament at Filament Track Switch');
    case STEP_SWITCHER_SWITCH: return t('Switch track at Filament Track Switch');
    case STEP_BATCH_PULL_FILA: return t('Batch pull back the filaments');
    default: return '';
  }
}

function operationText(t: TFunction, operation?: FilamentChangeStatus['operation']) {
  // A single-slot change reports its direction, so name it. Batch runs can mix
  // both directions and keep the merged label.
  if (operation === 'load') return t('Loading Filament');
  if (operation === 'unload') return t('Unloading Filament');
  return t('Loading/Unloading Filament');
}

function displaySlotName(t: TFunction, status: FilamentChangeStatus) {
  const slot = status.running_slot;
  if (!slot) return '';
  return slot.ams_id && slot.ams_id !== '0'
    ? `AMS-${slot.ams_id}${Number(slot.slot_id) + 1}`
    : t('Slot {{n}}', { n: slot.display_index });
}

function resolveSteps(t: TFunction, status: FilamentChangeStatus): DisplayStep[] {
  const firmwareSteps = status.steps ?? [];
  const sourceSteps = firmwareSteps.length
    ? firmwareSteps
    : (typeof status.current_step === 'number' ? [status.current_step] : []);

  return sourceSteps.map((step, index) => {
    const stepId = typeof step === 'number' ? step : step.id;
    const stepLabel = typeof step === 'number' ? '' : (step.label ?? '');
    const stepStyle = typeof step === 'number' ? undefined : step.step_style;
    const stepStyleText = typeof step === 'number' ? undefined : step.step_style_text;
    const stepText = typeof step === 'number' ? undefined : step.step_text;
    return {
      id: stepId,
      label: filamentStepLabel(stepId, t)
        || status.step_labels?.[index]
        || stepLabel
        || (stepId === status.current_step ? status.current_step_name : '')
        || t('Step {{n}}', { n: stepId }),
      step_style: stepStyle,
      step_style_text: stepStyleText,
      step_text: stepText,
    };
  });
}

function resolveActiveIndex(status: FilamentChangeStatus, steps: DisplayStep[]) {
  if (typeof status.current_step === 'number') {
    const byStep = steps.findIndex((step) => step.id === status.current_step);
    if (byStep >= 0) return byStep;
  }
  if (typeof status.current_step_index === 'number' && status.current_step_index > 0) {
    return Math.min(status.current_step_index - 1, steps.length - 1);
  }
  return 0;
}

export function FilamentChangeProgressPanel({
  status,
  onAction,
}: {
  status?: FilamentChangeStatus;
  onAction?: (action: string) => void;
}) {
  const { t } = useTranslation();
  if (!status?.active && status?.mode !== 'batch_reserved') return null;
  if (status.mode === 'batch_manual_pull') return null;

  if (status.mode === 'batch_reserved') {
    const items = status.batch_reserved?.items ?? [];
    const summary = status.batch_reserved;
    return (
      <section className="filament-change-panel filament-change-panel-reserved" data-testid="ams-filament-change-panel">
        <div className="filament-change-header">
          <div className="filament-change-title" data-testid="ams-filament-change-title">
            {t('Batch {{op}} reserved', { op: operationText(t, summary?.operation) })}
          </div>
          {typeof summary?.current === 'number' && typeof summary?.total === 'number' && (
            <div className="filament-change-meta">{summary.current} / {summary.total}</div>
          )}
        </div>
        {!!items.length && (
          <div className="filament-change-batch-list">
            {items.map((item) => (
              <span
                key={`${item.display_index}-${item.state}`}
                className={`filament-change-batch-item state-${item.state}`}
              >
                {t('Slot {{n}}', { n: item.display_index })}
              </span>
            ))}
          </div>
        )}
      </section>
    );
  }

  const slotName = displaySlotName(t, status);
  const op = operationText(t, status.operation);
  // Batch-run mode reuses the per-step layout below for the currently-
  // active slot but prepends a horizontal step strip so the user can see
  // overall queue progress. C++ only emits `batch_running.items` when
  // ≥2 slots are queued, so a 1-slot batch falls back to plain single.
  const batchItems = status.mode === 'batch_running'
    ? (status.batch_running?.items ?? [])
    : [];
  const showBatchStrip = batchItems.length >= 2;
  // Title reflects the run scope: in batch mode, the per-slot detail is
  // already conveyed by the strip + step list, so the bar collapses to a
  // generic "Batch ... in progress" message; single-slot keeps the
  // firmware-supplied message or the slot-specific fallback.
  const title = showBatchStrip
    ? t('Batch filament {{op}} in progress', { op })
    : status.message
      || (slotName
        ? t('{{slot}} filament {{op}} in progress', { slot: slotName, op })
        : t('Filament {{op}} in progress', { op }));
  const steps = resolveSteps(t, status);
  const activeIndex = resolveActiveIndex(status, steps);

  return (
    <section className="filament-change-panel" data-testid="ams-filament-change-panel">
      {showBatchStrip && <BatchProgressStrip items={batchItems} />}
      <div className="filament-change-status-bar">
        <div className="filament-change-title" data-testid="ams-filament-change-title">
          <span className="filament-change-spinner" />
          {title}
        </div>
      </div>

      <div className="filament-change-detail">
        <div className="filament-change-task-list" data-testid="ams-filament-change-step">
          {steps.map((step, index) => {
            const state = index < activeIndex ? 'done' : (index === activeIndex ? 'active' : 'pending');
            return (
              <div key={`${index}-${step.id}`} className={`filament-change-task-row ${state}`}>
                <span className="filament-change-task-mark">
                  {state === 'done' ? '✓' : index + 1}
                </span>
                <span className="filament-change-task-label">{step.label}</span>
                {step.step_style && MANUAL_CARD_STEP_STYLES.has(step.step_style) && (
                  <div className="filament-change-manual-card">
                    <div className="filament-change-manual-title">{t('Manual action required')}</div>
                    <div className="filament-change-manual-text">
                      {step.step_style_text || step.step_text || t("Insert the filament into the slot's loading port. Push gently until it stops or you hear a soft click.")}
                    </div>
                  </div>
                )}
              </div>
            );
          })}
        </div>

        <div className="filament-change-hotend-visual" aria-hidden="true">
          <Nozzle nozzleState="Active" displayIndex={status.running_slot?.display_index ?? 0} />
        </div>
      </div>

      <div className="filament-change-actions">
        {status.can_retry && (
          <button
            type="button"
            className="primary"
            onClick={() => onAction?.('retry_filament_change')}
          >
            {t('Retry')}
          </button>
        )}
        <button
          type="button"
          disabled={status.can_stop === false}
          onClick={() => onAction?.('stop_filament_change')}
        >
          {t('Stop')}
        </button>
      </div>
    </section>
  );
}
