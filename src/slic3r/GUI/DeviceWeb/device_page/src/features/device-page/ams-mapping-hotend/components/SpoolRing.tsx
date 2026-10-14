import type { AmsMappingHotendColorType } from '../types';

const DEFAULT_SIZE = 52;
const OUTER = 26; // svg center

// Stroke frame (grey-600, 1px each) with 2px white gaps on both sides of the
// color ring. Ring ratio inner:outer = 18:52 per design (inner ring Ø18 => r9,
// outer ring Ø52 => outer edge at r26). Radii in the 52-unit viewBox, from the
// outer edge inward:
//   outer stroke  25..26   (1px, grey-600, outer ring Ø52)
//   white gap     23..25   (2px)
//   color ring    11.5..23
//   white gap     9.5..11.5 (2px)
//   inner stroke  8.5..9.5 (1px, grey-600, inner ring Ø18)
//   white hole    < 8.5
const STROKE_W = 1;
// Structural (non-filament) colors follow the popup theme via CSS variables
// defined on `.ams-map-hotend-page` (light) and its `[data-theme="dark"]`
// override. Fallbacks keep the light look when rendered outside the popup.
// The filament color content itself is never themed.
const STROKE_COLOR = 'var(--spool-frame, #909090)'; // grey-600 frame
const SURFACE_COLOR = 'var(--spool-surface, #ffffff)'; // base / gaps / hole
const TRACK_COLOR = 'var(--spool-track, #eeeeee)'; // grey-300 consumed amount
// Outline drawn around the color ring for light filament so it stays
// distinguishable from the consumed track / gaps. Same color as the outer
// frame, half its width per design.
const OUTLINE_W = STROKE_W / 2;
const OUTER_STROKE_R = 25.5;
const COLOR_OUTER = 23;
const COLOR_INNER = 11.5;
const INNER_STROKE_R = 9;
// Checker backdrop for translucent filament. Same tile / tones as the device
// page spool (SpoolGraphic.tsx) so both surfaces read identically. It must
// stay achromatic: a tinted backdrop shows through the filament fill and gives
// translucent white / clear spools a colour they do not have.
const CHECKER_TILE = 6;
const CHECKER_BG = '#ffffff';
const CHECKER_FG = '#d9d9d9';

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
  if (hex === 'transparent') return CHECKER_BG;
  if (typeof hex !== 'string' || hex.length !== 9 || hex[0] !== '#') return hex;
  return hex.slice(0, 7);
}

function alphaFromHex(hex: string): number | null {
  if (typeof hex !== 'string' || hex.length !== 9 || hex[0] !== '#') return null;
  const alpha = Number.parseInt(hex.slice(7, 9), 16);
  return Number.isNaN(alpha) ? null : alpha;
}

// Any partial alpha counts as translucent, not just a fully clear spool.
function isTransparentColor(hex: string): boolean {
  if (hex === 'transparent') return true;
  const alpha = alphaFromHex(hex);
  return alpha !== null && alpha < 255;
}

// Firmware alpha drives the paint opacity; floor it so a near-clear spool
// still tints the checker instead of vanishing into it.
function colorOpacity(hex: string): number {
  if (hex === 'transparent') return 0;
  const alpha = alphaFromHex(hex);
  if (alpha === null) return 1;
  if (alpha === 0) return 0;
  return Math.max(0.2, alpha / 255);
}

