// View-model types for the AMS / hotend Web panel.

export type AmsFilamentHotendCardState =
  | 'Empty'        // 空槽:        灰盘 + 斜杠
  | 'AwaitingTpuRead' // TPU285 外挂 RFID 待读取: 灰盘 + 刷新图标
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
  | 'None'      // 未插喷嘴/工具: 虚线框 + 禁止圆
  | 'Error'     // 红色
  | 'Lost'
  | 'FeedSuspect' // 进料怀疑: 橙色 ?
  | 'Unknown';    // 未知热端: 灰色 ?

export type AmsFilamentHotendTab = 'filament' | 'hotend';
export type AmsFilamentHotendColorType = 'single' | 'multi' | 'gradient';

// Health states mirror AmsFilamentHotendNozzleState (shared C++ resolver).
// Rack jobs are hotend-only and override health while a job runs.
export type HotendOpState =
  // 健康态, 与耗材页同源
  | 'Normal'      // 普通 normal
  | 'Active'      // 当前正在使用
  | 'None'        // 未插喷嘴/工具
  | 'Error'       // 错误态
  | 'Lost'        // 工具有连接但已掉落: 喷嘴体叠加灰色 "!" 徽标
  | 'FeedSuspect' // 进料可疑: 热端页不配图标, 按 Normal 渲染
  | 'Unknown'     // 喷嘴类型未知: 喷嘴体叠加灰色 "?" 徽标, 标签显示 --
  // 刀架作业态, 仅热端页
  | 'Reading'     // 读取中:    Reading 弧形
  | 'Taking'      // 取用中:    工具头正在取用
  | 'Placing'     // 放置中:    工具头正在装上
  | 'Removing';   // 取下中:    工具头正在卸下; 后端暂无信号驱动

// Split from op_state so a fault does not hide the heating / cooling arrow.
export type HotendTempTrend = 'Steady' | 'Heating' | 'Cooling';

export type HotendToolKind =
  | 'nozzle'   // 普通喷嘴
  | 'cutter'   // 切刀
  | 'laser'    // 激光
  | 'fan'      // 风扇
  | 'pen'      // 荧光笔/标记笔
  | 'unknown'  // 未识别工具
  | 'empty';   // 空槽

export interface HotendSlotInfo {
  current_temp?: number | null; // 当前温度, deg C; null / undefined => "--"
  target_temp?: number;      // 目标温度, deg C; 0 / undefined => "0℃"
  max_temp?: number;         // 目标温度上限, deg C; 超出时截断到该值; undefined => 350
  op_state?: HotendOpState;  // 健康态 / 刀架作业态
  temp_trend?: HotendTempTrend; // 温度趋势, 与 op_state 正交
  tool_kind?: HotendToolKind;
  nozzle_label?: string;     // 主标签, e.g. "0.4 mm Std" / "刀名?" / "荧光笔"
  nozzle_subtext?: string;   // 副标签, optional
  selected?: boolean;        // hotend tab 自己的选中态(可与 filament tab 独立)
  tooltip?: string;          // 卡片 hover 提示(如 2D 工具); 空则不显示
}

export interface HotendDetailDialogInfo {
  filament_name?: string;    // detail dialog: filament currently associated with this hotend
  filament_color?: string;
  show_filament?: boolean;
  tool_image_name?: 'hotend_detail_nozzle' | 'empty' | string;
  tool_type?: string;
  nozzle_type?: string;      // detail dialog: diameter + flow + material/type
  firmware_version?: string;
  serial_number?: string;
  used_time_hours?: string | null;
  switch_count?: number | null;
}

