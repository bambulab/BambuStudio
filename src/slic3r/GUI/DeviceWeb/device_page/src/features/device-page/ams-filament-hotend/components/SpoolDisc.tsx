import { useId } from 'react';
import type { AmsFilamentHotendSlotViewModel } from '../types';
import filamentTagUrl from '../assets/spool_filament_tag_figma.svg';
import unknownSpoolUrl from '../assets/spool_unknown_figma.svg';
import {
  MaterialDonut,
  normalizeColors,
  SPOOL_SIZE as SIZE,
  SPOOL_OUTER as OUTER,
  SPOOL_RING as RING,
  SPOOL_HOLE as HOLE,
} from './SpoolGraphic';

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

