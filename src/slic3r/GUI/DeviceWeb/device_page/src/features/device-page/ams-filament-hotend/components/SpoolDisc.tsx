import { useId } from 'react';
import type { AmsFilamentHotendSlotViewModel } from '../types';
import filamentTagUrl from '../assets/spool_filament_tag_figma.svg';
import unknownSpoolUrl from '../assets/spool_unknown_figma.svg';

const SIZE = 52;
const OUTER = 26;
const RING = 23;
const HOLE = 9;

export function SpoolDisc({
  slot,
  notAllowed,
}: {
  slot: AmsFilamentHotendSlotViewModel;
  // When true, dim the spool body and overlay a 🚫 mark — used by the
  // batch-mode card whose select_action.can_select is false.
  notAllowed?: boolean;
}) {
  const { state, color, color_type } = slot.filament_card;
  const reactId = useId().replace(/:/g, '');
  const display_index = slot.display_index;
  const cardColors = slot.filament_card.colors;
  const colors = normalizeColors(cardColors && cardColors.length ? cardColors : [color]);
  // Loading/Unloading slots still show the spool body in its loaded shape
  // (filled donut + tag); the rotating progress mark moves to the card-head
  // top-right slot instead of obscuring the disc centre. Per the firmware
  // contract a Loading/Unloading slot always has a known filament colour.
  const isTransporting = state === 'Loading' || state === 'Unloading';
  const isFilled =
    state === 'ReadyToLoad' ||
    state === 'ReadyToUse' ||
    isTransporting;
  const isError = state === 'Error';
  const showFilamentTag = isFilled;
  const center = SIZE / 2;

  const remain = typeof slot.filament_card.remain === 'number' ? slot.filament_card.remain : 100;
  const materialId = `spool-material-${reactId}-${display_index}`;

  return (
    <svg
      className={`spool spool-${state.toLowerCase()}${notAllowed ? ' not-allowed' : ''}`}
      viewBox={`0 0 ${SIZE} ${SIZE}`}
      role="img"
      aria-label={`spool-${display_index}`}
    >
      <circle cx={center} cy={center} r={OUTER} fill="var(--ams-overlay-bg)" />
      <circle cx={center} cy={center} r={OUTER - 0.5} fill="none" stroke="var(--ams-border)" strokeWidth={1} />

      {isFilled && (
        <MaterialDonut
          center={center}
          colors={colors}
          colorType={color_type}
          pct={remain}
          idPrefix={materialId}
        />
      )}

      {isError && (
        <>
          <circle cx={center} cy={center} r={RING} fill="var(--ams-danger)" />
          <circle cx={center} cy={center} r={HOLE} fill="var(--ams-card-text-bg)" />
          <g stroke="var(--ams-text-on-brand)" strokeWidth={2.5} strokeLinecap="round">
            <line x1={center - 5} y1={center - 5} x2={center + 5} y2={center + 5} />
            <line x1={center - 5} y1={center + 5} x2={center + 5} y2={center - 5} />
          </g>
        </>
      )}

      {showFilamentTag && (
        <image href={filamentTagUrl} x={0} y={0} width={SIZE} height={SIZE} preserveAspectRatio="none" />
      )}

      {state === 'Reading' && <ReadingArc center={center} />}

      {state === 'Empty' && (
        <text
          x={center + 0.5}
          y={center + 1}
          fontFamily="HarmonyOS Sans SC, system-ui, sans-serif"
          fontSize={14}
          fill="var(--ams-text-disabled)"
          textAnchor="middle"
          dominantBaseline="central"
        >
          /
        </text>
      )}

      {state === 'Unknown' && (
        <image href={unknownSpoolUrl} x={0} y={0} width={SIZE} height={SIZE} preserveAspectRatio="none" />
      )}

      {notAllowed && <NotAllowedOverlay center={center} />}

    </svg>
  );
}

// Universal "🚫" mark used by the batch-mode card when the slot is not
// eligible for the current mode (mirrors Figma 965:17996 not-allowed).
// Drawn at the bottom-right quadrant of the spool so the colour ring stays
// visible underneath.
function NotAllowedOverlay({ center }: { center: number }) {
  const cx = center + 14;
  const cy = center + 14;
  const r = 9;
  return (
    <g className="spool-not-allowed">
      <circle cx={cx} cy={cy} r={r} fill="var(--ams-card-text-bg)" stroke="var(--ams-text-muted)" strokeWidth={1.2} />
      <line
        x1={cx - r * 0.55}
        y1={cy - r * 0.55}
        x2={cx + r * 0.55}
        y2={cy + r * 0.55}
        stroke="var(--ams-text-muted)"
        strokeWidth={1.6}
        strokeLinecap="round"
      />
    </g>
  );
}

