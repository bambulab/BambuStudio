import type { AmsFilamentHotendSlotViewModel, FilamentPageMode } from '../types';
import { useCardContextMenu } from '../useCardContextMenu';
import { BatchSlotCheckbox } from './BatchSlotCheckbox';
import { CardContextMenu, hasCardContextMenuItems } from './CardContextMenu';
import { IndexPill } from './IndexPill';
import { LoadingArcIcon } from './LoadingArcIcon';
import { PrefeedIndicator } from './PrefeedIndicator';
import { SpoolDisc } from './SpoolDisc';
import { CardText } from './CardText';
import amsFilamentHintUrl from '../assets/ams_filament_hint.svg';

export function FilamentCard({
  slot,
  mode,
  batchChecked,
  onToggleSelect,
  onAction,
}: {
  slot: AmsFilamentHotendSlotViewModel;
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
  // Loading is a transport state carried by filament_line, so it can coexist
  // with material identity states such as Unknown.
  const lineState = slot.filament_line.state;
  const isTransporting = lineState === 'loading' || lineState === 'unloading';
  const prefeedIndicator = slot.filament_card.prefeed_indicator;
  const showPrefeedIndicator = !inBatchMode && !!prefeedIndicator?.visible;

  const cls = ['filament-card'];
  if (slot.selected || (inBatchMode && batchChecked)) cls.push('selected');
  if (slot.is_running) cls.push('running');
  if (!slot.enabled) cls.push('disabled');
  if (inBatchMode) cls.push('batch-mode');
  if (notAllowed) cls.push('not-allowed');

  // Context menu is normal-mode-only. Item visibility comes from Schema
  // menu_actions; skip opening when no item would render.
  const ctx = useCardContextMenu({
    disabled: inBatchMode || !slot.enabled || !hasCardContextMenuItems(slot),
  });

  // Batch mode -> toggle checkbox. Normal mode -> open menu at pointer.
  // The "new filament" hint badge stops its own propagation, so clicking it
  // does not reach here.
  const handleBodyClick = (event: React.MouseEvent<HTMLDivElement>) => {
    if (inBatchMode) {
      if (canSelect) onToggleSelect(slot);
      return;
    }
    ctx.openAt(event.clientX, event.clientY);
  };

  return (
    <div
      className={cls.join(' ')}
      data-testid={`ams-hotend-slot-${slot.display_index}`}
      data-card-state={slot.filament_card.state}
      data-line-state={slot.filament_line.state}
      data-batch-mode={inBatchMode ? mode : undefined}
      onClick={handleBodyClick}
      onContextMenu={ctx.bind.onContextMenu}
      onTouchStart={ctx.bind.onTouchStart}
      onTouchMove={ctx.bind.onTouchMove}
      onTouchEnd={ctx.bind.onTouchEnd}
      onTouchCancel={ctx.bind.onTouchCancel}
    >
      <div className="card-spool-wrap">
        <div className="card-head">
          <IndexPill slot={slot} />
          {inBatchMode ? (
            <BatchSlotCheckbox
              checked={batchChecked}
              canSelect={canSelect}
              reason={selectAction?.reason}
              displayIndex={slot.display_index}
              onToggle={() => onToggleSelect(slot)}
            />
          ) : (isTransporting || showPrefeedIndicator) ? (
            <span className="card-head-right">
              {isTransporting && <LoadingArcIcon />}
              {showPrefeedIndicator && (
                <PrefeedIndicator
                  displayIndex={slot.display_index}
                  colors={prefeedIndicator?.colors}
                  colorType={prefeedIndicator?.color_type}
                  filaType={prefeedIndicator?.prefeed_fila_type}
                />
              )}
            </span>
          ) : null}
        </div>
        <SpoolDisc slot={slot} notAllowed={notAllowed} />
      </div>
      <CardText slot={slot} />
      {slot.filament_card.new_filament_hint && (
        <button
          type="button"
          aria-label="New filament"
          className="filament-card-hint-badge"
          onClick={(e) => {
            e.stopPropagation();
            onAction('open_filament_mgr_hint', slot);
          }}
        >
          <img src={amsFilamentHintUrl} alt="" aria-hidden width={14} height={14} />
        </button>
      )}
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
