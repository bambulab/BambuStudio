import type { CSSProperties } from 'react';
import type { AmsFilamentHotendEnvUnit, AmsFilamentHotendSlotViewModel } from '../types';
import { HumidityIcon, TemperatureIcon } from './AmsEnvIcons';

// Width of the readout chip. It is right-aligned against the left edge of the
// first column of its zone, filling the half-pitch gap the interleaved tiers
// leave there.
const CHIP_WIDTH = 60;

export function AmbientInfoDecoration({
  startIndex,
  humidity,
  temp,
}: {
  startIndex: number;
  humidity: number;
  temp: number | null;
}) {
  // Top columns sit at odd multiples of `--slot-pitch` (see Stage.tsx).
  const left = `calc(${1 + 2 * startIndex} * var(--slot-pitch) - ${CHIP_WIDTH}px)`;

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

// Anchor each AMS unit's readout to its leftmost rendered column. The readout
// describes the whole unit, so one entry produces at most one chip no matter
// how many columns the unit spans.
export function findAmbientAnchors(
  slots: AmsFilamentHotendSlotViewModel[],
  units: AmsFilamentHotendEnvUnit[],
): Array<{ start: number; humidity: number; temp: number | null }> {
  if (!units?.length) return [];
  const byStart = new Map<number, { start: number; humidity: number; temp: number | null }>();
  const toIds = (v?: Array<number | string>) => (v ?? []).map(Number).filter((n) => !Number.isNaN(n));

  for (const unit of units) {
    // Rack positions first: an external spool overrides `slot.ams_id` with its
    // virtual tray id, which would otherwise push the anchor past the first
    // top-tier column of this AMS unit.
    const addrIds = toIds(unit.zone_addrs);
    let start = -1;
    if (addrIds.length > 0) {
      const addrSet = new Set(addrIds);
      start = slots.findIndex((slot) => addrSet.has(Number(slot.addr_id)));
    } else if (unit.ams_id) {
      start = slots.findIndex((slot) => slot.ams_id === unit.ams_id);
    }
    if (start < 0 || byStart.has(start)) continue;

    const humidity = typeof unit.humidity_raw === 'number' && unit.humidity_raw >= 0 ? unit.humidity_raw : -1;
    const temp = typeof unit.temp === 'number' ? unit.temp : null;
    byStart.set(start, { start, humidity, temp });
  }

  return [...byStart.values()];
}
