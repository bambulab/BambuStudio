import type { FilamentLineArea, SlotView } from '../types';
import { findSlotLink } from '../normalize';
import { SlotCard, type SlotHandlers } from './SlotCard';
import { lineColor, panelMinWidth } from './style';

export function ExtPanel({
  slot,
  lineArea,
  handlers,
}: {
  slot: SlotView;
  lineArea: FilamentLineArea;
  handlers: SlotHandlers;
}) {
  const link = findSlotLink(lineArea, slot.ams_id, slot.slot_id);
  // No port and no extruder means the slot has no line at all, which is how an
  // external spool behaves once the filament switch owns its path.
  const showLine = !link || link.switcher_port !== '' || link.extruder_ids.length > 0;

  return (
    <div className="rounded-md bg-[#F8F8F8] px-3 pb-3 pt-2" style={{ minWidth: panelMinWidth(1) }}>
      <SlotCard slot={slot} handlers={handlers} />
      {showLine ? (
        <div
          className="mx-auto mt-2 h-8 w-px"
          style={{ background: lineColor(link?.state ?? 'idle', link?.color ?? '') }}
        />
      ) : (
        <div className="mt-2 h-8" />
      )}
    </div>
  );
}
