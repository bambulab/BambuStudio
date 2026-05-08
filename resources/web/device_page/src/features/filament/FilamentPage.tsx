import { useEffect, useState, useMemo, useCallback } from 'react';
import { useTranslation } from 'react-i18next';
import useStore from '../../store/AppStore';
import { useFilamentBridge } from './useFilamentBridge';
import { SpoolTable } from './SpoolTable';
import { AddEditDialog } from './AddEditDialog';
import { DetailDialog } from './DetailDialog';
import { ConfirmDialog } from './ConfirmDialog';
import { CloudBadge } from './CloudBadge';
import { CloudHistoryPopover } from './CloudHistoryPopover';
import { ToastStack } from './ToastStack';
import { DebugLogPanel } from './DebugLogPanel';
import type { Spool, MachineItem, AmsData } from './types';
import './filament.css';

type TabMode = 'all' | 'ams';
// 列表筛选项（label ↔ 本地字段 ↔ 云端字段）：
//   Brand         brand          filamentVendor
//   Filament Type material_type  filamentType   （类型大类，PLA / PETG / PA ...）
//   Material Type series         filamentName   （完整名，PLA Basic / Support For PA/PET）
type FilterKey = 'brand' | 'material_type' | 'series';

const FILTER_LABEL_KEYS: Record<FilterKey, string> = {
  brand: 'Brand', material_type: 'Filament Type', series: 'Material Type',
};

