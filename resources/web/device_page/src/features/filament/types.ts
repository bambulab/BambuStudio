// Spool object — matches C++ FilamentSpool / wgtFilaManagerStore
export interface Spool {
  spool_id: string;
  cloud_synced?: boolean;
  brand: string;
  material_type: string;
  series: string;
  color_code: string;
  color_name: string;
  // STUDIO-17977: gradient / multicolor support (mirrors C++ FilamentSpool).
  //   color_type: 0 = gradient, 1 = multicolor, 2 = single (swagger layout)
  // Both fields are optional so legacy `spools.json` payloads (no colors/
  // color_type keys) still parse cleanly via the C++ from_json fallback.
  colors?: string[];
  color_type?: 0 | 1 | 2;
  diameter: number;
  initial_weight: number;
  net_weight?: number;
  spool_weight: number;
  unit_price?: number;
  dry_reminder_days?: number;
  dry_date?: string;
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
    setting_id?: string;
    name?: string;
    // STUDIO-18110 / STUDIO-18117: true 表示该 item 来自用户自建 / 云端 pull
    // 同步过来的 preset（!is_system && !is_default）。下拉选项仍要展示它们，
    // 但 AMS RFID setting_id 反查路径必须跳过，避免命中后让 series 被
    // unsanitized user alias 污染。
    is_user?: boolean;
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
  // STUDIO-17977: AMS-side multicolor / gradient hex list and type,
  // forwarded by FilaManagerVM::build_ams_data.  Same swagger layout as
  // FilamentSpool.color_type (0 = gradient / 1 = multicolor / 2 = single).
  colors?: string[];
  color_type?: 0 | 1 | 2;
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

// STUDIO-18155 / openspec 20260506耗材管理器AMS自动同步云端：
// AMS 自动同步路径完成 / 手动 push_all_now 完成后 C++ 通过
// `submod=sync, action=auto_push_summary` 报告本次决策摘要。
//   - trigger='auto'   表示来自 AMS sync 流程（设备 mqtt push 触发）
//   - trigger='manual' 表示来自 StatsView "推送本地到云端" 按钮
// 摘要仅在 pushed > 0（auto 模式）或总是（manual 模式）触发，
// 这样 AMS 全 skipped 时 UI 不会被无意义的 0 推送 toast 打扰。
export interface CloudAutoPushSummary {
  trigger: 'auto' | 'manual';
  // 'busy' / 'idle'（auto 触发时）或 'manual'（手动触发时）。
  // UI 用它解释"为啥被节流"。
  device_state: 'busy' | 'idle' | 'manual';
  pushed: number;
  skipped_cooldown: number;
  skipped_no_diff: number;
  skipped_no_rfid: number;
  // 仅 manual trigger 携带：跳过的"未填整卷净重 spool"数。
  skipped_no_total_nw?: number;
  // Web 层观察到 ReportMsg 时打的本地 ms epoch，用作"最近一次"展示。
  observed_at: number;
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

// STUDIO-17977 / Task 10: candidate color list backed by FilamentColorCodeQuery.
// `name` is already localized to the active app language by the C++ side, so
// the front-end never carries its own colour-name translation table.
//
// Wire schema: response of the JSON-RPC action
//   { module: 'filament', submod: 'colors', action: 'query_for_id',
//     payload: { fila_id: string } }
// (see openspec design.md §9.4). The C++ side internally maps the
// FilamentColor::ColorType enum onto the swagger semantics used everywhere
// else in this layer (FilamentSpool::color_type / AmsTray::color_type), so
// front-end consumers can treat 0/1/2 as "gradient/multicolor/single"
// without knowing about the upstream FilamentColor enum mismatch.
export interface CandidateColor {
  color_code: string;       // eg. "Q01B00"
  colors: string[];         // hex list, #RRGGBB
  color_type: 0 | 1 | 2;    // swagger semantics: 0=gradient / 1=multicolor / 2=single
  name: string;
}

export interface FilamentColorCodesResponse {
  fila_id: string;
  fila_type: string;
  candidates: CandidateColor[];
}
