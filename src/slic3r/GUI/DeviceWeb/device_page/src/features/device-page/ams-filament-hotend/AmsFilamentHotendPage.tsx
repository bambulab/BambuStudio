import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { RefObject } from 'react';
import { useAmsFilamentHotendBridge } from './useAmsFilamentHotendBridge';
import { useSendToCpp } from '../../../hooks/Bridge';
import { useDeviceWebTheme } from '../../../hooks/useDeviceWebTheme';
import { useFitHostWidth } from '../../../hooks/useFitHostWidth';
import type {
  AmsFilamentHotendSlotActionPayload,
  AmsFilamentHotendSlotViewModel,
  AmsFilamentHotendTab,
  FilamentPageActionState,
  FilamentPageMode,
  HotendSlotViewModel,
} from './types';
import { PageToolbar } from './components/PageToolbar';
import { Stage, selectionKey } from './components/Stage';
import { HotendPanel } from './components/HotendPanel';
import { PageFooter } from './components/PageFooter';
import { BatchModeFooter } from './components/BatchModeFooter';
import { FilamentChangeProgressPanel } from './components/FilamentChangeProgressPanel';
import { FilamentManualPullBanner } from './components/FilamentManualPullDialog';
import { HOTEND_PAGE_MIN_WIDTH, resolveHostContentSize } from './hostContentSize';
import './ams-filament-hotend.css';

const HOST_LAYOUT_MODULE = 'device_host';

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

// Map a toolbar action name to the batch mode it should switch to. Dry is
// intentionally absent because it triggers a single-step dialog (HandleAction
// "dry") instead of entering a selection mode.
const ACTION_TO_BATCH_MODE: Record<string, FilamentPageMode> = {
  load_auto: 'batch_ams_load',
  load_external: 'batch_ext_load',
  unload: 'batch_unload',
};

