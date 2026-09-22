import {
  CONTENT_WIDTH,
  DOWN_ROAD,
  EXTRUDER,
  HUMIDITY,
  LITE_CAN,
  LITE_GRID_GAP,
  LITE_GRID_PAD_TOP,
  MIXED_EXT_WIDTH,
  MIXED_LITE_WIDTH,
  NOZZLE_GAP,
  NOZZLE_ICON,
  PREVIEW_BODY_GAP,
  PREVIEW_STRIP,
  SLOT_LIB,
  SLOT_LIB_LITE,
  SLOT_REFRESH,
  SWITCHER,
  UNIT_BODY,
  UNIT_BODY_SINGLE,
} from './dip';

export type SlotLayout = 'row4' | 'lite_cross' | 'single_ht';

export const ROW4_ROAD = { xStart: 40, step: 63, barY: 21, nub: { width: 28, height: 10 } } as const;

export function row4SlotCenters(count: number) {
  return Array.from({ length: count }, (_, i) => ROW4_ROAD.xStart + ROW4_ROAD.step * i);
}

export const SLOT_STACK_HEIGHT = SLOT_REFRESH.size + SLOT_REFRESH.gap + SLOT_LIB.height;

export const LITE_INNER = {
  cellWidth: LITE_CAN.width,
  width: LITE_CAN.width * 2 + LITE_GRID_GAP.col,
  height: SLOT_LIB_LITE.height * 2 + LITE_GRID_GAP.row,
} as const;

export const LITE_INNER_LEFT = (UNIT_BODY.width - LITE_INNER.width) / 2;

export const LITE_INNER_TOP = LITE_GRID_PAD_TOP;

// C++ ScalableBitmap(..., 134) and integer (size - bmp) / 2.
export const LITE_FRAMEWORK = {
  width: 35,
  height: 134,
  top: (UNIT_BODY.height - 134) / 2 - 4,
  left: Math.floor((UNIT_BODY.width - 35) / 2),
} as const;

export function liteInnerLeft(bodyWidth: number = UNIT_BODY.width) {
  return (bodyWidth - LITE_INNER.width) / 2;
}

export function liteFrameworkLeft(bodyWidth: number = UNIT_BODY.width) {
  return Math.floor((bodyWidth - LITE_FRAMEWORK.width) / 2);
}

export function unitWidth(layout: SlotLayout, mixedCompact = false) {
  if (layout === 'single_ht') return UNIT_BODY_SINGLE.width;
  if (layout === 'lite_cross' && mixedCompact) return MIXED_LITE_WIDTH;
  return UNIT_BODY.width;
}

export const EXT_PANEL_WIDTH = UNIT_BODY_SINGLE.width;

export function extPanelWidth(mixedCompact = false) {
  return mixedCompact ? MIXED_EXT_WIDTH : EXT_PANEL_WIDTH;
}

// Drop aims at the colour rect centre, not the 49px card centre (tray flange).
// AMS Lite: AMSLib::render_lite_lib FromDIP(10) / libsize.y - 20.
export const LITE_COLOR_INSET = { left: 10, right: 7, top: 10, bottom: 10 } as const;
// Lite Ext (mid tray): same function, EXT_SPOOL branch FromDIP(8) / libsize.y - 16.
export const LITE_EXT_COLOR_INSET = { left: 10, right: 7, top: 8, bottom: 8 } as const;

export function liteColorWidth() {
  return SLOT_LIB_LITE.width - LITE_COLOR_INSET.left - LITE_COLOR_INSET.right;
}

export function extCardLeft(liteStyle: boolean, panelWidth: number = EXT_PANEL_WIDTH) {
  const width = liteStyle ? SLOT_LIB_LITE.width : SLOT_LIB.width;
  return Math.trunc((panelWidth - width) / 2);
}

export function extExitOffset(_liteStyle: boolean, panelWidth: number = EXT_PANEL_WIDTH) {
  // C++ AMSRoadUpPart uses size.x/2 of the Ext panel.
  return snapRoadX(panelWidth / 2);
}

export function extFrameLeft(slotOffset: number) {
  return (UNIT_BODY.width - EXT_PANEL_WIDTH) / 2 + slotOffset;
}

