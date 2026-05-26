// View-model types for the AMS / hotend Web panel.

export type AmsFilamentHotendCardState =
  | 'Empty'        // 空槽:        灰盘 + 斜杠
  | 'Reading'      // 读取中:      灰盘 + 顶部 1/4 弧
  | 'Unknown'      // 未知耗材:    灰盘+浅芯+右侧缺口; 主文案 ?
  | 'ReadyToLoad'  // 已就绪待进料: 单色甜甜圈
  | 'ReadyToUse'   // 已进料:      单色甜甜圈 + 内圈高亮
  | 'Loading'      // 进料中:      单色甜甜圈 + 进度弧
  | 'Unloading'    // 退料中:      单色甜甜圈 + 反向进度弧
  | 'Error';       // 错误态:      红色甜甜圈 + 中心 X

export type AmsFilamentHotendLineState =
  | 'no_filament'    // 无料: tube exists but has no filament loaded
  | 'loaded'         // 已进料
  | 'loading'        // 进料中
  | 'unloading'      // 退料中
  | 'no_hotend';     // 无热端: hide the tube but keep layout spacing

export type AmsFilamentHotendNozzleState =
  | 'Normal'    // 普通灰色
  | 'Active'    // 当前正在使用,绿色
  | 'Unknown'   // 未连接
  | 'Error';    // 红色

export type AmsFilamentHotendTab = 'filament' | 'hotend';
export type AmsFilamentHotendColorType = 'single' | 'multi' | 'gradient';

export type HotendOpState =
  | 'Idle'        // 普通 normal: 温度静止
  | 'Heating'     // 加热中:    温度上升箭头
  | 'Cooling'     // 降温中:    温度下降箭头
  | 'Reading'     // 读取中:    Reading 弧形
  | 'Taking'      // 取用中:    工具头正在取用
  | 'Placing'     // 放置中:    工具头正在装上
  | 'Removing'    // 取下中:    工具头正在卸下
  | 'Error';      // 错误态

export type HotendToolKind =
  | 'nozzle'   // 普通喷嘴
  | 'cutter'   // 切刀
  | 'pen'      // 荧光笔/标记笔
  | 'unknown'  // 未识别工具
  | 'empty';   // 空槽

export interface HotendSlotInfo {
  current_temp?: number | null; // 当前温度, deg C; null / undefined => "--"
  target_temp?: number;      // 目标温度, deg C; 0 / undefined => "0℃"
  op_state?: HotendOpState;  // 操作态
  tool_kind?: HotendToolKind;
  nozzle_label?: string;     // 主标签, e.g. "0.4 mm Std" / "刀名?" / "荧光笔"
  nozzle_subtext?: string;   // 副标签, optional
  selected?: boolean;        // hotend tab 自己的选中态(可与 filament tab 独立)
}

// Filament card render group (spool disc + labels). Mirrors C++ keys::filament_card.
export interface FilamentCardInfo {
  state: AmsFilamentHotendCardState;          // was slot.card_state
  color: string;                              // hex; #D9D9D9 = unknown
  color_type?: AmsFilamentHotendColorType;
  colors?: string[];
  name: string;                               // was slot.filament_name
  type: string;                               // was slot.filament_type
  remain?: number;                            // 0..100; default 100
  status_text: string;
}

// Feed-line render group. Mirrors C++ keys::filament_line.
export interface FilamentLineInfo {
  state: AmsFilamentHotendLineState;          // was slot.line_state
  has_filament?: boolean;                     // true iff loaded/loading/unloading
  // Color of filament currently in the tube; empty when no filament. Today
  // mirrors filament_card.color because C++ has no separate signal yet.
  filament_color?: string;
}

// Nozzle render group. Mirrors C++ keys::filament_nozzle. `has_filament` is
// the direct mirror of DevNozzle::HasFilamentInNozzle().
export interface FilamentNozzleInfo {
  state: AmsFilamentHotendNozzleState;        // was slot.nozzle_state
  has_filament?: boolean;                     // was slot.hotend.has_filament_in_nozzle
  // Color of filament currently inside the nozzle; empty when no filament.
  filament_color?: string;
  // True when this rack nozzle is the one mounted on the extruder right now.
  on_extruder?: boolean;
}

/**
 * Slot payload consumed by the Filament tab only. Mirrors C++
 * `state.filament_page.hotend_slots[]`. Hotend tool fields live on the
 * separate {@link HotendSlotViewModel}.
 */
