import { COLORS, HUMIDITY, SLOT_REFRESH, UNIT_BODY, px } from '../dip';
import { EXT_PANEL_WIDTH, extCardLeft, extFrameLeft } from '../geometry';
import type { SlotView } from '../types';
import type { SlotHandlers } from './AmsUnitPanel';
import { SlotCard } from './SlotCard';

export function ExtPanel({
  slot,
  liteStyle,
  framed,
  slotOffset = 0,
  panelWidth = EXT_PANEL_WIDTH,
  onSelect,
  onEdit,
  onView,
  onFilamentHint,
}: {
  slot: SlotView;
  liteStyle: boolean;
  framed?: boolean;
  slotOffset?: number;
  panelWidth?: number;
} & SlotHandlers) {
  const labelTop = HUMIDITY.height;
  const cardTop = labelTop + SLOT_REFRESH.size + SLOT_REFRESH.gap;
  const cardLeft = extCardLeft(liteStyle, panelWidth);
  const left = extFrameLeft(slotOffset);

  const item = (
    <div
      className="relative shrink-0 overflow-visible"
      style={{ width: px(panelWidth), minWidth: px(panelWidth), maxWidth: px(panelWidth), height: px(UNIT_BODY.height) }}
    >
      <div
        className={`absolute inset-x-0 flex justify-center ${liteStyle ? 'items-end text-[11px] leading-[14px]' : 'items-center text-[13px] leading-[17px]'}`}
        style={{ top: px(labelTop), height: px(SLOT_REFRESH.size), color: COLORS.gray800 }}
      >
        {slot.label}
      </div>

      <div className="absolute" style={{ top: px(cardTop), left: px(cardLeft) }}>
        <SlotCard
          slot={slot}
          variant={liteStyle ? 'lite-ext' : 'ext'}
          onSelect={onSelect}
          onEdit={onEdit}
          onView={onView}
          onFilamentHint={onFilamentHint}
        />
      </div>
    </div>
  );

  if (!framed) return item;

  return (
    <div
      className="relative shrink-0 overflow-visible"
      style={{ width: px(UNIT_BODY.width), minWidth: px(UNIT_BODY.width), maxWidth: px(UNIT_BODY.width), height: px(UNIT_BODY.height), background: COLORS.blockBg }}
    >
      <div className="absolute top-0" style={{ left: px(left) }}>
        {item}
      </div>
    </div>
  );
}