export function extDropX(
  framed: boolean,
  slotOffset: number,
  liteStyle: boolean,
  panelWidth: number = EXT_PANEL_WIDTH,
) {
  const roadX = extExitOffset(liteStyle, panelWidth);
  return snapRoadX(framed ? extFrameLeft(slotOffset) + roadX : roadX);
}

export const DOWN_ROAD_INSET = (CONTENT_WIDTH - DOWN_ROAD.width) / 2;

export const ROAD_STAGE_HEIGHT = UNIT_BODY.height + DOWN_ROAD.height;

export const DOWN_ROAD_MID_Y = UNIT_BODY.height + DOWN_ROAD.height / 2;

// 1px past the 10px band so a 2px stroke meets the next widget (switcher or
// nozzle) instead of clipping short. That widget paints on top, so the line
// touches the edge and does not run through the icon.
export const ROAD_CONTACT_PX = 1;

export function overlayStageHeight(slotAreaVisible: boolean) {
  return (slotAreaVisible ? UNIT_BODY.height : 0) + DOWN_ROAD.height;
}

// py-[4px] on the page root.
const PAGE_PAD_Y = 8;
// SwitcherSetupHint: mt-[5px] + 32px bar.
const SETUP_HINT_HEIGHT = 37;
// WebView2 rounding slack. Full stack (preview + switcher + hint) lands on 340.
const HOST_HEIGHT_SLACK = 4;

// StatusPanel host height. Compute from visible bands instead of measuring the
// width:100% root, which would echo the container and ping-pong Fit().
export function amsControlWebContentHeight(opts: {
  hasPreview: boolean;
  slotAreaVisible: boolean;
  hasSwitcher: boolean;
  hasSetupHint: boolean;
}) {
  return PAGE_PAD_Y
    + (opts.hasPreview ? PREVIEW_STRIP.height + PREVIEW_BODY_GAP : 0)
    + overlayStageHeight(opts.slotAreaVisible)
    + (opts.hasSwitcher ? SWITCHER.height : 0)
    + EXTRUDER.height
    + (opts.hasSetupHint ? SETUP_HINT_HEIGHT : 0)
    + HOST_HEIGHT_SLACK;
}

export function overlayLineBand(slotAreaVisible: boolean) {
  const startY = slotAreaVisible ? UNIT_BODY.height : 0;
  const bandBottom = startY + DOWN_ROAD.height;
  return {
    startY,
    midY: startY + DOWN_ROAD.height / 2,
    endY: bandBottom + ROAD_CONTACT_PX,
  };
}

export function extRoadTop(liteStyle: boolean) {
  const cardTop = HUMIDITY.height + SLOT_REFRESH.size + SLOT_REFRESH.gap;
  const cardHeight = liteStyle ? SLOT_LIB_LITE.height : SLOT_LIB.height;
  return liteStyle ? cardTop + cardHeight - LITE_EXT_COLOR_INSET.bottom : cardTop + cardHeight;
}

/** wx DC coordinates are integers. Snap once in overlay space. */
export function snapRoadX(x: number) {
  return Math.round(x);
}

// Offset the Ext so the 10px down-road can draw an L instead of a stacked vertical.
export const SINGLE_EXT_ELBOW = 13;

// SVG presentation attributes do not always resolve CSS vars.
export const ROAD_IDLE_COLOR = '#ACACAC';

function hex2(value: number) {
  return Math.max(0, Math.min(255, Math.round(value))).toString(16).padStart(2, '0');
}

export function stripFilamentColor(color: string) {
  return /^#[0-9a-fA-F]{8}$/.test(color) ? color.slice(0, 7) : color;
}

// C++ AMSRoad _get_diff_clr: keep alpha on the stroke. Alpha 0 is bumped to
// 150 so a clear spool still shows a line (STUDIO-10249). White on light
// theme is darkened 20 (STUDIO-10093).
export function roadFilamentColor(color: string) {
  const raw = color.startsWith('#') ? color.slice(1) : color;
  if (!/^[0-9a-fA-F]{6}$|^[0-9a-fA-F]{8}$/.test(raw)) return color;

  let r = parseInt(raw.slice(0, 2), 16);
  let g = parseInt(raw.slice(2, 4), 16);
  let b = parseInt(raw.slice(4, 6), 16);
  let a = raw.length === 8 ? parseInt(raw.slice(6, 8), 16) : 255;
  if (a === 0) a = 150;

  const dark = typeof document !== 'undefined'
    && document.documentElement.getAttribute('data-theme') === 'dark';
  if (!dark && r === 255 && g === 255 && b === 255) {
    r = g = b = 235;
  }

  if (a >= 254) return `#${hex2(r)}${hex2(g)}${hex2(b)}`;
  return `rgba(${r}, ${g}, ${b}, ${(a / 255).toFixed(3)})`;
}

