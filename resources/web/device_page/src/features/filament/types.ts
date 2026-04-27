// Spool object — matches C++ FilamentSpool / wgtFilaManagerStore
export interface Spool {
  spool_id: string;
  cloud_synced?: boolean;
  brand: string;
  material_type: string;
  series: string;
  color_code: string;
  color_name: string;
  diameter: number;
  initial_weight: number;
  net_weight?: number;
  spool_weight: number;
  remain_percent: number;
  status: string;        // "active" | "empty" | "archived"
  favorite: boolean;
  entry_method: string;  // "manual" | "ams_sync" | "rfid"
  note: string;
  tag_uid?: string;
  setting_id?: string;
  bound_ams_id?: string;
  bound_dev_id?: string;
}

// Preset data for brand/type/series cascading selectors
export interface PresetSeries {
  name: string;
  series: string[];
  items?: Array<{
    series: string;
    filament_id: string;
  }>;
}

export interface PresetVendor {
  name: string;
  types: PresetSeries[];
}

export interface PresetOptions {
  vendors: PresetVendor[];
}

// Machine list
export interface MachineItem {
  dev_id: string;
  dev_name: string;
  is_online: boolean;
  // True when the printer is in LAN mode. Surfaced so the Printer dropdown
  // can follow SelectMachineDialog's "<dev_name>(LAN)" display convention.
  is_lan?: boolean;
}

// AMS data
// NOTE: `weight` and `diameter` are reported as *strings* by the device
// (C++ DevFilaSystem::weight / diameter are std::string, mirroring the MQTT
// `tray_weight` / `diameter` fields); accept `number | string` to remain
// tolerant to both wire formats. Consumers must coerce via parseInt/parseFloat.
export interface AmsTray {
  slot_id: string;
  is_exists: boolean;
  tag_uid?: string;
  setting_id?: string;
  fila_type?: string;
  sub_brands?: string;
  color?: string;
  weight?: number | string;
  remain?: number;
  diameter?: number | string;
  is_bbl?: boolean;
}

export interface AmsUnit {
  ams_id: string;
  ams_type: number;
  trays: AmsTray[];
}

export interface AmsData {
  selected_dev_id: string;
  ams_units: AmsUnit[];
}

// Init response
export interface InitData {
  theme: 'dark' | 'light';
  spools: Spool[];
  presets: PresetOptions;
  cloud_sync?: CloudSyncState;
  debug_enabled?: boolean;
}

// Cloud sync state — mirrors FilaManagerVM::build_sync_state() payload
export interface CloudSyncError {
  code: number;
  message: string;
}

export interface CloudSyncState {
  logged_in: boolean;
  is_syncing: boolean;
  is_pulling: boolean;
  last_synced_at: string;   // ISO-8601 UTC, empty if never synced
  last_error: CloudSyncError;
}

// Cloud sync history — one row per pull_done / push_done / push_failed the
// C++ dispatcher reports. Kept independent of debugEnabled so it works in
// both Release and Debug builds, and capped to the most recent entries.
export type CloudSyncHistoryKind = 'pull' | 'push';
export type CloudSyncHistoryOp = 'create' | 'update' | 'delete' | '';
export type CloudSyncHistoryStatus = 'ok' | 'error';

export interface CloudSyncHistoryEntry {
  id: number;      // monotonic, used as React key
  ts: number;      // ms epoch, captured when the web layer observed the event
  kind: CloudSyncHistoryKind;
  op: CloudSyncHistoryOp;
  status: CloudSyncHistoryStatus;
  // Ready-to-render short description, already localized by the caller.
  summary: string;
  // Optional machine-readable detail (e.g. added/updated counts, error code).
  detail?: Record<string, unknown>;
}

// Toast message surfaced by push_failed reports
export interface CloudToast {
  id: number;                // monotonically increasing, used as React key
  level: 'info' | 'warn' | 'error';
  text: string;
  op?: string;               // "create" | "update" | "delete"
  spool_id?: string;
}

// Cloud filament config — raw shape from GET /filament/config
export interface CloudFilamentConfig {
  categories?: string[];
  filamentSettings?: Array<{
    filamentVendor: string;
    filamentType: string;
    filamentName: string;
    filamentId: string;
    isSupport: boolean;
  }>;
  // Normalized compatibility fields consumed by the current UI.
  vendors?: string[];
  types?: Array<{ vendor: string; type: string; series?: string[] }>;
  [key: string]: unknown;
}

export type DebugLogCategory = 'data' | 'bridge' | 'http';
export type DebugLogLevel = 'info' | 'warn' | 'error';
export type DebugLogFilter = 'all' | DebugLogCategory;

export interface DebugLogEntry {
  id: number;
  ts: number;
  category: DebugLogCategory;
  level: DebugLogLevel;
  title: string;
  summary: string;
  detail?: unknown;
}

// Bridge response body (DeviceWeb protocol)
export interface BridgeResponseBody {
  module: string;
  submod: string;
  action: string;
  error_code: number;
  message: string;
  payload: Record<string, unknown>;
}
