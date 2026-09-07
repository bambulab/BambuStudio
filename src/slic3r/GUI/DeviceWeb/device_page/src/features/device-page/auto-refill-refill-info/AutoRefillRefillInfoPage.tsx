import { useEffect, useRef, useState } from 'react';
import type { RefObject } from 'react';
import { useTranslation } from 'react-i18next';
import { MODULE, useAutoRefillRefillInfoBridge } from './useAutoRefillRefillInfoBridge';
import { useSendToCpp } from '../../../hooks/Bridge';
import { useDeviceWebTheme } from '../../../hooks/useDeviceWebTheme';
import { GroupColumn } from './components/GroupColumn';
import { RefillToastStack } from './components/RefillToastStack';
import { SchemaDebugOverlay } from './components/SchemaDebugOverlay';
import { AddRefillFilamentsPage } from './add-refill-filaments/AddRefillFilamentsPage';
import type {
  AutoRefillAddRefillFilamentsMode,
  AutoRefillAddRefillFilamentsSlotRef,
} from './add-refill-filaments/types';
import { resolveHostContentSize } from './hostContentSize';
import emptyRefillGroupImage from './assets/empty_refill_group.png';
import './auto-refill-refill-info.css';

const HOST_LAYOUT_MODULE = 'device_host';

interface StandalonePickerConfig {
  mode: AutoRefillAddRefillFilamentsMode;
  to?: AutoRefillAddRefillFilamentsSlotRef;
}

function readStandalonePickerConfig(): StandalonePickerConfig | null {
  const query = window.location.hash.split('?')[1];
  const params = new URLSearchParams(query ?? '');
  if (params.get('picker') !== '1') return null;

  if (params.get('mode') !== 'add_to_group') {
    return { mode: 'create_group' };
  }
  const amsId = Number(params.get('ams_id'));
  const slotId = Number(params.get('slot_id'));
  if (!Number.isInteger(amsId) || !Number.isInteger(slotId)) {
    return { mode: 'add_to_group' };
  }
  return { mode: 'add_to_group', to: { ams_id: amsId, slot_id: slotId } };
}

// Observe the real rendered size of the page and report it to the wx host via
// device_host/layout/content_size so the dialog can size itself instead of
// relying only on the initial SetMinClientSize.
function useReportHostContentSize(rootRef: RefObject<HTMLElement | null>) {
  const send = useSendToCpp();
  const seqRef = useRef(1_000_000);

  useEffect(() => {
    const root = rootRef.current;
    if (!root) return;

    let rafId = 0;
    let lastSizeKey = '';

    const report = () => {
      rafId = 0;
      const { width, height } = resolveHostContentSize(root);
      if (width <= 0 || height <= 0) return;

      const sizeKey = `${width}x${height}`;
      if (sizeKey === lastSizeKey) return;
      lastSizeKey = sizeKey;

      send(
        { version: '1.0', type: 'request', seq: seqRef.current++, ts: Date.now() },
        {
          module: HOST_LAYOUT_MODULE,
          submod: 'layout',
          action: 'content_size',
          payload: { width, height },
        },
      );
    };

    const scheduleReport = () => {
      if (rafId) return;
      rafId = window.requestAnimationFrame(report);
    };

    scheduleReport();
    window.addEventListener('resize', scheduleReport);

    const observer = typeof ResizeObserver === 'function' ? new ResizeObserver(scheduleReport) : null;
    observer?.observe(root);

    return () => {
      if (rafId) window.cancelAnimationFrame(rafId);
      observer?.disconnect();
      window.removeEventListener('resize', scheduleReport);
    };
  }, [rootRef, send]);
}

// Shown while the printer is answering a refill command (Figma node
// scUlaMhMMExp1ZdZN6GAOW:8015:41169). `label` names the pending work: the
// filament query when opening the picker, or the write-back being applied.
function AddRefillFilamentsLoading({ label = 'Getting filament information' }: { label?: string }) {
  const { t } = useTranslation();

  return (
    <div className="refill-loading" data-testid="add-refill-filaments-loading">
      <span className="refill-loading__spinner" aria-hidden="true" />
      <div className="refill-loading__label">{t(label)}</div>
      <div className="refill-loading__label">{t('Please wait...')}</div>
    </div>
  );
}

