// Small rotating brand-coloured arc shown in the card-head right slot while
// a slot is in `Loading` / `Unloading`. Mirrors Figma 1498:84068 and Figma
// 965:45176 (loading icon). Sits in the same 16×16 slot the BatchSlotCheckbox
// occupies in batch mode; FilamentCard chooses one over the other.

export function LoadingArcIcon() {
  return (
    <span
      className="card-loading-arc"
      role="img"
      aria-label="loading"
      data-testid="ams-hotend-card-loading"
    >
      <svg width={16} height={16} viewBox="0 0 16 16" aria-hidden="true">
        <g className="spool-loading-rotor" style={{ transformOrigin: '8px 8px' }}>
          <circle
            className="spool-loading-mark"
            cx={8}
            cy={8}
            r={6}
            fill="none"
            stroke="var(--ams-brand)"
            strokeWidth={2}
            strokeLinecap="round"
            // ~75% of the circumference filled: large enough to read as
            // "progress", short enough to leave a visible gap that telegraphs
            // the rotation when animated.
            strokeDasharray={`${2 * Math.PI * 6 * 0.75} 999`}
          />
        </g>
      </svg>
    </span>
  );
}
