export type AmsColorType = 0 | 1 | 2; // DevFilaColorType: 0 gradient, 1 multi, 2 single

export interface AmsListTray {
  ams_id: string;
  slot_id: string;
  tray_label: string;
  is_exists: boolean;
  fila_type: string;
  sub_brands: string;
  setting_id: string;
  tag_uid: string;
  color: string;
  colors: string[];
  color_type: AmsColorType;
  remain: number;
  remain_g: number;
  is_bbl: boolean;
  reading: boolean;
  info_ready: boolean;
  binded_extruder_ids: number[];
  current_extruder_id: number;
  switcher_port: string;
}

export type HumidityDisplayType = 'none' | 'level' | 'percent';

export interface AmsListUnit {
  ams_id: string;
  ams_type: number;
  ams_type_name: string;
  // N9 AMS-Lite reports itself as AMS_LITE; only this flag tells it apart.
  is_ams_lite_mixed: boolean;
  humidity_level: number;
  humidity_percent: number;
  humidity_display_type: HumidityDisplayType;
  binded_extruder_ids: number[];
  switcher_port: string;
  trays: AmsListTray[];
}

export interface AmsListData {
  selected_dev_id: string;
  ams_units: AmsListUnit[];
  ext_slots: AmsListTray[];
  detect_remain_enabled: boolean;
  support_ams_humidity: boolean;
}

export interface AmsControlActions {
  show_auto_refill: boolean;
  show_settings: boolean;
  can_load: boolean;
  can_unload: boolean;
  load_tips: string;
  unload_tips: string;
}

export type SlotState = 'none' | 'brand' | 'third_brand' | 'empty' | 'virtual';
export type LinkState = 'idle' | 'loaded' | 'loading' | 'unloading';
export type LayoutStyle = 'single' | 'left_right';
export type PanelPos = 'single' | 'left' | 'right';
export type ExtruderState = 'idle' | 'active' | 'loading';

export interface MenuActions {
  show_edit: boolean;
  show_read: boolean;
  show_filament_mgr_hint: boolean;
}

export interface SlotRemainLine {
  show_line: boolean;
  remain_percent: number;
}

export interface SlotView {
  ams_id: string;
  slot_id: string;
  label: string;
  slot_state: SlotState;
  color: string;
  colors: string[];
  color_type: AmsColorType;
  remain: number;
  show_remain: boolean;
  // When true the card fill height encodes remain. Default off: full-bleed colour.
  show_remain_height: boolean;
  fila_type: string;
  selected: boolean;
  loaded: boolean;
  reading: boolean;
  show_rfid: boolean;
  // The spool is in but its info is not trustworthy yet, so the card shows '?'.
  show_unknown: boolean;
  // PA Factor K. Empty / false on Lite cards and when C++ would hide the line.
  k_text: string;
  k_loading: boolean;
  k_loading_text: string;
  menu_actions: MenuActions;
  // Capsule remain bar above the card.
  slot_remain_line: SlotRemainLine;
}

export interface HumidityView {
  display_type: HumidityDisplayType;
  level: number;
  percent: number;
  // 1..5, 5 = driest, -1 unknown.
  display_idx: number;
  drying: boolean;
  left_dry_time: number;
  // Hardware can dry; independent of `drying`.
  support_drying: boolean;
}

export type HubKind = 'merge4' | 'cross4' | 'passthrough';

// Slot to hub only. Deliberately not LinkState: that one answers the longer
// question, so the two are free to gain states independently.
export type HubLinkState = 'idle' | 'loaded' | 'loading' | 'unloading';

// AMS-internal routing: the fitting that merges this unit's slots into its one
// output tube. Whether the filament made it past that tube is SlotLink's job.
export interface UnitHub {
  kind: HubKind;
  port_count: number;
  // Draw the grey fitting block. Only the four-slot hub has visible art.
  show_body: boolean;
  // Slot the unit routes right now, empty while idle.
  active_slot_id: string;
  state: HubLinkState;
  color: string;
}

export interface UnitView {
  ams_id: string;
  ams_type_name: string;
  active: boolean;
  slot_count: number;
  humidity: HumidityView;
  hub: UnitHub;
  slots: SlotView[];
}

export interface PreviewCube {
  color: string;
  colors: string[];
  color_type: AmsColorType;
  is_exists: boolean;
}

export interface PreviewItem {
  ams_id: string;
  ams_type_name: string;
  active: boolean;
  slot_count: number;
  cubes: PreviewCube[];
}

export interface PanelGroup {
  ams_ids: string[];
}

export interface PanelView {
  pos: PanelPos;
  visible: boolean;
  active_ams_id: string;
  groups: PanelGroup[];
}

export interface PanelLayout {
  layout_style: LayoutStyle;
  panels: PanelView[];
}

export interface AmsPreviewArea {
  visible: boolean;
  layout: PanelLayout;
  items: PreviewItem[];
}

export interface AmsExtArea {
  visible: boolean;
  // C++ AMS_LITE / f1: Ext cards use Lite spool art even with no AMS unit.
  lite_style: boolean;
  layout: PanelLayout;
  units: UnitView[];
  ext_slots: SlotView[];
}

export interface SlotLink {
  ams_id: string;
  slot_id: string;
  switcher_port: string;
  // Empty means no line to draw.
  extruder_ids: number[];
  state: LinkState;
  color: string;
}

export interface FilamentLineArea {
  visible: boolean;
  links: SlotLink[];
}

export interface SwitcherArea {
  visible: boolean;
  installed: boolean;
  ready: boolean;
  show_setup_hint: boolean;
  setup_hint: string;
}

// 0 = main extruder on the right, 1 = deputy on the left.
export interface ExtruderView {
  id: number;
  state: ExtruderState;
  has_filament: boolean;
  filament_color: string;
  // C++ AMSextruder::updateNozzleNum asset key.
  icon: string;
}

export interface ExtruderArea {
  visible: boolean;
  extruders: ExtruderView[];
}

export interface AmsControlDisplay {
  visible: boolean;
  ams_preview_area: AmsPreviewArea;
  ams_ext_area: AmsExtArea;
  filament_line_area: FilamentLineArea;
  switcher_area: SwitcherArea;
  extruder_area: ExtruderArea;
}

export interface AmsControlWebViewModel {
  data: AmsListData;
  display: AmsControlDisplay;
  selected_ams_id: string;
  selected_slot_id: string;
  actions: AmsControlActions;
}

export interface BridgeResponseBody<T> {
  module: string;
  submod: string;
  action: string;
  error_code: number;
  message?: string;
  payload?: T;
}
