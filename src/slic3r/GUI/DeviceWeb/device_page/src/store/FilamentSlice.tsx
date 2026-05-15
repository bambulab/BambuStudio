import type { StateCreator } from 'zustand';
import type { RootState } from './AppStore';
import type {
  Spool, PresetOptions, MachineItem, AmsData,
  CloudSyncState, CloudToast, CloudFilamentConfig,
  CloudSyncHistoryEntry, CloudAutoPushSummary,
  DebugLogEntry, DebugLogFilter,
  CandidateColor,
} from '../features/filament/types';

const DEFAULT_SYNC_STATE: CloudSyncState = {
  logged_in: false,
  is_syncing: false,
  is_pulling: false,
  last_synced_at: '',
  last_error: { code: 0, message: '' },
};

export interface FilamentState {
  spools: Spool[];
  presets: PresetOptions;
  machines: MachineItem[];
  amsData: AmsData | null;
  // F4.7: mirror of Studio's global `DeviceManager::get_selected_machine()`,
  // updated by the `filament/machine/selected_changed` report. Consumed by
  // `AddEditDialog` so the "Read from AMS" tab defaults to — and follows —
  // whichever printer the rest of Studio is pointing at.
  selectedMachineDevId: string;
  theme: 'dark' | 'light';
  isLoading: boolean;
  error: string | null;

  // Cloud sync layer (Web-layer mirror of FilaManagerVM cloud state)
  cloudSync: CloudSyncState;
  cloudConfig: CloudFilamentConfig | null;
  cloudSyncHistory: CloudSyncHistoryEntry[];
  // STUDIO-18155：最近一次 AMS auto / 手动 push 决策摘要（null = 还没发生过）
  cloudAutoPushSummary: CloudAutoPushSummary | null;
  toasts: CloudToast[];
  debugEnabled: boolean;
  debugLogs: DebugLogEntry[];
  debugFilter: DebugLogFilter;

  // STUDIO-17977 / Task 10: per-fila_id cache of the candidate colour list
  // returned by `filament.colors.query_for_id`. The Add/Edit dialog populates
  // this lazily on filament selection so the colour palette becomes a live
  // mirror of FilamentColorCodeQuery (gradient + multicolor + single) instead
  // of a hardcoded BAMBU_COLORS hex array. An empty array means "queried,
  // none available" — used as a negative cache so we don't re-issue the RPC
  // on every dialog re-open for filaments without preset colours.
  candidatesByFilaId: Record<string, CandidateColor[]>;

  // Keep legacy fields for backward compatibility
  items: Spool[];
}

export interface FilamentActions {
  setSpools: (spools: Spool[]) => void;
  setPresets: (presets: PresetOptions) => void;
  setMachines: (machines: MachineItem[]) => void;
  setAmsData: (data: AmsData) => void;
  setSelectedMachineDevId: (devId: string) => void;
  setTheme: (theme: 'dark' | 'light') => void;
  setLoading: (v: boolean) => void;
  setError: (e: string | null) => void;

  // Cloud
  setCloudSync: (s: CloudSyncState) => void;
  setCloudConfig: (c: CloudFilamentConfig) => void;
  appendCloudSyncHistory: (entry: Omit<CloudSyncHistoryEntry, 'id'>) => void;
  clearCloudSyncHistory: () => void;
  setCloudAutoPushSummary: (summary: CloudAutoPushSummary | null) => void;
  pushToast: (t: Omit<CloudToast, 'id'>) => void;
  dismissToast: (id: number) => void;
  setDebugEnabled: (enabled: boolean) => void;
  appendDebugLog: (entry: Omit<DebugLogEntry, 'id'>) => void;
  clearDebugLogs: () => void;
  setDebugFilter: (filter: DebugLogFilter) => void;

  // STUDIO-17977 / Task 10: colour candidate cache writer. The actual RPC is
  // issued by the dialog (it owns the `useDeviceBridge` hook); the slice only
  // stores the result so the cache survives dialog open/close cycles.
  setColorCandidates: (filaId: string, candidates: CandidateColor[]) => void;

  // Legacy
  setItems: (items: Spool[]) => void;
}

export interface FilamentSlice {
  filament: FilamentState & FilamentActions;
}

/** Detect theme from UserAgent set by C++ RecreateAll():
 *  "BBL-Slicer/v... (dark) ..." or "(light)" */
function detectTheme(): 'dark' | 'light' {
  return /\(\s*light\s*\)/i.test(navigator.userAgent) ? 'light' : 'dark';
}

export const createFilamentSlice: StateCreator<
  RootState,
  [['zustand/immer', never]],
  [],
  FilamentSlice
