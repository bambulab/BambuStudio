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
  // DevAmsType enumerator name, e.g. 'N3F'.
  ams_type_name: string;
  // The N9 AMS-Lite variant reports itself as AMS_LITE, so only this flag tells
  // it apart. It drives the single-extruder layout.
  is_ams_lite_mixed: boolean;
  humidity_level: number;
  humidity_percent: number;
  humidity_display_type: HumidityDisplayType;
  // Extruders the whole unit can feed, and the switch port it is wired to.
  // Reported per unit, which is what decides the column it is drawn in.
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
  // Reason the matching button is disabled, empty when it is enabled.
  load_tips: string;
  unload_tips: string;
}

// `display` carries every rendering decision already resolved in C++; the page
// renders from it and only reads `data` for things it sends back.
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
  fila_type: string;
  selected: boolean;
  loaded: boolean;
  reading: boolean;
  show_rfid: boolean;
  // The spool is in but its info is not trustworthy yet, so the card shows '?'.
  show_unknown: boolean;
  menu_actions: MenuActions;
}

export interface HumidityView {
  display_type: HumidityDisplayType;
  level: number;
  percent: number;
  // 1..5 humidity icon index (5 = driest), -1 when unknown.
  display_idx: number;
  drying: boolean;
  left_dry_time: number;
}

export interface UnitView {
  ams_id: string;
  ams_type_name: string;
  active: boolean;
  slot_count: number;
  humidity: HumidityView;
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

// One cell of a column, the classic AmsItem. A four-slot unit fills a cell on
// its own, single-slot units share one two at a time. An id resolves against
// `AmsExtArea.units`, or against its `ext_slots` for an external spool.
export interface PanelGroup {
  ams_ids: string[];
}

export interface PanelView {
  pos: PanelPos;
  visible: boolean;
  // The unit this column has open, empty when the column holds no AMS unit.
  active_ams_id: string;
  groups: PanelGroup[];
}

// How the units and ext slots spread over columns: one column per extruder, so
// a single extruder machine stacks everything into one. The preview strip and
// the slot cards always share the same arrangement.
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
  layout: PanelLayout;
  units: UnitView[];
  ext_slots: SlotView[];
}

// Where one slot's filament goes, and what that line carries right now.
export interface SlotLink {
  ams_id: string;
  slot_id: string;
  // Filament switch port, empty when the slot reaches its extruders directly.
  switcher_port: string;
  // Empty means the slot has no line to draw at all.
  extruder_ids: number[];
  state: LinkState;
  color: string;
}

export interface FilamentLineArea {
  visible: boolean;
  links: SlotLink[];
}

// Per-port routing is not carried here: SlotLink.switcher_port already says which
// input every slot reaches the switch through.
export interface SwitcherArea {
  visible: boolean;
  installed: boolean;
  ready: boolean;
  show_setup_hint: boolean;
  setup_hint: string;
}

// The id doubles as the side the extruder sits on: 0 is the main extruder on
// the right, 1 the deputy one on the left.
export interface ExtruderView {
  id: number;
  state: ExtruderState;
  has_filament: boolean;
  filament_color: string;
}

export interface ExtruderArea {
  visible: boolean;
  extruders: ExtruderView[];
}

export interface AmsControlDisplay {
  // False hides the whole AMS panel, as an A / B nozzle rack does.
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