export function FilamentPage() {
  const { t } = useTranslation();
  const {
    init, addSpool, batchAddSpool, updateSpool,
    removeSpool, batchRemoveSpool,
    fetchMachines, requestMachinePushall, fetchAmsData,
    fetchCloudSyncStatus, triggerCloudPull, fetchCloudFilamentConfig,
  } = useFilamentBridge();

  const spools      = useStore((s) => s.filament.spools);
  const presets     = useStore((s) => s.filament.presets);
  const theme       = useStore((s) => s.filament.theme);
  const isLoading   = useStore((s) => s.filament.isLoading);
  const cloudSync   = useStore((s) => s.filament.cloudSync);
  const toasts      = useStore((s) => s.filament.toasts);
  const dismissToast = useStore((s) => s.filament.dismissToast);
  const debugEnabled = useStore((s) => s.filament.debugEnabled);
  const debugLogs    = useStore((s) => s.filament.debugLogs);
  const debugFilter  = useStore((s) => s.filament.debugFilter);
  const setDebugFilter = useStore((s) => s.filament.setDebugFilter);
  const clearDebugLogs = useStore((s) => s.filament.clearDebugLogs);

  // View state
  const [tab, setTab]         = useState<TabMode>('all');
  const [search, setSearch]   = useState('');
  const [filters, setFilters] = useState<Partial<Record<FilterKey, string>>>({});
  const [grouped, setGrouped] = useState(true);
  const [selected, setSelected] = useState<Set<string>>(new Set());
  const canBatchDelete = selected.size > 0;
  const isLoggedIn = cloudSync.logged_in;

  // Filter dropdown
  const [openFilter, setOpenFilter] = useState<FilterKey | null>(null);

  // Cloud sync history popover (triggered by the button next to CloudBadge).
  const [historyOpen, setHistoryOpen] = useState(false);

  // Dialogs
  const [dialogOpen, setDialogOpen]       = useState(false);
  const [editingSpool, setEditingSpool]   = useState<Spool | null>(null);
  const [prefilledSpool, setPrefilledSpool] = useState<Partial<Spool> | null>(null);
  const [detailSpool, setDetailSpool]     = useState<Spool | null>(null);
  const [detailOpen, setDetailOpen]       = useState(false);

  // Custom confirm modal (replaces native `window.confirm()` which leaks the
  // page URL in its title bar when running inside a WebView2 host).
  const [confirmState, setConfirmState] = useState<{
    title: string;
    message?: string;
    confirmText?: string;
    onConfirm: () => void | Promise<unknown>;
  } | null>(null);

  // Internal build: enable debug panel immediately (independent of init() success)
  const setDebugEnabled = useStore((s) => s.filament.setDebugEnabled);
  useEffect(() => {
    if ((window as any).__internalBuild) {
      setDebugEnabled(true);
    }
  }, [setDebugEnabled]);

  // Init
  useEffect(() => { init(); }, [init]);

  // Kick off cloud status + config at mount; if already logged in (e.g. user
  // opened Filament tab after signing in), trigger pull so list matches cloud
  // (C++ also enqueues pull on login — this covers WebView init race / tab
  // opened after pull completed without bridge attached).
  useEffect(() => {
    let cancelled = false;
    void (async () => {
      await fetchCloudSyncStatus();
      if (cancelled) return;
      void fetchCloudFilamentConfig();
      const { cloudSync: st } = useStore.getState().filament;
      if (st.logged_in) {
        await triggerCloudPull();
      }
    })();
    return () => { cancelled = true; };
  }, [fetchCloudSyncStatus, fetchCloudFilamentConfig, triggerCloudPull]);

  useEffect(() => {
    if (isLoggedIn) return;
    setSelected(new Set());
    setDetailOpen(false);
    setDetailSpool(null);
    setDialogOpen(false);
    setEditingSpool(null);
    setPrefilledSpool(null);
  }, [isLoggedIn]);

  // Apply theme
  useEffect(() => {
    document.documentElement.dataset.theme = theme;
  }, [theme]);

  // Filter spools
  const filtered = useMemo(() => {
    // Archived spools are hidden in this version (Stats / Archive entries are
    // removed from UX; archive data is still preserved in the store for a
    // future release).
    let list = spools.filter((s) => s.status !== 'archived');

    // Tab filter
    if (tab === 'ams') list = list.filter((s) => s.entry_method === 'ams_sync');

    // Search
    if (search.trim()) {
      const kw = search.toLowerCase();
      list = list.filter((s) =>
        `${s.brand} ${s.material_type} ${s.series} ${s.color_name}`.toLowerCase().includes(kw)
      );
    }

    // Filters
    for (const [k, v] of Object.entries(filters)) {
      if (v) list = list.filter((s) => (s as any)[k] === v);
    }

    return list;
  }, [spools, tab, search, filters]);

  useEffect(() => {
    setSelected((prev) => {
      if (prev.size === 0) return prev;
      const visibleIds = new Set(filtered.map((s) => s.spool_id));
      const next = new Set([...prev].filter((id) => visibleIds.has(id)));
      return next.size === prev.size ? prev : next;
    });
  }, [filtered]);

  // Filter dropdown options
  const getFilterOptions = (key: FilterKey): string[] => {
    const vals = new Set<string>();
    spools.forEach((s) => { const v = (s as any)[key]; if (v) vals.add(v); });
    return [...vals].sort();
  };

  // Selection
  const handleToggleSelect = useCallback((id: string) => {
    setSelected((prev) => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id); else next.add(id);
      return next;
    });
  }, []);

  const handleSelectAll = useCallback(() => {
    setSelected((prev) =>
      prev.size === filtered.length ? new Set() : new Set(filtered.map((s) => s.spool_id))
    );
  }, [filtered]);

  // Actions
  // Delete is uniform across views: confirm -> remove from local store -> cloud DELETE.
  const handleDelete = useCallback((id: string) => {
    setConfirmState({
      title: t('Delete Filament'),
      message: t('Are you sure to permanently delete this filament? This action cannot be undone.'),
      confirmText: t('Delete'),
      onConfirm: () => removeSpool(id),
    });
  }, [removeSpool, t]);

  const handleBatchDelete = useCallback(() => {
    const ids = [...selected];
    if (!ids.length) return;
    setConfirmState({
      title: t('Delete {{count}} Filaments', { count: ids.length }),
      message: t('Are you sure to permanently delete the selected filaments? This action cannot be undone.'),
      confirmText: t('Delete'),
      onConfirm: async () => {
        await batchRemoveSpool(ids);
        setSelected(new Set());
      },
    });
  }, [selected, batchRemoveSpool, t]);

  const handleDetail = useCallback((id: string) => {
    const s = spools.find((x) => x.spool_id === id);
    if (s) { setDetailSpool(s); setDetailOpen(true); }
  }, [spools]);

  const handleAddSimilar = useCallback((id: string) => {
    const s = spools.find((x) => x.spool_id === id);
    if (!s) return;
    setEditingSpool(null);
    setPrefilledSpool({
      brand: s.brand, material_type: s.material_type, series: s.series,
      color_code: s.color_code, color_name: s.color_name,
      initial_weight: s.initial_weight, spool_weight: s.spool_weight,
    });
    setDialogOpen(true);
  }, [spools]);

  const handleEditFromDetail = useCallback((spool: Spool) => {
    setDetailOpen(false);
    setEditingSpool(spool);
    setPrefilledSpool(null);
    setDialogOpen(true);
  }, []);

  const handleOpenAddDialog = useCallback(() => {
    if (!isLoggedIn) return;
    setEditingSpool(null);
    setPrefilledSpool(null);
    setDialogOpen(true);
  }, [isLoggedIn]);

  const handleSubmitAdd = useCallback(async (data: Partial<Spool>, qty: number) => {
    if (qty > 1) return batchAddSpool(data, qty);
    return addSpool(data);
  }, [addSpool, batchAddSpool]);

  const handleSubmitUpdate = useCallback(async (data: Partial<Spool>) => {
    return updateSpool(data);
  }, [updateSpool]);

  // AMS bridge callbacks for dialog
  const handleFetchMachines = useCallback(async (): Promise<MachineItem[]> => {
    await fetchMachines();
    return useStore.getState().filament.machines;
  }, [fetchMachines]);

  const handleFetchAmsData = useCallback(async (
    devId?: string,
    switchSelected: boolean = false,
  ): Promise<AmsData | null> => {
    await fetchAmsData(devId, switchSelected);
    return useStore.getState().filament.amsData;
  }, [fetchAmsData]);

  // Cloud is the source of truth: the global sync button is a pure "pull"
  // button. Local mutations are pushed immediately from the C++ side and the
  // dispatcher auto-pulls right after, so the list always reflects the cloud
  // state without the user having to trigger anything.
  const handleCloudSyncClick = useCallback(async () => {
    if (!isLoggedIn) return;
    await Promise.all([
      triggerCloudPull(),
      fetchCloudFilamentConfig({ force: true }),
    ]);
  }, [isLoggedIn, triggerCloudPull, fetchCloudFilamentConfig]);

  return (
    <div className="flex h-screen overflow-hidden bg-fm-base text-fm-text-primary text-xs leading-[19px] font-['HarmonyOS_Sans_SC',-apple-system,'Segoe_UI',sans-serif] fm-native-form">
      {/* Main content (sidebar removed: Stats / Archive entries are not
          shipped in this version, and "My Filaments" is the only remaining
          view so a single-item nav is redundant.) */}
      <div className="flex-1 min-h-0 flex flex-col overflow-hidden p-6">
        <div className="flex-1 min-h-0 flex flex-col overflow-hidden gap-4">
          <>
              {/* Toolbar */}
              <div className="flex items-center justify-between gap-4 shrink-0">
              <div className="flex items-center gap-4">
                {/* Tabs */}
                <div className={`flex gap-2 ${!isLoggedIn ? 'opacity-40 pointer-events-none' : ''}`}>
                  {(['all', 'ams'] as const).map((tb) => (
                    <div
                      key={tb}
                      className={`px-[10px] py-1 h-7 rounded-md cursor-pointer text-xs text-fm-text-secondary flex items-center transition-colors duration-150 hover:bg-fm-hover ${tab === tb ? 'bg-fm-input text-fm-text-strong' : ''}`}
                      onClick={() => setTab(tb)}
                    >
                      {tb === 'all' ? t('All') : 'AMS'}
                    </div>
                  ))}
                </div>

                <div className="w-px h-[11px] bg-fm-border" />

                {/* Filters */}
                <div className={`flex gap-2 ${!isLoggedIn ? 'opacity-40 pointer-events-none' : ''}`}>
                  {(['brand', 'material_type', 'series'] as const).map((fk) => (
                    <div key={fk} style={{ position: 'relative' }}>
                      <div
                        className={`flex items-center gap-1 px-2 pl-[6px] py-[2px] h-6 rounded-md cursor-pointer text-sm text-fm-text-primary transition-colors duration-150 hover:bg-fm-hover ${filters[fk] ? 'bg-fm-brand/15 text-fm-brand font-medium' : ''}`}
                        onClick={(e) => { e.stopPropagation(); setOpenFilter(openFilter === fk ? null : fk); }}
                      >
                        {t(FILTER_LABEL_KEYS[fk])}
                        <svg width="8" height="5" viewBox="0 0 8 5" fill="none">
                          <path d="M1 1l3 3 3-3" stroke="currentColor" strokeWidth="1" />
                        </svg>
                      </div>
                      {openFilter === fk && (
                        <FilterDropdown
                          options={getFilterOptions(fk)}
                          current={filters[fk] || ''}
                          onSelect={(val) => {
                            setFilters((prev) => {
                              const next = { ...prev };
                              if (val) next[fk] = val; else delete next[fk];
                              return next;
                            });
                            setOpenFilter(null);
                          }}
                          onClose={() => setOpenFilter(null)}
                        />
                      )}
                    </div>
                  ))}
                </div>
              </div>

              <div className="flex items-center gap-4">
                <div className={`flex items-center gap-1 bg-fm-inner2 rounded-md px-2 h-[30px] w-[200px] ${!isLoggedIn ? 'opacity-40' : ''}`}>
                  <svg className="text-fm-text-detail shrink-0" width="14" height="14" viewBox="0 0 14 14" fill="none">
                    <circle cx="6" cy="6" r="4.5" stroke="currentColor" strokeWidth="1.2" />
                    <path d="M9.5 9.5L13 13" stroke="currentColor" strokeWidth="1.2" />
                  </svg>
                  <input
                    className="bg-transparent border-none outline-none text-fm-text-primary text-xs w-full placeholder:text-fm-text-detail disabled:cursor-not-allowed"
                    type="text"
                    placeholder={t('Search Filament')}
                    value={search}
                    onChange={(e) => setSearch(e.target.value)}
                    disabled={!isLoggedIn}
                  />
                </div>

                <button
                  className={`inline-flex items-center gap-1 h-[30px] px-3 rounded-lg border-none text-xs whitespace-nowrap transition-colors duration-150 bg-fm-inner text-fm-text-primary border border-fm-border-focus/50 hover:bg-fm-hover ${grouped ? '!bg-[rgba(44,173,0,0.08)] !border-fm-brand !text-[#50e81d]' : ''} ${!isLoggedIn ? 'opacity-40 cursor-not-allowed hover:bg-fm-inner' : 'cursor-pointer'}`}
                  onClick={() => setGrouped(!grouped)}
                  disabled={!isLoggedIn}
                >
                  {t('Group')}
                </button>
                <CloudBadge
                  state={cloudSync}
                  onPullClick={handleCloudSyncClick}
                />
                <button
                  type="button"
                  className={`inline-flex items-center justify-center h-[30px] w-[30px] rounded-lg border border-fm-border-focus/50 bg-fm-inner text-fm-text-secondary transition-colors duration-150 ${!isLoggedIn ? 'opacity-40 cursor-not-allowed' : 'cursor-pointer hover:bg-fm-hover'}`}
                  title={t('Sync History')}
                  aria-label={t('Sync History')}
                  onClick={() => setHistoryOpen(true)}
                  disabled={!isLoggedIn}
                >
                  <svg width="18" height="18" viewBox="0 0 18 18" fill="none" aria-hidden="true">
                    <path d="M3.5 9a5.5 5.5 0 1 0 1.6-3.9" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" strokeLinejoin="round" />
                    <path d="M3.5 4v3h3" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" strokeLinejoin="round" />
                    <path d="M9 6v3.25L11 10.5" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" strokeLinejoin="round" />
                  </svg>
                </button>
                <button
                  className={`inline-flex items-center gap-1 h-[30px] px-3 rounded-lg border-none text-xs whitespace-nowrap transition-colors duration-150 font-medium ${
                    isLoggedIn
                      ? 'cursor-pointer bg-fm-brand text-white hover:bg-fm-brand-hover'
                      : 'cursor-not-allowed bg-fm-brand/40 text-white/70'
                  }`}
                  disabled={!isLoggedIn}
                  onClick={handleOpenAddDialog}
                >
                  {t('Add Filament')}
                </button>
              </div>
              </div>

              {/* Selection action bar — sole batch-delete entry point for F6.3. */}
              {canBatchDelete && (
                <div className="flex items-center justify-between shrink-0 bg-[rgba(224,64,64,0.08)] border border-fm-danger/60 rounded-lg px-4 py-2">
                  <div className="flex items-center gap-2 text-fm-danger text-sm">
                    <svg width="16" height="16" viewBox="0 0 16 16" fill="none" className="shrink-0">
                      <circle cx="8" cy="8" r="7" stroke="currentColor" strokeWidth="1.2" fill="none" />
                      <path d="M8 4v5M8 11v.5" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" />
                    </svg>
                    <span>{t('Selected {{count}} items', { count: selected.size })}</span>
                  </div>
                  <div className="flex items-center gap-2">
                    <button
                      className="inline-flex items-center gap-[6px] h-[28px] px-3 rounded-md border border-fm-danger bg-fm-danger text-white text-xs cursor-pointer hover:brightness-110"
                      onClick={handleBatchDelete}
                    >
                      <svg width="14" height="14" viewBox="0 0 16 16" fill="none" className="shrink-0">
                        <path d="M4 5h8l-.6 8H4.6L4 5z" stroke="currentColor" strokeWidth="1.1" />
                        <path d="M6 3h4" stroke="currentColor" strokeWidth="1.1" />
                        <path d="M3 5h10" stroke="currentColor" strokeWidth="1.1" />
                        <path d="M6.5 7v4M9.5 7v4" stroke="currentColor" strokeWidth="1" />
                      </svg>
                      {t('Batch Delete')}
                    </button>
                    <button
                      className="h-[28px] px-3 rounded-md border border-fm-border-focus/50 bg-transparent text-fm-text-primary text-xs cursor-pointer hover:bg-fm-hover"
                      onClick={() => setSelected(new Set())}
                    >
                      {t('Clear Selection')}
                    </button>
                  </div>
                </div>
              )}

              {/* Table */}
              {isLoading ? (
                <div className="flex flex-col items-center justify-center py-20 text-fm-text-detail gap-4"><p>{t('Loading...')}</p></div>
              ) : !isLoggedIn ? (
                <div className="flex flex-col items-center justify-center py-20 text-center gap-3 rounded-lg border border-fm-border bg-fm-inner">
                  <p className="m-0 text-[15px] leading-[22px] text-fm-text-strong">{t('Not signed in — no data available')}</p>
                  <p className="m-0 text-xs leading-[19px] text-fm-text-detail">{t('Please sign in to view your filament library.')}</p>
                </div>
              ) : (
                <SpoolTable
                  spools={filtered}
                  selected={selected}
                  onToggleSelect={handleToggleSelect}
                  onSelectAll={handleSelectAll}
                  grouped={grouped}
                  onDetail={handleDetail}
                  onAddSimilar={handleAddSimilar}
                  onEmptyAdd={handleOpenAddDialog}
                  onDelete={handleDelete}
                />
              )}
          </>
        </div>

        {debugEnabled && (
          <div className="pt-4 mt-auto shrink-0">
            <DebugLogPanel
              logs={debugLogs}
              filter={debugFilter}
              onFilterChange={setDebugFilter}
              onClear={clearDebugLogs}
            />
          </div>
        )}
      </div>

      {/* Dialogs */}
      <AddEditDialog
        open={dialogOpen}
        editingSpool={editingSpool}
        prefilledSpool={prefilledSpool}
        presets={presets.vendors}
        onClose={() => { setDialogOpen(false); setEditingSpool(null); setPrefilledSpool(null); }}
        onSubmitAdd={handleSubmitAdd}
        onSubmitUpdate={handleSubmitUpdate}
        onFetchMachines={handleFetchMachines}
        onRequestPushall={requestMachinePushall}
        onFetchAmsData={handleFetchAmsData}
      />

      <DetailDialog
        open={detailOpen}
        spool={detailSpool}
        filteredSpools={filtered}
        onClose={() => setDetailOpen(false)}
        onEdit={handleEditFromDetail}
        onNavigate={(id) => {
          const s = spools.find((x) => x.spool_id === id);
          if (s) setDetailSpool(s);
        }}
      />

      <ToastStack toasts={toasts} onDismiss={dismissToast} />

      <CloudHistoryPopover
        open={historyOpen}
        onClose={() => setHistoryOpen(false)}
      />

      <ConfirmDialog
        open={confirmState !== null}
        title={confirmState?.title ?? ''}
        message={confirmState?.message}
        confirmText={confirmState?.confirmText}
        danger
        onCancel={() => setConfirmState(null)}
        onConfirm={async () => {
          const fn = confirmState?.onConfirm;
          setConfirmState(null);
          if (fn) await fn();
        }}
      />
    </div>
  );
}

