import type { ConnectedSlotsSchematic } from '../types';

/**
 * Nozzle-rack position indicator (left schematic): one chip per rack position,
 * with the position this dialog was opened for highlighted.
 * Figma 2440:143513 draws it as two centered rows of 6x8 rounded rects inside
 * a white board.
 */
export function HotendSlotSchematic({ schematic }: { schematic: ConnectedSlotsSchematic }) {
  const slotCount = Math.max(schematic.slot_count ?? 0, 0);
  const selectedIndex = schematic.selected_index ?? -1;
  const positions = Array.from({ length: slotCount }, (_, i) => i);
  // The rack staggers its positions across two rows: displayed numbers 2/4/6/8
  // sit on the back row, 1/3/5/7/9 on the front one.
  const rows = [
    positions.filter((position) => position % 2 === 1),
    positions.filter((position) => position % 2 === 0),
  ].filter((row) => row.length > 0);
  return (
    <div
      className="cs-hotend-slot-schematic"
      data-testid="hotend-slot-schematic"
      data-selected={schematic.selected ? 'true' : undefined}
    >
      <div className="cs-hotend-slot-schematic__body">
        <div className="cs-hotend-slot-schematic__grid">
          {rows.map((row, rowIndex) => (
            <div className="cs-hotend-slot-schematic__row" key={`row-${rowIndex}`}>
              {row.map((position) => (
                <i
                  key={`chip-${position}`}
                  className={`cs-hotend-slot-schematic__chip${position === selectedIndex ? ' cs-hotend-slot-schematic__chip--current' : ''}`}
                />
              ))}
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