// A color counts as "light" when its perceived luminance reaches that of the
// design threshold grey #CDCDCD (205). Such colors blend into the grey-300
// consumed track / white gaps, so the ring gets an extra outline.
const LIGHT_LUMA_THRESHOLD = 205; // luminance of #CDCDCD (205,205,205)
function isLightColor(hex: string): boolean {
  const m = /^#?([0-9a-fA-F]{6})/.exec(stripAlpha(hex));
  if (!m) return false;
  const n = parseInt(m[1], 16);
  const r = (n >> 16) & 0xff;
  const g = (n >> 8) & 0xff;
  const b = n & 0xff;
  const luma = 0.299 * r + 0.587 * g + 0.114 * b;
  return luma >= LIGHT_LUMA_THRESHOLD;
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

  const isTransparent = !isEmpty && !isThird && rawColors.some(isTransparentColor);

  // Outline the color ring only for light real filament. Use the color that
  // sits on the ring's outer edge, which is the one bordering the grey-300
  // track / white base. Gradient and multi both run inner->outer, so that is
  // the last entry; a single-color spool only has the first one.
  const outerEdgeColor =
    effectiveColors.length === 1 || colorType === 'single'
      ? effectiveColors[0]
      : effectiveColors[effectiveColors.length - 1];
  const needsOutline =
    !isEmpty && !isThird && !isTransparent && isLightColor(outerEdgeColor);

  return (
    <svg
      className="spool-ring"
      width={size}
      height={size}
      viewBox={`0 0 ${DEFAULT_SIZE} ${DEFAULT_SIZE}`}
      role="img"
    >
      {/* Surface base: every gap and the center hole reads as the surface. */}
      <circle cx={center} cy={center} r={OUTER} fill={SURFACE_COLOR} />

      {/* Consumed-amount track (grey-300): for a used spool the color arc only
          covers the remaining amount, so the rest of the ring shows this track.
          Full spools / gradient / multi / third-party paint over it entirely. */}
      {!isEmpty && <circle cx={center} cy={center} r={COLOR_OUTER} fill={TRACK_COLOR} />}

      {/* Color content: each branch fills only up to COLOR_OUTER; the inner
          part is carved back to white below so the inner gap + hole show. */}
      {isEmpty ? null : isThird ? (
        <ThirdPartyDisc center={center} />
      ) : (
        <RemainDonut
          center={center}
          colors={rawColors}
          remain={clampedRemain}
          colorType={colorType}
          isTransparent={isTransparent}
        />
      )}

      {/* Carve the inner gap + hole back to the surface above the color. */}
      <circle cx={center} cy={center} r={COLOR_INNER} fill={SURFACE_COLOR} />

      {/* Light filament: outline the color ring so it stays distinguishable
          from the grey-300 consumed track and the white gaps. Drawn after the
          inner carve so the inner edge stays visible. */}
      {needsOutline && <ColorOutline center={center} remain={clampedRemain} />}

      {/* grey-600 frame: outer + inner strokes, both 2px, each separated from
          the color ring by a 2px white gap. */}
      <circle cx={center} cy={center} r={OUTER_STROKE_R} fill="none" stroke={STROKE_COLOR} strokeWidth={STROKE_W} />
      <circle cx={center} cy={center} r={INNER_STROKE_R} fill="none" stroke={STROKE_COLOR} strokeWidth={STROKE_W} />

      {/* Empty slot marker sits in the hole, above the frame. */}
      {isEmpty && (
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
      )}

      {/* Small grey filament tag (notch); the gradient sheen overlay is dropped
          so the ring stays a flat color per design. Third-party (solid grey
          disc) shows it too regardless of remain. */}
      {!isEmpty && (isThird || clampedRemain > 0) && (
        <path
          d="M39.5 24.5L46 24.5C46.8284 24.5 47.5 25.1716 47.5 26C47.5 26.8284 46.8284 27.5 46 27.5L39.5 27.5C38.6716 27.5 38 26.8284 38 26C38 25.1716 38.6716 24.5 39.5 24.5Z"
          fill="#949495"
          fillOpacity={0.4}
        />
      )}
    </svg>
  );
}

// Unknown / third-party filament ("?"): a solid #7F7F7F gray ring per design.
function ThirdPartyDisc({ center }: { center: number }) {
  return <circle cx={center} cy={center} r={COLOR_OUTER} fill="#7f7f7f" />;
}

