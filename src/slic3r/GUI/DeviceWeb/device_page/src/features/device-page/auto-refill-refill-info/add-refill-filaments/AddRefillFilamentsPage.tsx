import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { SpoolGraphic } from '../../ams-filament-hotend/components/SpoolGraphic';
import nozzleIconUrl from './nozzle-icon.svg';
import nozzleEmptyUrl from './nozzle-empty.svg';
import nozzleErrorBadgeUrl from './nozzle-error-badge.svg';
import nozzleErrorUrl from './nozzle-error.svg';
import nozzleLostBadgeUrl from './nozzle-lost-badge.svg';
import nozzleLostUrl from './nozzle-lost.svg';
import nozzleNonNozzleBadgeUrl from './nozzle-non-nozzle-badge.svg';
import nozzleNonNozzleUrl from './nozzle-non-nozzle.svg';
import nozzleUnknownBadgeUrl from './nozzle-unknown-badge.svg';
import nozzleUnknownUrl from './nozzle-unknown.svg';
import type {
  AutoRefillAddRefillFilamentsSlotRef,
  AutoRefillAddRefillFilamentsSlot,
  AutoRefillAddRefillFilamentsViewModel,
  AutoRefillSlotNozzle,
} from './types';
import { BlockCodeDialog } from './BlockCodeDialog';
import { formatAutoRefillNozzle } from '../nozzleDisplay';

interface AddRefillFilamentsPageProps {
  state: AutoRefillAddRefillFilamentsViewModel;
  // `add_to_group` only: the existing group members shown as a fixed baseline.
  // They stay selected for reference, define the order badges, and must not
  // count towards the new picks sent back on confirm.
  baselineSlots?: AutoRefillAddRefillFilamentsSlotRef[];
  error: string;
  busy: boolean;
  onSelectionChange: (slots: AutoRefillAddRefillFilamentsSlotRef[]) => Promise<void>;
  onQueryNext: (
    slots: AutoRefillAddRefillFilamentsSlotRef[],
    to: AutoRefillAddRefillFilamentsSlotRef,
  ) => Promise<void>;
  // Re-runs the baseline-less Query, for when the selection empties out again.
  onQuery: () => Promise<void>;
  onCancel: () => void;
  onConfirm: (slots: AutoRefillAddRefillFilamentsSlotRef[]) => Promise<void>;
  // Performs the jump a block-code dialog's confirm button names.
  onFollowUp: (slot: AutoRefillAddRefillFilamentsSlot) => Promise<void>;
  // Persists a "Don't show again" tick so the code stops asking.
  onDismissWarning: (blockCode: number) => Promise<void>;
  // Asks the host whether this warning has already been suppressed.
  onCheckWarningSuppressed: (blockCode: number) => Promise<boolean>;
  onToast: (text: string) => void;
}

function slotKey(slot: AutoRefillAddRefillFilamentsSlotRef): string {
  return `${slot.ams_id}:${slot.slot_id}`;
}

function nozzlePresentation(nozzle: AutoRefillSlotNozzle) {
  const label = formatAutoRefillNozzle(nozzle.label, nozzle.subtext);
  switch (nozzle.state) {
    case 'Lost':
      return { base: nozzleLostUrl, badge: nozzleLostBadgeUrl, text: label || '--' };
    case 'Error':
      return { base: nozzleErrorUrl, badge: nozzleErrorBadgeUrl, text: 'Error' };
    case 'NonNozzle':
      return { base: nozzleNonNozzleUrl, badge: nozzleNonNozzleBadgeUrl, text: '--' };
    case 'Empty':
      return { base: nozzleEmptyUrl, text: 'Empty' };
    case 'Unknown':
      return { base: nozzleUnknownUrl, badge: nozzleUnknownBadgeUrl, text: 'Unknown' };
    case 'Active':
    case 'Normal':
    default:
      return { base: nozzleIconUrl, text: label || '--' };
  }
}

