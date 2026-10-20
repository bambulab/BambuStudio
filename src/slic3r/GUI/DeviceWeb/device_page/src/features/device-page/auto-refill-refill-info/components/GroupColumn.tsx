import { useEffect, useRef, useState } from 'react';
import type { PointerEvent as ReactPointerEvent } from 'react';
import { useTranslation } from 'react-i18next';
import { SpoolGraphic } from '../../ams-filament-hotend/components/SpoolGraphic';
import nozzleIcon from '../../ams-mapping-hotend/assets/nozzle_icon.svg';
import resetIcon from '../assets/reset_clockwise.svg';
import { SlotRow } from './SlotRow';
import type { RefillGroupViewModel, RefillSlotViewModel } from '../types';
import { formatAutoRefillNozzle } from '../nozzleDisplay';

interface GroupColumnProps {
  group: RefillGroupViewModel;
  // C++-driven: when false the group is display-only and the edit affordances
  // (drag handle, add-compatible, reset) are hidden.
  editable: boolean;
  busy?: boolean;
  onAdd?: (group: RefillGroupViewModel) => void;
  onReset?: (groupTag: string) => void;
  onRemove?: (slot: RefillSlotViewModel) => void;
  onMove?: (
    slot: RefillSlotViewModel,
    member: RefillSlotViewModel,
    asHead: boolean,
  ) => Promise<boolean>;
}

const DRAG_START_THRESHOLD_PX = 4;

function slotKey(slot: RefillSlotViewModel): string {
  return `${slot.ams_id}:${slot.slot_id}`;
}

function destIndexAfterMove(from: number, over: number, insertBefore: boolean, count: number): number {
  let dest = insertBefore ? over : over + 1;
  if (from < dest) dest -= 1;
  if (dest < 0) return 0;
  if (dest > count - 1) return count - 1;
  return dest;
}

function orderSlots(slots: RefillSlotViewModel[], from: number, dest: number): RefillSlotViewModel[] {
  const next = slots.slice();
  const [moved] = next.splice(from, 1);
  next.splice(dest, 0, moved);
  return next;
}

function resolveMove(
  slots: RefillSlotViewModel[],
  from: number,
  dest: number,
): { slot: RefillSlotViewModel; member: RefillSlotViewModel; asHead: boolean } | null {
  if (from === dest || slots.length < 2) return null;
  const next = orderSlots(slots, from, dest);
  const slot = next[dest];
  const asHead = dest === 0;
  const member = asHead ? next[1] : next[dest - 1];
  if (!slot || !member) return null;
  return { slot, member, asHead };
}

// "0.4 Std": diameter trimmed to one decimal, plus the flow label the C++ side
// already formats ("Std" / "HF" / ...). Empty when no nozzle is reported.
function nozzleLabel(group: RefillGroupViewModel): string {
  return formatAutoRefillNozzle(group.nozzle_diameter, group.nozzle_flow_type);
}

