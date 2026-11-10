import type { PresetVendor, Spool } from './types';

// Comparison only: never use this key to rewrite a stored brand or filament ID.
export function vendorKey(name: string | null | undefined): string {
  return (name ?? '').toLowerCase();
}

export function sameVendor(left: string, right: string): boolean {
  return vendorKey(left) === vendorKey(right);
}

export function uniqueVendorNames(names: string[]): string[] {
  const vendors = new Map<string, string>();
  for (const name of names) {
    if (name && !vendors.has(vendorKey(name))) vendors.set(vendorKey(name), name);
  }
  return [...vendors.values()];
}

// Prefer an existing exact spelling when resolving a profile. Case-insensitive
// fallback must not switch an existing selection to a different profile ID.
export function matchingVendorItems<T>(
  items: T[],
  brand: string,
  nameOf: (item: T) => string,
): T[] {
  const exact: T[] = [];
  const variants: T[] = [];
  for (const item of items) {
    const name = nameOf(item);
    if (name === brand) exact.push(item);
    else if (sameVendor(name, brand)) variants.push(item);
  }
  return [...exact, ...variants];
}

export function buildVendorOptions(
  presets: PresetVendor[],
  cloudVendors: string[] | undefined,
  spools: Spool[],
): string[] {
  return uniqueVendorNames([
    ...presets.map((v) => v.name),
    ...(cloudVendors ?? []),
    ...spools.map((s) => s.brand),
  ]).sort();
}