function SlotCard({
  slot,
  selectionIndex,
  busy,
  onToggle,
}: {
  slot: AutoRefillAddRefillFilamentsSlot;
  selectionIndex?: number;
  busy: boolean;
  onToggle: () => void;
}) {
  const { t } = useTranslation();
  const selected = selectionIndex !== undefined;
  const enabled = selected || slot.selectable;
  // Warning-level codes only ask for a confirmation, so their card stays bright
  // even while something else keeps it unselectable; error level still greys out.
  const dimmed = !enabled && slot.interaction?.policy !== 'confirm_then_select';
  const colors = slot.has_filament ? slot.colors : ['#D9D9D9'];
  const nozzle = nozzlePresentation(slot.nozzle);
  // A dimmed card still has to accept clicks whenever its code has
  // something to say - that is how the user learns the reason.
  const explains = !enabled && (slot.interaction?.feedback ?? 'none') !== 'none';

  return (
    <button
      type="button"
      className={`add-refill-slot${selected ? ' add-refill-slot--selected' : ''}${dimmed ? ' add-refill-slot--dimmed' : ''}`}
      disabled={(!enabled && !explains) || busy}
      title={!enabled && slot.disable_reason ? t(slot.disable_reason) : undefined}
      aria-pressed={selected}
      onClick={onToggle}
    >
      <span className="add-refill-slot__head">
        <span className="add-refill-slot__index">{slot.slot_name}</span>
        <span className={`add-refill-slot__check${selected ? ' add-refill-slot__check--selected' : ''}`}>
          {selectionIndex}
        </span>
      </span>
      <span className="add-refill-slot__spool">
        <SpoolGraphic
          size={52}
          colors={colors}
          colorType={slot.color_type}
          remain={slot.has_filament ? slot.remain : 0}
          showTag={slot.has_filament}
        />
      </span>
      <span className="add-refill-slot__name">
        {slot.has_filament ? (slot.filament_name || slot.filament_type || t('Unknown')) : t('Empty')}
      </span>
      <span className="add-refill-slot__divider" />
      <span className="add-refill-slot__nozzle" data-state={slot.nozzle.state}>
        <span className="add-refill-slot__nozzle-icon">
          <img src={nozzle.base} alt="" />
          {nozzle.badge && <img className="add-refill-slot__nozzle-badge" src={nozzle.badge} alt="" />}
        </span>
        <span>{t(nozzle.text)}</span>
      </span>
    </button>
  );
}

