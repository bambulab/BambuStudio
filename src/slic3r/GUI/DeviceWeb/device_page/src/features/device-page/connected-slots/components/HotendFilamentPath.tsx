import { useCallback, useEffect, useLayoutEffect, useRef, useState, type RefObject } from 'react';
import { Nozzle } from '../../ams-filament-hotend/components/Nozzle';
import { visibleFeedLineColor } from '../../ams-filament-hotend/visibleFilamentColor';
import { documentZoom } from '../../../../hooks/useFitHostWidth';
import type { ConnectedSlotsLineInfo, ConnectedSlotsSlotViewModel } from '../types';

const EMPTY_LINE_COLOR = '#cecece';
const HUB_COLOR = '#c2c2c2';
// Figma 2440:143981: the shared horizontal rail runs 12px above the hotend top
// edge, and both routes turn with a 2px corner radius.
const RAIL_OFFSET = 12;
const CORNER_RADIUS = 2;
// Figma 8113:48393: inside a multi-tray board every tray stub drops 8px below
// the card row onto a feeder rail that converges on a 36x6 hub at the board
// centre; the hub then feeds the hotend route.
const FEEDER_OFFSET = 8;
const HUB_WIDTH = 36;
const HUB_HEIGHT = 6;
const ROUTE_DASH = '8 8';
const FEEDER_DASH = '4 4';

type Anchor = { x: number; y: number };

type BoardGeometry = {
  trays: Anchor[];
  feedIndex: number;
  hubX: number;
};

type RouteGeometry = {
  left: BoardGeometry | null;
  right: BoardGeometry | null;
  hotend: Anchor | null;
};

type Side = 'left' | 'right';

type Stroke = { key: string; d: string; color: string; dashed: boolean; dash: string };

const EMPTY_GEOMETRY: RouteGeometry = { left: null, right: null, hotend: null };

// Fully transparent tray colours (e.g. an unset Ext spool reports `00000000`)
// would otherwise paint an invisible route.
function strokeOf(line: ConnectedSlotsLineInfo, fallback: string) {
  if (!line.has_filament) return EMPTY_LINE_COLOR;
  return visibleFeedLineColor(line.filament_color || fallback);
}

// Rounded to half a pixel so sub-pixel layout noise does not re-render.
function geometryKey(geometry: RouteGeometry) {
  return JSON.stringify(geometry, (_, v) => (typeof v === 'number' ? Math.round(v * 2) / 2 : v));
}

/**
 * Orthogonal feed route (Figma Line 518 / 519): a vertical stub down from the
 * tray card, the shared horizontal rail, then a vertical drop into the hotend.
 */
function buildRoutePath(from: Anchor, rail: number, to: Anchor): string {
  const r = CORNER_RADIUS;
  const run = to.x - from.x;
  if (Math.abs(run) < 2 * r) {
    return `M ${from.x} ${from.y} V ${to.y}`;
  }
  const dir = run > 0 ? 1 : -1;
  return [
    `M ${from.x} ${from.y}`,
    `V ${rail - r}`,
    `Q ${from.x} ${rail} ${from.x + dir * r} ${rail}`,
    `H ${to.x - dir * r}`,
    `Q ${to.x} ${rail} ${to.x} ${rail + r}`,
    `V ${to.y}`,
  ].join(' ');
}

/** Tray stub down to the feeder rail, then along it to the hub centre. */
function buildFeederPath(from: Anchor, feeder: number, hubX: number): string {
  const r = CORNER_RADIUS;
  const run = hubX - from.x;
  if (Math.abs(run) < r) {
    return `M ${from.x} ${from.y} V ${feeder}`;
  }
  const dir = run > 0 ? 1 : -1;
  return [
    `M ${from.x} ${from.y}`,
    `V ${feeder - r}`,
    `Q ${from.x} ${feeder} ${from.x + dir * r} ${feeder}`,
    `H ${hubX}`,
  ].join(' ');
}

// Anchors come from the live layout instead of hard-coded design offsets so the
// routes stay attached when a board grows (tray count, longer unit name, DPI).
function useRouteGeometry(
  stageRef: RefObject<HTMLDivElement | null>,
  nozzleRef: RefObject<HTMLDivElement | null>,
  revision: unknown,
) {
  const [geometry, setGeometry] = useState<RouteGeometry>(EMPTY_GEOMETRY);

  const measure = useCallback(() => {
    const stage = stageRef.current;
    if (!stage) return;
    const base = stage.getBoundingClientRect();
    if (base.width === 0) return;

    const zoom = documentZoom();
    const toLocalX = (clientX: number) => (clientX - base.left) / zoom;
    const toLocalY = (clientY: number) => (clientY - base.top) / zoom;

    const boardOf = (side: Side): BoardGeometry | null => {
      const board = stage.querySelector<HTMLElement>(`[data-cs-board-anchor="${side}"]`);
      if (!board) return null;
      const cards = Array.from(board.querySelectorAll<HTMLElement>('[data-cs-tray-anchor]'));
      if (cards.length === 0) return null;
      const boardRect = board.getBoundingClientRect();
      return {
        trays: cards.map((card) => {
          const rect = card.getBoundingClientRect();
          return { x: toLocalX(rect.left + rect.width / 2), y: toLocalY(rect.bottom) };
        }),
        feedIndex: cards.findIndex((card) => card.dataset.csFeed === 'true'),
        hubX: toLocalX(boardRect.left + boardRect.width / 2),
      };
    };

    const nozzle = nozzleRef.current;
    const nozzleRect = nozzle?.getBoundingClientRect();
    const next: RouteGeometry = {
      left: boardOf('left'),
      right: boardOf('right'),
      hotend: nozzleRect
        ? {
          x: toLocalX(nozzleRect.left + nozzleRect.width / 2),
          y: toLocalY(nozzleRect.top),
        }
        : null,
    };

    setGeometry((prev) => (geometryKey(prev) === geometryKey(next) ? prev : next));
  }, [nozzleRef, stageRef]);

  useLayoutEffect(() => {
    measure();
  }, [measure, revision]);

  useEffect(() => {
    const stage = stageRef.current;
    if (!stage || typeof ResizeObserver !== 'function') return;
    const observer = new ResizeObserver(() => measure());
    observer.observe(stage);
    return () => observer.disconnect();
  }, [measure, stageRef]);

  return geometry;
}

