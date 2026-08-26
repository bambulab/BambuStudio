import { useCallback, useEffect, useLayoutEffect, useRef, useState, type RefObject } from 'react';
import { Nozzle } from '../../ams-filament-hotend/components/Nozzle';
import { visibleFilamentColor } from '../../ams-filament-hotend/visibleFilamentColor';
import type { ConnectedSlotsLineInfo, ConnectedSlotsSlotViewModel } from '../types';

const EMPTY_LINE_COLOR = '#cecece';
// Figma 2440:143981: the shared horizontal rail runs 12px above the hotend top
// edge, and both routes turn with a 2px corner radius.
const RAIL_OFFSET = 12;
const CORNER_RADIUS = 2;

type Anchor = { x: number; y: number };

type RouteGeometry = {
  left: Anchor | null;
  right: Anchor | null;
  hotend: Anchor | null;
};

const EMPTY_GEOMETRY: RouteGeometry = { left: null, right: null, hotend: null };

function strokeOf(line: ConnectedSlotsLineInfo, fallback: string) {
  if (!line.has_filament) return EMPTY_LINE_COLOR;
  return visibleFilamentColor(line.filament_color || fallback);
}

function sameAnchor(a: Anchor | null, b: Anchor | null) {
  if (!a || !b) return a === b;
  return Math.abs(a.x - b.x) < 0.5 && Math.abs(a.y - b.y) < 0.5;
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

    const bottomCenterOf = (selector: string): Anchor | null => {
      const el = stage.querySelector<HTMLElement>(selector);
      if (!el) return null;
      const rect = el.getBoundingClientRect();
      return { x: rect.left + rect.width / 2 - base.left, y: rect.bottom - base.top };
    };

    const nozzle = nozzleRef.current;
    const nozzleRect = nozzle?.getBoundingClientRect();
    const next: RouteGeometry = {
      left: bottomCenterOf('[data-cs-feed-anchor="left"]'),
      right: bottomCenterOf('[data-cs-feed-anchor="right"]'),
      hotend: nozzleRect
        ? {
          x: nozzleRect.left + nozzleRect.width / 2 - base.left,
          y: nozzleRect.top - base.top,
        }
        : null,
    };

    setGeometry((prev) => (
      sameAnchor(prev.left, next.left)
        && sameAnchor(prev.right, next.right)
        && sameAnchor(prev.hotend, next.hotend)
        ? prev
        : next
    ));
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
  const { left: leftAnchor, right: rightAnchor, hotend } = useRouteGeometry(stageRef, nozzleRef, slot);

  const leftLine = slot.top_slots_area.left_slot.line;
  const rightLine = slot.top_slots_area.right_slots.line;
  const leftColor = strokeOf(leftLine, '#00ae42');
  const rightColor = strokeOf(rightLine, '#00ae42');
  const nozzleColor = slot.bottom_hotend.nozzle.filament_color || rightColor;
  const rail = hotend ? hotend.y - RAIL_OFFSET : 0;

  // Loaded routes paint last so they stay on top where the drops overlap.
  const routes = [
    { key: 'left', anchor: leftAnchor, line: leftLine, color: leftColor },
    { key: 'right', anchor: rightAnchor, line: rightLine, color: rightColor },
  ]
    .filter((route) => route.anchor && hotend)
    .sort((a, b) => Number(!!a.line.has_filament) - Number(!!b.line.has_filament));

  return (
    <div className="cs-hotend-filament-path" data-testid="hotend-filament-path">
      <svg className="cs-hotend-filament-path__routes" aria-hidden="true">
        {routes.map((route) => (
          <path
            key={route.key}
            d={buildRoutePath(route.anchor as Anchor, rail, hotend as Anchor)}
            fill="none"
            stroke={route.color}
            strokeWidth="2"
            strokeDasharray={route.line.has_filament ? undefined : '8 8'}
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
          solid={slot.bottom_hotend.line.state === 'loaded'}
        />
      </div>
    </div>
  );
}
