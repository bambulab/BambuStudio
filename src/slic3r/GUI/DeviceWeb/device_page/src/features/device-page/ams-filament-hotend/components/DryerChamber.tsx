import type { CSSProperties } from 'react';
import { useTranslation } from 'react-i18next';
import type { AmsFilamentHotendDryerZone, AmsFilamentHotendSlotViewModel } from '../types';
import { DryingIcon, HumidityIcon } from './AmsEnvIcons';

const COL = 80;

export function DryerChamberDecoration({
  startIndex,
  endIndex,
  totalSlots,
  humidity,
  status,
  isDrying,
  amsId,
  onDry,
}: {
  startIndex: number;
  endIndex: number;
  totalSlots: number;
  humidity: number;
  status: string;
  isDrying?: boolean;
  amsId: string;
  onDry?: (amsId: string) => void;
}) {
  if (totalSlots !== 4) return null;
  // Top columns sit at odd multiples of `--slot-pitch` (see Stage.tsx).
  const startStr = `calc(${1 + 2 * startIndex} * var(--slot-pitch) - 4px)`;
  const endStr = `calc(${1 + 2 * endIndex} * var(--slot-pitch) + ${COL + 4}px)`;

  return (
    <div
      className={`dryer-chamber-bg${isDrying ? ' is-drying' : ''}`}
      data-testid="ams-hotend-dryer-chamber"
      style={{
        ['--chamber-left' as never]: startStr,
        ['--chamber-right' as never]: endStr,
      } as CSSProperties}
    >
      <DryerInfo humidity={humidity} status={status} amsId={amsId} onDry={onDry} />
    </div>
  );
}

export function DryerInfo({
  humidity,
  status,
  amsId,
  onDry,
}: {
  humidity: number;
  status: string;
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
        <DryingIcon />
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

// Non-idle dryer states are shown as active.
function isDryingStatus(status?: string) {
  return !!status && status !== 'Idle' && status !== 'Off';
}

// Split each dryer zone into contiguous rendered slot runs.
export function findDryerRuns(
  slots: AmsFilamentHotendSlotViewModel[],
  zones: AmsFilamentHotendDryerZone[],
): Array<{
  start: number;
  end: number;
  humidity: number;
  status: string;
  isDrying: boolean;
  amsId: string;
}> {
  if (!zones?.length) return [];
  const runs: Array<{ start: number; end: number; humidity: number; status: string; isDrying: boolean; amsId: string }> = [];

  for (const zone of zones) {
    const ids = (zone.zone_slots ?? []).map((v) => Number(v)).filter((n) => !Number.isNaN(n));
    if (!ids.length) continue;
    const idSet = new Set(ids);

    const matchedCols: number[] = [];
    slots.forEach((slot, idx) => {
      if (zone.ams_id && slot.ams_id && zone.ams_id !== slot.ams_id) return;
      if (idSet.has(Number(slot.slot_id))) matchedCols.push(idx);
    });
    if (!matchedCols.length) continue;

    const humidity = typeof zone.humidity_raw === 'number' && zone.humidity_raw >= 0 ? zone.humidity_raw : -1;
    const status = zone.dry_status_text || '';
    const isDrying = isDryingStatus(zone.dry_status) || (typeof zone.dry_time === 'number' && zone.dry_time > 0);

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

  return runs;
}
