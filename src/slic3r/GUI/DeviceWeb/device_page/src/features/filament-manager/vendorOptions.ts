import type { PresetVendor, Spool } from './types';

export function buildVendorOptions(
  presets: PresetVendor[],
  cloudVendors: string[] | undefined,
  spools: Spool[],
): string[] {
  const set = new Set<string>();
  presets.forEach((v) => { if (v.name) set.add(v.name); });
  cloudVendors?.forEach((n) => { if (n) set.add(n); });
  spools.forEach((s) => { if (s.brand) set.add(s.brand); });
  return [...set].sort();
}
