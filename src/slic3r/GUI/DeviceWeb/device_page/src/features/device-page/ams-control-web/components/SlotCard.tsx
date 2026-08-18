import type { SlotView } from '../types';
import { remainPercent, cn, isLightColor, slotFill } from './style';

function PencilIcon({ className }: { className?: string }) {
  return (
    <svg viewBox="0 0 16 16" className={className} fill="none" aria-hidden>
      <path d="M11.3 2.3a1 1 0 0 1 1.4 0l1 1a1 1 0 0 1 0 1.4L6.2 12.2 3 13l.8-3.2 7.5-7.5Z" stroke="currentColor" strokeWidth="1.2" />
    </svg>
  );
}

function EyeIcon({ className }: { className?: string }) {
  return (
    <svg viewBox="0 0 16 16" className={className} fill="none" aria-hidden>
      <path d="M1.5 8S3.9 4 8 4s6.5 4 6.5 4-2.4 4-6.5 4S1.5 8 1.5 8Z" stroke="currentColor" strokeWidth="1.2" />
      <circle cx="8" cy="8" r="1.8" stroke="currentColor" strokeWidth="1.2" />
    </svg>
  );
}

function RfidIcon({ spinning }: { spinning?: boolean }) {
  return (
    <svg viewBox="0 0 20 20" className={cn('h-5 w-5', spinning && 'animate-spin')} fill="none" aria-hidden>
      <circle cx="10" cy="10" r="7.25" stroke="#8A8A8A" strokeWidth="1.4" />
      <path d="M10 5.2v3.1l2.1-1.2" stroke="#8A8A8A" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  );
}

// Bundled so the panels in between do not have to thread three callbacks each.
export interface SlotHandlers {
  onSelect: (slot: SlotView) => void;
  onEdit: (slot: SlotView) => void;
  onRead: (slot: SlotView) => void;
}

export function SlotCard({
  slot,
  handlers,
}: {
  slot: SlotView;
  handlers: SlotHandlers;
}) {
  const empty = slot.slot_state === 'empty' || slot.slot_state === 'none';
  const fill = empty ? '#E8E8E8' : slotFill(slot);
  const light = empty || isLightColor(slot.color);
  const textColor = light ? '#1F1F1F' : '#FFFFFF';
  const { menu_actions: menu } = slot;

  return (
    <div className="flex w-[3.25rem] flex-col items-center">
      <div className="flex h-6 flex-col items-center justify-end">
        {slot.show_rfid ? (
          <button
            type="button"
            className="text-[#8A8A8A]"
            aria-label="Read RFID"
            disabled={slot.reading}
            onClick={() => handlers.onRead(slot)}
          >
            <RfidIcon spinning={slot.reading} />
          </button>
        ) : null}
      </div>
      <div className="text-[11px] leading-4 text-[#6B6B6B]">{slot.label}</div>
      <div className="relative mt-0.5">
        <button
          type="button"
          data-testid={`ams-slot-${slot.ams_id}-${slot.slot_id}`}
          onClick={() => handlers.onSelect(slot)}
          className={cn(
            'relative block h-[5rem] w-[2.75rem] overflow-hidden rounded-[4px] border',
            slot.selected ? 'border-[#00AE42] ring-1 ring-[#00AE42]' : 'border-[#D0D0D0]',
            slot.loaded && 'shadow-[inset_0_0_0_2px_rgba(0,174,66,0.35)]',
          )}
          style={{ background: fill }}
        >
          {empty ? null : (
            <>
              <span className="absolute inset-x-0 top-5 text-center text-[11px] font-medium" style={{ color: textColor }}>
                {slot.show_unknown ? '?' : slot.fila_type}
              </span>
              {menu.show_filament_mgr_hint ? (
                <span className="absolute right-1 top-1 h-1.5 w-1.5 rounded-full bg-[#00AE42]" title="New filament" />
              ) : null}
              {slot.show_remain ? (
                <span className="absolute inset-x-1 bottom-1.5 h-[3px] rounded-full bg-black/20">
                  <span
                    className="block h-full rounded-full bg-black/45"
                    style={{ width: `${Math.max(8, remainPercent(slot.remain))}%` }}
                  />
                </span>
              ) : (
                <span className="absolute bottom-1.5 left-1/2 h-0.5 w-3 -translate-x-1/2 rounded-full bg-black/25" />
              )}
            </>
          )}
        </button>
        {/* Sits over the slot rather than inside it: both are buttons, and this
            one opens the filament dialog instead of selecting the slot. */}
        {!empty && (menu.show_edit || menu.show_read) ? (
          <button
            type="button"
            aria-label={menu.show_edit ? 'Edit filament' : 'View filament'}
            className="absolute bottom-5 left-1/2 -translate-x-1/2"
            style={{ color: textColor }}
            onClick={() => handlers.onEdit(slot)}
          >
            {menu.show_edit ? <PencilIcon className="h-3.5 w-3.5" /> : <EyeIcon className="h-3.5 w-3.5" />}
          </button>
        ) : null}
      </div>
    </div>
  );
}
