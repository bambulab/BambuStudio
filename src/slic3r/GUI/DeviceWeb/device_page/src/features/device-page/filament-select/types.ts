// View-model types of the Select Filament dialog.
//
// Mirrors FilamentSelectBuilder::BuildState() on the C++ side.
// C++ Schema (single source of truth for field names and enum values):
//   src/slic3r/GUI/DeviceWeb/ViewModels/DevicePage/FilamentSelect/Schema.hpp

export const FILAMENT_SELECT_MODULE = 'device_page_filament_select';

export type FilamentSelectTheme = 'light' | 'dark';

export type FilamentSelectChipKind = 'all' | 'brand' | 'other' | 'unsupported';

export type FilamentSelectTailKind = 'slot' | 'recent' | 'none';

export type FilamentSelectTab = 'manager' | 'presets';

export interface FilamentSelectChip {
  key: string;
  kind: FilamentSelectChipKind;
  /** Brand name; empty unless kind === 'brand' (the other kinds are translated by the page). */
  label: string;
}

export interface FilamentSelectSwatch {
  /** 0 = gradient, 1 = multicolour, 2 = single */
  color_type: 0 | 1 | 2;
  colors: string[];
  color_code: string;
}

export interface FilamentSelectTail {
  kind: FilamentSelectTailKind;
  /** "A1" etc., only set when kind === 'slot'. */
  slot_name: string;
}

export interface FilamentSelectRow {
  /** Index into the C++ snapshot; the only handle the page sends back. */
  row_key: string;
  chip_key: string;
  /** Already lower-cased by C++. */
  search_key: string;
  name: string;
  weight_g: number;
  swatch: FilamentSelectSwatch;
  show_bbl_logo: boolean;
  tail: FilamentSelectTail;
  /** false = dimmed, clicks ignored. */
  selectable: boolean;
}

export interface FilamentSelectManager {
  chips: FilamentSelectChip[];
  rows: FilamentSelectRow[];
}

export interface FilamentSelectPresets {
  /** kind is 'brand' or 'other'. */
  brands: FilamentSelectChip[];
  /** brand key -> sorted aliases */
  aliases: Record<string, string[]>;
  initial_brand: string;
  checked_alias: string;
}

export interface FilamentSelectState {
  theme: FilamentSelectTheme;
  manager: FilamentSelectManager;
  presets: FilamentSelectPresets;
}

export type FilamentSelectConfirmPayload =
  | { tab: 'manager'; row_key: string }
  | { tab: 'presets'; alias: string };

export interface BridgeResponseBody<T> {
  module?: string;
  submod?: string;
  action?: string;
  error_code: number;
  message?: string;
  payload?: T;
}
