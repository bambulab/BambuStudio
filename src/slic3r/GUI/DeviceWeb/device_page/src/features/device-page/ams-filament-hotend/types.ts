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

  card_state: AmsFilamentHotendCardState;
  line_state: AmsFilamentHotendLineState;
  nozzle_state: AmsFilamentHotendNozzleState;

  filament_name: string;     // e.g. "PLA Basic"
  filament_type: string;     // e.g. "PLA" -- preferred for the compact 31558 layout
  color: string;             // primary hex; legacy single-color spools use this
  color_type?: AmsFilamentHotendColorType; // single / multi / gradient
  status_text: string;       // e.g. "待进料"; pre-rendered server-side for i18n
  colors?: string[];         // multi-color filament (>=2 segments). [0] === color.

  progress_pct?: number;     // 0..100 -- partial arc on the spool

  selected?: boolean;        // user UI selection (red border + green index pill)
  is_on_extruder?: boolean;  // current hotend is physically mounted on the extruder
  // True when this slot is currently fed by an external spool (Ext slot)
  // rather than an AMS slot. The hotend card shows an "Ext" badge next to
  // the slot index when this flag is set. Optional for backward compatibility.
  is_external_spool?: boolean;
  is_running?: boolean;      // batch flow currently executing this slot (S6)
  in_dryer?: boolean;        // physically inside the dryer chamber
  dryer_humidity_pct?: number; // -1 means invalid / unsupported
  dryer_status_text?: string;  // "Idle" / "Drying" / "Cooling" ...
  is_drying?: boolean;       // chamber actively drying (highlight the chamber bg)

  hotend?: HotendSlotInfo;
}

export interface HotendRackStatus {
  active: boolean;
  mode: 'idle' | 'single' | 'all';
  display_index: number;
  current: number;
  total: number;
  message: string;
  tips: string;
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
  mode: 'idle' | 'single' | 'batch_reserved';
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
}

export interface AmsFilamentHotendActionState {
  can_dry: boolean;
  can_unload: boolean;

  /** @deprecated  Prefer can_load_external + can_load_auto on new bridges. */
  can_load?: boolean;
  can_load_external?: boolean;
  can_load_auto?: boolean;
  can_read_all_hotends?: boolean;
  read_all_disabled_reason?: string;

  show_ams_settings: boolean;
  show_auto_refill: boolean;
  tips: string;
}

export interface AmsFilamentHotendDryerZone {
  ams_id?: string;
  zone_id?: number;
  zone_slots?: Array<number | string>;
  humidity?: number;
  humidity_raw?: number;
  dry_time?: number;
  dry_status?: string;
  dry_sub_status?: string;
  info?: string;
}

export interface AmsFilamentHotendSlotActionPayload extends Record<string, unknown> {
  target_temp?: number;
}

export interface AmsFilamentHotendViewModel {
  selected_dev_id: string;
  ams?: {
    units: Array<{
      ams_id: string;
      ams_type?: string;
      slot_count?: number;
      exist?: boolean;
      temp?: number;
      humidity?: number;
      humidity_raw?: number;
      dry_time?: number;
      dry_status?: string;
      dry_sub_status?: string;
      trays: Array<{
        ams_id: string;
        slot_id: string;
        exists: boolean;
        tray_state: 'empty' | 'reading' | 'ready' | 'unknown' | 'error';
        filament_type: string;
        filament_name: string;
        color: string;
        color_type?: AmsFilamentHotendColorType;
        colors?: string[];
        reading?: boolean;
        remain?: number;
      }>;
    }>;
    zones?: AmsFilamentHotendDryerZone[];
  };
  hotend_slots: AmsFilamentHotendSlotViewModel[];
  hotend_rack_status?: HotendRackStatus;
  filament_change_status?: FilamentChangeStatus;
  actions: AmsFilamentHotendActionState;
}

export interface BridgeResponseBody<T = unknown> {
  module: string;
  submod: string;
  action: string;
  error_code: number;
  message?: string;
  payload?: T;
}
