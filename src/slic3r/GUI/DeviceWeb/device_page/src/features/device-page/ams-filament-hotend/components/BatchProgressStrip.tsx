import { Fragment } from 'react';
import { useTranslation } from 'react-i18next';
import type { FilamentChangeBatchReservedItem } from '../types';

// Horizontal step strip that sits between the slot stage and the per-step
// progress panel during a multi-slot filament change run. Mirrors Figma
// 1670:80943.
//
// Layout: pips and connectors are siblings of the strip flex container
// (no per-cell wrapper). The connectors carry `flex: 1 1 0` with a
// 5px..20px clamp so they auto-fill when the panel is wide and shrink
// gracefully when many slots are queued. `justify-content: center` then
// centers the whole group inside the panel — when connectors are at their
// max width and the total content is narrower than the panel, the leftover
// horizontal space distributes equally on both sides.

export function BatchProgressStrip({
  items,
}: {
  items: FilamentChangeBatchReservedItem[];
}) {
  const { t } = useTranslation();
  if (!items.length) return null;
  return (
    <div
      className="batch-progress-strip"
      role="progressbar"
      aria-label={t('Batch progress')}
      data-testid="ams-batch-progress-strip"
    >
      {items.map((item, index) => {
        const isLast = index === items.length - 1;
        return (
          <Fragment key={`${index}-${item.display_index}`}>
            <span
              className={`batch-progress-pip state-${item.state}`}
              title={item.reason || t('Slot {{n}}', { n: item.display_index })}
              data-testid={`ams-batch-progress-pip-${item.display_index}`}
            >
              {item.state === 'failed' ? '×' : item.display_index}
            </span>
            {!isLast && <span className="batch-progress-connector" aria-hidden="true" />}
          </Fragment>
        );
      })}
    </div>
  );
}