/* ===== Sub-components ===== */

function FilterDropdown({ options, current, onSelect, onClose }: {
  options: string[]; current: string;
  onSelect: (val: string) => void; onClose: () => void;
}) {
  const { t } = useTranslation();
  useEffect(() => {
    const handler = () => onClose();
    document.addEventListener('click', handler);
    return () => document.removeEventListener('click', handler);
  }, [onClose]);

  return (
    <div className="absolute z-[100] bg-fm-sidebar border border-fm-border rounded-lg p-1 min-w-[120px] max-h-60 overflow-y-auto shadow-[0_4px_12px_rgba(0,0,0,0.4)]" onClick={(e) => e.stopPropagation()}>
      <div
        className={`px-3 py-[6px] rounded-sm cursor-pointer text-xs text-fm-text-primary hover:bg-fm-hover ${!current ? 'bg-fm-brand/15 text-fm-brand font-medium' : ''}`}
        onClick={() => onSelect('')}
      >{t('All')}</div>
      {options.map((v) => (
        <div
          key={v}
          className={`px-3 py-[6px] rounded-sm cursor-pointer text-xs text-fm-text-primary hover:bg-fm-hover ${current === v ? 'bg-fm-brand/15 text-fm-brand font-medium' : ''}`}
          onClick={() => onSelect(v)}
        >{v}</div>
      ))}
    </div>
  );
}
