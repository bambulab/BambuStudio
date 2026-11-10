import type { CSSProperties } from 'react';
import type { AmsFilamentHotendEnvUnit, AmsFilamentHotendSlotViewModel } from '../types';
import { HumidityIcon, TemperatureIcon } from './AmsEnvIcons';

// Fits the half-pitch gap next to a top column
const CHIP_WIDTH = 60;
const COL = 80;

export type AmbientInfoSide = 'left' | 'right';

export interface AmbientAnchor {
  col: number;
  side: AmbientInfoSide;
  humidity: number;
  temp: number | null;
}

export function AmbientInfoDecoration({
  col,
  side,
  humidity,
  temp,
}: {
  col: number;
  side: AmbientInfoSide;
  humidity: number;
  temp: number | null;
}) {
  // Top columns sit at odd multiples of `--slot-pitch` (see Stage.tsx).
  const left = side === 'left'
    ? `calc(${1 + 2 * col} * var(--slot-pitch) - ${CHIP_WIDTH}px)`
    : `calc(${1 + 2 * col} * var(--slot-pitch) + ${COL}px)`;

  return (
    <div
      className="ambient-info"
      data-testid="ams-hotend-ambient-info"
      aria-label="ams humidity and temperature"
      style={{ ['--ambient-left' as never]: left } as CSSProperties}
    >
      <div className="ambient-info-row">
        <HumidityIcon />
        <span className="ambient-value">
          {humidity >= 0 ? humidity : '--'}
          <small>%</small>
        </span>
      </div>
      <div className="ambient-info-row">
        <TemperatureIcon />
        <span className="ambient-value">
          {temp === null ? '--' : Math.round(temp)}
          <small>℃</small>
        </span>
      </div>
    </div>
  );
}

// Left of the zone, or right of it when a dryer chamber is on the left
export function findAmbientAnchors(
  slots: AmsFilamentHotendSlotViewModel[],
  units: AmsFilamentHotendEnvUnit[],
  dryerRuns: ReadonlyArray<{ start: number; end: number }> = [],
): AmbientAnchor[] {
  if (!units?.length) return [];
  const byKey = new Map<string, AmbientAnchor>();
  const toIds = (v?: Array<number | string>) => (v ?? []).map(Number).filter((n) => !Number.isNaN(n));

  for (const unit of units) {
    // Rack positions first: an external spool overrides `slot.ams_id` with its
    // virtual tray id, which would otherwise push the anchor past the first
    // top-tier column of this AMS unit.
    const addrIds = toIds(unit.zone_addrs);
    const addrSet = new Set(addrIds);
    const cols: number[] = [];
    slots.forEach((slot, idx) => {
      if (addrIds.length > 0 ? addrSet.has(Number(slot.addr_id)) : (!!unit.ams_id && slot.ams_id === unit.ams_id)) {
        cols.push(idx);
      }
    });
    if (!cols.length) continue;

    const first = cols[0];
    const last = cols[cols.length - 1];
    const dryerOnLeft = dryerRuns.some((run) => run.end < first);
    const anchor: AmbientAnchor = {
      col: dryerOnLeft ? last : first,
      side: dryerOnLeft ? 'right' : 'left',
      humidity: typeof unit.humidity_raw === 'number' && unit.humidity_raw >= 0 ? unit.humidity_raw : -1,
      temp: typeof unit.temp === 'number' ? unit.temp : null,
    };
    const key = `${anchor.side}-${anchor.col}`;
    if (!byKey.has(key)) byKey.set(key, anchor);
  }

  return [...byKey.values()];
}