export interface AmsFilamentHotendSlotViewModel {
  nozzle_pos: number;
  display_index: number;
  ams_id: string;
  slot_id: string;
  bind_ams?: Array<{
    ams_id: string;
    ams_type?: string;
    slot_count: number;
  }>;
  bind_ams_slot_count?: number;
  is_top_tier: boolean;
  enabled: boolean;
  menu_actions?: {
    can_edit?: boolean;
    can_load?: boolean;
    can_unload?: boolean;
    can_read?: boolean;
    can_ams_operation?: boolean;
  };
  /**
   * Backend data for the AMS Operation dialog of this slot. When
   * `can_ams_operation === true` C++ ships the bound AMS units (with trays)
   * here; this is the sole source for the dialog (there is no top-level
   * units array anymore).
   */
  ams_operation?: {
    available?: boolean;
    units: AmsUnitInfo[];
  };

  // Grouped render zones. C++ guarantees these objects always exist on a
  // live payload; mock data should populate them too.
  filament_card: FilamentCardInfo;
  filament_line: FilamentLineInfo;
  filament_nozzle: FilamentNozzleInfo;

  selected?: boolean;        // user UI selection (red border + green index pill)
  // True when this slot is currently fed by an external spool (Ext slot)
  // rather than an AMS slot. The hotend card shows an "Ext" badge next to
  // the slot index when this flag is set. Optional for backward compatibility.
  is_external_spool?: boolean;
  is_running?: boolean;      // batch flow currently executing this slot (S6)
  /** Batch-mode-only: per-mode selection eligibility for the card checkbox.
   *  Absent in normal mode. */
  select_action?: BatchSelectAction;
}

/** Four UI modes the filament_page can render. The default `normal` mirrors
 *  the current behaviour; the three batch_* modes are entered by clicking
 *  the respective toolbar button. `Dry` is not a mode (it opens
 *  AMSDryCtrWin directly). */
export type FilamentPageMode =
  | 'normal'
  | 'batch_ams_load'
  | 'batch_ext_load'
  | 'batch_unload';

/** Per-slot eligibility for the active batch mode. The Web renders a
 *  checkbox for cards with `can_select`, greys out the rest and shows
 *  `reason` as a tooltip. `ext_label` lights the EXT badge for external-spool
 *  cards in `batch_ext_load`. */
export interface BatchSelectAction {
  can_select: boolean;
  reason: string;
  ext_label?: boolean;
}

/** Wrapper for `filament_page.actions.<name>` when the page is in a batch
 *  mode — same key set as normal but value is `{enabled, reason}` instead
 *  of a plain bool. */
export interface BatchActionState {
  enabled: boolean;
  reason: string;
}

/** Slot payload consumed by the Hotend tab only. Mirrors C++
 *  `state.hotend_page.hotend_slots[]`. */
export interface HotendSlotViewModel {
  nozzle_pos: number;
  display_index: number;
  ams_id: string;
  slot_id: string;
  enabled: boolean;
  /** True when this rack nozzle is currently mounted on the extruder; the
   *  Hotend tab card shows a "Taken" overlay instead of the live temperature. */
  taken?: boolean;
  hotend: HotendSlotInfo;
}

export interface HotendRackStatus {
  active: boolean;
  mode: 'idle' | 'single' | 'all';
  display_index: number;
  current: number;
  total: number;
  message: string;
  tips: string;
  job?: number;
  operation?: 'idle' | 'take' | 'place' | 'exchange' | 'read_all' | 'read_single' | 'move';
  state?: 'idle' | 'running' | 'failed';
  target_display_index?: number;
  current_step?: number | null;
  steps?: Array<{
    id: number;
    label: string;
    state: 'done' | 'active' | 'pending' | 'failed';
  }>;
  can_stop?: boolean;
  can_retry?: boolean;
}

export interface FilamentChangeSlotRef {
  ams_id: string;
  slot_id: string;
  display_index: number;
}

export interface FilamentChangeBatchReservedItem {
  display_index: number;
  state: 'pending' | 'running' | 'success' | 'failed';
  reason?: string;
}

export interface FilamentChangeBatchReserved {
  operation?: 'load' | 'unload' | 'change';
  current?: number;
  total?: number;
  items?: FilamentChangeBatchReservedItem[];
}

