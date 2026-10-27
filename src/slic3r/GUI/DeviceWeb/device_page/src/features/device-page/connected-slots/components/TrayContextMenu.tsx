import { useLayoutEffect, useRef, useState, type CSSProperties } from 'react';
import { createPortal } from 'react-dom';
import { useTranslation } from 'react-i18next';
import { documentZoom } from '../../../../hooks/useFitHostWidth';
import type { ConnectedSlotsTrayAction, ConnectedSlotsTrayInfo } from '../types';

const VIEWPORT_MARGIN = 4;

/**
 * Menu item visibility for one tray. C++ owns the decision; the fallbacks only
 * keep the menu usable against mock payloads that predate `tray.actions`.
 */
export function resolveTrayMenuFlags(tray: ConnectedSlotsTrayInfo) {
  const a = tray.actions;
  return {
    canEdit: a?.can_edit ?? tray.exists,
    canLoad: a?.can_load ?? false,
    canUnload: a?.can_unload ?? false,
    canRead: a?.can_read ?? tray.exists,
    readLabel: a?.read_label ?? 'Read',
  };
}

export function hasTrayMenuItems(tray: ConnectedSlotsTrayInfo) {
  const { canEdit, canLoad, canUnload, canRead } = resolveTrayMenuFlags(tray);
  return canEdit || canLoad || canUnload || canRead;
}

/**
 * Floating tray menu portalled to body at a fixed anchor. Owns only the
 * viewport-edge clamping; open/close lives in `useCardContextMenu`.
 */
export function TrayContextMenu({
  tray,
  anchor,
  onAction,
  onClose,
}: {
  tray: ConnectedSlotsTrayInfo;
  anchor: { x: number; y: number };
  onAction: (action: ConnectedSlotsTrayAction) => void;
  onClose: () => void;
}) {
  const { t } = useTranslation();
  const ref = useRef<HTMLDivElement>(null);
  const [pos, setPos] = useState({ left: anchor.x, top: anchor.y });

  const { canEdit, canLoad, canUnload, canRead, readLabel } = resolveTrayMenuFlags(tray);

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

  if (!canEdit && !canLoad && !canUnload && !canRead) return null;

  const zoom = documentZoom();
  const style: CSSProperties = { position: 'fixed', left: pos.left / zoom, top: pos.top / zoom };

  const run = (action: ConnectedSlotsTrayAction) => {
    onClose();
    onAction(action);
  };

  return createPortal(
    <div
      ref={ref}
      className="cs-tray-context-menu"
      role="menu"
      data-card-context-menu="true"
      data-testid={`connected-slots-tray-${tray.ams_id}-${tray.slot_id}-context-menu`}
      style={style}
      onClick={(e) => e.stopPropagation()}
    >
      {canEdit   && <button type="button" role="menuitem" onClick={() => run('edit')}  >{t('Edit')}</button>}
      {canLoad   && <button type="button" role="menuitem" onClick={() => run('load')}  >{t('Load')}</button>}
      {canUnload && <button type="button" role="menuitem" onClick={() => run('unload')}>{t('Unload')}</button>}
      {canRead   && <button type="button" role="menuitem" onClick={() => run('read')}  >{t(readLabel)}</button>}
    </div>,
    document.body,
  );
}