/**
 * Strokes for one board. A single-tray board (rack slot, Ext) routes straight
 * into the hotend; a multi-tray board draws every tray stub into its hub and
 * routes the hub instead, painting only the feeding tray's stub solid.
 */
function boardStrokes(
  side: Side,
  board: BoardGeometry,
  line: ConnectedSlotsLineInfo,
  color: string,
  hotend: Anchor,
  rail: number,
): { strokes: Stroke[]; hub: Anchor | null } {
  const loaded = !!line.has_filament;
  if (board.trays.length === 1) {
    return {
      strokes: [{
        key: `${side}-route`,
        d: buildRoutePath(board.trays[0], rail, hotend),
        color,
        dashed: !loaded,
        dash: ROUTE_DASH,
      }],
      hub: null,
    };
  }

  const feeder = Math.max(...board.trays.map((t) => t.y)) + FEEDER_OFFSET;
  const xs = board.trays.map((t) => t.x);
  const strokes: Stroke[] = board.trays.map((tray, i) => ({
    key: `${side}-stub-${i}`,
    d: `M ${tray.x} ${tray.y} V ${feeder}`,
    color: EMPTY_LINE_COLOR,
    dashed: true,
    dash: FEEDER_DASH,
  }));
  strokes.push({
    key: `${side}-feeder`,
    d: `M ${Math.min(...xs, board.hubX)} ${feeder} H ${Math.max(...xs, board.hubX)}`,
    color: EMPTY_LINE_COLOR,
    dashed: true,
    dash: FEEDER_DASH,
  });
  if (loaded && board.feedIndex >= 0) {
    strokes.push({
      key: `${side}-feed`,
      d: buildFeederPath(board.trays[board.feedIndex], feeder, board.hubX),
      color,
      dashed: false,
      dash: '',
    });
  }
  strokes.push({
    key: `${side}-route`,
    d: buildRoutePath({ x: board.hubX, y: feeder + HUB_HEIGHT / 2 }, rail, hotend),
    color,
    dashed: !loaded,
    dash: ROUTE_DASH,
  });
  return { strokes, hub: { x: board.hubX, y: feeder } };
}

/**
 * Dual-column routes from the two unit boards into the shared hotend.
 * Not the vertical FeedLine tube used by the main filament page.
 */
export function HotendFilamentPath({
  slot,
  stageRef,
}: {
  slot: ConnectedSlotsSlotViewModel;
  stageRef: RefObject<HTMLDivElement | null>;
}) {
  const nozzleRef = useRef<HTMLDivElement>(null);
  const { left: leftBoard, right: rightBoard, hotend } = useRouteGeometry(stageRef, nozzleRef, slot);

  const leftLine = slot.top_slots_area.left_slot.line;
  const rightLine = slot.top_slots_area.right_slots.line;
  const leftColor = strokeOf(leftLine, '#00ae42');
  const rightColor = strokeOf(rightLine, '#00ae42');
  const nozzleColor = slot.bottom_hotend.nozzle.filament_color || rightColor;
  const rail = hotend ? hotend.y - RAIL_OFFSET : 0;

  const strokes: Stroke[] = [];
  const hubs: { key: string; at: Anchor }[] = [];
  if (hotend) {
    const boards = [
      { side: 'left' as const, board: leftBoard, line: leftLine, color: leftColor },
      { side: 'right' as const, board: rightBoard, line: rightLine, color: rightColor },
    ];
    for (const { side, board, line, color } of boards) {
      if (!board) continue;
      const result = boardStrokes(side, board, line, color, hotend, rail);
      strokes.push(...result.strokes);
      if (result.hub) hubs.push({ key: side, at: result.hub });
    }
  }
  // Loaded strokes paint last so they stay on top where the drops overlap.
  strokes.sort((a, b) => Number(!a.dashed) - Number(!b.dashed));

  return (
    <div className="cs-hotend-filament-path" data-testid="hotend-filament-path">
      <svg className="cs-hotend-filament-path__routes" aria-hidden="true">
        {strokes.map((stroke) => (
          <path
            key={stroke.key}
            d={stroke.d}
            fill="none"
            stroke={stroke.color}
            strokeWidth="2"
            strokeDasharray={stroke.dashed ? stroke.dash : undefined}
          />
        ))}
        {hubs.map((hub) => (
          <rect
            key={hub.key}
            x={hub.at.x - HUB_WIDTH / 2}
            y={hub.at.y - HUB_HEIGHT / 2}
            width={HUB_WIDTH}
            height={HUB_HEIGHT}
            rx={2}
            fill={HUB_COLOR}
          />
        ))}
      </svg>
      <div className="cs-hotend-filament-path__nozzle" ref={nozzleRef}>
        <Nozzle
          nozzleState={slot.bottom_hotend.nozzle.state}
          displayIndex={slot.display_index}
          color={nozzleColor}
          hasFilamentInNozzle={slot.bottom_hotend.nozzle.has_filament}
          onExtruder={slot.bottom_hotend.nozzle.on_extruder}
        />
      </div>
    </div>
  );
}