function MaterialDonut({
  center,
  colors,
  colorType,
  pct,
  idPrefix,
}: {
  center: number;
  colors: string[];
  colorType?: 'single' | 'multi' | 'gradient';
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
            r={RING}
            fx={center}
            fy={center}
            gradientUnits="userSpaceOnUse"
          >
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

      <circle cx={center} cy={center} r={RING} fill="var(--ams-overlay-bg)" />
      <g mask={`url(#${maskId})`}>
        {transparent && <circle cx={center} cy={center} r={RING} fill={`url(#${checkerId})`} />}
        <MaterialFill center={center} colors={colors} colorType={colorType} gradientId={gradientId} />
      </g>
      <circle cx={center} cy={center} r={RING} fill="none" stroke="var(--color-fm-chip-ring)" strokeWidth={1} />
      <circle cx={center} cy={center} r={HOLE} fill="var(--ams-card-text-bg)" />
      <circle cx={center} cy={center} r={HOLE} fill="none" stroke="var(--ams-border)" strokeWidth={1} />
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
  colorType?: 'single' | 'multi' | 'gradient';
  gradientId: string;
}) {
  if (colorType === 'gradient') {
    return <circle cx={center} cy={center} r={RING} fill={`url(#${gradientId})`} />;
  }
  if (colors.length > 1) {
    return <ConcentricRings center={center} colors={colors} />;
  }
  return (
    <circle cx={center} cy={center} r={RING} fill={opaqueColor(colors[0])} fillOpacity={colorOpacity(colors[0])} />
  );
}

function ConcentricRings({ center, colors }: { center: number; colors: string[] }) {
  const band = (RING - HOLE) / colors.length;
  return (
    <>
      {colors.map((color, index) => {
        const radius = RING - band * index;
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
  const ringRadius = (RING + HOLE) / 2;
  const stroke = RING - HOLE;
  const circumference = 2 * Math.PI * ringRadius;
  const filled = (pct / 100) * circumference;
  const gap = circumference - filled;
  return (
    <mask id={id} maskUnits="userSpaceOnUse">
      <rect x={0} y={0} width={SIZE} height={SIZE} fill="black" />
      {pct >= 100 ? (
        <circle cx={center} cy={center} r={RING} fill="white" />
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

function CheckerPattern({ id }: { id: string }) {
  const tile = 6;
  return (
    <pattern id={id} width={tile} height={tile} patternUnits="userSpaceOnUse">
      <rect width={tile} height={tile} fill="#dfeee4" />
      <rect width={tile / 2} height={tile / 2} fill="#9ed3ae" />
      <rect x={tile / 2} y={tile / 2} width={tile / 2} height={tile / 2} fill="#9ed3ae" />
    </pattern>
  );
}

function ReadingArc({ center }: { center: number }) {
  const r = RING - 3;
  const dashLen = 2 * Math.PI * r * 0.28;
  return (
    <g className="spool-loading-rotor" style={{ transformOrigin: `${center}px ${center}px` }}>
      <circle
        className="spool-loading-mark"
        cx={center}
        cy={center}
        r={r}
        fill="none"
        stroke="var(--ams-brand)"
        strokeWidth={3}
        strokeLinecap="round"
        strokeDasharray={`${dashLen} 999`}
      />
    </g>
  );
}

function normalizeColors(colors: string[]) {
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
  if (color === 'transparent') return '#9ed3ae';
  if (/^#[0-9a-fA-F]{8}$/.test(color)) return color.slice(0, 7);
  return color;
}

function colorOpacity(color: string) {
  const alpha = alphaFromHex(color);
  return alpha === null ? 1 : Math.max(0.2, alpha / 255);
}

function gradientStopOffset(index: number, total: number) {
  if (total <= 1) return 100;
  return (index / (total - 1)) * 100;
}

function alphaFromHex(color: string) {
  if (!/^#[0-9a-fA-F]{8}$/.test(color)) return null;
  return Number.parseInt(color.slice(7, 9), 16);
}
