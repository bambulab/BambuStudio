import type { FilamentLineArea, HumidityView, UnitView } from '../types';
import { findSlotLink } from '../normalize';
import { SlotCard, type SlotHandlers } from './SlotCard';
import { cn, humidityLetter, lineColor, panelMinWidth } from './style';

function HumidityBadge({ humidity }: { humidity: HumidityView }) {
  const text = humidity.display_type === 'percent' && humidity.percent >= 0
    ? `${humidity.percent}%`
    : humidityLetter(humidity.display_idx);
  if (!text) return null;
  return (
    <div
      className="flex h-6 items-center justify-center rounded-full bg-[#3B9AE1] px-2 text-[11px] font-semibold text-white"
      title={humidity.drying ? `Drying, ${humidity.left_dry_time} min left` : 'Humidity'}
    >
      {text}
      {humidity.drying ? <span className="ml-1">·</span> : null}
    </div>
  );
}

export function AmsUnitPanel({
  unit,
  lineArea,
  handlers,
}: {
  unit: UnitView;
  lineArea: FilamentLineArea;
  handlers: SlotHandlers;
}) {
  // A lone slot has nothing to share a bus with, so it drops straight down the way
  // an external spool does.
  const multiSlot = unit.slots.length > 1;

  return (
    <div
      className="relative rounded-md bg-[#F8F8F8] px-3 pb-3 pt-2"
      style={{ minWidth: panelMinWidth(unit.slots.length) }}
    >
      <div className="pointer-events-none absolute left-1/2 top-2 -translate-x-1/2">
        <HumidityBadge humidity={unit.humidity} />
      </div>
      <div className="flex items-end justify-evenly pt-1">
        {unit.slots.map((slot) => (
          <SlotCard key={`${slot.ams_id}-${slot.slot_id}`} slot={slot} handlers={handlers} />
        ))}
      </div>
      <div className={cn('relative mt-2 h-8', multiSlot && 'mx-6')}>
        {multiSlot ? <div className="absolute inset-x-4 top-0 h-px bg-[#C2C2C2]" /> : null}
        {unit.slots.map((slot, index) => {
          const link = findSlotLink(lineArea, slot.ams_id, slot.slot_id);
          if (link && link.switcher_port === '' && link.extruder_ids.length === 0) return null;
          const left = ((index + 0.5) / Math.max(unit.slots.length, 1)) * 100;
          return (
            <div
              key={`${slot.ams_id}-${slot.slot_id}-line`}
              className="absolute top-0 w-px"
              style={{
                left: `${left}%`,
                height: '100%',
                background: lineColor(link?.state ?? 'idle', link?.color ?? ''),
              }}
            />
          );
        })}
        <div className="absolute bottom-0 left-1/2 h-2 w-px -translate-x-1/2 bg-[#C2C2C2]" />
      </div>
    </div>
  );
}
