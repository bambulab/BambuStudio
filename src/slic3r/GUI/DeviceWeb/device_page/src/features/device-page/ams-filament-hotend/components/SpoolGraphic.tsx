import { useId } from 'react';
import filamentTagUrl from '../assets/spool_filament_tag_figma.svg';
import type { AmsFilamentHotendColorType } from '../types';

// Shared pure-SVG spool graphics used by the N1x device pages (filament tab
// SpoolDisc, auto-refill group header, ...). The colored donut supports every
// color model the firmware reports: single, multi (concentric rings),
// gradient (radial) and transparent (checker). All colours resolve through the
// global --ams-* / --color-fm-chip-ring tokens in styles.css so the spool
// matches the surrounding device UI in both light and dark themes.

export const SPOOL_SIZE = 52;
export const SPOOL_OUTER = 26;
export const SPOOL_RING = 23;
export const SPOOL_HOLE = 9;

// Standalone filled spool: outer body ring + colored donut + glossy tag. Driven
// only by colors / colorType / remain so any feature can render a spool without
// synthesizing a full slot view-model.
export function SpoolGraphic({
  size = SPOOL_SIZE,
  colors,
  colorType,
  remain = 100,
  showTag = true,
  className,
}: {
  size?: number;
  colors: string[];
  colorType?: AmsFilamentHotendColorType;
  remain?: number;
  showTag?: boolean;
  className?: string;
}) {
  const reactId = useId().replace(/:/g, '');
  const center = SPOOL_SIZE / 2;
  const norm = normalizeColors(colors);
  const materialId = `spool-material-${reactId}`;

  return (
    <svg
      className={className ? `spool ${className}` : 'spool'}
      width={size}
      height={size}
      viewBox={`0 0 ${SPOOL_SIZE} ${SPOOL_SIZE}`}
      role="img"
    >
      <circle cx={center} cy={center} r={SPOOL_OUTER} fill="var(--ams-overlay-bg)" />
      <circle cx={center} cy={center} r={SPOOL_OUTER - 0.5} fill="none" stroke="var(--ams-border)" strokeWidth={1} />
      <MaterialDonut center={center} colors={norm} colorType={colorType} pct={remain} idPrefix={materialId} />
      {showTag && remain > 0 && (
        <image href={filamentTagUrl} x={0} y={0} width={SPOOL_SIZE} height={SPOOL_SIZE} preserveAspectRatio="none" />
      )}
    </svg>
  );
}

export function MaterialDonut({
  center,
  colors,
  colorType,
  pct,
  idPrefix,
}: {
  center: number;
  colors: string[];
  colorType?: AmsFilamentHotendColorType;
  pct: number;
  idPrefix: string;
}) {
  const maskId = `${idPrefix}-remain`;
  const gradientId = `${idPrefix}-gradient`;
  const checkerId = `${idPrefix}-checker`;
  const transparent = colors.some(isTransparentColor);
  const safePct = Math.max(0, Math.min(100, pct));

  return (
    <>
      <defs>
        <RemainMask id={maskId} center={center} pct={safePct} />
        {colorType === 'gradient' && (
          <radialGradient
            id={gradientId}
            cx={center}
            cy={center}
            r={SPOOL_RING}
            fx={center}
            fy={center}
            gradientUnits="userSpaceOnUse"
          >
            {/* Flat lead-in across the hidden centre so the inner edge of the
                visible ring starts exactly on colors[0]. */}
            <stop offset="0%" stopColor={opaqueColor(colors[0])} stopOpacity={colorOpacity(colors[0])} />
            {colors.map((c, i) => (
              <stop
                key={`${c}-${i}`}
                offset={`${gradientStopOffset(i, colors.length)}%`}
                stopColor={opaqueColor(c)}
                stopOpacity={colorOpacity(c)}
              />
            ))}
          </radialGradient>
        )}
        {transparent && <CheckerPattern id={checkerId} />}
      </defs>

      <circle cx={center} cy={center} r={SPOOL_RING} fill="var(--ams-overlay-bg)" />
      <g mask={`url(#${maskId})`}>
        {transparent && <circle cx={center} cy={center} r={SPOOL_RING} fill={`url(#${checkerId})`} />}
        <MaterialFill center={center} colors={colors} colorType={colorType} gradientId={gradientId} />
      </g>
      <circle cx={center} cy={center} r={SPOOL_RING} fill="none" stroke="var(--color-fm-chip-ring)" strokeWidth={1} />
      <circle cx={center} cy={center} r={SPOOL_HOLE} fill="var(--ams-spool-hole-bg)" />
      <circle cx={center} cy={center} r={SPOOL_HOLE} fill="none" stroke="var(--ams-border)" strokeWidth={1} />
    </>
  );
}

