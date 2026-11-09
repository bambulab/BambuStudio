// View-model types for the Connected Slots dialog.
//
// Mirrors ConnectedSlotsVM::BuildState() on the C++ side.
// C++ Schema (single source of truth for field names and enum values):
//   src/slic3r/GUI/DeviceWeb/ViewModels/DevicePage/ConnectedSlots/Schema.hpp

export type ConnectedSlotsSourceKind = 'ams' | 'ext';

export type ConnectedSlotsLineState =
  | 'no_filament'
  | 'loaded'
  | 'loading'
  | 'unloading'
  | 'no_hotend';

export type ConnectedSlotsNozzleState = 'Normal' | 'Active' | 'None' | 'Error' | 'Lost' | 'FeedSuspect' | 'Unknown';

export type ConnectedSlotsTrayState =
  | 'empty'
  | 'reading'
  | 'ready'
  | 'unknown'
  | 'error'
  | 'loading'
  | 'unloading';

export type ConnectedSlotsColorType = 'single' | 'multi' | 'gradient';

export type ConnectedSlotsTrayAction = 'edit' | 'load' | 'unload' | 'read';

/** Visibility of the tray context menu items. */
export interface ConnectedSlotsTrayActions {
  can_edit?: boolean;
  can_load?: boolean;
  can_unload?: boolean;
  can_read?: boolean;
  /** i18n key: 'Read' for unknown filament, 'Reread' otherwise. */
  read_label?: string;
}

export interface ConnectedSlotsTrayInfo {
  ams_id: string;
  slot_id: string;
  exists: boolean;
  tray_state: ConnectedSlotsTrayState;
  filament_type: string;
  filament_name: string;
  color: string;
  color_type?: ConnectedSlotsColorType;
  colors?: string[];
  remain?: number;
  actions?: ConnectedSlotsTrayActions;
}

export interface ConnectedSlotsUnitInfo {
  ams_id: string;
  ams_type?: string;
  /** C++ product name, e.g. "AMS H3" / "AMS-A". */
  display_name?: string;
  slot_count?: number;
  can_dry?: boolean;
  humidity_raw?: number;
  /** Classic-AMS droplet level (1..5); -1/absent when the unit has none. */
  humidity_level?: number;
  /** Which humidity figure the unit exposes; mirrors AmsControlWeb. */
  humidity_display_type?: 'none' | 'level' | 'percent';
  temp?: number | null;
  /** Stable English enum ("Idle", "Drying", ...); compare against this. */
  dry_status?: string;
  /** Translated dry_status for display; empty when the unit cannot dry. */
  dry_status_text?: string;
  trays: ConnectedSlotsTrayInfo[];
}

export interface ConnectedSlotsLineInfo {
  state: ConnectedSlotsLineState;
  has_filament?: boolean;
  filament_color?: string;
}

export interface ConnectedSlotsNozzleInfo {
  state: ConnectedSlotsNozzleState;
  has_filament?: boolean;
  filament_color?: string;
  on_extruder?: boolean;
}

/** Ext / AMS chip inside the right-hand 外接示意图. `label` starting with Ext → Ext chip. */
export interface ConnectedSlotsSchematicItem {
  /** 1-based; identifies the item when selecting a source. */
  ext_index: number;
  /** Empty for the Ext (virtual tray) item. */
  ams_id?: string;
  label?: string;
  /** Comma separated, one entry per slot; blanks mark empty slots. */
  color?: string;
  /** Slot count of the unit; picks the single- / four-slot AMS silhouette. */
  slot_count?: number;
  empty: boolean;
  selected?: boolean;
}

/** Mini diagram above a column (槽位示意图 / 外接示意图). */
export interface ConnectedSlotsSchematic {
  kind: ConnectedSlotsSourceKind;
  selected?: boolean;
  /** Nozzle-rack capacity; only when kind === 'ams'. */
  slot_count?: number;
  /** 0-based rack position this dialog is for, -1 when unknown; only when kind === 'ams'. */
  selected_index?: number;
  /** Ext 1..N; only when kind === 'ext'. */
  items?: ConnectedSlotsSchematicItem[];
}

/** One visual column: schematic + 料盘 + 料线. */
export interface ConnectedSlotsColumn {
  schematic: ConnectedSlotsSchematic;
  /** 料盘. Null when this column has no AMS/Ext unit. */
  unit: ConnectedSlotsUnitInfo | null;
  line: ConnectedSlotsLineInfo;
}

export interface ConnectedSlotsTopSlotsArea {
  left_slot: ConnectedSlotsColumn;
  right_slots: ConnectedSlotsColumn;
}

export interface ConnectedSlotsBottomHotend {
  line: ConnectedSlotsLineInfo;
  nozzle: ConnectedSlotsNozzleInfo;
}

/** One hotend's Connected Slots dialog, matching the Figma layout tree. */
export interface ConnectedSlotsSlotViewModel {
  addr_id: number;
  display_index: number;
  top_slots_area: ConnectedSlotsTopSlotsArea;
  bottom_hotend: ConnectedSlotsBottomHotend;
}

export interface ConnectedSlotsViewModel {
  selected_dev_id: string;
  session_seq: number;
  theme?: 'dark' | 'light';
  slots: ConnectedSlotsSlotViewModel[];
}

export interface BridgeResponseBody<T> {
  module: string;
  submod: string;
  action: string;
  error_code: number;
  message?: string;
  payload?: T;
}