// Filament card render group (spool disc + labels). Mirrors C++ keys::filament_card.
export interface FilamentCardInfo {
  state: AmsFilamentHotendCardState;          // was slot.card_state
  color: string;                              // hex; #D9D9D9 = unknown
  color_type?: AmsFilamentHotendColorType;
  colors?: string[];
  prefeed_indicator?: {
    visible?: boolean;
    colors?: string[];
    color_type?: AmsFilamentHotendColorType;
    /** Preload tray filament type; empty string means unknown (show "?" marker). */
    prefeed_fila_type?: string;
  };
  text: string;                               // primary card text controlled by C++
  name: string;                               // was slot.filament_name
  type: string;                               // was slot.filament_type
  remain?: number;                            // 0..100; default 100
  status_text: string;
  /** True when a newly auto-recorded official filament badge is pending for this slot. */
  new_filament_hint?: boolean;
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
  // Tool type in this slot; picks cutter/pen silhouette over the default nozzle.
  tool_kind?: HotendToolKind;
  // Hover text for alert states; empty when the nozzle has nothing to report.
  tooltip?: string;
  // Caption under the nozzle. Same meaning as HotendSlotInfo.nozzle_label /
  // nozzle_subtext, but the diameter is bare, e.g. "0.4" + "HF".
  nozzle_label?: string;
  nozzle_subtext?: string;
}

/**
 * Slot payload consumed by the Filament tab only. Mirrors C++
 * `state.filament_page.hotend_slots[]`. Hotend tool fields live on the
 * separate {@link HotendSlotViewModel}.
 */
