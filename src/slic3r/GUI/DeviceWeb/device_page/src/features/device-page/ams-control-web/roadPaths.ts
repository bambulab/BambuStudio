import { COLORS, CONTENT_WIDTH, UNIT_BODY } from './dip';
import {
  LITE_CELLS,
  ROAD_IDLE_STROKE,
  ROW4_ROAD,
  UNIT_ROAD_TOP,
  extDropX,
  extRoadTop,
  liteInnerLeft,
  liteRoadOrdinates,
  liteSlotCenterX,
  overlayLineBand,
  overlayStageHeight,
  passingRoadStroke,
  linkRoadStroke,
  row4SlotCenters,
  snapRoadX,
  unitExitOffset,
  unitWidth,
  type SlotLayout,
} from './geometry';
import type { SlotLink, SlotView, UnitHub } from './types';

export type RoadLayer = 'slot' | 'line';

export interface RoadPolyline {
  key: string;
  layer: RoadLayer;
  points: string;
  color: string;
  width: number;
  linecap: 'butt' | 'round';
  linejoin: 'miter' | 'round';
}

export interface RoadNub {
  key: string;
  x: number;
  y: number;
  width: number;
  height: number;
  fill: string;
}

export interface RoadOverlayModel {
  width: number;
  height: number;
  polylines: RoadPolyline[];
  nubs: RoadNub[];
}

export interface RoadExit {
  extruderId: number;
  active: boolean;
  color: string;
}

export type RoadBlock =
  | {
      key: string;
      origin: number;
      kind: SlotLayout;
      slots: SlotView[];
      hub: UnitHub;
      links: SlotLink[];
      exit?: RoadExit;
      bodyWidth?: number;
    }
  | {
      key: string;
      origin: number;
      kind: 'ext';
      liteStyle: boolean;
      framed: boolean;
      slotOffset: number;
      link?: SlotLink;
      exit?: RoadExit;
      panelWidth?: number;
    };

type OmitOrigin<T> = T extends unknown ? Omit<T, 'origin'> : never;
export type RoadBlockDraft = OmitOrigin<RoadBlock>;

function pts(coords: Array<[number, number]>) {
  return coords.map(([x, y]) => `${snapRoadX(x)},${Math.round(y)}`).join(' ');
}

// The slot-to-hub run belongs to whichever record proves the filament left the
// slot. A live SlotLink covers the whole path, hub included, so it wins; the hub
// answers the shorter question and only speaks up once the link falls back to
// idle, which is the case where the filament reached the fitting and stopped.
interface UnitRun {
  slotId: string;
  color: string;
  // The tube below the fitting carries filament out of the unit, so it belongs to
  // the link. A hub-only run stops at the fitting and leaves that tube grey.
  exitsUnit: boolean;
}

function unitRun(block: Extract<RoadBlock, { kind: SlotLayout }>): UnitRun {
  const live = block.links.find((link) => link.state !== 'idle');
  // Reuse the down-road test instead of re-deriving one: a link may be non-idle
  // while the target extruder is still empty, and then the road below stays grey.
  if (live) return { slotId: live.slot_id, color: live.color, exitsUnit: !!block.exit?.active };
  if (block.hub.state !== 'idle')
    return { slotId: block.hub.active_slot_id, color: block.hub.color, exitsUnit: false };
  return { slotId: '', color: '', exitsUnit: false };
}

function unitRunStroke(run: UnitRun, slotId: string) {
  return linkRoadStroke(slotId && run.slotId === slotId ? run.color : undefined);
}

function paint(stroke: { color: string; width: number }) {
  if (stroke.width === 4) {
    return { color: stroke.color || COLORS.brand, width: 4 };
  }
  return stroke;
}

function elbowPoints(x: number, startY: number, targetX: number, midY: number, endY: number) {
  return pts([
    [x, startY],
    [x, midY],
    [targetX, midY],
    [targetX, endY],
  ]);
}

function appendLineSeg(
  polylines: RoadPolyline[],
  key: string,
  from: [number, number],
  to: [number, number],
  stroke: { color: string; width: number },
) {
  const painted = paint(stroke);
  polylines.push({
    key,
    layer: 'line',
    points: pts([from, to]),
    color: painted.color,
    width: painted.width,
    linecap: 'butt',
    linejoin: 'miter',
  });
}

function appendLineElbow(
  polylines: RoadPolyline[],
  block: RoadBlock,
  x: number,
  band: { startY: number; midY: number; endY: number },
  nozzleXOf: (extruderId: number) => number,
) {
  if (!block.exit) return;
  const stroke = paint(passingRoadStroke(block.exit.active, block.exit.color));
  polylines.push({
    key: `${block.key}-line`,
    layer: 'line',
    points: elbowPoints(x, band.startY, nozzleXOf(block.exit.extruderId), band.midY, band.endY),
    color: stroke.color,
    width: stroke.width,
    linecap: 'butt',
    linejoin: 'miter',
  });
}

