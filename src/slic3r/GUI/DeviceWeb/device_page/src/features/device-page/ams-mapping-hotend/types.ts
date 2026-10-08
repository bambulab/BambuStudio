// View-model types for the "send to print" filament mapping panel.
//
// Mirrors DevicePageAmsMappingHotendVM::BuildState() on the C++ side.
// C++ Schema (single source of truth for field names and enum values):
//   src/slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AmsMappingHotend/DevicePageAmsMappingHotendSchema.hpp

export type AmsMappingHotendShowType =
  | 'LEFT'
  | 'RIGHT'
  | 'LEFT_AND_RIGHT'
  | 'LEFT_AND_RIGHT_DYNAMIC';

// Symmetric on the dimension that has a problem:
//   Material* = filament dimension; Nozzle* = nozzle dimension
//   - MaterialUnmatch: filament loaded but type mismatch
//   - MaterialEmpty:   nozzle present but no filament loaded
//   - NozzleUnmatch:   nozzle present but dia / flow mismatch
//   - NozzleEmpty:     no nozzle on this rack position
// NozzleLost / NozzleUncalibrated / NozzleError / NozzleUnknown are
// device-reported hardware faults; each one gets its own label and tooltip
// because the user action differs (reinstall / calibrate / check the cable /
// re-read the info).
export type AmsMappingHotendSlotState =
  | 'Normal'
  | 'MaterialUnmatch'
  | 'MaterialEmpty'
  | 'NozzleUnmatch'
  | 'NozzleEmpty'
  | 'NozzleLost'
  | 'NozzleUncalibrated'
  | 'NozzleError'
  | 'NozzleUnknown';

// States that render the red warning badge over the nozzle glyph and can never
// be picked. Exported so the card, the tooltip resolver and the CSS-facing
// checks all agree on the same set.
export const NOZZLE_FAULT_STATES: readonly AmsMappingHotendSlotState[] = [
  'NozzleLost',
  'NozzleUncalibrated',
  'NozzleError',
  'NozzleUnknown',
];

export function isNozzleFaultState(state: AmsMappingHotendSlotState): boolean {
  return NOZZLE_FAULT_STATES.includes(state);
}

export type AmsMappingHotendColorType = 'single' | 'multi' | 'gradient';

// Why the printer cannot feed a slot on its own (mirrors DevSlotFeedBlock).
// Only 'ext_spool' makes a slot non-selectable and is surfaced here; the others
// are raised as send-page pre-print warnings instead.
export type AmsMappingHotendFeedBlock =
  | 'none'
  | 'need_manual_feed'
  | 'rack_ams'
  | 'ext_spool';

// Kind of tool mounted on the rack position. Only a printing nozzle can be
// matched to a filament; cutter/pen carry their own icon and are forced into a
// non-pickable NozzleUnmatch state by the backend. Optional for backward
// compatibility — a missing value is treated as a regular nozzle.
export type AmsMappingHotendToolKind = 'nozzle' | 'cutter' | 'pen';

export interface AmsMappingHotendNozzleInfo {
  dia: string;        // e.g. "0.4 mm"
  flow_type: string;  // e.g. "Standard"
  tool_kind?: AmsMappingHotendToolKind;
}

// Display vs. selectability are decoupled on purpose:
//   `has_filament` drives the spool ring (real color vs empty placeholder)
//   `is_pickable`  drives `disabled` (clickability under all-ext rules)
//   `state`        drives CSS opacity / cursor (dimming style)
// A slot can have filament loaded while its nozzle is missing — we still
// want to show the spool color even though the card is non-clickable.
export interface AmsMappingHotendSubSlotViewModel {
  ams_id: string;
  slot_id: string;
  tray_id: number;
  index: string;
  ext: boolean;
  state: AmsMappingHotendSlotState;
  filament_type: string;
  filament_name: string;
  color: string;
  ctype: number;
  color_type: AmsMappingHotendColorType;
  remain: number;
  has_filament: boolean;
  is_pickable: boolean;
  // Whether the inline edit (pencil) entry is shown. True for third-party
  // (non-BBL) spools; stays clickable even when the card itself is disabled.
  can_edit: boolean;
  checked: boolean;
  multi_colors: string[];
  // Why the printer cannot feed this slot by itself, if it cannot.
  feed_block?: AmsMappingHotendFeedBlock;
}

export interface AmsMappingHotendSubAmsGroupViewModel {
  name: string;
  is_preload?: boolean;
  slots: AmsMappingHotendSubSlotViewModel[];
}

export interface AmsMappingHotendSlotViewModel {
  ams_id: string;
  slot_id: string;
  tray_id: number;
  index: string;
  is_top_tier: boolean;
  ext: boolean;
  state: AmsMappingHotendSlotState;
  filament_type: string;
  filament_name: string;
  color: string;
  ctype: number;
  color_type: AmsMappingHotendColorType;
  remain: number;
  has_filament: boolean;
  is_pickable: boolean;
  checked: boolean;
  can_edit: boolean;
  multi_colors: string[];
  nozzle?: AmsMappingHotendNozzleInfo;
  sub_ams?: AmsMappingHotendSubAmsGroupViewModel[];
  // What the filament engaged in this hotend forbids its other feed sources
  // from doing. 'none' when the hotend is empty or holds sub-AMS filament.
  hotend_feed_block?: AmsMappingHotendFeedBlock;
}

export interface AmsMappingHotendViewModel {
  selected_dev_id: string;
  current_filament_id: number;
  show_type: AmsMappingHotendShowType;
  material_index: string;
  reset_visible: boolean;
  is_all_ext: boolean;
  // Bumped by the C++ side on every Refresh() (i.e. each popup-open). The
  // React page watches this to reset its sub-AMS sub-page navigation so a
  // re-opened popup always lands on the main view, never the previously
  // entered sub-page.
  session_seq: number;
  hotend_slots: AmsMappingHotendSlotViewModel[];
  // Host UI theme; mirrored onto <html data-theme> so the popup chrome follows
  // Studio's light/dark mode. The filament color content is never themed.
  theme?: 'dark' | 'light';
}

export interface BridgeResponseBody<T> {
  module: string;
  submod: string;
  action: string;
  error_code: number;
  message?: string;
  payload?: T;
}
