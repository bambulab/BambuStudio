// View-model types for the RefillInfo auto-refill management dialog.
//
// Mirrors AutoRefillRefillInfoVM::BuildState() on the C++ side.
// C++ Schema (single source of truth for field names and enum values):
//   src/slic3r/GUI/DeviceWeb/ViewModels/DevicePage/AutoRefill_RefillInfo/refill_info/Schema.hpp
//
// Only the RefillVersion::RefillInfo variant is modelled here; the legacy
// FilaBackup dialog keeps its own native rendering.

// Mirrors DevFilaColorType.
export type RefillColorType = 'single' | 'multi' | 'gradient';

// How the refill relation for a slot was created (mirrors
// DevAmsRefillSlot::CreateType). `auto` = system generated, `manual` = user
// added, `unknown` = not reported.
export type RefillCreateType = 'auto' | 'manual' | 'unknown';

// One physical tray inside a group, in refill-priority order.
export interface RefillSlotViewModel {
  ams_id: number;
  slot_id: number;
  create_type: RefillCreateType;
  has_filament: boolean;
  // The fields below are only present when the tray is resolvable.
  tray_name?: string;
  // Display name resolved by C++ against the local presets ("PLA Basic",
  // "PLA Matte"); always prefer it over the bare type.
  filament_name?: string;
  filament_type?: string;
  sub_brand?: string;
  color?: string;
  colors?: string[];
  color_type?: RefillColorType;
  weight_gram?: number; // remaining grams; omitted when unknown
}

// A refill group = one filament "chain": ordered slots that back each other up.
export interface RefillGroupViewModel {
  // Device-side identity of the chain (e.g. "GFA00_2_1_C12E1FFF_H_0.4"). This
  // is what `action/reset` must send as `group_tag`; `filament_id` is shared by
  // every chain of the same filament, whatever its colour or nozzle.
  refill_tag_id: string;
  filament_id: string;
  filament_type: string;
  colors: string[];
  color_type: RefillColorType;
  nozzle_diameter?: number;
  nozzle_flow_type?: string;
  slots: RefillSlotViewModel[];
}

export interface RefillExtruderViewModel {
  extruder_id: number;
  groups: RefillGroupViewModel[];
}

export interface AutoRefillRefillInfoViewModel {
  selected_dev_id: string;
  // Bumped by the C++ side on every Refresh() (i.e. each dialog-open) so the
  // web page can tell a fresh open apart from an in-place data refresh.
  session_seq: number;
  auto_refill_enabled: boolean;
  // Page-level presentation resolved by C++ (see Schema.hpp keys::root):
  //   description   - localized explanatory text under the title; hidden when omitted.
  //   editable      - refill relations can be reordered / added / removed.
  //   can_add_group - the "add material group" entry button is shown.
  description?: string;
  editable?: boolean;
  can_add_group?: boolean;
  extruders: RefillExtruderViewModel[];
}

// Success toast queued after a refill write-back is acknowledged by the
// printer. `text` is an untranslated key resolved at render time.
export interface RefillToast {
  id: number;
  text: string;
}

export interface BridgeResponseBody<T> {
  module: string;
  submod: string;
  action: string;
  error_code: number;
  message?: string;
  payload?: T;
}