function MaterialFill({
  center,
  colors,
  colorType,
  gradientId,
}: {
  center: number;
  colors: string[];
  colorType?: AmsFilamentHotendColorType;
  gradientId: string;
}) {
  if (colorType === 'gradient') {
    return <circle cx={center} cy={center} r={SPOOL_RING} fill={`url(#${gradientId})`} />;
  }
  if (colors.length > 1) {
    return <ConcentricRings center={center} colors={colors} />;
  }
  return (
    <circle cx={center} cy={center} r={SPOOL_RING} fill={opaqueColor(colors[0])} fillOpacity={colorOpacity(colors[0])} />
  );
}

// colors[0] is the innermost band, colors[N-1] the outermost one. The discs are
// painted from the outside in so each smaller one covers the previous.
function ConcentricRings({ center, colors }: { center: number; colors: string[] }) {
  const band = (SPOOL_RING - SPOOL_HOLE) / colors.length;
  return (
    <>
      {colors.map((_, index) => {
        const color = colors[colors.length - 1 - index];
        const radius = SPOOL_RING - band * index;
        return (
          <circle
            key={`${color}-${index}`}
            cx={center}
            cy={center}
            r={radius}
            fill={opaqueColor(color)}
            fillOpacity={colorOpacity(color)}
          />
        );
      })}
    </>
  );
}

function RemainMask({ id, center, pct }: { id: string; center: number; pct: number }) {
  const ringRadius = (SPOOL_RING + SPOOL_HOLE) / 2;
  const stroke = SPOOL_RING - SPOOL_HOLE;
  const circumference = 2 * Math.PI * ringRadius;
  const filled = (pct / 100) * circumference;
  const gap = circumference - filled;
  return (
    <mask id={id} maskUnits="userSpaceOnUse">
      <rect x={0} y={0} width={SPOOL_SIZE} height={SPOOL_SIZE} fill="black" />
      {pct >= 100 ? (
        <circle cx={center} cy={center} r={SPOOL_RING} fill="white" />
      ) : (
        <g transform={`rotate(-90 ${center} ${center})`}>
          <circle
            cx={center}
            cy={center}
            r={ringRadius}
            fill="none"
            stroke="white"
            strokeWidth={stroke}
            strokeDasharray={`${filled} ${gap}`}
          />
        </g>
      )}
    </mask>
  );
}

// Neutral checkerboard, same tones / tile as the auto-refill
// FilamentColorSwatch. It must stay achromatic: a tinted backdrop shows
// through the filament fill and gives translucent white / clear spools a
// colour they do not have.
const CHECKER_BG = '#ffffff';
const CHECKER_FG = '#d9d9d9';

function CheckerPattern({ id }: { id: string }) {
  const tile = 6;
  return (
    <pattern id={id} width={tile} height={tile} patternUnits="userSpaceOnUse">
      <rect width={tile} height={tile} fill={CHECKER_BG} />
      <rect width={tile / 2} height={tile / 2} fill={CHECKER_FG} />
      <rect x={tile / 2} y={tile / 2} width={tile / 2} height={tile / 2} fill={CHECKER_FG} />
    </pattern>
  );
}

export function normalizeColors(colors: string[]) {
  return colors.length ? colors.map(normalizeColor) : ['#D9D9D9'];
}

function normalizeColor(raw: string) {
  if (!raw) return '#D9D9D9';
  if (raw === 'transparent') return raw;
  return raw[0] === '#' ? raw : `#${raw}`;
}

function isTransparentColor(color: string) {
  if (color === 'transparent') return true;
  const alpha = alphaFromHex(color);
  return alpha !== null && alpha < 255;
}

function opaqueColor(color: string) {
  if (color === 'transparent') return CHECKER_BG;
  if (/^#[0-9a-fA-F]{8}$/.test(color)) return color.slice(0, 7);
  return color;
}

// Firmware alpha drives the paint opacity. A fully clear spool paints nothing
// so only the checker shows; a partially translucent one keeps its own hue at
// the reported alpha, floored so a near-clear spool still tints the checker.
function colorOpacity(color: string) {
  if (color === 'transparent') return 0;
  const alpha = alphaFromHex(color);
  if (alpha === null) return 1;
  if (alpha === 0) return 0;
  return Math.max(0.2, alpha / 255);
}

// Firmware reports the colour list from the inside of the spool outwards, so
// colors[0] must land on the inner edge of the visible ring (r = SPOOL_HOLE)
// and colors[N-1] on its outer edge (r = SPOOL_RING). Spreading the stops over
// the full 0..SPOOL_RING radius instead buries the first colour(s) under the
// centre hole, which is why a 4-colour spool only showed 3 colours.
const RING_INNER_OFFSET = SPOOL_HOLE / SPOOL_RING;

function gradientStopOffset(index: number, total: number) {
  if (total <= 1) return 100;
  return (RING_INNER_OFFSET + (1 - RING_INNER_OFFSET) * (index / (total - 1))) * 100;
}

function alphaFromHex(color: string) {
  if (!/^#[0-9a-fA-F]{8}$/.test(color)) return null;
  return Number.parseInt(color.slice(7, 9), 16);
}
