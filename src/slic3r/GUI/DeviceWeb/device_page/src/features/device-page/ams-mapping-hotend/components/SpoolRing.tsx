import filamentTagUrl from '../../ams-filament-hotend/assets/spool_filament_tag_figma.svg';
import unknownSpoolUrl from '../../ams-filament-hotend/assets/spool_unknown_figma.svg';
import type { AmsMappingHotendColorType } from '../types';

const DEFAULT_SIZE = 52;
const OUTER = 26;
const RING = 23;
const HOLE = 9;

interface SpoolRingProps {
  size?: number;
  color: string;
  colors?: string[];
  remain: number;
  isEmpty?: boolean;
  isThird?: boolean;
  colorType?: AmsMappingHotendColorType;
}

let s_uid = 0;
function nextUid(prefix: string) {
  s_uid = (s_uid + 1) | 0;
  return `${prefix}-${s_uid}`;
}

// Strip optional alpha. C++ side now emits #RRGGBBAA; legacy callers may
// still pass #RRGGBB. Returns the 6-digit RGB form for non-alpha consumers.
function stripAlpha(hex: string): string {
  if (typeof hex !== 'string' || hex.length !== 9 || hex[0] !== '#') return hex;
  return hex.slice(0, 7);
}

function isTransparentHex(hex: string): boolean {
  if (typeof hex !== 'string' || hex.length !== 9 || hex[0] !== '#') return false;
  return hex.slice(7, 9).toUpperCase() === '00';
}

export function SpoolRing({
  size = DEFAULT_SIZE,
  color,
  colors,
  remain,
  isEmpty,
  isThird,
  colorType,
}: SpoolRingProps) {
  const center = OUTER;
  const rawColors = colors && colors.length > 1 ? colors : [color];
  const effectiveColors = rawColors.map(stripAlpha);
  // `remain` already carries the display value (the C++ side floors a present
  // spool's remain to >= 5% so its color arc stays visible). The web layer
  // only renders it.
  const clampedRemain = Math.max(0, Math.min(100, remain));

  const isTransparent =
    !isEmpty &&
    !isThird &&
    (isTransparentHex(color) || rawColors.every(isTransparentHex));

  return (
    <svg
      className="spool-ring"
      width={size}
      height={size}
      viewBox={`0 0 ${DEFAULT_SIZE} ${DEFAULT_SIZE}`}
      role="img"
    >
      <circle cx={center} cy={center} r={OUTER} fill="#e6e6e6" />
      <circle cx={center} cy={center} r={OUTER - 0.5} fill="none" stroke="#d4d4d4" strokeWidth={1} />

      {isEmpty ? (
        <EmptyDisc center={center} />
      ) : isThird ? (
        <ThirdPartyDisc center={center} />
      ) : isTransparent ? (
        <TransparentDisc center={center} />
      ) : (
        <RemainDonut
          center={center}
          colors={effectiveColors}
          remain={clampedRemain}
          colorType={colorType}
        />
      )}

      {!isEmpty && !isThird && !isTransparent && clampedRemain > 0 && (
        <image href={filamentTagUrl} x={0} y={0} width={DEFAULT_SIZE} height={DEFAULT_SIZE} preserveAspectRatio="none" />
      )}
    </svg>
  );
}

function EmptyDisc({ center }: { center: number }) {
  return (
    <>
      <circle cx={center} cy={center} r={HOLE} fill="#ffffff" />
      <text
        x={center}
        y={center + 1}
        fontFamily="HarmonyOS Sans SC, system-ui, sans-serif"
        fontSize={14}
        fill="#c2c2c2"
        textAnchor="middle"
        dominantBaseline="central"
      >
        /
      </text>
    </>
  );
}

// Unknown / third-party filament: use the shared spool_unknown_figma asset
// to stay consistent with the device filament tab (SpoolDisc.tsx).
function ThirdPartyDisc({ center }: { center: number }) {
  return (
    <>
      <image
        href={unknownSpoolUrl}
        x={0}
        y={0}
        width={DEFAULT_SIZE}
        height={DEFAULT_SIZE}
        preserveAspectRatio="none"
      />
      <circle cx={center} cy={center} r={HOLE} fill="#ffffff" />
      <circle cx={center} cy={center} r={HOLE} fill="none" stroke="#e6e6e6" strokeWidth={1} />
    </>
  );
}

// Transparent filament (alpha == 0): draw a checker pattern on the donut so
// the user can recognise it as see-through. Mirrors the wxWidgets
// `m_filament_wheel_transparent` bitmap behaviour.
function TransparentDisc({ center }: { center: number }) {
  const patternId = nextUid('checker');
  return (
    <>
      <defs>
        <pattern id={patternId} width={4} height={4} patternUnits="userSpaceOnUse">
          <rect width={4} height={4} fill="#ffffff" />
          <rect width={2} height={2} fill="#cfe8d3" />
          <rect x={2} y={2} width={2} height={2} fill="#cfe8d3" />
        </pattern>
      </defs>
      <circle cx={center} cy={center} r={RING} fill={`url(#${patternId})`} />
      <circle cx={center} cy={center} r={RING} fill="none" stroke="rgba(0,0,0,0.06)" strokeWidth={1} />
      <circle cx={center} cy={center} r={HOLE} fill="#ffffff" />
      <circle cx={center} cy={center} r={HOLE} fill="none" stroke="#e6e6e6" strokeWidth={1} />
    </>
  );
}

