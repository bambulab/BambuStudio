import { resolveFeedTraySlotId } from '../feedAnchor';
import type { ConnectedSlotsColumn, ConnectedSlotsTrayAction, ConnectedSlotsTrayInfo } from '../types';
import { AmsClimateStatus } from './AmsClimateStatus';
import { AmsTrayCard } from './AmsTrayCard';

/** One AMS / Ext unit board: title, climate pill, and tray cards. */
export function AmsUnitBoard({
  column,
  side,
  onDry,
  onTrayAction,
}: {
  column: ConnectedSlotsColumn;
  // Which feed routes anchor on this board; HotendFilamentPath measures the
  // marked board and tray cards to place the stubs and the hub.
  side: 'left' | 'right';
  onDry?: (amsId: string) => void;
  onTrayAction?: (action: ConnectedSlotsTrayAction, tray: ConnectedSlotsTrayInfo) => void;
}) {
  const unit = column.unit;
  if (!unit) return null;
  const showHead = (unit.trays?.length ?? 0) > 1;
  const feedSlotId = resolveFeedTraySlotId(column);
  return (
    <section
      className={`cs-ams-unit-board cs-ams-unit-board--${side}`}
      data-testid={`ams-unit-board-${unit.ams_id}`}
      data-cs-board-anchor={side}
    >
      <div className="cs-ams-unit-board__head">
        <span className="cs-ams-unit-board__name">{unit.display_name || unit.ams_id}</span>
        {/* The rack slot board is a single 80px card; the climate pill belongs to the external AMS only. */}
        {side === 'right' && <AmsClimateStatus unit={unit} onDry={onDry} />}
      </div>
      <div className="cs-ams-unit-board__trays">
        {unit.trays.map((tray, i) => (
          <AmsTrayCard
            key={`${tray.ams_id}-${tray.slot_id}`}
            tray={tray}
            index={Number(tray.slot_id) + 1 || i + 1}
            showHead={showHead}
            isFeed={tray.slot_id === feedSlotId}
            onAction={onTrayAction}
          />
        ))}
      </div>
    </section>
  );
}