export function AmsFilamentHotendPage() {
  const pageRef = useRef<HTMLDivElement>(null);
  const { state, error, refresh, runSlotAction, runBatchAction, openBridgeDebugDialog } = useAmsFilamentHotendBridge();
  const [tab, setTab] = useState<AmsFilamentHotendTab>('filament');
  const [debugOpening, setDebugOpening] = useState(false);
  // Local checkbox set, keyed by `selectionKey(slot)` so two slots that share
  // a display_index across different AMS units cannot collide. Reset whenever
  // the page mode changes (entering / leaving / switching batch modes).
  const [selectedKeys, setSelectedKeys] = useState<ReadonlySet<string>>(() => new Set());
  // Mode this page last asked C++ for. The batch focus/poll refresh below
  // reads it so an in-flight poll cannot resurrect a mode the user just left.
  const requestedModeRef = useRef<FilamentPageMode>('normal');
  const showDebugButton = Boolean((window as Window & { __internalBuild?: boolean }).__internalBuild);
  useReportHostContentSize(pageRef);
  // Panel hosts are FromDIP(560) wide; resizable dialogs only zoom when dragged below that.
  useFitHostWidth(HOTEND_PAGE_MIN_WIDTH);
  // Sync host theme into page CSS variables.
  useDeviceWebTheme();

  const currentMode: FilamentPageMode = state.filament_page.mode ?? 'normal';
  const inBatchMode = currentMode !== 'normal';

  const fp = state.filament_page;
  const showAmsEnvUnits = fp.show_ams_env_units ?? true;
  const hp = state.hotend_page;
  const manualPullStatus = fp.filament_change_status?.mode === 'batch_manual_pull'
    ? fp.filament_change_status.manual_pull
    : undefined;

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

  // The batch-eligible subset, computed solely from the C++ select_action.
  // Not used for rendering the cards (those iterate the full slot list and
  // grey out non-eligible ones); only used to drive `Select All` and the
  // commit payload.
  const selectableSlots = useMemo(
    () => filamentSlots.filter((s) => !!s.select_action?.can_select),
    [filamentSlots],
  );
  const selectedSlots = useMemo(
    () => selectableSlots.filter((s) => selectedKeys.has(selectionKey(s))),
    [selectableSlots, selectedKeys],
  );

  // Reset the local selection set whenever C++ switches the page mode (e.g.
  // when entering Auto Load, leaving via Exit, or after a successful commit
  // that flips back to normal). Without this the selection would survive
  // across mode boundaries and leak stale slot ids into batch_run payloads.
  useEffect(() => {
    setSelectedKeys(new Set());
  }, [currentMode]);

  // Drop slot keys that no longer correspond to a selectable slot. Happens
  // when a slot's eligibility flips during batch mode (e.g. RFID finishes
  // reading or a load progresses), so stale ticks do not survive into the
  // commit payload.
  useEffect(() => {
    if (!inBatchMode) return;
    const validKeys = new Set(selectableSlots.map(selectionKey));
    setSelectedKeys((prev) => {
      let pruned: Set<string> | null = null;
      for (const key of prev) {
        if (!validKeys.has(key)) {
          if (!pruned) pruned = new Set(prev);
          pruned.delete(key);
        }
      }
      return pruned ?? prev;
    });
  }, [inBatchMode, selectableSlots]);

  // External-filament editing happens in a separate C++ modal with its own
  // WebView/bridge, so the report emitted after the native edit dialog closes
  // does not automatically reach this page. While the external-load selection
  // mode is active, refresh on focus and with a light poll so the card grid
  // behind the edit dialog catches up after filament metadata changes.
  useEffect(() => {
    if (currentMode !== 'batch_ext_load' && currentMode !== 'batch_ams_load') return;
    let refreshing = false;
    const refreshExtMode = () => {
      // The user may have left batch mode while this poll was pending; do not
      // re-request the stale mode and pull the page back into selection.
      if (requestedModeRef.current !== currentMode) return;
      if (refreshing) return;
      refreshing = true;
      void refresh(currentMode).finally(() => {
        refreshing = false;
      });
    };
    window.addEventListener('focus', refreshExtMode);
    document.addEventListener('visibilitychange', refreshExtMode);
    const timer = window.setInterval(refreshExtMode, 2000);
    return () => {
      window.removeEventListener('focus', refreshExtMode);
      document.removeEventListener('visibilitychange', refreshExtMode);
      window.clearInterval(timer);
    };
  }, [currentMode, refresh]);

  // Merge tab-specific toolbar actions. In batch modes `fp.actions` values
  // are `{enabled, reason}` objects instead of plain booleans; PageToolbar
  // and PageFooter currently consume the boolean shape, so this build
  // collapses the object form to its `.enabled` boolean to keep the UI
  // alive end-to-end while the toolbar still ships its v1 (P0). The
  // BatchModeFooter (next subiteration) will own the `{enabled, reason}`
  // semantics and tooltip rendering.
  const fpActionsAsBool = useMemo(() => {
    const out: Record<string, unknown> = {};
    for (const [key, value] of Object.entries(fp.actions ?? {})) {
      if (value && typeof value === 'object' && 'enabled' in value) {
        out[key] = (value as { enabled?: boolean }).enabled ?? false;
      } else {
        out[key] = value;
      }
    }
    return out as unknown as FilamentPageActionState;
  }, [fp.actions]);

  const toolbarActions = useMemo(
    () => ({ ...fpActionsAsBool, ...hp.actions }),
    [fpActionsAsBool, hp.actions],
  );
  const footerActions: FilamentPageActionState = useMemo(
    () => (fp.filament_change_status?.active ? { ...fpActionsAsBool, tips: '' } : fpActionsAsBool),
    [fpActionsAsBool, fp.filament_change_status?.active],
  );

  const handleToolbarAction = (action: string) => {
    if (action === 'dry') {
      const dryZone = fp.ams_dry_zones?.find((z) => z.ams_id);
      runSlotAction(action, undefined, dryZone ? { ams_id: dryZone.ams_id } : {});
      return;
    }
    // Three batch entry points enter selection mode by re-requesting state
    // for the matching mode. Any other action keeps the legacy direct path.
    const nextMode = ACTION_TO_BATCH_MODE[action];
    if (nextMode) {
      requestedModeRef.current = nextMode;
      void refresh(nextMode);
      return;
    }
    runSlotAction(action);
  };

  const exitBatchMode = useCallback(() => {
    requestedModeRef.current = 'normal';
    void refresh('normal');
  }, [refresh]);

  // Batch selection lives entirely on the Filament tab, so leaving that tab
  // drops the pending selection and returns the page to normal mode instead
  // of forcing the user to press Exit before the Hotend tab becomes usable.
  const handleTabChange = useCallback((next: AmsFilamentHotendTab) => {
    setTab(next);
    if (next !== 'filament' && inBatchMode) exitBatchMode();
  }, [inBatchMode, exitBatchMode]);
  // Dev-only escape hatch the bridge debug dialog can fall back to.
  useEffect(() => {
    (window as Window & { __exitBatchMode?: () => void }).__exitBatchMode = exitBatchMode;
  }, [exitBatchMode]);

  const handleSlotAction = (
    action: string,
    slot?: AmsFilamentHotendSlotViewModel | HotendSlotViewModel,
    payload?: AmsFilamentHotendSlotActionPayload,
  ) => runSlotAction(action, slot, payload);

  const handleDryZoneClick = useCallback((amsId: string) => {
    runSlotAction('dry', undefined, { ams_id: amsId });
  }, [runSlotAction]);

  const onToggleSelect = useCallback((slot: AmsFilamentHotendSlotViewModel) => {
    const key = selectionKey(slot);
    setSelectedKeys((prev) => {
      const next = new Set(prev);
      if (next.has(key)) next.delete(key); else next.add(key);
      return next;
    });
  }, []);

  const onSelectAll = useCallback(() => {
    setSelectedKeys(new Set(selectableSlots.map(selectionKey)));
  }, [selectableSlots]);

  const onClearSelection = useCallback(() => {
    setSelectedKeys(new Set());
  }, []);

  const onBatchCommit = useCallback(() => {
    if (!inBatchMode) return;
    if (selectedSlots.length === 0) return;
    void runBatchAction(currentMode, selectedSlots);
  }, [inBatchMode, selectedSlots, runBatchAction, currentMode]);

  const onBatchEdit = useCallback(() => {
    if (currentMode !== 'batch_ext_load' && currentMode !== 'batch_ams_load') return;
    if (selectedSlots.length === 0) return;
    runSlotAction('batch_filament_edit', undefined, {
      intent: 'edit',
      mode: currentMode,
      slots: selectedSlots.map((s) => ({
        addr_id: s.addr_id,
        ams_id: s.ams_id,
        slot_id: s.slot_id,
        display_index: s.display_index,
      })),
    });
  }, [currentMode, selectedSlots, runSlotAction]);

  return (
    <div ref={pageRef} className="ams-filament-hotend-page" data-testid="ams-filament-hotend-page">
      <PageToolbar
        selectedTab={tab}
        onTabChange={handleTabChange}
        actions={toolbarActions}
        mode={currentMode}
        onAction={handleToolbarAction}
      />

      {error && <div className="page-error">{error}</div>}

      {tab === 'filament' ? (
        <>
          <Stage
            topSlots={topSlots}
            bottomSlots={bottomSlots}
            amsDryZones={fp.ams_dry_zones ?? []}
            amsEnvUnits={showAmsEnvUnits ? fp.ams_env_units ?? [] : []}
            hotendMode={false}
            mode={currentMode}
            selectedKeys={selectedKeys}
            onToggleSelect={onToggleSelect}
            onAction={handleSlotAction}
            onDry={handleDryZoneClick}
          />
          <FilamentChangeProgressPanel
            status={fp.filament_change_status}
            onAction={handleToolbarAction}
          />
          <FilamentManualPullBanner
            status={manualPullStatus}
            onOpen={() => runSlotAction('manual_pull_detail')}
          />
        </>
      ) : (
        <HotendPanel
          slots={hotendSlots}
          rackStatus={hp.hotend_rack_status}
          onAction={handleSlotAction}
        />
      )}

      {/* The tab-driven exit is async, so gate the batch footer on the tab as
          well to avoid flashing it under the Hotend panel while C++ replies. */}
      {inBatchMode && tab === 'filament' ? (
        <BatchModeFooter
          mode={currentMode}
          selectableSlots={selectableSlots}
          selectedSlots={selectedSlots}
          selectedKeys={selectedKeys}
          onSelectAll={onSelectAll}
          onClearSelection={onClearSelection}
          onCommit={onBatchCommit}
          onEdit={onBatchEdit}
          onExit={exitBatchMode}
        />
      ) : (
        <PageFooter
          actions={footerActions}
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
      )}
    </div>
  );
}
