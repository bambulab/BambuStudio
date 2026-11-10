// "Prints With": let a spool point at the Bambu preset family it should print
// as, so third-party spools the Filament Manager can't auto-match to an
// official filament still become first-class everywhere Studio keys off a
// filament_id (AMS slot selection, colour candidates, print settings).
//
// The chosen value is a filament_id. It is written to the spool's setting_id,
// which the C++ store maps onto FilamentSpool::filament_id and the cloud sync
// writes back as `filamentId`, so the link follows the spool to every machine
// signed into the same account. No local-only state, no rules file.

import type { PresetVendor } from './types';

export interface PrintsWithOption {
  filamentId: string; // preset item filament_id, stored as the spool setting_id
  label: string;      // "Bambu PLA Matte"
}

// Human label for a preset item. Series is the specific name ("PLA Matte");
// fall back to the item name or the bare type.
function familyName(type: string, series: string | undefined, name: string | undefined): string {
  const s = (series || '').trim();
  if (s) return s;
  const n = (name || '').trim();
  if (n) return n;
  return (type || '').trim();
}

// Every distinct preset family across all vendors, as "<vendor> <family>",
// deduped by filament_id (first vendor+family label wins) and sorted by label.
export function buildPrintsWithOptions(presets: PresetVendor[]): PrintsWithOption[] {
  const byId = new Map<string, string>();
  for (const vendor of presets || []) {
    for (const tp of vendor.types || []) {
      for (const item of tp.items || []) {
        const id = (item.filament_id || '').trim();
        if (!id || byId.has(id)) continue;
        const family = familyName(tp.name, item.series, item.name);
        const label = `${vendor.name} ${family}`.trim();
        byId.set(id, label);
      }
    }
  }
  return [...byId.entries()]
    .map(([filamentId, label]) => ({ filamentId, label }))
    .sort((a, b) => a.label.localeCompare(b.label));
}

// The label for a spool's current link (its setting_id / filament_id), or ''
// when nothing matches. Accepts an id that matches either a preset item's
// filament_id or its setting_id, mirroring findPresetMatchByFilamentId.
export function labelForFilamentId(presets: PresetVendor[], id: string): string {
  const want = (id || '').trim();
  if (!want) return '';
  for (const opt of buildPrintsWithOptions(presets)) {
    if (opt.filamentId === want) return opt.label;
  }
  for (const vendor of presets || []) {
    for (const tp of vendor.types || []) {
      for (const item of tp.items || []) {
        if ((item.setting_id || '').trim() === want && (item.filament_id || '').trim()) {
          return `${vendor.name} ${familyName(tp.name, item.series, item.name)}`.trim();
        }
      }
    }
  }
  return '';
}
