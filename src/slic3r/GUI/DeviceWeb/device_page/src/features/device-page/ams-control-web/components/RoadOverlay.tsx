import { CONTENT_WIDTH, px } from '../dip';
import type { RoadOverlayModel, RoadLayer } from '../roadPaths';

export function RoadOverlay({
  model,
  showSlot,
  showLine,
}: {
  model: RoadOverlayModel;
  showSlot: boolean;
  showLine: boolean;
}) {
  if (!showSlot && !showLine) return null;

  const visible = (layer: RoadLayer) => (layer === 'slot' ? showSlot : showLine);

  return (
    <svg
      data-testid="ams-road-overlay"
      className="pointer-events-none absolute left-0 top-0"
      width={CONTENT_WIDTH}
      height={model.height}
      aria-hidden
      overflow="visible"
      style={{ display: 'block', width: px(CONTENT_WIDTH), height: px(model.height) }}
    >
      {model.polylines.filter((line) => visible(line.layer)).map((line) => (
        <polyline
          key={line.key}
          points={line.points}
          fill="none"
          stroke={line.color}
          strokeWidth={line.width}
          strokeLinecap={line.linecap}
          strokeLinejoin={line.linejoin}
        />
      ))}
      {showSlot
        ? model.nubs.map((nub) => (
            <rect
              key={nub.key}
              x={nub.x}
              y={nub.y}
              width={nub.width}
              height={nub.height}
              fill={nub.fill}
            />
          ))
        : null}
    </svg>
  );
}
