import { resolveFeedTraySlotId } from '../feedAnchor';
import type { ConnectedSlotsColumn, ConnectedSlotsTrayAction, ConnectedSlotsTrayInfo } from '../types';
import { AmsClimateStatus } from './AmsClimateStatus';
import { AmsTrayCard } from './AmsTrayCard';

/** One AMS / Ext unit board: title, climate pill, and tray cards. */
export function AmsUnitBoard({
  column,
  side,
  onTrayAction,
}: {
  column: ConnectedSlotsColumn;
  // Which feed route anchors on this board; HotendFilamentPath measures the
  // marked tray card to place the vertical stub.
  side: 'left' | 'right';
  onTrayAction?: (action: ConnectedSlotsTrayAction, tray: ConnectedSlotsTrayInfo) => void;
}) {
  const unit = column.unit;
  if (!unit) return null;
  const showHead = (unit.trays?.length ?? 0) > 1;
  const feedSlotId = resolveFeedTraySlotId(column);
  return (
    <section
      className="cs-ams-unit-board"
      data-testid={`ams-unit-board-${unit.ams_id}`}
    >
      <div className="cs-ams-unit-board__head">
        <span className="cs-ams-unit-board__name">{unit.display_name || unit.ams_id}</span>
        <AmsClimateStatus unit={unit} />
      </div>
      <div className="cs-ams-unit-board__trays">
        {unit.trays.map((tray, i) => (
          <AmsTrayCard
            key={`${tray.ams_id}-${tray.slot_id}`}
            tray={tray}
            index={Number(tray.slot_id) + 1 || i + 1}
            showHead={showHead}
            feedAnchor={tray.slot_id === feedSlotId ? side : undefined}
            onAction={onTrayAction}
          />
        ))}
      </div>
    </section>
  );
}
