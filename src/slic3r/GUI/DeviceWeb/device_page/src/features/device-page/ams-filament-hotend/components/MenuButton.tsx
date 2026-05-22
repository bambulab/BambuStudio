import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { AmsFilamentHotendSlotViewModel } from '../types';

export function MenuButton({
  slot,
  onAction,
}: {
  slot: AmsFilamentHotendSlotViewModel;
  globalBusy?: boolean;
  onAction: (action: string, slot: AmsFilamentHotendSlotViewModel) => void;
}) {
  const { t } = useTranslation();
  const [open, setOpen] = useState(false);
  const rootRef = useRef<HTMLSpanElement>(null);
  const lineState = slot.filament_line.state;
  const cardState = slot.filament_card.state;
  const menuActions = slot.menu_actions;
  const canEdit = menuActions?.can_edit ?? cardState !== 'Empty';
  const canLoad = menuActions?.can_load ?? (lineState === 'no_filament' && cardState !== 'Empty');
  const canUnload = menuActions?.can_unload ?? lineState === 'loaded';
  const canRead = menuActions?.can_read ?? (cardState !== 'Reading' && cardState !== 'Empty');
  const canAmsOperation = menuActions?.can_ams_operation ?? (slot.bind_ams_slot_count ?? 0) > 0;
  const disabled = !slot.enabled || !(canEdit || canLoad || canUnload || canRead || canAmsOperation);

  useEffect(() => {
    if (!open) return;
    const onPointerDown = (event: PointerEvent) => {
      if (!rootRef.current?.contains(event.target as Node)) setOpen(false);
    };
    document.addEventListener('pointerdown', onPointerDown);
    return () => document.removeEventListener('pointerdown', onPointerDown);
  }, [open]);

  const run = (action: string) => {
    setOpen(false);
    onAction(action, slot);
  };

  return (
    <span className="card-menu-wrap" ref={rootRef}>
      <button
        type="button"
        className={`card-menu${disabled ? ' disabled' : ''}${open ? ' open' : ''}`}
        aria-label="slot menu"
        aria-haspopup="menu"
        aria-expanded={open}
        disabled={disabled}
        onClick={(event) => {
          event.stopPropagation();
          setOpen((value) => !value);
        }}
      >
        <span /><span /><span />
      </button>
      {open && (
        <div className="slot-menu-popover" role="menu">
          {canEdit && <button type="button" role="menuitem" onClick={() => run('edit')}>{t('Edit')}</button>}
          {canLoad && <button type="button" role="menuitem" onClick={() => run('load')}>{t('Load')}</button>}
          {canUnload && <button type="button" role="menuitem" onClick={() => run('unload')}>{t('Unload')}</button>}
          {canRead && <button type="button" role="menuitem" onClick={() => run('read')}>{t('Read')}</button>}
          {canAmsOperation && <button type="button" role="menuitem" onClick={() => run('ams_operation')}>{t('AMS Operation')}</button>}
        </div>
      )}
    </span>
  );
}