// C++ AMSRoadDownPart DOUBLE: one grey skeleton (shared horizontal +
// per-unit drops + nozzle drop), then the loaded 4px path on top.
// Two full elbows on the same nozzle left a 2px idle stroke sitting in
// the middle of the 4px filament line.
function appendDownRoadLines(
  polylines: RoadPolyline[],
  blocks: RoadBlock[],
  band: { startY: number; midY: number; endY: number },
  nozzleXOf: (extruderId: number) => number,
) {
  const groups = new Map<number, RoadBlock[]>();
  for (const block of blocks) {
    if (!block.exit) continue;
    const list = groups.get(block.exit.extruderId);
    if (list) list.push(block);
    else groups.set(block.exit.extruderId, [block]);
  }

  for (const [extruderId, group] of groups) {
    const nozzleX = nozzleXOf(extruderId);
    const xs = group.map((block) => blockExitX(block));
    const left = Math.min(nozzleX, ...xs);
    const right = Math.max(nozzleX, ...xs);

    for (const block of group) {
      if (block.exit?.active) continue;
      const x = blockExitX(block);
      appendLineSeg(
        polylines,
        `${block.key}-line-drop`,
        [x, band.startY],
        [x, band.midY],
        ROAD_IDLE_STROKE,
      );
    }

    if (left !== right) {
      appendLineSeg(
        polylines,
        `line-trunk-${extruderId}-h`,
        [left, band.midY],
        [right, band.midY],
        ROAD_IDLE_STROKE,
      );
    }
    appendLineSeg(
      polylines,
      `line-trunk-${extruderId}-v`,
      [nozzleX, band.midY],
      [nozzleX, band.endY],
      ROAD_IDLE_STROKE,
    );

    for (const block of group) {
      if (!block.exit?.active) continue;
      appendLineElbow(polylines, block, blockExitX(block), band, nozzleXOf);
    }
  }
}

function appendLite(
  polylines: RoadPolyline[],
  block: Extract<RoadBlock, { kind: SlotLayout }>,
  slotAreaVisible: boolean,
) {
  if (block.kind !== 'lite_cross' || !slotAreaVisible) return;
  const road = liteRoadOrdinates();
  const bodyWidth = block.bodyWidth ?? UNIT_BODY.width;
  const innerLeft = liteInnerLeft(bodyWidth);
  const exitX = snapRoadX(block.origin + unitExitOffset('lite_cross', bodyWidth));
  const bySlotId = new Map(block.slots.map((slot) => [slot.slot_id, slot]));
  const run = unitRun(block);
  const loadedTurnY = run.slotId ? road.bySlot[run.slotId] : road.mergeY;

  for (const { slotId, row, col } of LITE_CELLS) {
    const slot = bySlotId.get(slotId);
    const stroke = paint(unitRunStroke(run, slot ? slotId : ''));
    const centerX = snapRoadX(block.origin + innerLeft + liteSlotCenterX(col));
    const stubY = row === 0 ? road.topStubY : road.bottomStubY;
    const turnY = road.bySlot[slotId];
    polylines.push({
      key: `${block.key}-lite-${slotId}`,
      layer: 'slot',
      points: pts([
        [centerX, stubY],
        [centerX, turnY],
        [exitX, turnY],
      ]),
      color: stroke.color,
      width: stroke.width,
      linecap: 'butt',
      linejoin: 'miter',
    });
  }

  polylines.push({
    key: `${block.key}-lite-idle-trunk`,
    layer: 'slot',
    points: pts([
      [exitX, road.mergeY],
      [exitX, UNIT_BODY.height],
    ]),
    color: ROAD_IDLE_STROKE.color,
    width: ROAD_IDLE_STROKE.width,
    linecap: 'butt',
    linejoin: 'miter',
  });

  if (run.slotId && run.color && run.exitsUnit) {
    const stroke = paint(unitRunStroke(run, run.slotId));
    polylines.push({
      key: `${block.key}-lite-loaded-trunk`,
      layer: 'slot',
      points: pts([
        [exitX, loadedTurnY],
        [exitX, UNIT_BODY.height],
      ]),
      color: stroke.color,
      width: stroke.width,
      linecap: 'butt',
      linejoin: 'miter',
    });
  }
}