> = (set) => ({
  filament: {
    spools: [],
    presets: { vendors: [] },
    machines: [],
    amsData: null,
    selectedMachineDevId: '',
    theme: detectTheme(),
    isLoading: false,
    error: null,
    cloudSync: { ...DEFAULT_SYNC_STATE },
    cloudConfig: null,
    cloudSyncHistory: [],
    cloudAutoPushSummary: null,
    toasts: [],
    debugEnabled: false,
    debugLogs: [],
    debugFilter: 'all',
    candidatesByFilaId: {},
    items: [],

    setSpools: (spools) =>
      set((s) => {
        s.filament.spools = spools;
        s.filament.items = spools; // sync legacy
        s.filament.error = null;
      }),
    setPresets: (presets) =>
      set((s) => {
        s.filament.presets = presets;
      }),
    setMachines: (machines) =>
      set((s) => {
        s.filament.machines = machines;
      }),
    setAmsData: (data) =>
      set((s) => {
        s.filament.amsData = data;
      }),
    setSelectedMachineDevId: (devId) =>
      set((s) => {
        s.filament.selectedMachineDevId = devId || '';
      }),
    setTheme: (theme) =>
      set((s) => {
        s.filament.theme = theme;
      }),
    setLoading: (v) =>
      set((s) => {
        s.filament.isLoading = v;
      }),
    setError: (e) =>
      set((s) => {
        s.filament.error = e;
        s.filament.isLoading = false;
      }),

    setCloudSync: (state) =>
      set((s) => {
        s.filament.cloudSync = state;
      }),
    setCloudConfig: (cfg) =>
      set((s) => {
        s.filament.cloudConfig = cfg;
      }),
    appendCloudSyncHistory: (entry) =>
      set((s) => {
        const list = s.filament.cloudSyncHistory;
        const id = (list[list.length - 1]?.id ?? 0) + 1;
        list.push({ id, ...entry });
        // Cap the history at the most recent 100 events to avoid unbounded growth.
        if (list.length > 100) {
          list.splice(0, list.length - 100);
        }
      }),
    clearCloudSyncHistory: () =>
      set((s) => {
        s.filament.cloudSyncHistory = [];
      }),
    setCloudAutoPushSummary: (summary) =>
      set((s) => {
        s.filament.cloudAutoPushSummary = summary;
      }),
    pushToast: (t) =>
      set((s) => {
        const id = (s.filament.toasts[s.filament.toasts.length - 1]?.id ?? 0) + 1;
        s.filament.toasts.push({ id, ...t });
        // Cap the queue at 5 most recent entries.
        if (s.filament.toasts.length > 5) {
          s.filament.toasts.splice(0, s.filament.toasts.length - 5);
        }
      }),
    dismissToast: (id) =>
      set((s) => {
        s.filament.toasts = s.filament.toasts.filter((t) => t.id !== id);
      }),
    setDebugEnabled: (enabled) =>
      set((s) => {
        s.filament.debugEnabled = enabled;
        if (!enabled) {
          s.filament.debugLogs = [];
          s.filament.debugFilter = 'all';
        }
      }),
    appendDebugLog: (entry) =>
      set((s) => {
        if (!s.filament.debugEnabled) return;
        const id = (s.filament.debugLogs[s.filament.debugLogs.length - 1]?.id ?? 0) + 1;
        s.filament.debugLogs.push({ id, ...entry });
        if (s.filament.debugLogs.length > 300) {
          s.filament.debugLogs.splice(0, s.filament.debugLogs.length - 300);
        }
      }),
    clearDebugLogs: () =>
      set((s) => {
        s.filament.debugLogs = [];
      }),
    setDebugFilter: (filter) =>
      set((s) => {
        s.filament.debugFilter = filter;
      }),

    setColorCandidates: (filaId, candidates) =>
      set((s) => {
        if (!filaId) return;
        const next = Array.isArray(candidates) ? candidates : [];
        // STUDIO-17977: don't clobber a populated cache entry with an empty
        // list. AddEditDialog.loadCandidates and useFilamentBridge's
        // prefetch effect can both fire RPCs for the same fila_id; the
        // dialog typically queues first when the user opens it, but the
        // prefetch may finish later — and in production the C++ side
        // occasionally returns an empty `candidates` array for a
        // fila_id whose FilamentColorCodeQuery index is still warming up
        // (cloud catalogue timing). Keeping the populated list when the
        // late writer is empty preserves the row tail's colorName / fila
        // code across the user-visible "瞬间正确，然后变成不正确" window.
        // Non-empty payloads always win so legitimate refreshes still
        // propagate.
        const cur = s.filament.candidatesByFilaId[filaId];
        if (next.length === 0 && Array.isArray(cur) && cur.length > 0) return;
        s.filament.candidatesByFilaId[filaId] = next;
      }),

    // Legacy
    setItems: (items) =>
      set((s) => {
        s.filament.items = items;
        s.filament.spools = items;
        s.filament.error = null;
      }),
  },
});