export interface FilamentChangeStatus {
  active: boolean;
  // `batch_reserved` is the pre-run state (queue listed, nothing started yet);
  // `batch_running` is the live multi-slot run with the same item shape but
  // a horizontal step strip rendering. Single-slot runs (or batches that the
  // firmware reports as a 1-element queue) keep `single`.
  mode: 'idle' | 'single' | 'batch_reserved' | 'batch_running';
  operation?: 'change' | 'unknown';
  running_slot?: FilamentChangeSlotRef | null;
  current_step?: number | null;
  current_step_name?: string;
  steps?: number[];
  step_labels?: string[];
  current_step_index?: number;
  current_step_total?: number;
  message?: string;
  tips?: string;
  batch_reserved?: FilamentChangeBatchReserved;
  batch_running?: FilamentChangeBatchReserved;
}

/** state.filament_page.actions */
export interface FilamentPageActionState {
  can_dry: boolean;
  can_unload: boolean;
  /** @deprecated Prefer can_load_external + can_load_auto on new bridges. */
  can_load?: boolean;
  can_load_external?: boolean;
  can_load_auto?: boolean;
  show_ams_settings: boolean;
  show_auto_refill: boolean;
  tips: string;
}

/** state.hotend_page.actions */
export interface HotendPageActionState {
  can_read_all_hotends?: boolean;
  read_all_disabled_reason?: string;
}

export interface AmsTrayInfo {
  ams_id: string;
  slot_id: string;
  exists: boolean;
  // tray_state == 'reading' subsumes the old top-level `reading` boolean,
  // which C++ no longer emits.
  tray_state: 'empty' | 'reading' | 'ready' | 'unknown' | 'error';
  filament_type: string;
  filament_name: string;
  color: string;
  color_type?: AmsFilamentHotendColorType;
  colors?: string[];
  remain?: number;
  // Per-tray menu controls for the AMS Operation dialog. Only items with a
  // truthy flag are rendered; default is read-only when actions is missing.
  actions?: {
    can_load?: boolean;
    can_unload?: boolean;
    can_read?: boolean;
  };
}

export interface AmsUnitInfo {
  ams_id: string;
  ams_type?: string;
  slot_count?: number;
  // Only `humidity_raw` (0..100 percent) and `dry_status` are surfaced; the
  // 0..5 humidity level, `temp`, `dry_time`, `dry_sub_status` and `exist`
  // were never consumed by any component so they no longer travel on the wire.
  humidity_raw?: number;
  dry_status?: string;
  trays: AmsTrayInfo[];
}

/**
 * One dryer chamber. The DryerChamber component renders one decoration per
 * zone, wrapping the slot columns whose `(ams_id, slot_id)` matches an entry
 * in `zone_slots`.
 *
 * - `dry_status`      — untranslated English enum used for logic (`!== 'Idle'`).
 * - `dry_status_text` — translated user-facing label shown in the chamber chip.
 */
export interface AmsFilamentHotendDryerZone {
  ams_id?: string;
  zone_slots?: Array<number | string>;  // native AMS slot_id values
  humidity_raw?: number;
  dry_time?: number;
  dry_status?: string;
  dry_status_text?: string;
  temp?: number;
}

export interface AmsFilamentHotendSlotActionPayload extends Record<string, unknown> {
  target_temp?: number;
}

export interface FilamentPageViewModel {
  /** Which UI mode this payload was built for. Defaults to "normal" on
   *  older C++ payloads that pre-date the mode field. */
  mode?: FilamentPageMode;
  /** In normal mode the values are plain booleans / strings (matches
   *  FilamentPageActionState). In batch modes the same keys hold
   *  `{enabled, reason}` BatchActionState objects instead — consumers
   *  unpack at runtime (see AmsFilamentHotendPage.fpActionsAsBool). The
   *  declared type stays the strict normal-mode shape so TypeScript can
   *  still catch normal-mode misuses; the union shape lives in C++ schema. */
  actions: FilamentPageActionState;
  filament_change_status?: FilamentChangeStatus;
  // Flat dryer-zone signal lifted from the old `ams.zones`. AMS-unit detail
  // ships per-slot via `hotend_slots[].ams_operation.units` instead of being
  // duplicated at the page root.
  // Only zones that physically support drying are listed here (C++ filter).
  ams_dry_zones?: AmsFilamentHotendDryerZone[];
  hotend_slots: AmsFilamentHotendSlotViewModel[];
}

export interface HotendPageViewModel {
  actions: HotendPageActionState;
  hotend_rack_status?: HotendRackStatus;
  hotend_slots: HotendSlotViewModel[];
}

export interface AmsFilamentHotendViewModel {
  filament_page: FilamentPageViewModel;
  hotend_page: HotendPageViewModel;
}

export interface BridgeResponseBody<T = unknown> {
  module: string;
  submod: string;
  action: string;
  error_code: number;
  message?: string;
  payload?: T;
}
