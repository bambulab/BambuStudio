import { useLayoutEffect, useRef, useState, type CSSProperties } from 'react';
import { createPortal } from 'react-dom';
import { useTranslation } from 'react-i18next';
import { documentZoom } from '../../../../hooks/useFitHostWidth';
import type { AmsFilamentHotendSlotViewModel } from '../types';

const VIEWPORT_MARGIN = 4;
// Above .stage slot columns and batch dialogs so the menu is never clipped by siblings.
const CONTEXT_MENU_Z_INDEX = 1100;

/** Resolve menu item visibility from Schema menu_actions (with local fallbacks). */
export function resolveCardMenuFlags(slot: AmsFilamentHotendSlotViewModel) {
  const lineState = slot.filament_line.state;
  const cardState = slot.filament_card.state;
  const m = slot.menu_actions;
  return {
    canEdit: m?.can_edit ?? cardState !== 'Empty',
    canLoad: m?.can_load ?? (lineState === 'no_filament' && cardState !== 'Empty'),
    canUnload: m?.can_unload ?? lineState === 'loaded',
    canRead: m?.can_read ?? (cardState !== 'Reading' && cardState !== 'Empty'),
    canAmsOperation: m?.can_ams_operation ?? (slot.bind_ams_count ?? 0) > 0,
    readLabel: m?.read_label ?? 'Read',
  };
}

export function hasCardContextMenuItems(slot: AmsFilamentHotendSlotViewModel) {
  const flags = resolveCardMenuFlags(slot);
  return flags.canEdit || flags.canLoad || flags.canUnload || flags.canRead || flags.canAmsOperation;
}

/**
 * Floating context menu rendered at a fixed (x, y) anchor coming from
 * useCardContextMenu. Owns the viewport-edge clamping so the popover never
 * spills off-screen; everything else (open/close, outside-click, escape)
 * lives in the hook.
 */
export function CardContextMenu({
  slot,
  anchor,
  onAction,
  onClose,
}: {
  slot: AmsFilamentHotendSlotViewModel;
  anchor: { x: number; y: number };
  onAction: (action: string, slot: AmsFilamentHotendSlotViewModel) => void;
  onClose: () => void;
}) {
  const { t } = useTranslation();
  const ref = useRef<HTMLDivElement>(null);
  const [pos, setPos] = useState<{ left: number; top: number }>({
    left: anchor.x,
    top: anchor.y,
  });

  const { canEdit, canLoad, canUnload, canRead, canAmsOperation, readLabel } = resolveCardMenuFlags(slot);

  // Clamp inside the viewport after the menu measures itself; flips to the
  // left / above the anchor when there isn't enough room to the right / below.
  useLayoutEffect(() => {
    const node = ref.current;
    if (!node) return;
    const rect = node.getBoundingClientRect();
    let left = anchor.x;
    let top = anchor.y;
    const maxLeft = window.innerWidth - rect.width - VIEWPORT_MARGIN;
    const maxTop = window.innerHeight - rect.height - VIEWPORT_MARGIN;
    if (left > maxLeft) left = Math.max(VIEWPORT_MARGIN, anchor.x - rect.width);
    if (top > maxTop) top = Math.max(VIEWPORT_MARGIN, anchor.y - rect.height);
    setPos({ left, top });
  }, [anchor.x, anchor.y]);

  // Bail out when the slot has no actionable items so right-click on a
  // fully-empty slot doesn't pop a useless empty box.
  if (!canEdit && !canLoad && !canUnload && !canRead && !canAmsOperation) {
    return null;
  }

  const zoom = documentZoom();
  const style: CSSProperties = {
    position: 'fixed',
    left: pos.left / zoom,
    top: pos.top / zoom,
    zIndex: CONTEXT_MENU_Z_INDEX,
  };

  const run = (action: string) => {
    onClose();
    onAction(action, slot);
  };

  return createPortal(
    <div
      ref={ref}
      className="card-context-menu"
      role="menu"
      data-card-context-menu="true"
      data-testid={`ams-hotend-slot-${slot.display_index}-context-menu`}
      style={style}
      // Stop the bubble: clicks inside the menu shouldn't trigger the card's
      // own click-to-select handler.
      onClick={(e) => e.stopPropagation()}
    >
      {canEdit          && <button type="button" role="menuitem" onClick={() => run('edit')}         >{t('Edit')}</button>}
      {canLoad          && <button type="button" role="menuitem" onClick={() => run('load')}         >{t('Load')}</button>}
      {canUnload        && <button type="button" role="menuitem" onClick={() => run('unload')}       >{t('Unload')}</button>}
      {canRead          && <button type="button" role="menuitem" onClick={() => run('read')}         >{t(readLabel)}</button>}
      {canAmsOperation  && <button type="button" role="menuitem" onClick={() => run('ams_operation')}>{t('Connected Slots')}</button>}
    </div>,
    document.body,
  );
}