export function isPassingRoad(state: string) {
  return state === 'loading' || state === 'unloading';
}

// SlotLink.state already says whether the filament is on the road: 'loaded' once it
// reached the extruder, 'loading' / 'unloading' while it is still in transit. Do not
// re-derive this from the extruder's filament flag, which stays false mid-load.
export function isFilamentRoadActive(link: { state: string; color: string }) {
  return link.state !== 'idle' && !!link.color;
}

// Switcher IN_A is the left / deputy nozzle (id 1), IN_B the right / main (id 0).
// With a switcher, binded_extruder_ids is {0,1} in set order, so [0] is always
// MAIN and cannot be the DownRoad destination. Empty port keeps the old
// single-binding path: extruder_ids[0].
export function roadExtruderId(link: { switcher_port: string; extruder_ids: number[] }): number | undefined {
  if (link.extruder_ids.length === 0) return undefined;
  const byPort = link.switcher_port === 'a' ? 1 : link.switcher_port === 'b' ? 0 : undefined;
  if (byPort !== undefined && link.extruder_ids.includes(byPort)) return byPort;
  return link.extruder_ids[0];
}

export const ROAD_IDLE_STROKE = { color: ROAD_IDLE_COLOR, width: 2 } as const;

export function filamentRoadStroke(color: string) {
  return { color: roadFilamentColor(color) || ROAD_IDLE_COLOR, width: 4 };
}

export function linkRoadStroke(color: string | undefined) {
  return color ? filamentRoadStroke(color) : ROAD_IDLE_STROKE;
}

export function passingRoadStroke(active: boolean, color: string) {
  return active && color ? filamentRoadStroke(color) : ROAD_IDLE_STROKE;
}

export function extruderIconSize(icon: string) {
  return NOZZLE_ICON[icon as keyof typeof NOZZLE_ICON] ?? NOZZLE_ICON.single_nozzle_xp;
}

// Native SVG viewBox and evenodd hole. left_nozzle is the 14x37 resource, not
// the padded 38-tall copy. AMSextruderImage scales both by height 36.
const NOZZLE_HOLE = {
  left_nozzle: { vbW: 14, vbH: 37, cx: 7.945, cy: 17.980, r: 4.181 },
  right_nozzle: { vbW: 15, vbH: 38, cx: 6.138, cy: 18.277, r: 4.181 },
  single_nozzle_n: { vbW: 19, vbH: 37, cx: 11.2702, cy: 15.2451, r: 4.263 },
  single_nozzle_xp: { vbW: 35, vbH: 51, cx: 17.6635, cy: 27.224, r: 6.986 },
} as const;

function nozzleHoleSpec(icon: string) {
  return NOZZLE_HOLE[icon as keyof typeof NOZZLE_HOLE] ?? NOZZLE_HOLE.single_nozzle_xp;
}

// BitmapCache: (int)(scale * dim + 0.5f)
function cppRoundPx(value: number) {
  return Math.floor(value + 0.5);
}

// ScalableBitmap(file, 36): uniform scale so height == cell height.
export function extruderBitmapSize(icon: string) {
  const hole = nozzleHoleSpec(icon);
  const scale = extruderIconSize(icon).height / hole.vbH;
  return {
    width: cppRoundPx(scale * hole.vbW),
    height: cppRoundPx(scale * hole.vbH),
  };
}

// DrawBitmap((cellW - bmpW) / 2, 0). Integer division toward zero; y is top.
export function extruderBitmapOffset(icon: string) {
  const cell = extruderIconSize(icon);
  const bmp = extruderBitmapSize(icon);
  return {
    x: Math.trunc((cell.width - bmp.width) / 2),
    y: 0,
  };
}