export function AddRefillFilamentsPage({
  state,
  baselineSlots = [],
  error,
  busy,
  onSelectionChange,
  onQueryNext,
  onQuery,
  onCancel,
  onConfirm,
  onFollowUp,
  onDismissWarning,
  onCheckWarningSuppressed,
  onToast,
}: AddRefillFilamentsPageProps) {
  const { t } = useTranslation();
  const baselineKeys = new Set(baselineSlots.map((slot) => slotKey(slot)));
  const [localBusy, setLocalBusy] = useState(false);
  // Slot whose block-code dialog is up. Null while no dialog is showing.
  const [blockDialogSlot, setBlockDialogSlot] = useState<AutoRefillAddRefillFilamentsSlot | null>(null);
  const [selected, setSelected] = useState<AutoRefillAddRefillFilamentsSlotRef[]>(() => {
    const initial = baselineSlots.slice();
    state.units
      .flatMap((unit) => unit.slots)
      .filter((slot) => slot.selected)
      .forEach(({ ams_id, slot_id }) => {
        const ref = { ams_id, slot_id };
        if (!initial.some((item) => slotKey(item) === slotKey(ref))) {
          initial.push(ref);
        }
      });
    return initial;
  });
  const pending = busy || localBusy;
  // Growing an existing group only asks for the spool being added; a brand-new
  // group is a chain, so it needs two members before it means anything.
  const picked = selected.filter((item) => !baselineKeys.has(slotKey(item))).length;
  const minPicked = state.mode === 'add_to_group' ? 1 : 2;

  const commitSelection = async (slot: AutoRefillAddRefillFilamentsSlot, isSelected: boolean) => {
    const key = slotKey(slot);
    const next = isSelected
      ? selected.filter((item) => slotKey(item) !== key)
      : [...selected, { ams_id: slot.ams_id, slot_id: slot.slot_id }];
    setSelected(next);

    // Adding to an existing group keeps the baseline the device already answered
    // for, so ticking a card changes nothing the host would rebuild differently.
    if (state.mode === 'add_to_group') return;

    setLocalBusy(true);
    try {
      // The first pick of a group is answered by the device, which tells us
      // what may still join it; later changes are rebuilt from that result.
      if (!isSelected && next.length === 1) {
        await onQueryNext(next, { ams_id: slot.ams_id, slot_id: slot.slot_id });
        return;
      }
      // Clearing the last pick removes the baseline QueryNext narrowed the
      // candidates around, so go back to the unconstrained Query rather than
      // rebuilding from a result that no longer describes anything.
      if (isSelected && next.length === 0) {
        await onQuery();
        return;
      }
      await onSelectionChange(next);
    } finally {
      setLocalBusy(false);
    }
  };

  const toggle = async (slot: AutoRefillAddRefillFilamentsSlot) => {
    const key = slotKey(slot);
    if (baselineKeys.has(key)) return;
    const isSelected = selected.some((item) => slotKey(item) === key);
    const interaction = slot.interaction;
    const asksConfirmation = interaction?.feedback === 'dialog'
      && interaction.policy === 'confirm_then_select';

    // Deselecting never asks anything.
    if (isSelected) {
      await commitSelection(slot, true);
      return;
    }

    // Warning-like slots may come back as non-selectable in the payload while
    // still requiring a confirm-before-select dialog (`confirm_then_select`).
    // Check suppression before the generic "blocked" early return so "Don't
    // show again" can bypass the dialog on the next click.
    if (asksConfirmation) {
      setLocalBusy(true);
      try {
        const suppressed = await onCheckWarningSuppressed(interaction.block_code);
        if (suppressed) {
          await commitSelection(slot, false);
          return;
        }
      } finally {
        setLocalBusy(false);
      }
      setBlockDialogSlot(slot);
      return;
    }

    // A blocked slot cannot be added; the click only explains why.
    if (!slot.selectable) {
      if (!interaction) return;
      if (interaction.feedback === 'dialog') setBlockDialogSlot(slot);
      // C++ sends the copy as the English source text; the dialog path also
      // routes it through t(), so the toast must do the same.
      else if (interaction.tooltip) onToast(t(interaction.tooltip));
      return;
    }

    await commitSelection(slot, false);
  };

  // Confirm of a block-code dialog means "keep the tick" for a warning and
  // "take me to the fix" for a blocked code.
  const confirmBlockDialog = async (dontShowAgain: boolean) => {
    const slot = blockDialogSlot;
    setBlockDialogSlot(null);
    if (!slot?.interaction) return;
    if (dontShowAgain) await onDismissWarning(slot.interaction.block_code);
    if (slot.interaction.policy === 'confirm_then_select') {
      await commitSelection(slot, false);
      return;
    }
    await onFollowUp(slot);
  };

  return (
    <div className="add-refill-page" data-testid="add-refill-filaments-page">
      <div className="add-refill-page__body">
        {state.notice && (
          <div className="add-refill-page__notice">
            {t(state.notice, { type: state.notice_filament_type ?? '' })}
          </div>
        )}
        {error && <div className="refill-page__error">{error}</div>}
        <div className="add-refill-page__units">
          {state.units.map((unit) => {
            const isHotend = unit.kind === 'hotend';
            // The hotend unit is two independently centered rows: even display
            // indices on top, odd below, matching the filament/hotend panel.
            const rows = isHotend
              ? [unit.slots.filter((s) => s.is_top_tier), unit.slots.filter((s) => !s.is_top_tier)]
              : [unit.slots];
            return (
              <section
                key={isHotend ? 'hotend' : `ams:${unit.ams_id}`}
                className={`add-refill-unit${isHotend ? ' add-refill-unit--wide' : ''}`}
              >
                <h2 className="add-refill-unit__title">{t(unit.name)}</h2>
                <div className="add-refill-unit__rows">
                  {rows.filter((row) => row.length > 0).map((row) => (
                    <div key={row[0].slot_name} className="add-refill-unit__row">
                      {row.map((slot) => (
                        <SlotCard
                          key={isHotend ? `hotend:${slot.addr_id}` : slotKey(slot)}
                          slot={slot}
                          selectionIndex={
                            selected.findIndex((item) => slotKey(item) === slotKey(slot)) + 1 || undefined
                          }
                          busy={pending}
                          onToggle={() => void toggle(slot)}
                        />
                      ))}
                    </div>
                  ))}
                </div>
              </section>
            );
          })}
        </div>
      </div>
      <div className="add-refill-page__footer">
        <button type="button" className="add-refill-page__button" disabled={pending} onClick={onCancel}>
          {t('Cancel')}
        </button>
        <button
          type="button"
          className="add-refill-page__button add-refill-page__button--primary"
          disabled={pending || picked < minPicked}
          onClick={() => void onConfirm(selected.filter((slot) => !baselineKeys.has(slotKey(slot))))}
        >
          {t('OK')}
        </button>
      </div>

      {blockDialogSlot?.interaction && (
        <BlockCodeDialog
          interaction={blockDialogSlot.interaction}
          slot={blockDialogSlot}
          // Older payloads (and the dev mock) may not carry it.
          groupReference={state.group_reference ?? { filament_name: '', filament_type: '', nozzle: '' }}
          onCancel={() => setBlockDialogSlot(null)}
          onConfirm={(dontShowAgain) => void confirmBlockDialog(dontShowAgain)}
        />
      )}
    </div>
  );
}