// Color content for real (non-empty, non-third) filament.
//   - single   : solid ring
//   - gradient : radial gradient, smooth inner->outer transition
//   - multi    : concentric bands with abrupt (cliff) transitions
// Both follow the firmware order: colors[0] innermost .. colors[N-1] outermost.
//
// Translucent filament composes as three layers inside the same remain wedge:
// grey-300 track (drawn by the caller) -> checker -> the fills above at the
// firmware alpha. Painting the color opaquely instead would make a translucent
// spool indistinguishable from a solid one.
function RemainDonut({
  center,
  colors,
  remain,
  colorType,
  isTransparent,
}: {
  center: number;
  colors: string[];
  remain: number;
  colorType?: AmsMappingHotendColorType;
  isTransparent?: boolean;
}) {
  if (remain <= 0) return null;

  const clip = remainClip(center, remain);
  const clipAttr = clip ? `url(#${clip.id})` : undefined;
  const checkerId = nextUid('checker');
  const gradId = nextUid('spool-grad');
  const isSingle = colors.length === 1 || colorType === 'single';
  const isGradient = !isSingle && colorType === 'gradient';
  const innerOffset = COLOR_INNER / COLOR_OUTER;
  const ringWidth = (COLOR_OUTER - COLOR_INNER) / colors.length;

  return (
    <>
      <defs>
        {clip?.el}
        {isTransparent && (
          <pattern id={checkerId} width={CHECKER_TILE} height={CHECKER_TILE} patternUnits="userSpaceOnUse">
            <rect width={CHECKER_TILE} height={CHECKER_TILE} fill={CHECKER_BG} />
            <rect width={CHECKER_TILE / 2} height={CHECKER_TILE / 2} fill={CHECKER_FG} />
            <rect
              x={CHECKER_TILE / 2}
              y={CHECKER_TILE / 2}
              width={CHECKER_TILE / 2}
              height={CHECKER_TILE / 2}
              fill={CHECKER_FG}
            />
          </pattern>
        )}
        {isGradient && (
          <radialGradient id={gradId} cx={center} cy={center} r={COLOR_OUTER} gradientUnits="userSpaceOnUse">
            <stop offset={0} stopColor={stripAlpha(colors[0])} stopOpacity={colorOpacity(colors[0])} />
            {colors.map((c, i) => (
              <stop
                key={i}
                offset={innerOffset + (1 - innerOffset) * (colors.length === 1 ? 1 : i / (colors.length - 1))}
                stopColor={stripAlpha(c)}
                stopOpacity={colorOpacity(c)}
              />
            ))}
          </radialGradient>
        )}
      </defs>

      <g clipPath={clipAttr}>
        {isTransparent && <circle cx={center} cy={center} r={COLOR_OUTER} fill={`url(#${checkerId})`} />}
        {isGradient ? (
          <circle cx={center} cy={center} r={COLOR_OUTER} fill={`url(#${gradId})`} />
        ) : isSingle ? (
          <circle
            cx={center}
            cy={center}
            r={COLOR_OUTER}
            fill={stripAlpha(colors[0])}
            fillOpacity={colorOpacity(colors[0])}
          />
        ) : (
          colors.map((c, i) => (
            <circle
              key={i}
              cx={center}
              cy={center}
              r={COLOR_INNER + i * ringWidth + ringWidth / 2}
              fill="none"
              stroke={stripAlpha(c)}
              strokeOpacity={colorOpacity(c)}
              strokeWidth={ringWidth}
            />
          ))
        )}
      </g>
    </>
  );
}

// Clip to the remaining-amount wedge (starting at 12 o'clock, clockwise) so
// gradient / multi fills only cover the remaining portion; the consumed part
// shows the grey-300 track underneath. Returns null when the spool is full.
function remainClip(center: number, remain: number): { id: string; el: React.ReactElement } | null {
  if (remain >= 100) return null;
  const id = nextUid('remain-clip');
  const angle = (remain / 100) * Math.PI * 2;
  const start = -Math.PI / 2;
  const d = wedgePath(center, center, COLOR_OUTER, 0, start, start + angle);
  return { id, el: <clipPath id={id}><path d={d} /></clipPath> };
}

// Outline of the color ring for light filament. Matches the color content
// geometry: a full donut (two rings) when full, otherwise the remaining wedge
// (outer arc + inner arc + the two radial edges bordering the grey-300 track).
function ColorOutline({ center, remain }: { center: number; remain: number }) {
  if (remain <= 0) return null;
  if (remain >= 100) {
    return (
      <>
        <circle cx={center} cy={center} r={COLOR_OUTER} fill="none" stroke={STROKE_COLOR} strokeWidth={OUTLINE_W} />
        <circle cx={center} cy={center} r={COLOR_INNER} fill="none" stroke={STROKE_COLOR} strokeWidth={OUTLINE_W} />
      </>
    );
  }
  const angle = (remain / 100) * Math.PI * 2;
  const start = -Math.PI / 2;
  const d = wedgePath(center, center, COLOR_OUTER, COLOR_INNER, start, start + angle);
  return <path d={d} fill="none" stroke={STROKE_COLOR} strokeWidth={OUTLINE_W} strokeLinejoin="round" />;
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
