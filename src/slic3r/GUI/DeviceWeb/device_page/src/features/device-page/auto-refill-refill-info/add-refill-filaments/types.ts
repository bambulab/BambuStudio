// Types for the add-refill-filaments picker.
// Mirrors C++ structs in:
//   src/slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AutoRefill_RefillInfo/add_refill_filaments/Schema.hpp
//   AutoRefillAddRefillFilamentsSchema::format::{Slot, Unit, State, SlotRef}

export type AutoRefillAddRefillFilamentsColorType = 'gradient' | 'multi' | 'single';

export type AutoRefillAddRefillFilamentsMode = 'create_group' | 'add_to_group';

// Unit.kind: the rack's associated-filament row vs a plain AMS unit.
export type AutoRefillAddRefillFilamentsUnitKind = 'hotend' | 'ams';

export type AutoRefillNozzleState =
  | 'Normal'
  | 'Active'
  | 'Lost'
  | 'Error'
  | 'NonNozzle'
  | 'Empty'
  | 'Unknown';

export interface AutoRefillSlotNozzle {
  state: AutoRefillNozzleState;
  label: string;
  subtext: string;
}

// Mirrors BlockCodeUtil.hpp. `policy` decides eligibility, `feedback` what a
// click answers with, `follow_up` which jump the confirm button performs, and
// `details` which live values the dialog has to fill in.
export type RefillBlockPolicy = 'selectable' | 'confirm_then_select' | 'blocked';

export type RefillBlockFeedback = 'none' | 'tooltip' | 'toast' | 'dialog';

export type RefillBlockFollowUp =
  | 'none'
  | 'select_anyway'
  | 'go_to_calibration'
  | 'view_hms_detail'
  | 'go_to_refresh_hotend'
  | 'go_to_edit_hotend'
  | 'go_to_edit_filament'
  | 'go_to_unbind'
  | 'go_to_load';

export type RefillBlockDetails =
  | 'none'
  | 'filament_comparison'
  | 'hotend_comparison'
  | 'hotend_index';

export interface RefillBlockInteraction {
  block_code: number;
  policy: RefillBlockPolicy;
  feedback: RefillBlockFeedback;
  allow_dont_show_again: boolean;
  follow_up: RefillBlockFollowUp;
  details: RefillBlockDetails;
  // Null when the code does not use the member.
  tooltip: string | null;
  title: string | null;
  message: string | null;
  confirm_label: string | null;
}

// What the mismatch dialogs compare a candidate against; members are empty
// until the first slot of the group has been picked.
export interface RefillGroupReference {
  filament_name: string;
  filament_type: string;
  nozzle: string;
}

export interface AutoRefillAddRefillFilamentsSlot {
  ams_id: number;
  slot_id: number;
  slot_name: string;
  // Hotend unit only: rack nozzle address and its 1-based display index.
  addr_id: number;
  display_index: number;
  // Hotend unit only: even display indices form the upper row of cards.
  is_top_tier: boolean;
  // False for a rack nozzle position with no bound AMS slot; ams_id / slot_id
  // are then -1 and the card renders as an empty, unselectable placeholder.
  has_slot: boolean;
  has_filament: boolean;
  filament_id: string;
  filament_name: string;
  filament_type: string;
  remain: number;
  color: string;
  colors: string[];
  color_type: AutoRefillAddRefillFilamentsColorType;
  selectable: boolean;
  selected: boolean;
  disable_reason: string;
  // `block` code from MQTT refill_command 5 (Query). 0 means unrestricted.
  // `warning` marks the codes that stay selectable but need a native
  // confirmation dialog before the slot is added.
  block_code: number;
  warning: boolean;
  // Every reason the device reported, primary first. Only the primary drives
  // the card; the rest are available for explaining a multi-reason slot.
  block_codes: number[];
  // How the card reacts to the primary code. Null when the slot carries no
  // `block` code at all.
  interaction: RefillBlockInteraction | null;
  nozzle: AutoRefillSlotNozzle;
}

export interface AutoRefillAddRefillFilamentsUnit {
  kind: AutoRefillAddRefillFilamentsUnitKind;
  ams_id: number;
  ams_type: string;
  name: string;
  slot_count: number;
  slots: AutoRefillAddRefillFilamentsSlot[];
}

export interface AutoRefillAddRefillFilamentsViewModel {
  selected_dev_id: string;
  session_seq: number;
  mode: AutoRefillAddRefillFilamentsMode;
  mode_title: string;
  /** Untranslated template; interpolate `notice_filament_type` as `{{type}}`. */
  notice: string;
  notice_filament_type?: string;
  group_reference: RefillGroupReference;
  units: AutoRefillAddRefillFilamentsUnit[];
}

export interface AutoRefillAddRefillFilamentsSlotRef {
  ams_id: number;
  slot_id: number;
}
