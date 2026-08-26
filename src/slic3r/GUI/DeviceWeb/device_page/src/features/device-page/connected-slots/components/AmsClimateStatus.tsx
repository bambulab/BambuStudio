import type { ConnectedSlotsUnitInfo } from '../types';

function DropletIcon() {
  return (
    <svg width={14} height={14} viewBox="0 0 16 16" aria-hidden="true">
      <path
        d="M8 2 C5 6 3 8.5 3 11 a5 5 0 1 0 10 0 c0 -2.5 -2 -5 -5 -9 z"
        fill="none"
        stroke="currentColor"
        strokeWidth={1.2}
      />
    </svg>
  );
}

function DryerIcon() {
  return (
    <svg width={14} height={14} viewBox="0 0 16 16" aria-hidden="true">
      <circle cx={8} cy={8} r={6.2} fill="none" stroke="currentColor" strokeWidth={1.2} />
      <path
        d="M5 9 q1.5 -2 3 0 t3 0"
        fill="none"
        stroke="currentColor"
        strokeWidth={1.2}
        strokeLinecap="round"
      />
    </svg>
  );
}

function isActiveDryStatus(status?: string) {
  return !!status && status !== 'Idle' && status !== 'Off';
}

/** Humidity % and drying status pill on an AMS unit board. */
export function AmsClimateStatus({ unit }: { unit: ConnectedSlotsUnitInfo }) {
  const humidity = typeof unit.humidity_raw === 'number' && unit.humidity_raw >= 0
    ? unit.humidity_raw
    : null;
  const dry = isActiveDryStatus(unit.dry_status) ? unit.dry_status : '';
  if (humidity === null && !dry) return null;
  return (
    <span className="cs-ams-climate-status" data-testid="ams-climate-status">
      {humidity !== null && (
        <span className="cs-ams-climate-status__item">
          <DropletIcon />
          <span>{humidity} %</span>
        </span>
      )}
      {humidity !== null && dry && <i className="cs-ams-climate-status__split" />}
      {dry && (
        <span className="cs-ams-climate-status__item">
          <DryerIcon />
          <span>{dry}</span>
        </span>
      )}
    </span>
  );
}
