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
const STEP_SWITCH_EXTRUDER = 0x09;
const STEP_SWITCH_HOTEND = 0x0A;
const STEP_AMS_FILA_COOLING = 0x0B;
const STEP_PUSH_SWITCHER_FILA = 0x0C;
const STEP_PULL_SWITCHER_FILA = 0x0D;
const STEP_SWITCHER_SWITCH = 0x0E;

interface DisplayStep {
  id: number;
  label: string;
}

// Step label keys in English; resolved via t() at render time so the
// i18next instance is guaranteed to be initialized.
const STEP_LABEL_KEYS: Record<number, string> = {
  [STEP_IDLE]: 'Idle',
  [STEP_PAUSE]: 'Paused',
  [STEP_HEAT_NOZZLE]: 'Heating nozzle',
  [STEP_CUT_FILAMENT]: 'Cutting filament',
  [STEP_PULL_CURR_FILAMENT]: 'Manually retract filament',
  [STEP_PUSH_NEW_FILAMENT]: 'Pushing new filament to extruder',
  [STEP_GRAB_NEW_FILAMENT]: 'Grabbing new filament',
  [STEP_PURGE_OLD_FILAMENT]: 'Purging old filament',
  [STEP_SWITCH_EXTRUDER]: 'Switching extruder',
  [STEP_SWITCH_HOTEND]: 'Switching hotend',
  [STEP_AMS_FILA_COOLING]: 'Waiting for AMS to cool down',
  [STEP_PUSH_SWITCHER_FILA]: 'Pushing filament to switcher',
  [STEP_PULL_SWITCHER_FILA]: 'Retracting filament from switcher',
  [STEP_SWITCHER_SWITCH]: 'Switching filament track',
};

function operationText(t: TFunction, _operation?: FilamentChangeStatus['operation'] | 'load' | 'unload') {
  // Data layer uses unified `change` semantics; UI label is the same regardless.
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
    const key = STEP_LABEL_KEYS[step];
    return {
      id: step,
      label: (key ? t(key) : '')
        || status.step_labels?.[index]
        || (step === status.current_step ? status.current_step_name : '')
        || t('Step {{n}}', { n: step }),
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
                {step.id === STEP_PULL_CURR_FILAMENT && (
                  <div className="filament-change-manual-card">
                    <div className="filament-change-manual-title">{t('Manual action required')}</div>
                    <div className="filament-change-manual-text">
                      {t("Insert the filament into the slot's loading port. Push gently until it stops or you hear a soft click.")}
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

      <button
        type="button"
        className="filament-change-stop"
        disabled={status.can_stop === false}
        onClick={() => onAction?.('stop_filament_change')}
      >
        {t('Stop')}
      </button>
    </section>
  );
}
