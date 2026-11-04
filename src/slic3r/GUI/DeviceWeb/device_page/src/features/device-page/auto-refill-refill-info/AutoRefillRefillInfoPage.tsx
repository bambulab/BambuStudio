import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { MODULE, useAutoRefillRefillInfoBridge } from './useAutoRefillRefillInfoBridge';
import type { AutoRefillPickerConfig } from './useAutoRefillRefillInfoBridge';
import { useDeviceWebTheme } from '../../../hooks/useDeviceWebTheme';
import { GroupColumn } from './components/GroupColumn';
import { RefillToastStack } from './components/RefillToastStack';
import { SchemaDebugOverlay } from './components/SchemaDebugOverlay';
import { AddRefillFilamentsPage } from './add-refill-filaments/AddRefillFilamentsPage';
import type { AutoRefillAddRefillFilamentsSlotRef } from './add-refill-filaments/types';
import emptyRefillGroupImage from './assets/empty_refill_group.svg';
import autoRefillDisabledNotice from './assets/auto_refill_disabled_notice.svg';
import './auto-refill-refill-info.css';

// The picker normally lives in this page, but the route still accepts
// `?picker=1` so a host that loads it standalone opens straight into it.
function readStandalonePickerConfig(): AutoRefillPickerConfig | null {
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
    pickerConfig,
    openPicker,
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
    isAddRefillWarningSuppressed,
    confirmAddRefillFilaments,
    resetGroup,
    removeSlot,
    enableAutoRefill,
    moveSlot,
    applyingRefillEdit,
    toasts,
    pushToast,
    dismissToast,
  } = useAutoRefillRefillInfoBridge();
  const [standaloneConfig] = useState(readStandalonePickerConfig);
  const [pickerBusy, setPickerBusy] = useState(false);
  // Kept separate from pickerBusy: the management page must stay usable no
  // matter what the picker is doing.
  const [groupBusy, setGroupBusy] = useState(false);
  // Sync host theme into page CSS variables.
  useDeviceWebTheme();

  const groups = state.extruders.flatMap((ext) => ext.groups);
  const groupCount = groups.length;
  const columnsClassName = groupCount === 3
    ? 'refill-page__columns refill-page__columns--spread'
    : 'refill-page__columns';
  // Presentation is C++-driven: text and affordances come from the view-model
  // rather than being hard-coded here (see Schema.hpp keys::root).
  const editable = state.editable ?? false;
  const autoRefillEnabled = state.auto_refill_enabled ?? false;
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


  const standaloneStartedRef = useRef(false);
  const invalidPickerTarget = standaloneConfig?.mode === 'add_to_group' && !standaloneConfig.to;
  useEffect(() => {
    if (!standaloneConfig || invalidPickerTarget || standaloneStartedRef.current) return;
    standaloneStartedRef.current = true;
    openPicker(standaloneConfig);
  }, [invalidPickerTarget, openPicker, standaloneConfig]);

  // The standalone route must not flash the group list between mount and the
  // effect above entering the picker.
  const standalonePending = !!standaloneConfig && !standaloneStartedRef.current;

  if (pickerConfig || standalonePending || invalidPickerTarget) {
    return (
      <div ref={pageRef} className="refill-page refill-page--picker" data-testid="auto-refill-filaments-dialog-page">
        {addRefillFilaments?.mode_title && (
          <div className="refill-page__title">{t(addRefillFilaments.mode_title)}</div>
        )}
        {invalidPickerTarget ? (
          <div className="refill-picker-error">
            <div className="refill-page__error">{t('Invalid refill group target.')}</div>
            <button type="button" className="add-refill-page__button" onClick={closeAddRefillFilaments}>
              {t('Cancel')}
            </button>
          </div>
        ) : queryingAddRefillFilaments || standalonePending ? (
          <AddRefillFilamentsLoading />
        ) : addRefillFilaments ? (
          <AddRefillFilamentsPage
            state={addRefillFilaments}
            baselineSlots={pickerConfig?.baselineSlots}
            error={error}
            busy={pickerBusy || confirmingAddRefillFilaments}
            onSelectionChange={updatePicker}
            onQueryNext={queryNextPicker}
            onQuery={queryAddRefillFilaments}
            onCancel={closeAddRefillFilaments}
            onConfirm={confirmPicker}
            onFollowUp={(slot) => {
              // Unbinding is performed on the management page, so leave the
              // picker before the host raises its removal prompt.
              if (slot.interaction?.follow_up === 'go_to_unbind') closeAddRefillFilaments();
              return followUpAddRefillBlock(
                slot.block_code,
                { ams_id: slot.ams_id, slot_id: slot.slot_id },
                slot.display_index,
              );
            }}
            onDismissWarning={dismissAddRefillWarning}
            onCheckWarningSuppressed={isAddRefillWarningSuppressed}
            onToast={pushToast}
          />
        ) : (
          <div className="refill-picker-error">
            <div className="refill-page__error">{error ? t(error) : t('Failed to get filament information.')}</div>
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
      {!autoRefillEnabled ? (
        <div className="refill-page__disabled">
          <div className="refill-page__disabled-card">
            <img className="refill-page__disabled-image" src={autoRefillDisabledNotice} alt="" />
            <div className="refill-page__disabled-copy">
              <div className="refill-page__disabled-title">{t('Auto Refill is turned off')}</div>
              <div className="refill-page__disabled-desc">
                {t('Printing will continue with identical filament when available')}
              </div>
            </div>
            <button
              type="button"
              className="refill-page__open-settings"
              onClick={() => { void enableAutoRefill(); }}
            >
              {t('Open Auto Refill')}
            </button>
            {error && <div className="refill-page__error">{t(error)}</div>}
          </div>
        </div>
      ) : (
        <>
      <div className="refill-page__intro">
        <div className="refill-page__title">
          {t('Auto-refill management')}
          {groupCount > 0 ? `（${groupCount}）` : ''}
        </div>
        {description && <div className="refill-page__desc">{description}</div>}
      </div>

      {error && <div className="refill-page__error">{t(error)}</div>}

      {state.can_add_group && (
        <div className="refill-page__actions">
          <button
            type="button"
            className="refill-page__add-group"
            onClick={() => openPicker({ mode: 'create_group' })}
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
        <div className={columnsClassName}>
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
                if (anchor) {
                  openPicker({
                    mode: 'add_to_group',
                    to: { ams_id: anchor.ams_id, slot_id: anchor.slot_id },
                    baselineSlots: group.slots.map((slot) => ({ ams_id: slot.ams_id, slot_id: slot.slot_id })),
                  });
                }
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
