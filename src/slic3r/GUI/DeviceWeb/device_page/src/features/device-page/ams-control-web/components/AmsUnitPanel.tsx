import frameworkUrl from '../assets/ams_extra_framework_mid_new.svg';
import {
  HUMIDITY,
  SLOT_LIB,
  SLOT_LIB_LITE,
  SLOT_REFRESH,
  UNIT_BODY,
  px,
} from '../dip';
import {
  LITE_CELLS,
  LITE_FRAMEWORK,
  LITE_INNER,
  LITE_INNER_TOP,
  SLOT_STACK_HEIGHT,
  liteCardLeft,
  liteFrameworkLeft,
  liteInnerLeft,
  liteSlotTop,
  row4SlotCenters,
  unitWidth,
} from '../geometry';
import type { SlotLayout } from '../geometry';
import type { SlotView, UnitView } from '../types';
import { HumidityBar } from './HumidityBar';
import { SlotCard } from './SlotCard';
import { SlotRefresh } from './SlotRefresh';

export interface SlotHandlers {
  onSelect: (slot: SlotView) => void;
  onEdit: (slot: SlotView) => void;
  onRead: (slot: SlotView) => void;
  onView: (slot: SlotView) => void;
  onFilamentHint: (slot: SlotView) => void;
}

interface UnitPanelProps extends SlotHandlers {
  unit: UnitView;
  slotLayout: SlotLayout;
  onHumidityClick?: (unit: UnitView) => void;
  bodyWidth?: number;
}

type UnitBodyProps = Omit<UnitPanelProps, 'slotLayout'>;

function SlotStack({
  slot,
  handlers,
  centerX,
}: {
  slot: SlotView;
  handlers: SlotHandlers;
  centerX: number;
}) {
  return (
    <div
      className="absolute flex flex-col items-center"
      style={{
        left: px(centerX - SLOT_LIB.width / 2),
        top: px(HUMIDITY.height),
        width: px(SLOT_LIB.width),
        height: px(SLOT_STACK_HEIGHT),
      }}
    >
      <SlotRefresh slot={slot} onRefresh={handlers.onRead} />
      <div style={{ height: px(SLOT_REFRESH.gap) }} />
      <SlotCard
        slot={slot}
        variant="generic"
        onSelect={handlers.onSelect}
        onEdit={handlers.onEdit}
        onView={handlers.onView}
        onFilamentHint={handlers.onFilamentHint}
      />
    </div>
  );
}

function Row4Unit({ unit, onHumidityClick, ...handlers }: UnitBodyProps) {
  const centers = row4SlotCenters(unit.slots.length);
  return (
    <div className="relative shrink-0 overflow-visible" style={{ width: px(UNIT_BODY.width), height: px(UNIT_BODY.height) }}>
      <div className="absolute inset-x-0 top-0 flex justify-center">
        <HumidityBar humidity={unit.humidity} onClick={onHumidityClick ? () => onHumidityClick(unit) : undefined} />
      </div>
      {unit.slots.map((slot, index) => (
        <SlotStack
          key={`${slot.ams_id}-${slot.slot_id}`}
          slot={slot}
          handlers={handlers}
          centerX={centers[index]}
        />
      ))}
    </div>
  );
}

function SingleHtUnit({ unit, onHumidityClick, ...handlers }: UnitBodyProps) {
  const width = unitWidth('single_ht');
  const slot = unit.slots[0];
  return (
    <div className="relative shrink-0 overflow-visible" style={{ width: px(width), height: px(UNIT_BODY.height) }}>
      <div className="absolute inset-x-0 top-0 flex justify-center">
        <HumidityBar humidity={unit.humidity} onClick={onHumidityClick ? () => onHumidityClick(unit) : undefined} />
      </div>
      {slot ? <SlotStack slot={slot} handlers={handlers} centerX={width / 2} /> : null}
    </div>
  );
}

function LiteCrossUnit({ unit, bodyWidth = UNIT_BODY.width, ...handlers }: UnitBodyProps) {
  const bySlotId = new Map(unit.slots.map((slot) => [slot.slot_id, slot]));
  const innerLeft = liteInnerLeft(bodyWidth);
  const frameworkLeft = liteFrameworkLeft(bodyWidth);

  return (
    <div className="relative shrink-0 overflow-visible" style={{ width: px(bodyWidth), minWidth: px(bodyWidth), maxWidth: px(bodyWidth), height: px(UNIT_BODY.height) }}>
      <img
        src={frameworkUrl}
        alt=""
        aria-hidden
        className="pointer-events-none absolute"
        style={{
          left: px(frameworkLeft),
          top: px(LITE_FRAMEWORK.top),
          width: px(LITE_FRAMEWORK.width),
          height: px(LITE_FRAMEWORK.height),
          objectFit: 'fill',
        }}
      />
      <div
        className="absolute overflow-visible"
        style={{
          left: px(innerLeft),
          top: px(LITE_INNER_TOP),
          width: px(LITE_INNER.width),
          height: px(LITE_INNER.height),
        }}
      >
        {LITE_CELLS.map(({ slotId, row, col }) => {
          const slot = bySlotId.get(slotId);
          if (!slot) return null;
          const left = col === 0 ? liteCardLeft(0) - SLOT_REFRESH.size : liteCardLeft(1);
          const refresh = (
            <div
              className="flex items-center justify-center"
              style={{ width: px(SLOT_REFRESH.size), height: px(SLOT_LIB_LITE.height) }}
            >
              <SlotRefresh slot={slot} onRefresh={handlers.onRead} />
            </div>
          );
          return (
            <div
              key={slotId}
              className="absolute flex items-center"
              style={{ left: px(left), top: px(liteSlotTop(row)), width: px(LITE_INNER.cellWidth) }}
            >
              {col === 0 ? refresh : null}
              <SlotCard
                slot={slot}
                variant="lite"
                onSelect={handlers.onSelect}
                onEdit={handlers.onEdit}
                onView={handlers.onView}
                onFilamentHint={handlers.onFilamentHint}
              />
              {col === 1 ? refresh : null}
            </div>
          );
        })}
      </div>
    </div>
  );
}

export function AmsUnitPanel(props: UnitPanelProps) {
  const { slotLayout, ...rest } = props;
  switch (slotLayout) {
    case 'lite_cross':
      return <LiteCrossUnit {...rest} />;
    case 'single_ht':
      return <SingleHtUnit {...rest} />;
    default:
      return <Row4Unit {...rest} />;
  }
}