function extruderBitmapScale(icon: string) {
  return extruderIconSize(icon).height / nozzleHoleSpec(icon).vbH;
}

// Ellipse on the evenodd cutout after height-scale. A full-width rect leaks
// past the silhouette in dark mode.
export function nozzleHoleFill(icon: string) {
  const hole = nozzleHoleSpec(icon);
  const offset = extruderBitmapOffset(icon);
  const scale = extruderBitmapScale(icon);
  const pad = 0.5;
  const rx = hole.r * scale + pad;
  const ry = hole.r * scale + pad;
  return {
    left: offset.x + hole.cx * scale - rx,
    top: offset.y + hole.cy * scale - ry,
    width: rx * 2,
    height: ry * 2,
  };
}

function nozzleInletOffset(icon: string) {
  const hole = nozzleHoleSpec(icon);
  const offset = extruderBitmapOffset(icon);
  return offset.x + hole.cx * extruderBitmapScale(icon);
}

export function nozzleIconLefts(iconNames: string[]) {
  const widths = iconNames.map((name) => extruderIconSize(name).width);
  const total = widths.reduce((sum, width) => sum + width, 0)
    + NOZZLE_GAP * Math.max(widths.length - 1, 0);
  const origin = (CONTENT_WIDTH - total) / 2;
  return widths.map((_, index) =>
    origin + widths.slice(0, index).reduce((sum, width) => sum + width + NOZZLE_GAP, 0),
  );
}

export function nozzleInletX(iconName: string, iconLeft: number) {
  return snapRoadX(iconLeft + nozzleInletOffset(iconName));
}

export function unitExitOffset(layout: SlotLayout, bodyWidth?: number) {
  // Lite exit is size.x - 3 of the item, past the RFID widgets, not the grid edge.
  if (layout === 'lite_cross') return (bodyWidth ?? UNIT_BODY.width) - 3;
  return unitWidth(layout) / 2;
}

export const UNIT_ROAD_TOP = HUMIDITY.height + SLOT_STACK_HEIGHT;

// Remainder of the 174px unit below the slot stack. Overlay drops continue
// from here to UNIT_BODY.height, then the 10px line band.
export const UNIT_ROAD_BAND = { height: UNIT_BODY.height - UNIT_ROAD_TOP } as const;

export function liteRoadOrdinates() {
  // C++ RenderLiteRoad uses size.y/2 of the 174px AmsItem, not the grid box.
  const half = UNIT_BODY.height / 2;
  return {
    exitX: UNIT_BODY.width - 3,
    // Flush with the colour-rect edge (inset 10). Do not start at the hub.
    topStubY: LITE_INNER_TOP + SLOT_LIB_LITE.height - LITE_COLOR_INSET.bottom,
    bottomStubY: LITE_INNER_TOP + SLOT_LIB_LITE.height + LITE_GRID_GAP.row + LITE_COLOR_INSET.top,
    bySlot: {
      '0': half,
      '3': half - 4,
      '1': half + 4,
      '2': half + 8,
    } as Record<string, number>,
    mergeY: half - 4,
  };
}

// Fill order: cans[0], cans[3], cans[1], cans[2].
export const LITE_CELLS: Array<{ slotId: string; row: 0 | 1; col: 0 | 1 }> = [
  { slotId: '0', row: 0, col: 0 },
  { slotId: '3', row: 0, col: 1 },
  { slotId: '1', row: 1, col: 0 },
  { slotId: '2', row: 1, col: 1 },
];

// Shift both columns toward the pillar so tray axle holes sit on the pegs.
const LITE_TOWARD_PILLAR = 7;

export function liteCardLeft(col: 0 | 1) {
  if (col === 0) return SLOT_REFRESH.size + LITE_TOWARD_PILLAR;
  return LITE_INNER.cellWidth + LITE_GRID_GAP.col - LITE_TOWARD_PILLAR;
}

export function liteColorCenterX(col: 0 | 1) {
  return liteCardLeft(col) + LITE_COLOR_INSET.left + liteColorWidth() / 2;
}

export function liteSlotCenterX(col: 0 | 1) {
  return liteColorCenterX(col);
}

export function liteSlotTop(row: 0 | 1) {
  return row === 0 ? 0 : SLOT_LIB_LITE.height + LITE_GRID_GAP.row;
}