export function AutoRefillRefillInfoPage() {
  const pageRef = useRef<HTMLDivElement>(null);
  const { t } = useTranslation();
  const {
    state,
    error,
    openAddRefillDialog,
    addRefillFilaments,
    queryingAddRefillFilaments,
    confirmingAddRefillFilaments,
    queryAddRefillFilaments,
    queryNextAddRefillFilaments,
    updateAddRefillFilaments,
    closeAddRefillFilaments,
    fetchDebugSnapshot,
    followUpAddRefillBlock,
    dismissAddRefillWarning,
    confirmAddRefillFilaments,
    resetGroup,
    removeSlot,
    moveSlot,
    applyingRefillEdit,
    toasts,
    pushToast,
    dismissToast,
  } = useAutoRefillRefillInfoBridge();
  const [pickerConfig] = useState(readStandalonePickerConfig);
  const [pickerBusy, setPickerBusy] = useState(false);
  // Kept separate from pickerBusy: the management page must stay usable no
  // matter what the picker is doing.
  const [groupBusy, setGroupBusy] = useState(false);
  useReportHostContentSize(pageRef);
  // Sync host theme into page CSS variables.
  useDeviceWebTheme();

  const groups = state.extruders.flatMap((ext) => ext.groups);
  const groupCount = groups.length;
  // Presentation is C++-driven: text and affordances come from the view-model
  // rather than being hard-coded here (see Schema.hpp keys::root).
  const editable = state.editable ?? false;
  const description = state.description
    ?.split('\n')
    .map((line) => t(line))
    .join('\n');

  const updatePicker = async (slots: AutoRefillAddRefillFilamentsSlotRef[]) => {
    setPickerBusy(true);
    try {
      await updateAddRefillFilaments(slots, pickerConfig?.mode);
    } finally {
      setPickerBusy(false);
    }
  };

  const queryNextPicker = async (
    slots: AutoRefillAddRefillFilamentsSlotRef[],
    to: AutoRefillAddRefillFilamentsSlotRef,
  ) => {
    setPickerBusy(true);
    try {
      await queryNextAddRefillFilaments(slots, to, pickerConfig?.mode);
    } finally {
      setPickerBusy(false);
    }
  };

  const confirmPicker = async (slots: AutoRefillAddRefillFilamentsSlotRef[]) => {
    setPickerBusy(true);
    try {
      await confirmAddRefillFilaments(slots, pickerConfig?.mode, pickerConfig?.to);
    } finally {
      setPickerBusy(false);
    }
  };

  const runGroupCommand = async (command: () => Promise<boolean>): Promise<boolean> => {
    setGroupBusy(true);
    try {
      return await command();
    } finally {
      setGroupBusy(false);
    }
  };


  const pickerStartedRef = useRef(false);
  const invalidPickerTarget = pickerConfig?.mode === 'add_to_group' && !pickerConfig.to;
  useEffect(() => {
    if (!pickerConfig || invalidPickerTarget || pickerStartedRef.current) return;
    pickerStartedRef.current = true;
    if (pickerConfig.mode === 'add_to_group' && pickerConfig.to) {
      void queryNextAddRefillFilaments([pickerConfig.to], pickerConfig.to, pickerConfig.mode);
    } else {
      void queryAddRefillFilaments();
    }
  }, [invalidPickerTarget, pickerConfig, queryAddRefillFilaments, queryNextAddRefillFilaments]);

  if (pickerConfig) {
    return (
      <div ref={pageRef} className="refill-page" data-testid="auto-refill-filaments-dialog-page">
        {invalidPickerTarget ? (
          <div className="refill-picker-error">
            <div className="refill-page__error">{t('Invalid refill group target.')}</div>
            <button type="button" className="add-refill-page__button" onClick={closeAddRefillFilaments}>
              {t('Cancel')}
            </button>
          </div>
        ) : queryingAddRefillFilaments || !pickerStartedRef.current ? (
          <AddRefillFilamentsLoading />
        ) : addRefillFilaments ? (
          <AddRefillFilamentsPage
            state={addRefillFilaments}
            error={error}
            busy={pickerBusy || confirmingAddRefillFilaments}
            onSelectionChange={updatePicker}
            onQueryNext={queryNextPicker}
            onQuery={queryAddRefillFilaments}
            onCancel={closeAddRefillFilaments}
            onConfirm={confirmPicker}
            onFollowUp={(slot) => followUpAddRefillBlock(
              slot.block_code,
              { ams_id: slot.ams_id, slot_id: slot.slot_id },
              slot.display_index,
            )}
            onDismissWarning={dismissAddRefillWarning}
            onToast={pushToast}
          />
        ) : (
          <div className="refill-picker-error">
            <div className="refill-page__error">{error || t('Failed to get filament information.')}</div>
            <button type="button" className="add-refill-page__button" onClick={closeAddRefillFilaments}>
              {t('Cancel')}
            </button>
          </div>
        )}
        {/* The picker raises its own toasts for the block codes whose
            feedback is a toast rather than a dialog. */}
        <RefillToastStack toasts={toasts} onDismiss={dismissToast} />

        <SchemaDebugOverlay
          view={`${MODULE} / add_refill_filaments`}
          data={addRefillFilaments}
          onFetchSnapshot={fetchDebugSnapshot}
        />
      </div>
    );
  }

  return (
    <div ref={pageRef} className="refill-page" data-testid="auto-refill-refill-info-page">
      {/* A published write-back leaves the group list stale until the printer
          answers, so the page waits instead of showing a half-applied state.
          The toast stack stays mounted across both states so a success toast
          keeps its own 3s lifetime. */}
      {applyingRefillEdit ? <AddRefillFilamentsLoading label="Updating..." /> : (
      <>
      <div className="refill-page__intro">
        <div className="refill-page__title">
          {t('Auto-refill management')}
          {groupCount > 0 ? `（${groupCount}）` : ''}
        </div>
        {description && <div className="refill-page__desc">{description}</div>}
      </div>

      {error && <div className="refill-page__error">{error}</div>}

      {state.can_add_group && (
        <div className="refill-page__actions">
          <button
            type="button"
            className="refill-page__add-group"
            onClick={() => void openAddRefillDialog('open_add_group')}
          >
            {t('Add material group')}
          </button>
        </div>
      )}

      {groupCount === 0 ? (
        <div className="refill-page__empty">
          <img className="refill-page__empty-image" src={emptyRefillGroupImage} alt="" />
          <div className="refill-page__empty-label">{t('No auto-refill filament.')}</div>
        </div>
      ) : (
        <div className="refill-page__columns">
          {groups.map((group) => (
            <GroupColumn
              key={group.filament_id}
              group={group}
              editable={editable}
              busy={groupBusy}
              onAdd={(group) => {
                // Anchor on the last slot of the group so the new filament is
                // appended to the tail of the refill chain.
                const anchor = group.slots[group.slots.length - 1];
                if (anchor) void openAddRefillDialog('open_add_filament', anchor);
              }}
              onReset={(groupTag) => void runGroupCommand(() => resetGroup(groupTag))}
              onRemove={(slot) => void runGroupCommand(() => removeSlot(slot))}
              onMove={(slot, member, asHead) => runGroupCommand(() => moveSlot(slot, member, asHead))}
            />
          ))}
        </div>
      )}
      </>
      )}

      <RefillToastStack toasts={toasts} onDismiss={dismissToast} />

      <SchemaDebugOverlay
        view={`${MODULE} / state`}
        data={state}
        onFetchSnapshot={fetchDebugSnapshot}
      />
    </div>
  );
}
