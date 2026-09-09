// 1 CSS px == 1 DIP at 100% zoom. Do not restyle these as rem / Tailwind widths.
export const CONTENT_WIDTH = 578;

export const PREVIEW_STRIP = { width: 264, height: 44 } as const;

export const PREVIEW_BODY_GAP = 10;

export const UNIT_BODY = { width: 264, height: 174 } as const;

export const UNIT_BODY_SINGLE = { width: 78, height: 174 } as const;

// AMSControl mixed (generic AMS + AMS_LITE): three simplebooks / preview strips.
export const MIXED_LITE_WIDTH = 197;
export const MIXED_EXT_WIDTH = 60;

export const DOWN_ROAD = { width: 568, height: 10 } as const;

export const SWITCHER = { width: 29, height: 16 } as const;

export const EXTRUDER = { width: 29, height: 37 } as const;

// AMSextruderImage widget (cell). The SVG is height-scaled to 36 inside it,
// not stretched to fill these widths (see extruderBitmapSize).
export const NOZZLE_ICON = {
  left_nozzle: { width: 13, height: 36 },
  right_nozzle: { width: 13, height: 36 },
  single_nozzle_n: { width: 18, height: 36 },
  single_nozzle_xp: { width: 25, height: 36 },
} as const;

// AMSextruder dual: wxLEFT FromDIP(2) between left_nozzle and right_nozzle.
export const NOZZLE_GAP = 2;

export const HUM_ICON = { numbered: 20, plain: 16, sun: 20, heating: 16 } as const;

export const TRAY_ICON = { sideWidth: 52, midWidth: 50, height: 72 } as const;

export const REFRESH_ICON = 32;

export const FOOTER_BUTTON = { width: 80, height: 34 } as const;

export const SETTINGS_ICON = 24;

export const FOOTER_SIDE_WIDTH = 180;

export const FOOTER_GAP = 20;

// Deliberate deviation from AMSControl, where the option row is flush with the
// body: on a single-Ext machine the body is only 264 wide, so a flush-left
// group reads as stranded. The right group stays flush.
export const FOOTER_LEFT_INSET = 16;

export const PREVIEW_CHIP_FOUR = { width: 52, height: 32 } as const;
export const PREVIEW_CHIP_SINGLE = { width: 28, height: 32 } as const;

export const PREVIEW_CHIP_GAP = 6;

export const PREVIEW_CUBE_FOUR = { width: 9, height: 14 } as const;
export const PREVIEW_CUBE_SINGLE = { width: 6, height: 12 } as const;

export const SLOT_LIB = { width: 52, height: 80 } as const;

export const SLOT_LIB_LITE = { width: 49, height: 72 } as const;

// C++ AmsMappingPopup::_DrawRemainArea. Radius there is 2 on a 6px track; the
// capsule uses height / 2 instead. Like the C++ original it rides above the
// card, floating in the widened SLOT_REFRESH gap.
export const SLOT_REMAIN_LINE = {
  height: 6,
  marginX: 4,
  gapToCard: 2,
  border: 1,
  // Capsule outline. Unfilled interior matches AmsMappingPopup's #E4E4E4 track
  // so a white / pale fill still reads against the remainder.
  borderColor: '#E4E4E4',
  trackColor: '#E4E4E4',
} as const;

// C++ gap is 4. The remain capsule floats in this gap, so it is widened enough
// to clear the 32px refresh glyph (which overflows its 28px hitbox by 2).
export const SLOT_REFRESH = { size: 28, gap: 12 } as const;

// C++ AMSRoadUpPart widget. Overlay paints the remainder of UNIT_BODY, see geometry.UNIT_ROAD_BAND.
export const UNIT_ROAD_HEIGHT = 34;

// C++ wxGridSizer(2,2, 18, 20) plus AddLiteCan wxUP|wxLEFT|wxRIGHT FromDIP(5).
// Col 30 = 5 + 20 + 5; row 23 = 18 vgap + next cell's wxUP 5.
export const LITE_CAN = { width: 80, height: 72 } as const;
export const LITE_GRID_GAP = { row: 23, col: 30 } as const;
export const LITE_GRID_PAD_TOP = 5;

export const HUMIDITY = { width: 93, widthNoPercent: 60, height: 26, dryWidth: 35 } as const;

export const COLORS = {
  brand: 'var(--ams-web-brand)',
  gray800: 'var(--ams-web-gray800)',
  gray700: 'var(--ams-web-gray700)',
  gray500: 'var(--ams-web-gray500)',
  border: 'var(--ams-web-border)',
  blockBg: 'var(--ams-web-block-bg)',
  libBg: 'var(--ams-web-lib-bg)',
  liteLibBg: 'var(--ams-web-lite-lib-bg)',
  chipBg: 'var(--ams-web-chip-bg)',
  disabled: 'var(--ams-web-disabled)',
  disabledText: 'var(--ams-web-disabled-text)',
  white: 'var(--ams-web-white)',
  text: 'var(--ams-web-text)',
  warn: 'var(--ams-web-warn)',
  humidity: 'var(--ams-web-humidity)',
  humidityTrack: 'var(--ams-web-humidity-track)',
  roadNub: 'var(--ams-web-road-nub)',
  outline: 'var(--ams-web-outline)',
  nozzleShell: 'var(--ams-web-nozzle-shell)',
  nozzleBody: 'var(--ams-web-nozzle-body)',
  nozzleTip: 'var(--ams-web-nozzle-tip)',
} as const;

export function px(value: number) {
  return `${value}px`;
}
