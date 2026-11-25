import type { CSSProperties } from 'react';
import { useTranslation } from 'react-i18next';
import type { AmsFilamentHotendDryerZone, AmsFilamentHotendSlotViewModel } from '../types';
import { DryingActiveIcon, DryingIcon, HumidityIcon } from './AmsEnvIcons';

const COL = 80;

// Gap between the chamber background and the readout trailing it.
const INFO_GAP = 6;

export type DryerInfoSide = 'left' | 'right';

export function DryerChamberDecoration({
  startIndex,
  endIndex,
  totalSlots,
  infoSide,
  humidity,
  status,
  isDrying,
  amsId,
  onDry,
}: {
  startIndex: number;
  endIndex: number;
  totalSlots: number;
  infoSide: DryerInfoSide;
  humidity: number;
  status: string;
  isDrying?: boolean;
  amsId: string;
  onDry?: (amsId: string) => void;
}) {
  // Top columns sit at odd multiples of `--slot-pitch` (see Stage.tsx).
  const startStr = `calc(${1 + 2 * startIndex} * var(--slot-pitch) - 4px)`;
  const endStr = `calc(${1 + 2 * endIndex} * var(--slot-pitch) + ${COL + 4}px)`;
  // Hard right boundary for the whole decoration: the next top column when the
  // run has one, otherwise the end of the row. Everything past it belongs to
  // an opaque card, so the readout must shrink rather than slide underneath.
  const capStr = endIndex + 1 < totalSlots
    ? `calc(${1 + 2 * (endIndex + 1)} * var(--slot-pitch) - ${INFO_GAP}px)`
    : '100%';

  return (
    <div
      className={`dryer-chamber-bg info-${infoSide}${isDrying ? ' is-drying' : ''}`}
      data-testid="ams-hotend-dryer-chamber"
      style={{
        ['--chamber-left' as never]: startStr,
        ['--chamber-right' as never]: endStr,
        ['--chamber-cap' as never]: capStr,
      } as CSSProperties}
    >
      {/* A left-side readout leads the chamber; a right-side one trails the zone spacer. */}
      {infoSide === 'right' && <div className="dryer-chamber-zone" aria-hidden="true" />}
      <DryerInfo humidity={humidity} status={status} isDrying={isDrying} amsId={amsId} onDry={onDry} />
    </div>
  );
}

export function DryerInfo({
  humidity,
  status,
  isDrying,
  amsId,
  onDry,
}: {
  humidity: number;
  status: string;
  isDrying?: boolean;
  amsId: string;
  onDry?: (amsId: string) => void;
}) {
  const { t } = useTranslation();
  const statusText = status || 'Idle';
  return (
    <div className="dryer-info" aria-label="dryer humidity and status">
      <div className="dryer-info-row">
        <HumidityIcon />
        <span className="dryer-value">
          {humidity >= 0 ? humidity : '--'}
          <small>%</small>
        </span>
      </div>
      <div className="dryer-info-row">
        {isDrying ? <DryingActiveIcon /> : <DryingIcon />}
        {/* Long localized statuses are ellipsized; keep the full text on hover. */}
        <span className="dryer-text" title={statusText}>{statusText}</span>
      </div>
      {onDry && (
        // A dryer chamber always supports drying, so its readout opens the
        // drying dialog for the owning AMS unit.
        <button
          type="button"
          className="dryer-info-hit"
          data-testid="ams-hotend-dryer-dry"
          title={t('Dry')}
          aria-label={t('Dry')}
          onClick={() => onDry(amsId)}
        />
      )}
    </div>
  );
}

// Matches DevAms::AmsIsDrying(), which the native AMS widget uses for its heating sun.
const ACTIVE_DRY_STATUSES = new Set(['Checking', 'Drying', 'Error', 'Heat fault']);

export interface DryerRun {
  start: number;
  end: number;
  infoSide: DryerInfoSide;
  humidity: number;
  status: string;
  isDrying: boolean;
  amsId: string;
}

// Split each dryer zone into contiguous rendered slot runs; the readout goes
// to the stage edge the run touches.
export function findDryerRuns(
  slots: AmsFilamentHotendSlotViewModel[],
  zones: AmsFilamentHotendDryerZone[],
): DryerRun[] {
  if (!zones?.length) return [];
  const runs: Array<Omit<DryerRun, 'infoSide'>> = [];

  for (const zone of zones) {
    const toIds = (v?: Array<number | string>) => (v ?? []).map(Number).filter((n) => !Number.isNaN(n));
    // Rack positions first: a slot reports whatever is feeding it, so an
    // external spool in a dryer bay would drop that column out of its zone.
    const addrIds = toIds(zone.zone_addrs);
    const byAddr = addrIds.length > 0;
    const idSet = new Set(byAddr ? addrIds : toIds(zone.zone_slots));
    if (!idSet.size) continue;

    const matchedCols: number[] = [];
    slots.forEach((slot, idx) => {
      if (byAddr) {
        if (idSet.has(Number(slot.addr_id))) matchedCols.push(idx);
        return;
      }
      if (zone.ams_id && slot.ams_id && zone.ams_id !== slot.ams_id) return;
      if (idSet.has(Number(slot.slot_id))) matchedCols.push(idx);
    });
    if (!matchedCols.length) continue;

    const humidity = typeof zone.humidity_raw === 'number' && zone.humidity_raw >= 0 ? zone.humidity_raw : -1;
    const status = zone.dry_status_text || '';
    const isDrying = ACTIVE_DRY_STATUSES.has(zone.dry_status ?? '')
      || (!zone.dry_status && typeof zone.dry_time === 'number' && zone.dry_time > 0);

    let runStart = matchedCols[0];
    let prev = matchedCols[0];
    for (let i = 1; i <= matchedCols.length; i++) {
      const cur = matchedCols[i];
      if (cur === prev + 1) {
        prev = cur;
        continue;
      }
      runs.push({ start: runStart, end: prev, humidity, status, isDrying, amsId: zone.ams_id ?? '' });
      if (cur === undefined) break;
      runStart = cur;
      prev = cur;
    }
  }

  const lastCol = slots.length - 1;
  return runs.map((run) => ({
    ...run,
    infoSide: run.start === 0 && run.end < lastCol ? 'left' : 'right',
  }));
}
