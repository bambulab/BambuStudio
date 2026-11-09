import type { AmsFilamentHotendLineState } from '../types';
import { STANDBY_COLOR, visibleFeedLineColor, visibleFilamentColor } from '../visibleFilamentColor';

// Default spool color emitted by C++ when no real filament color is known;
// fall back to standby grey so the line never paints a faux "loaded" hue.
const FALLBACK_SPOOL_COLOR = '#D9D9D9';

export function FeedLine({
  height,
  state,
  color,
  hasFilamentInNozzle,
  useFeedLineDisplay,
}: {
  height: number;
  state: AmsFilamentHotendLineState;
  color?: string;
  hasFilamentInNozzle?: boolean;
  // Normal Filament-tab only. Batch modes keep the historical white-tint path.
  useFeedLineDisplay?: boolean;
}) {
  if (state === 'no_hotend') return <div className="feed-line feed-line-empty" style={{ height }} />;

  // Drive the tube tint from the rack nozzle signal (DevNozzle::HasFilamentInNozzle)
  // shipped on hotend.has_filament_in_nozzle; line_state stays as the fallback
  // for legacy payloads that do not include it. The spool color paints the
  // tube only when both the nozzle holds filament AND C++ knows the real
  // color (default `#D9D9D9` is treated as "unknown" and falls back to grey).
  const hasRealColor = !!color && color.toLowerCase() !== FALLBACK_SPOOL_COLOR.toLowerCase();
  const isLive = hasFilamentInNozzle ?? (state === 'loaded' || state === 'loading' || state === 'unloading');
  const lineColor = !isLive
    ? STANDBY_COLOR
    : useFeedLineDisplay
      ? visibleFeedLineColor(color)
      : (hasRealColor && color ? visibleFilamentColor(color) : STANDBY_COLOR);

  // Per Figma 1477:113586, the feed line is solid when filament is fully
  // loaded; loading / unloading keep the dashed look to convey motion;
  // no_filament stays dashed as the standby tube hint.
  const isSolid = state === 'loaded';

  // Match the Nozzle SVG width (24px) so both children of `.slot-column`
  // center on the same subpixel — otherwise differing intrinsic widths
  // (20 vs 24) can round to a noticeable horizontal offset between the
  // feed line and the nozzle below it at certain zoom levels.
  return (
    <svg
      className={`feed-line${isSolid ? ' feed-line-solid' : ''}`}
      width={24}
      height={height}
      viewBox={`0 0 24 ${height}`}
      preserveAspectRatio="none"
      aria-hidden="true"
    >
      <line
        x1={12}
        y1={0}
        x2={12}
        y2={height}
        stroke={lineColor}
        strokeWidth={2}
        strokeDasharray={isSolid ? undefined : '2.025 4.05'}
      />
    </svg>
  );
}