function appendRow4(
  polylines: RoadPolyline[],
  nubs: RoadNub[],
  block: Extract<RoadBlock, { kind: SlotLayout }>,
  slotAreaVisible: boolean,
) {
  if (block.kind !== 'row4' || !slotAreaVisible) return;
  const centers = row4SlotCenters(4).map((x) => snapRoadX(block.origin + x));
  const barY = UNIT_ROAD_TOP + ROW4_ROAD.barY;
  const barLeft = centers[0];
  const barRight = centers[centers.length - 1];
  const middle = snapRoadX(block.origin + UNIT_BODY.width / 2);
  const run = unitRun(block);
  const loaded = run.slotId ? block.slots.find((slot) => slot.slot_id === run.slotId) : undefined;
  const loadedIndex = loaded ? block.slots.indexOf(loaded) : -1;
  const loadedX = loadedIndex >= 0 && loadedIndex < centers.length ? centers[loadedIndex] : middle;

  for (const x of centers) {
    polylines.push({
      key: `${block.key}-row4-stub-${x}`,
      layer: 'slot',
      points: pts([
        [x, UNIT_ROAD_TOP],
        [x, barY],
      ]),
      color: ROAD_IDLE_STROKE.color,
      width: ROAD_IDLE_STROKE.width,
      linecap: 'round',
      linejoin: 'round',
    });
  }

  polylines.push({
    key: `${block.key}-row4-bar`,
    layer: 'slot',
    points: pts([
      [barLeft, barY],
      [barRight, barY],
    ]),
    color: ROAD_IDLE_STROKE.color,
    width: ROAD_IDLE_STROKE.width,
    linecap: 'round',
    linejoin: 'round',
  });

  polylines.push({
    key: `${block.key}-row4-drop`,
    layer: 'slot',
    points: pts([
      [middle, barY],
      [middle, UNIT_BODY.height],
    ]),
    color: ROAD_IDLE_STROKE.color,
    width: ROAD_IDLE_STROKE.width,
    linecap: 'round',
    linejoin: 'round',
  });

  if (loaded && loadedIndex >= 0 && loadedIndex < centers.length) {
    const stroke = paint(unitRunStroke(run, loaded.slot_id));
    const points: Array<[number, number]> = [
      [loadedX, UNIT_ROAD_TOP],
      [loadedX, barY],
      [middle, barY],
    ];
    if (run.exitsUnit) points.push([middle, UNIT_BODY.height]);
    polylines.push({
      key: `${block.key}-row4-loaded`,
      layer: 'slot',
      points: pts(points),
      color: stroke.color,
      width: stroke.width,
      linecap: 'round',
      linejoin: 'round',
    });
  }

  nubs.push({
    key: `${block.key}-row4-nub`,
    x: middle - ROW4_ROAD.nub.width / 2,
    y: barY - 5,
    width: ROW4_ROAD.nub.width,
    height: ROW4_ROAD.nub.height,
    fill: COLORS.roadNub,
  });
}

function appendSingle(
  polylines: RoadPolyline[],
  block: Extract<RoadBlock, { kind: SlotLayout }>,
  slotAreaVisible: boolean,
) {
  if (block.kind !== 'single_ht' || !slotAreaVisible) return;
  const width = unitWidth('single_ht');
  const x = snapRoadX(block.origin + width / 2);
  const slot = block.slots[0];
  const stroke = paint(unitRunStroke(unitRun(block), slot ? slot.slot_id : ''));
  polylines.push({
    key: `${block.key}-single`,
    layer: 'slot',
    points: pts([
      [x, UNIT_ROAD_TOP],
      [x, UNIT_BODY.height],
    ]),
    color: stroke.color,
    width: stroke.width,
    linecap: 'round',
    linejoin: 'round',
  });
}

function appendExt(
  polylines: RoadPolyline[],
  block: Extract<RoadBlock, { kind: 'ext' }>,
  slotAreaVisible: boolean,
) {
  if (!slotAreaVisible) return;
  if (!block.link || block.link.extruder_ids.length === 0) return;
  const x = snapRoadX(block.origin + extDropX(block.framed, block.slotOffset, block.liteStyle, block.panelWidth));
  const top = extRoadTop(block.liteStyle);
  const stroke = paint(
    block.exit ? passingRoadStroke(block.exit.active, block.exit.color) : ROAD_IDLE_STROKE,
  );
  polylines.push({
    key: `${block.key}-ext`,
    layer: 'slot',
    points: pts([
      [x, top],
      [x, UNIT_BODY.height],
    ]),
    color: stroke.color,
    width: stroke.width,
    linecap: 'butt',
    linejoin: 'miter',
  });
}

function blockExitX(block: RoadBlock) {
  if (block.kind === 'ext') {
    return snapRoadX(block.origin + extDropX(block.framed, block.slotOffset, block.liteStyle, block.panelWidth));
  }
  return snapRoadX(block.origin + unitExitOffset(block.kind, block.bodyWidth));
}

export function buildRoadOverlay(opts: {
  blocks: RoadBlock[];
  slotAreaVisible: boolean;
  nozzleXOf: (extruderId: number) => number;
}): RoadOverlayModel {
  const polylines: RoadPolyline[] = [];
  const nubs: RoadNub[] = [];
  const band = overlayLineBand(opts.slotAreaVisible);

  for (const block of opts.blocks) {
    if (block.kind === 'ext') {
      appendExt(polylines, block, opts.slotAreaVisible);
    } else {
      appendLite(polylines, block, opts.slotAreaVisible);
      appendRow4(polylines, nubs, block, opts.slotAreaVisible);
      appendSingle(polylines, block, opts.slotAreaVisible);
    }
  }
  appendDownRoadLines(polylines, opts.blocks, band, opts.nozzleXOf);

  return {
    width: CONTENT_WIDTH,
    height: overlayStageHeight(opts.slotAreaVisible),
    polylines,
    nubs,
  };
}