// One refill group rendered as a vertical column: a spool header describing the
// filament chain, its trays in refill-priority order, then the add/reset row.
export function GroupColumn({
  group,
  editable,
  busy,
  onAdd,
  onReset,
  onRemove,
  onMove,
}: GroupColumnProps) {
  const { t } = useTranslation();
  const nozzle = nozzleLabel(group);
  const hasManualSlot = group.slots.some((slot) => slot.create_type === 'manual');
  const resetTooltip = !hasManualSlot
    ? t('该续料组暂无手动添加耗材，重置仅对手动添加项生效')
    : undefined;
  const listRef = useRef<HTMLDivElement>(null);
  const slotsRef = useRef(group.slots);
  const [slots, setSlots] = useState(group.slots);
  const [dragFrom, setDragFrom] = useState<number | null>(null);
  const [dropBefore, setDropBefore] = useState<number | null>(null);
  slotsRef.current = slots;

  useEffect(() => {
    setSlots(group.slots);
  }, [group.slots]);

  const beginHandleDrag = (fromIndex: number, event: ReactPointerEvent<HTMLImageElement>) => {
    if (!editable || busy || onMove == null || slotsRef.current.length < 2) return;
    event.preventDefault();
    event.stopPropagation();
    event.currentTarget.setPointerCapture?.(event.pointerId);

    const pointerId = event.pointerId;
    const originY = event.clientY;
    const originSlots = slotsRef.current.slice();
    let started = false;
    const from = fromIndex;
    let over = fromIndex;
    let insertBefore = true;

    const rowAt = (clientY: number): { index: number; insertBefore: boolean } | null => {
      const root = listRef.current;
      if (!root) return null;
      const rows = Array.from(root.querySelectorAll<HTMLElement>('[data-testid="refill-slot"]'));
      for (let i = 0; i < rows.length; i += 1) {
        const rect = rows[i].getBoundingClientRect();
        if (clientY < rect.top) return { index: i, insertBefore: true };
        if (clientY <= rect.bottom) {
          return { index: i, insertBefore: clientY < rect.top + rect.height / 2 };
        }
      }
      if (rows.length === 0) return null;
      return { index: rows.length - 1, insertBefore: false };
    };

    const applyHover = (clientY: number) => {
      const hit = rowAt(clientY);
      if (!hit) return;
      over = hit.index;
      insertBefore = hit.insertBefore;
      setDropBefore(insertBefore ? over : over + 1);
    };

    const handleMove = (moveEvent: PointerEvent) => {
      if (moveEvent.pointerId !== pointerId) return;
      if (!started) {
        if (Math.abs(moveEvent.clientY - originY) < DRAG_START_THRESHOLD_PX) return;
        started = true;
        setDragFrom(from);
        document.body.classList.add('refill-slot-dragging');
      }
      moveEvent.preventDefault();
      applyHover(moveEvent.clientY);
    };

    const finish = (commit: boolean) => {
      window.removeEventListener('pointermove', handleMove);
      window.removeEventListener('pointerup', handleUp);
      window.removeEventListener('pointercancel', handleCancel);
      document.body.classList.remove('refill-slot-dragging');
      setDragFrom(null);
      setDropBefore(null);

      if (!commit || !started) {
        setSlots(originSlots);
        return;
      }
      const dest = destIndexAfterMove(from, over, insertBefore, originSlots.length);
      const command = resolveMove(originSlots, from, dest);
      if (!command) {
        setSlots(originSlots);
        return;
      }
      setSlots(orderSlots(originSlots, from, dest));
      void onMove(command.slot, command.member, command.asHead).then((ok) => {
        if (!ok) setSlots(originSlots);
      });
    };

    const handleUp = (upEvent: PointerEvent) => {
      if (upEvent.pointerId !== pointerId) return;
      finish(true);
    };
    const handleCancel = (cancelEvent: PointerEvent) => {
      if (cancelEvent.pointerId !== pointerId) return;
      finish(false);
    };

    window.addEventListener('pointermove', handleMove);
    window.addEventListener('pointerup', handleUp);
    window.addEventListener('pointercancel', handleCancel);
  };

  return (
    <div className="refill-group" data-testid="refill-group">
      <div className="refill-group__header">
        <SpoolGraphic
          size={52}
          colors={group.colors}
          colorType={group.color_type}
          remain={100}
        />
        <div className="refill-group__caption">
          <span className="refill-group__type">{group.filament_type}</span>
          {nozzle && (
            <span className="refill-group__nozzle">
              <img className="refill-group__nozzle-icon" src={nozzleIcon} alt="" />
              {nozzle}
            </span>
          )}
        </div>
      </div>

      <div className="refill-group__slots" ref={listRef}>
        {slots.map((slot, index) => (
          <SlotRow
            key={slotKey(slot)}
            slot={slot}
            divider={index < slots.length - 1}
            editable={editable}
            busy={busy}
            dragging={dragFrom === index}
            dropBefore={dropBefore === index}
            dropAfter={dropBefore === slots.length && index === slots.length - 1}
            onHandlePointerDown={
              editable && !busy && slots.length >= 2
                ? (event) => beginHandleDrag(index, event)
                : undefined
            }
            onRemove={onRemove}
          />
        ))}
      </div>

      {editable && (
        <div className="refill-group__footer">
          <button
            type="button"
            className="refill-group__add"
            disabled={busy || group.slots.length === 0}
            onClick={() => onAdd?.(group)}
          >
            {t('+ Add similar')}
          </button>
          <span title={resetTooltip}>
            <button
              type="button"
              className="refill-group__reset"
              aria-label={t('Reset')}
              title={resetTooltip}
              disabled={busy || !group.refill_tag_id || !hasManualSlot}
              onClick={() => onReset?.(group.refill_tag_id)}
            >
              <img src={resetIcon} alt="" />
            </button>
          </span>
        </div>
      )}
    </div>
  );
}
