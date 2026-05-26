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
  const { state, color } = slot.filament_card;
  const display_index = slot.display_index;
  const cardColors = slot.filament_card.colors;
  const colors = cardColors && cardColors.length > 1 ? cardColors : [color];
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

  // Show a partial ring only for single-color spools with known remaining percent.
  const remain = typeof slot.filament_card.remain === 'number' ? slot.filament_card.remain : 100;
  const showPartialRing = isFilled && colors.length === 1 && remain > 0 && remain < 100;

  return (
    <svg
      className={`spool spool-${state.toLowerCase()}${notAllowed ? ' not-allowed' : ''}`}
      viewBox={`0 0 ${SIZE} ${SIZE}`}
      role="img"
      aria-label={`spool-${display_index}`}
    >
      <circle cx={center} cy={center} r={OUTER} fill="var(--ams-overlay-bg)" />
      <circle cx={center} cy={center} r={OUTER - 0.5} fill="none" stroke="var(--ams-border)" strokeWidth={1} />

      {isFilled && !showPartialRing && (
        <FilledDonut center={center} colors={colors} />
      )}

      {showPartialRing && (
        <PartialDonut center={center} color={colors[0]} pct={remain} />
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

function FilledDonut({ center, colors }: { center: number; colors: string[] }) {
  if (colors.length === 1) {
    return (
      <>
        <circle cx={center} cy={center} r={RING} fill={colors[0]} />
        <circle cx={center} cy={center} r={RING} fill="none" stroke="var(--color-fm-chip-ring)" strokeWidth={1} />
        <circle cx={center} cy={center} r={HOLE} fill="var(--ams-card-text-bg)" />
        <circle cx={center} cy={center} r={HOLE} fill="none" stroke="var(--ams-border)" strokeWidth={1} />
      </>
    );
  }

  const slices = colors.map((_, i) => {
    const start = (i / colors.length) * Math.PI * 2 - Math.PI / 2;
    const end = ((i + 1) / colors.length) * Math.PI * 2 - Math.PI / 2;
    return wedgePath(center, center, RING, HOLE, start, end);
  });

  return (
    <>
      {slices.map((d, i) => (
        <path key={i} d={d} fill={colors[i]} />
      ))}
      <circle cx={center} cy={center} r={RING} fill="none" stroke="var(--color-fm-chip-ring)" strokeWidth={1} />
      <circle cx={center} cy={center} r={HOLE} fill="var(--ams-card-text-bg)" />
      <circle cx={center} cy={center} r={HOLE} fill="none" stroke="var(--ams-border)" strokeWidth={1} />
    </>
  );
}

// Partial fill arc from 12 o'clock clockwise.
function PartialDonut({ center, color, pct }: { center: number; color: string; pct: number }) {
  const ringRadius = (RING + HOLE) / 2;
  const stroke = RING - HOLE;
  const circumference = 2 * Math.PI * ringRadius;
  const filled = (pct / 100) * circumference;
  const gap = circumference - filled;
  return (
    <g transform={`rotate(-90 ${center} ${center})`}>
      <circle
        cx={center}
        cy={center}
        r={ringRadius}
        fill="none"
        stroke="var(--ams-overlay-bg)"
        strokeWidth={stroke}
      />
      <circle
        cx={center}
        cy={center}
        r={ringRadius}
        fill="none"
        stroke={color}
        strokeWidth={stroke}
        strokeDasharray={`${filled} ${gap}`}
      />
      <circle cx={center} cy={center} r={HOLE} fill="var(--ams-card-text-bg)" />
      <circle cx={center} cy={center} r={HOLE} fill="none" stroke="var(--ams-border)" strokeWidth={1} />
    </g>
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
