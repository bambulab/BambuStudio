import { useEffect, useRef, useState } from 'react';
import type { RefObject } from 'react';
import { useAmsMappingHotendBridge } from './useAmsMappingHotendBridge';
import { useSendToCpp } from '../../../hooks/Bridge';
import { SlotCard } from './components/SlotCard';
import { SubAmsPage } from './components/SubAmsPage';
import { Tips } from './components/Tips';
import { resolveHostContentSize } from './hostContentSize';
import type { AmsMappingHotendSlotViewModel } from './types';
import './ams-mapping-hotend.css';

const HOST_LAYOUT_MODULE = 'device_host';

// Observe the real rendered size of the mapping page and report it to the wx
// host via device_host/layout/content_size so the popup can Fit itself instead
// of relying on the initial SetMinSize.
function useReportHostContentSize(rootRef: RefObject<HTMLElement | null>) {
  const send = useSendToCpp();
  const seqRef = useRef(1_000_000);

  useEffect(() => {
    const root = rootRef.current;
    if (!root) return;

    let rafId = 0;
    let lastSizeKey = '';

    const report = () => {
      rafId = 0;
      const { width, height } = resolveHostContentSize(root);
      if (width <= 0 || height <= 0) return;

      const sizeKey = `${width}x${height}`;
      if (sizeKey === lastSizeKey) return;
      lastSizeKey = sizeKey;

      send(
        {
          version: '1.0',
          type: 'request',
          seq: seqRef.current++,
          ts: Date.now(),
        },
        {
          module: HOST_LAYOUT_MODULE,
          submod: 'layout',
          action: 'content_size',
          payload: { width, height },
        },
      );
    };

    const scheduleReport = () => {
      if (rafId) return;
      rafId = window.requestAnimationFrame(report);
    };

    scheduleReport();
    window.addEventListener('resize', scheduleReport);

    const observer = typeof ResizeObserver === 'function' ? new ResizeObserver(scheduleReport) : null;
    observer?.observe(root);

    return () => {
      if (rafId) window.cancelAnimationFrame(rafId);
      observer?.disconnect();
      window.removeEventListener('resize', scheduleReport);
    };
  }, [rootRef, send]);
}

export function AmsMappingHotendPage() {
  const pageRef = useRef<HTMLDivElement>(null);
  const { state, error, selectSlot } = useAmsMappingHotendBridge();
  const [subAmsSlot, setSubAmsSlot] = useState<AmsMappingHotendSlotViewModel | null>(null);
  useReportHostContentSize(pageRef);

  const topSlots = state.hotend_slots.filter((s) => s.is_top_tier);
  const bottomSlots = state.hotend_slots.filter((s) => !s.is_top_tier);
  const rows = [topSlots, bottomSlots].filter((r) => r.length > 0);

  if (subAmsSlot) {
    return (
      <div ref={pageRef} className="ams-map-hotend-page" data-testid="ams-mapping-hotend-page">
        <SubAmsPage
          slot={subAmsSlot}
          onSelect={(sub) => {
            void selectSlot(sub);
            setSubAmsSlot(null);
          }}
          onBack={() => setSubAmsSlot(null)}
        />
      </div>
    );
  }

  return (
    <div ref={pageRef} className="ams-map-hotend-page" data-testid="ams-mapping-hotend-page">
      {error && <Tips text={error} />}

      <div className="ams-map-hotend-page__grid">
        {rows.map((row, rowIdx) => {
          const isCentered = row === topSlots && topSlots.length > 0;
          return (
            <div key={rowIdx} className="ams-map-hotend-page__row-group">
              <div
                className={`ams-map-hotend-page__row${isCentered ? ' ams-map-hotend-page__row--centered' : ''}`}
              >
                {row.map((slot) => (
                  <SlotCard
                    key={`${slot.ams_id}:${slot.slot_id}`}
                    slot={slot}
                    onClick={() => void selectSlot(slot)}
                    onEnterSubAms={() => setSubAmsSlot(slot)}
                  />
                ))}
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}
