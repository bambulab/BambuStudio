import type { AmsFilamentHotendSlotViewModel, FilamentPageMode } from '../types';
import { useCardContextMenu } from '../useCardContextMenu';
import { BatchSlotCheckbox } from './BatchSlotCheckbox';
import { CardContextMenu } from './CardContextMenu';
import { IndexPill } from './IndexPill';
import { LoadingArcIcon } from './LoadingArcIcon';
import { SpoolDisc } from './SpoolDisc';
import { CardText } from './CardText';

export function FilamentCard({
  slot,
  globalBusy,
  mode,
  batchChecked,
  onToggleSelect,
  onAction,
}: {
  slot: AmsFilamentHotendSlotViewModel;
  globalBusy?: boolean;
  // Page mode the card is rendered under. In batch_* modes the right-click
  // menu is suppressed and a checkbox replaces the empty card-head slot.
  mode: FilamentPageMode;
  batchChecked: boolean;
  onToggleSelect: (slot: AmsFilamentHotendSlotViewModel) => void;
  onAction: (action: string, slot: AmsFilamentHotendSlotViewModel) => void;
}) {
  const inBatchMode = mode !== 'normal';
  const selectAction = slot.select_action;
  // C++ is authoritative on per-slot eligibility; Web only renders.
  const canSelect = inBatchMode ? !!selectAction?.can_select : false;
  const notAllowed = inBatchMode && !canSelect;
  // Card-head right slot priority: loading > batch checkbox > nothing.
  // Loading wins because the running filament-change run keeps that slot
  // out of any batch selection anyway, and the user needs the visual cue
  // that the slot is busy regardless of page mode.
  const cardState = slot.filament_card.state;
  const isTransporting = cardState === 'Loading' || cardState === 'Unloading';

  const cls = ['filament-card'];
  if (slot.selected || (inBatchMode && batchChecked)) cls.push('selected');
  if (slot.is_running) cls.push('running');
  if (!slot.enabled) cls.push('disabled');
  if (inBatchMode) cls.push('batch-mode');
  if (notAllowed) cls.push('not-allowed');

  // Right-click menu is normal-mode-only. In batch mode the only interactive
  // surface on the card is the checkbox, so we disable the context menu and
  // make a body click toggle the selection (when eligible).
  const ctx = useCardContextMenu({
    disabled: inBatchMode || !slot.enabled || !!globalBusy,
  });

  const handleBodyClick = () => {
    if (!inBatchMode) return;
    if (!canSelect) return;
    onToggleSelect(slot);
  };

  return (
    <div
      className={cls.join(' ')}
      data-testid={`ams-hotend-slot-${slot.display_index}`}
      data-card-state={slot.filament_card.state}
      data-line-state={slot.filament_line.state}
      data-batch-mode={inBatchMode ? mode : undefined}
      onClick={inBatchMode ? handleBodyClick : undefined}
      onContextMenu={ctx.bind.onContextMenu}
      onTouchStart={ctx.bind.onTouchStart}
      onTouchMove={ctx.bind.onTouchMove}
      onTouchEnd={ctx.bind.onTouchEnd}
      onTouchCancel={ctx.bind.onTouchCancel}
    >
      <div className="card-spool-wrap">
        <div className="card-head">
          <IndexPill slot={slot} />
          {isTransporting ? (
            <LoadingArcIcon />
          ) : inBatchMode ? (
            <BatchSlotCheckbox
              checked={batchChecked}
              canSelect={canSelect}
              reason={selectAction?.reason}
              displayIndex={slot.display_index}
              onToggle={() => onToggleSelect(slot)}
            />
          ) : null}
        </div>
        <SpoolDisc slot={slot} notAllowed={notAllowed} />
      </div>
      <CardText slot={slot} />
      {ctx.open && ctx.anchor && (
        <CardContextMenu
          slot={slot}
          anchor={ctx.anchor}
          onAction={onAction}
          onClose={ctx.close}
        />
      )}
    </div>
  );
}