// Filled donut for real (non-transparent, non-empty, non-unknown) filament.
// Branches by color type:
//   - single   : solid ring + remain arc (single color)
//   - gradient : SVG linearGradient across the whole ring (matches wxWidgets
//                ctype == 0 horizontal gradient)
//   - multi    : equal wedges per color (ctype == 1 segmented)
function RemainDonut({
  center,
  colors,
  remain,
  colorType,
}: {
  center: number;
  colors: string[];
  remain: number;
  colorType?: AmsMappingHotendColorType;
}) {
  if (colors.length === 1 || colorType === 'single') {
    return (
      <>
        {remain > 0 && (
          <RemainArc center={center} r={RING} color={colors[0]} remain={remain} />
        )}
        <circle cx={center} cy={center} r={RING} fill="none" stroke="rgba(0,0,0,0.06)" strokeWidth={1} />
        <circle cx={center} cy={center} r={HOLE} fill="#ffffff" />
        <circle cx={center} cy={center} r={HOLE} fill="none" stroke="#e6e6e6" strokeWidth={1} />
      </>
    );
  }

  if (colorType === 'gradient') {
    // Conic gradient simulated with many small arc segments (pure SVG, no foreignObject).
    const SEGMENTS = 36;
    const ringR = (RING + HOLE) / 2;
    const strokeW = RING - HOLE;
    const arcs: React.ReactElement[] = [];
    for (let i = 0; i < SEGMENTS; i++) {
      const t = i / SEGMENTS;
      const tNext = (i + 1) / SEGMENTS;
      const a0 = t * Math.PI * 2 - Math.PI / 2;
      const a1 = tNext * Math.PI * 2 - Math.PI / 2;
      const color = lerpColors(colors, t);
      const x0 = center + ringR * Math.cos(a0);
      const y0 = center + ringR * Math.sin(a0);
      const x1 = center + ringR * Math.cos(a1);
      const y1 = center + ringR * Math.sin(a1);
      arcs.push(
        <path
          key={i}
          d={`M ${x0} ${y0} A ${ringR} ${ringR} 0 0 1 ${x1} ${y1}`}
          fill="none"
          stroke={color}
          strokeWidth={strokeW}
        />
      );
    }
    return (
      <>
        {remain > 0 && arcs}
        <circle cx={center} cy={center} r={RING} fill="none" stroke="rgba(0,0,0,0.06)" strokeWidth={1} />
        <circle cx={center} cy={center} r={HOLE} fill="#ffffff" />
        <circle cx={center} cy={center} r={HOLE} fill="none" stroke="#e6e6e6" strokeWidth={1} />
      </>
    );
  }

  // multi (ctype == 1): concentric rings, outermost = colors[0], innermost = colors[N-1].
  const ringWidth = (RING - HOLE) / colors.length;
  return (
    <>
      {remain > 0 && colors.map((c, i) => {
        const r = RING - i * ringWidth - ringWidth / 2;
        return (
          <circle
            key={i}
            cx={center}
            cy={center}
            r={r}
            fill="none"
            stroke={c}
            strokeWidth={ringWidth}
          />
        );
      })}
      <circle cx={center} cy={center} r={RING} fill="none" stroke="rgba(0,0,0,0.06)" strokeWidth={1} />
      <circle cx={center} cy={center} r={HOLE} fill="#ffffff" />
      <circle cx={center} cy={center} r={HOLE} fill="none" stroke="#e6e6e6" strokeWidth={1} />
    </>
  );
}

function RemainArc({ center, r, color, remain }: { center: number; r: number; color: string; remain: number }) {
  if (remain >= 100) {
    return <circle cx={center} cy={center} r={r} fill={color} />;
  }

  const angle = (remain / 100) * Math.PI * 2;
  const startAngle = -Math.PI / 2;
  const endAngle = startAngle + angle;
  const d = wedgePath(center, center, r, HOLE, startAngle, endAngle);
  return <path d={d} fill={color} />;
}

// Linearly interpolate between colors in the array at position t (0..1).
function lerpColors(colors: string[], t: number): string {
  if (colors.length === 1) return colors[0];
  const scaled = t * (colors.length - 1);
  const i = Math.min(Math.floor(scaled), colors.length - 2);
  const f = scaled - i;
  return lerpHex(colors[i], colors[i + 1], f);
}

function lerpHex(a: string, b: string, t: number): string {
  const ra = parseInt(a.slice(1, 3), 16), ga = parseInt(a.slice(3, 5), 16), ba2 = parseInt(a.slice(5, 7), 16);
  const rb = parseInt(b.slice(1, 3), 16), gb = parseInt(b.slice(3, 5), 16), bb = parseInt(b.slice(5, 7), 16);
  const r = Math.round(ra + (rb - ra) * t);
  const g = Math.round(ga + (gb - ga) * t);
  const bv = Math.round(ba2 + (bb - ba2) * t);
  return `#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${bv.toString(16).padStart(2, '0')}`;
}

function wedgePath(cx: number, cy: number, r1: number, r0: number, a0: number, a1: number) {
  const x0o = cx + r1 * Math.cos(a0);
  const y0o = cy + r1 * Math.sin(a0);
  const x1o = cx + r1 * Math.cos(a1);
  const y1o = cy + r1 * Math.sin(a1);
  const x0i = cx + r0 * Math.cos(a1);
  const y0i = cy + r0 * Math.sin(a1);
  const x1i = cx + r0 * Math.cos(a0);
  const y1i = cy + r0 * Math.sin(a0);
  const large = a1 - a0 > Math.PI ? 1 : 0;
  return [
    `M ${x0o} ${y0o}`,
    `A ${r1} ${r1} 0 ${large} 1 ${x1o} ${y1o}`,
    `L ${x0i} ${y0i}`,
    `A ${r0} ${r0} 0 ${large} 0 ${x1i} ${y1i}`,
    'Z',
  ].join(' ');
}
