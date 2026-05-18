import { useEffect, useRef, useState } from 'react';
import type { AmsFilamentHotendSlotViewModel } from '../types';

export function MenuButton({
  slot,
  onAction,
}: {
  slot: AmsFilamentHotendSlotViewModel;
  onAction: (action: string, slot: AmsFilamentHotendSlotViewModel) => void;
}) {
  const [open, setOpen] = useState(false);
  const rootRef = useRef<HTMLSpanElement>(null);
  const busy = slot.line_state === 'loading' || slot.line_state === 'unloading' || slot.is_running;
  const disabled = !slot.enabled || slot.card_state === 'Empty' || busy;
  const canEdit = !busy && slot.card_state !== 'Empty';
  const canLoad = !busy && slot.card_state !== 'Empty' && slot.line_state === 'no_filament';
  const canUnload = !busy && slot.line_state === 'loaded';
  const canRead = !busy && slot.card_state !== 'Reading' && slot.card_state !== 'Empty';
  const showAmsAction = (slot.bind_ams_slot_count ?? 0) > 0;

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
          <button type="button" role="menuitem" disabled={!canEdit} onClick={() => run('edit')}>编辑</button>
          {canUnload
            ? <button type="button" role="menuitem" onClick={() => run('unload')}>退料</button>
            : <button type="button" role="menuitem" disabled={!canLoad} onClick={() => run('load')}>进料</button>}
          <button type="button" role="menuitem" disabled={!canRead} onClick={() => run('read')}>读取</button>
          {showAmsAction && <button type="button" role="menuitem" onClick={() => run('ams_operation')}>AMS操作</button>}
        </div>
      )}
    </span>
  );
}
