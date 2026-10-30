import type { CSSProperties } from 'react';
import type {
  AmsFilamentHotendDryerZone,
  AmsFilamentHotendEnvUnit,
  AmsFilamentHotendSlotViewModel,
  FilamentPageMode,
} from '../types';
import { SlotColumn } from './SlotColumn';
import { DryerChamberDecoration, findDryerRuns } from './DryerChamber';
import { AmbientInfoDecoration, findAmbientAnchors } from './AmbientInfo';

// Top feed lines cross the lower row before reaching the shared nozzle row.
export const TOP_LINE_HEIGHT = 167;
export const BOTTOM_LINE_HEIGHT = 26;

// Slot column width, kept in sync with `.slot-column` / `.filament-card`.
const COLUMN_WIDTH = 80;

// Horizontal distance between two neighbouring slots, counting both tiers as
// one evenly spaced sequence (bottom, top, bottom, ...). The outermost columns
// touch the stage edges, so the pitch depends on the total slot count: with 9
// slots it is 1/8 of the free width, with 5 slots 1/4 of it. Deriving it here
// keeps the tiers interleaved for any slot count instead of hard-coding the
// 9-slot case in CSS. The measured width is stored in pixels: container units
// are not reliable in older WKWebView builds, while percentages resolve where
// they are used and can give a nested dryer child the wrong geometry basis.
export function stagePitchStyle(totalSlots: number): CSSProperties {
  const gaps = totalSlots - 1;
  return {
    ['--slot-pitch' as never]: gaps > 0 ? `calc((var(--lw-stage-w, 0px) - ${COLUMN_WIDTH}px) / ${gaps})` : '0px',
  } as CSSProperties;
}

// A callback ref rather than an effect, so the observer follows the <section>
// itself and is torn down with it whenever React remounts it.
function stageWidthRef(el: HTMLElement | null) {
  if (!el) return;
  const update = () => {
    const cs = getComputedStyle(el);
    const w = el.clientWidth - parseFloat(cs.paddingLeft) - parseFloat(cs.paddingRight);
    el.style.setProperty('--lw-stage-w', `${w}px`);
  };
  update();
  if (typeof ResizeObserver !== 'function') return;
  const ro = new ResizeObserver(update);
  ro.observe(el);
  return () => ro.disconnect();
}

// Stable key for the local selection set. Composes ams_id + slot_id so two
// slots that share a display_index (rare but possible across AMS units) do
// not collide. Used by both <Stage> and <BatchModeFooter>.
export function selectionKey(slot: { ams_id: string; slot_id: string }): string {
  return `${slot.ams_id}::${slot.slot_id}`;
}

export function Stage({
  topSlots,
  bottomSlots,
  amsDryZones,
  amsEnvUnits,
  hotendMode,
  mode,
  selectedKeys,
  onToggleSelect,
  onAction,
  onDry,
}: {
  topSlots: AmsFilamentHotendSlotViewModel[];
  bottomSlots: AmsFilamentHotendSlotViewModel[];
  amsDryZones: AmsFilamentHotendDryerZone[];
  amsEnvUnits: AmsFilamentHotendEnvUnit[];
  hotendMode: boolean;
  mode: FilamentPageMode;
  selectedKeys: ReadonlySet<string>;
  onToggleSelect: (slot: AmsFilamentHotendSlotViewModel) => void;
  onAction: (action: string, slot: AmsFilamentHotendSlotViewModel) => void;
  onDry: (amsId: string) => void;
}) {
  // Dryer chambers are physically attached to the top tier in the current
  // hardware layout; only top slots can fall under a chamber decoration.
  const dryerRuns = findDryerRuns(topSlots, amsDryZones);
  const ambientAnchors = findAmbientAnchors(topSlots, amsEnvUnits);
  const dryClickable = mode === 'normal';

  return (
    <section
      ref={stageWidthRef}
      className={`stage${hotendMode ? ' hotend-mode' : ''}${mode !== 'normal' ? ' batch-mode' : ''}`}
      style={stagePitchStyle(topSlots.length + bottomSlots.length)}
    >
      <div className="top-tier-row" data-testid="ams-hotend-top-tier">
        {dryerRuns.map((run) => (
          <DryerChamberDecoration
            key={`dryer-${run.start}-${run.end}`}
            startIndex={run.start}
            endIndex={run.end}
            totalSlots={topSlots.length}
            humidity={run.humidity}
            status={run.status}
            isDrying={run.isDrying}
            amsId={run.amsId}
            onDry={dryClickable ? onDry : undefined}
          />
        ))}
        {ambientAnchors.map((anchor) => (
          <AmbientInfoDecoration
            key={`ambient-${anchor.start}`}
            startIndex={anchor.start}
            humidity={anchor.humidity}
            temp={anchor.temp}
          />
        ))}
        {topSlots.map((slot) => (
          <SlotColumn
            key={slot.display_index}
            slot={slot}
            lineHeight={TOP_LINE_HEIGHT}
            mode={mode}
            batchChecked={selectedKeys.has(selectionKey(slot))}
            onToggleSelect={onToggleSelect}
            onAction={onAction}
          />
        ))}
      </div>

      <div className="bottom-tier-row" data-testid="ams-hotend-bottom-tier">
        {bottomSlots.map((slot) => (
          <SlotColumn
            key={slot.display_index}
            slot={slot}
            lineHeight={BOTTOM_LINE_HEIGHT}
            mode={mode}
            batchChecked={selectedKeys.has(selectionKey(slot))}
            onToggleSelect={onToggleSelect}
            onAction={onAction}
          />
        ))}
      </div>
    </section>
  );
}
