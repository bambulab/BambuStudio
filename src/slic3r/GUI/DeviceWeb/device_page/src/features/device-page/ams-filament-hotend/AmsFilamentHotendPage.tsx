import { useEffect, useMemo, useRef, useState } from 'react';
import type { RefObject } from 'react';
import { useAmsFilamentHotendBridge } from './useAmsFilamentHotendBridge';
import { useSendToCpp } from '../../../hooks/Bridge';
import type {
  AmsFilamentHotendSlotActionPayload,
  AmsFilamentHotendSlotViewModel,
  AmsFilamentHotendTab,
  HotendSlotViewModel,
} from './types';
import { PageToolbar } from './components/PageToolbar';
import { Stage } from './components/Stage';
import { HotendPanel } from './components/HotendPanel';
import { PageFooter } from './components/PageFooter';
import { FilamentChangeProgressPanel } from './components/FilamentChangeProgressPanel';
import './ams-filament-hotend.css';

const HOST_LAYOUT_MODULE = 'device_host';
const HOTEND_PAGE_MIN_WIDTH = 560;

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
      const width = Math.ceil(Math.max(root.scrollWidth, HOTEND_PAGE_MIN_WIDTH));
      const height = Math.ceil(root.scrollHeight || root.offsetHeight);
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

export function AmsFilamentHotendPage() {
  const pageRef = useRef<HTMLDivElement>(null);
  const { state, error, runSlotAction, openBridgeDebugDialog } = useAmsFilamentHotendBridge();
  const [tab, setTab] = useState<AmsFilamentHotendTab>('filament');
  const [debugOpening, setDebugOpening] = useState(false);
  const showDebugButton = Boolean((window as Window & { __internalBuild?: boolean }).__internalBuild);
  useReportHostContentSize(pageRef);

  const fp = state.filament_page;
  const hp = state.hotend_page;

  const filamentSlots = useMemo(
    () => [...fp.hotend_slots].sort((a, b) => a.display_index - b.display_index),
    [fp.hotend_slots],
  );
  const hotendSlots = useMemo(
    () => [...hp.hotend_slots].sort((a, b) => a.display_index - b.display_index),
    [hp.hotend_slots],
  );
  const topSlots = filamentSlots.filter((s) => s.is_top_tier);
  const bottomSlots = filamentSlots.filter((s) => !s.is_top_tier);

  // Toolbar actions: filament tab → filament_page.actions; hotend tab →
  // hotend_page.actions. Both are passed through and PageToolbar picks fields
  // by tab.
  const toolbarActions = useMemo(
    () => ({ ...fp.actions, ...hp.actions }),
    [fp.actions, hp.actions],
  );
  const footerActions = useMemo(
    () => (fp.filament_change_status?.active ? { ...fp.actions, tips: '' } : fp.actions),
    [fp.actions, fp.filament_change_status?.active],
  );

  const handleToolbarAction = (action: string) => runSlotAction(action);

  const handleSlotAction = (
    action: string,
    slot?: AmsFilamentHotendSlotViewModel | HotendSlotViewModel,
    payload?: AmsFilamentHotendSlotActionPayload,
  ) => runSlotAction(action, slot, payload);

  return (
    <div ref={pageRef} className="ams-filament-hotend-page" data-testid="ams-filament-hotend-page">
      <PageToolbar
        selectedTab={tab}
        onTabChange={setTab}
        actions={toolbarActions}
        onAction={handleToolbarAction}
      />

      {error && <div className="page-error">{error}</div>}

      {tab === 'filament' ? (
        <>
          <Stage
            topSlots={topSlots}
            bottomSlots={bottomSlots}
            amsDryZones={fp.ams_dry_zones ?? []}
            hotendMode={false}
            onAction={handleSlotAction}
          />
          <FilamentChangeProgressPanel
            status={fp.filament_change_status}
            onAction={handleToolbarAction}
          />
        </>
      ) : (
        <HotendPanel
          slots={hotendSlots}
          rackStatus={hp.hotend_rack_status}
          onAction={handleSlotAction}
        />
      )}

      <PageFooter
        actions={footerActions}
        onAction={handleToolbarAction}
        showDebugButton={showDebugButton}
        debugOpening={debugOpening}
        onDebug={async () => {
          setDebugOpening(true);
          try {
            await openBridgeDebugDialog(state);
          } finally {
            setDebugOpening(false);
          }
        }}
      />
    </div>
  );
}