export interface AmsFilamentHotendSlotViewModel {
  addr_id: number;
  display_index: number;
  ams_id: string;
  slot_id: string;
  bind_ams?: Array<{
    ams_id: string;
    ams_type?: string;
    slot_count: number;
  }>;
  /** Number of AMS units bound to this nozzle (not their combined slot count). */
  bind_ams_count?: number;
  is_top_tier: boolean;
  enabled: boolean;
  menu_actions?: {
    can_edit?: boolean;
    can_load?: boolean;
    can_unload?: boolean;
    can_read?: boolean;
    /** i18n key: "Read" (unknown type) or "Reread" (known type). Built by C++ Schema. */
    read_label?: 'Read' | 'Reread';
    can_ams_operation?: boolean;
  };
  /**
   * Lightweight availability flag for the AMS Operation menu. Full dialog data
   * lives in `ams_ext_operation_page.slots[]`.
   */
  ams_operation?: {
    available?: boolean;
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
  addr_id: number;
  display_index: number;
  ams_id: string;
  slot_id: string;
  enabled: boolean;
  menu_actions?: {
    can_read?: boolean;
    can_detail?: boolean;
    can_set_temperature?: boolean;
    can_take_place?: boolean;
    /** Why fault-disabled items are greyed out (Lost / Error); shown on hover. */
    disabled_reason?: string;
  };
  /** True when this rack nozzle is mounted on the extruder; Web shows Taken UX. */
  taken?: boolean;
  /** Filament inside the nozzle; the card tints the nozzle filament with it. */
  filament_nozzle?: FilamentNozzleInfo;
  hotend: HotendSlotInfo;
  hotend_detail_dialog?: HotendDetailDialogInfo;
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
  // Gate for Retry visibility: Web renders the button only when true.
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

export interface FilamentManualPullItem {
  ams_id: string;
  slot_id: string;
  display_index: number;
  filament_name?: string;
  filament_type?: string;
  color?: string;
  color_type?: AmsFilamentHotendColorType;
  state: 'pending' | 'success' | 'failed';
  reason?: string;
}

export interface FilamentManualPullStatus {
  title?: string;
  items?: FilamentManualPullItem[];
  can_exit?: boolean;
  can_close?: boolean;
}

export interface FilamentChangeStep {
  id: number;
  label?: string;
  step_style?: 'manual_load' | 'maunal_unload' | string;
  step_style_text?: string;
  step_text?: string;
}

export interface FilamentChangeStatus {
  active: boolean;
  // `batch_reserved` is the pre-run state (queue listed, nothing started yet);
  // `batch_running` is the live multi-slot run with the same item shape but
  // a horizontal step strip rendering. Single-slot runs (or batches that the
  // firmware reports as a 1-element queue) keep `single`.
  mode: 'idle' | 'single' | 'batch_reserved' | 'batch_running' | 'batch_manual_pull';
  // `load` / `unload` carry the direction of a single-slot change; `change` is
  // the direction-agnostic fallback used by batch runs.
  operation?: 'change' | 'load' | 'unload' | 'unknown';
  running_slot?: FilamentChangeSlotRef | null;
  current_step?: number | null;
  current_step_name?: string;
  steps?: Array<number | FilamentChangeStep>;
  step_labels?: string[];
  current_step_index?: number;
  current_step_total?: number;
  message?: string;
  tips?: string;
  // Gate for the Stop button: only true while active AND the printer is not
  // printing/calibrating. Absent on legacy bridges, so treat undefined as
  // "stoppable" to preserve the previous always-enabled behavior.
  can_stop?: boolean;
  // Gate for the Retry button: only true while active AND ams.stat reports
  // abnormal AMS task status. Web shows the button only when this is true.
  can_retry?: boolean;
  batch_reserved?: FilamentChangeBatchReserved;
  batch_running?: FilamentChangeBatchReserved;
  manual_pull?: FilamentManualPullStatus;
}

/** state.filament_page.actions */
export interface FilamentPageActionState {
  can_dry: boolean;
  can_unload: boolean;
  /** @deprecated Prefer can_load_external + can_show_load_auto on new bridges. */
  can_load?: boolean;
  can_load_external?: boolean;
  // Controls whether the Auto Load button is rendered at all (show/hide), not
  // just its enabled state. Undefined on legacy bridges → treated as shown.
  can_show_load_auto?: boolean;
  show_ams_settings: boolean;
  show_auto_refill: boolean;
  // Rack-B-only: whether the toolbar "read all" icon button is shown.
  show_read_all?: boolean;
  // Rack-B-only: enabled state of the "read all" icon (connected ZMULTI_B AMS
  // and printer not printing/upgrading).
  can_read_all_slots?: boolean;
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
    /** i18n key: "Read" (unknown type) or "Reread" (known type). Built by C++ Schema. */
    read_label?: 'Read' | 'Reread';
  };
}

export interface AmsUnitInfo {
  ams_id: string;
  ams_type?: string;
  slot_count?: number;
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
  /** Rack positions of `zone_slots`, matched against `slot.addr_id`. Empty
   *  when the rack cannot be resolved; see Schema.hpp `dryer_zone`. */
  zone_addrs?: Array<number | string>;
  humidity_raw?: number;
  dry_time?: number;
  dry_status?: string;
  dry_status_text?: string;
  temp?: number;
}

/**
 * AMS-unit level temperature / humidity. Covers the whole unit rather than a
 * single chamber, so it is matched by `ams_id` alone and rendered as a bare
 * readout anchored to the unit's leftmost column — no chamber background and
 * no dry state.
 *
 * - `humidity_raw` — -1 when the device has not reported a valid value.
 * - `temp`         — null / undefined when no valid temperature is available.
 */
export interface AmsFilamentHotendEnvUnit {
  ams_id?: string;
  /** Rack positions of the non-drying zone slots, matched against `slot.addr_id`. */
  zone_addrs?: Array<number | string>;
  humidity_raw?: number;
  temp?: number | null;
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
  // Unit-level temperature / humidity for AMS units that are not entirely a
  // dryer chamber.
  ams_env_units?: AmsFilamentHotendEnvUnit[];
  show_ams_env_units?: boolean;
  hotend_slots: AmsFilamentHotendSlotViewModel[];
}

export interface HotendPageViewModel {
  actions: HotendPageActionState;
  hotend_rack_status?: HotendRackStatus;
  hotend_slots: HotendSlotViewModel[];
}

export interface AmsExtOperationSlotViewModel {
  addr_id: number;
  display_index: number;
  ams_id: string;
  slot_id: string;
  bind_ams?: Array<{
    ams_id: string;
    ams_type?: string;
    slot_count: number;
  }>;
  bind_ams_count?: number;
  units: AmsUnitInfo[];
}

export interface AmsExtOperationPageViewModel {
  slots: AmsExtOperationSlotViewModel[];
}

export interface AmsFilamentHotendViewModel {
  filament_page: FilamentPageViewModel;
  hotend_page: HotendPageViewModel;
  ams_ext_operation_page?: AmsExtOperationPageViewModel;
}

export interface BridgeResponseBody<T = unknown> {
  module: string;
  submod: string;
  action: string;
  error_code: number;
  message?: string;
  payload?: T;
}
