import type { CSSProperties } from 'react';
import type { AmsFilamentHotendSlotViewModel } from '../types';

const COL = 80;

export function DryerChamberDecoration({
  startIndex,
  endIndex,
  totalSlots,
  humidity,
  status,
  isDrying,
}: {
  startIndex: number;
  endIndex: number;
  totalSlots: number;
  humidity: number;
  status: string;
  isDrying?: boolean;
}) {
  if (totalSlots !== 4) return null;
  const startStr = `calc((100% - 80px) / 8 + ${startIndex} * (100% - 80px) / 4 - 4px)`;
  const endStr = `calc((100% - 80px) / 8 + ${endIndex} * (100% - 80px) / 4 + ${COL}px + 4px)`;

  return (
    <div
      className={`dryer-chamber-bg${isDrying ? ' is-drying' : ''}`}
      data-testid="ams-hotend-dryer-chamber"
      style={{
        ['--chamber-left' as never]: startStr,
        ['--chamber-right' as never]: endStr,
      } as CSSProperties}
    >
      <DryerInfo humidity={humidity} status={status} />
    </div>
  );
}

export function DryerInfo({ humidity, status }: { humidity: number; status: string }) {
  return (
    <div className="dryer-info" aria-label="dryer humidity and status">
      <div className="dryer-info-row">
        <DropletIcon />
        <span className="dryer-value">
          {humidity >= 0 ? humidity : '--'}
          <small>%</small>
        </span>
      </div>
      <div className="dryer-info-row">
        <DryerIcon />
        <span className="dryer-text">{status || 'Idle'}</span>
      </div>
    </div>
  );
}

function DropletIcon() {
  return (
    <svg width={14} height={14} viewBox="0 0 16 16" aria-hidden="true">
      <path
        d="M8 2 C5 6 3 8.5 3 11 a5 5 0 1 0 10 0 c0 -2.5 -2 -5 -5 -9 z"
        fill="none"
        stroke="#5c5c5c"
        strokeWidth={1.2}
      />
    </svg>
  );
}

function DryerIcon() {
  return (
    <svg width={14} height={14} viewBox="0 0 16 16" aria-hidden="true">
      <circle cx={8} cy={8} r={6.2} fill="none" stroke="#5c5c5c" strokeWidth={1.2} />
      <path
        d="M5 9 q1.5 -2 3 0 t3 0"
        fill="none"
        stroke="#5c5c5c"
        strokeWidth={1.2}
        strokeLinecap="round"
      />
    </svg>
  );
}

export function findDryerRuns(slots: AmsFilamentHotendSlotViewModel[]): Array<{
  start: number;
  end: number;
  humidity: number;
  status: string;
  isDrying: boolean;
}> {
  const runs: ReturnType<typeof findDryerRuns> = [];
  for (let i = 0; i < slots.length; i++) {
    if (!slots[i].in_dryer) continue;
    let j = i;
    while (j + 1 < slots.length && slots[j + 1].in_dryer) j++;
    // Use the first concrete dryer signal across the grouped slots.
    let humidity = -1;
    let status = '';
    let isDrying = false;
    for (let k = i; k <= j; k++) {
      const s = slots[k];
      if (humidity < 0 && typeof s.dryer_humidity_pct === 'number' && s.dryer_humidity_pct >= 0) {
        humidity = s.dryer_humidity_pct;
      }
      if (!status && s.dryer_status_text) status = s.dryer_status_text;
      if (s.is_drying) isDrying = true;
    }
    runs.push({ start: i, end: j, humidity, status, isDrying });
    i = j;
  }
  return runs;
}
