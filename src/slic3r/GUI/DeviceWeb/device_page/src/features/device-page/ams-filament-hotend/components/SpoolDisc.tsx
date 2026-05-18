import type { AmsFilamentHotendSlotViewModel } from '../types';
import filamentTagUrl from '../assets/spool_filament_tag_figma.svg';
import unknownSpoolUrl from '../assets/spool_unknown_figma.svg';

const SIZE = 52;
const OUTER = 26;
const RING = 23;
const HOLE = 9;

export function SpoolDisc({ slot }: { slot: AmsFilamentHotendSlotViewModel }) {
  const { card_state: state, color, display_index } = slot;
  const colors = slot.colors && slot.colors.length > 1 ? slot.colors : [color];
  const isFilled =
    state === 'Loading' ||
    state === 'Unloading' ||
    state === 'ReadyToLoad' ||
    state === 'ReadyToUse';
  const isError = state === 'Error';
  const showFilamentTag = state !== 'Empty' && state !== 'Unknown';
  const center = SIZE / 2;

  return (
    <svg
      className={`spool spool-${state.toLowerCase()}`}
      viewBox={`0 0 ${SIZE} ${SIZE}`}
      role="img"
      aria-label={`spool-${display_index}`}
    >
      <circle cx={center} cy={center} r={OUTER} fill="#e6e6e6" />
      <circle cx={center} cy={center} r={OUTER - 0.5} fill="none" stroke="#d4d4d4" strokeWidth={1} />

      {isFilled && (
        <FilledDonut center={center} colors={colors} />
      )}

      {isError && (
        <>
          <circle cx={center} cy={center} r={RING} fill="#D95047" />
          <circle cx={center} cy={center} r={HOLE} fill="#ffffff" />
          <g stroke="#fff" strokeWidth={2.5} strokeLinecap="round">
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
          fill="#c2c2c2"
          textAnchor="middle"
          dominantBaseline="central"
        >
          /
        </text>
      )}

      {state === 'Unknown' && (
        <image href={unknownSpoolUrl} x={0} y={0} width={SIZE} height={SIZE} preserveAspectRatio="none" />
      )}

      {(state === 'Loading' || state === 'Unloading') && typeof slot.progress_pct === 'number' && (
        <ProgressArc
          center={center}
          radius={(RING + HOLE) / 2}
          percent={slot.progress_pct}
          reversed={state === 'Unloading'}
        />
      )}
    </svg>
  );
}

function FilledDonut({ center, colors }: { center: number; colors: string[] }) {
  if (colors.length === 1) {
    return (
      <>
        <circle cx={center} cy={center} r={RING} fill={colors[0]} />
        <circle cx={center} cy={center} r={RING} fill="none" stroke="rgba(0,0,0,0.06)" strokeWidth={1} />
        <circle cx={center} cy={center} r={HOLE} fill="#ffffff" />
        <circle cx={center} cy={center} r={HOLE} fill="none" stroke="#e6e6e6" strokeWidth={1} />
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
      <circle cx={center} cy={center} r={RING} fill="none" stroke="rgba(0,0,0,0.06)" strokeWidth={1} />
      <circle cx={center} cy={center} r={HOLE} fill="#ffffff" />
      <circle cx={center} cy={center} r={HOLE} fill="none" stroke="#e6e6e6" strokeWidth={1} />
    </>
  );
}

function ReadingArc({ center }: { center: number }) {
  const r = RING - 3;
  const dashLen = 2 * Math.PI * r * 0.28;
  return (
    <circle
      cx={center}
      cy={center}
      r={r}
      fill="none"
      stroke="#00AE42"
      strokeWidth={3}
      strokeLinecap="round"
      strokeDasharray={`${dashLen} 999`}
      transform={`rotate(-90 ${center} ${center})`}
    />
  );
}

function ProgressArc({
  center,
  radius,
  percent,
  reversed,
}: {
  center: number;
  radius: number;
  percent: number;
  reversed?: boolean;
}) {
  const clamped = Math.min(Math.max(percent, 0), 100);
  const total = 2 * Math.PI * radius;
  const filled = (clamped / 100) * total;
  return (
    <circle
      cx={center}
      cy={center}
      r={radius}
      fill="none"
      stroke="#00AE42"
      strokeWidth={4}
      strokeLinecap="round"
      strokeDasharray={`${filled} ${total - filled}`}
      transform={`rotate(${reversed ? 90 : -90} ${center} ${center})`}
    />
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
