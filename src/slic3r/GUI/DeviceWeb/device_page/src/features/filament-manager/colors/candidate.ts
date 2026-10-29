// Color identity must match the filament family and preserve transparency.

import type { Spool, CandidateColor } from '../types';
// Keep identity comparison separate from six-digit display formatting.
export function colorIdentityKey(value: string | undefined | null): string {
  const raw = (value ?? '').trim().replace(/^#/, '').toUpperCase();
  if (/^[0-9A-F]{6}$/.test(raw)) return raw + 'FF';
  return /^[0-9A-F]{8}$/.test(raw) ? raw : '';
}

function identityList(values: readonly string[] | undefined): string[] {
  if (!values) return [];
  const keys = values.map(colorIdentityKey);
  return keys.every(Boolean) ? keys : [];
}

function identitySetKey(values: readonly string[]): string {
  return identityList(values).sort().join(',');
}

export function isBambuBrand(brand: string): boolean {
  return brand.trim().toLowerCase() === 'bambu lab';
}

// A borrowed palette supplies colors, not a different product's identity.
export function paletteForFilament(
  candidates: readonly CandidateColor[], brand: string, sameFamily: boolean,
): CandidateColor[] {
  if (isBambuBrand(brand) && sameFamily) return [...candidates];
  return candidates.map(c => ({ ...c, colors: [...c.colors], name: '',
    color_code: c.colors[0] || '' }));
}

/**
 * "Official" candidates have a real BBL fila code (e.g. "11400" / "Q01B00")
 * rather than a raw hex string. User-owned candidates synthesised from
 * existing spools store the hex itself in `color_code` and have an empty
 * `name`; they're useful to keep the picker visually consistent but should
 * never win a reverse-lookup race against an official entry.
 */
export function isOfficialCandidate(c: CandidateColor): boolean {
  const code = (c.color_code ?? '').trim();
  if (!code) return false;
  if (code.startsWith('#')) return false;
  if (/^[0-9A-F]{6,8}$/i.test(code)) return false;
  return true;
}

interface MatchOptions {
  /** When true, skip non-official candidates (the user-owned layer). */
  officialOnly?: boolean;
  /** When set, only single-hex candidates are eligible (used for cross-fila pass). */
  singleHexOnly?: boolean;
}

/**
 * Return the first candidate whose hex multiset matches `targetHexes`. The
 * comparison is order- and case-insensitive (canonicalises both sides),
 * matching the AMS-vs-BBL hex-order mismatches observed on real machines.
 */
export function matchHexMultiset(
  list: readonly CandidateColor[] | undefined,
  targetHexes: readonly string[],
  options: MatchOptions = {},
): CandidateColor | null {
  if (!Array.isArray(list) || list.length === 0) return null;
  const target = identityList(targetHexes);
  if (target.length === 0) return null;
  const targetKey = identitySetKey(target);
  for (const c of list) {
    if (options.officialOnly && !isOfficialCandidate(c)) continue;
    const cHexes = identityList(c.colors);
    if (options.singleHexOnly && cHexes.length !== 1) continue;
    if (cHexes.length !== target.length) continue;
    if (identitySetKey(cHexes) === targetKey) return c;
  }
  return null;
}

/** Match only the spool's own Bambu family and complete color identity. */
export function resolveCandidateForSpool(
  s: Spool, cache: Record<string, CandidateColor[]>,
): CandidateColor | null {
  if (!isBambuBrand(s.brand)) return null;
  const ownList = cache[(s.setting_id || '').trim()];
  const colors = s.colors?.length ? s.colors : [s.color_code];
  return matchHexMultiset(ownList, colors, { officialOnly: true });
}

/**
 * Predicate matching AddEditDialog's `isCandidateSelected`: does the given
 * candidate match the form's current colour state? Single-colour forms
 * match candidates whose `color_type === 2` and primary hex equals
 * `formColorCode`. Multi-hex forms match candidates whose `colors[]` is
 * the same multiset, ignoring `color_type` (the AMS payload occasionally
 * disagrees with the BBL preset registry on gradient-vs-multicolor for
 * the same hex pair, and either tag is "right enough" for highlighting).
 */
export function candidateMatchesFormState(args: {
  candidate: CandidateColor;
  formColorCode: string;
  formColors: readonly string[];
}): boolean {
  const { candidate, formColorCode, formColors } = args;
  const formCode = colorIdentityKey(formColorCode);
  if (!formCode) return false;
  const candPrimary = colorIdentityKey((candidate.colors ?? [])[0]);
  if (candidate.color_type === 2) {
    // A single-hex candidate matches any single-hex form state.  The
    // canonical invariant is `formColors=[]` for single, but we tolerate
    // `formColors=[hex]` (length 1) for older AMS payloads / defensive
    // copies. Reject only when the form has 2+ hexes.
    if (formColors.length > 1) return false;
    return formCode === candPrimary;
  }
  // Multi-hex candidate (color_type 0=gradient or 1=multicolor). Treat
  // colors[] as a multiset (ignoring colorType) — same SKU may persist
  // hexes in different orders across AMS / BBL preset registry.
  if (formColors.length < 2) return false;
  const candHexes = identityList(candidate.colors);
  if (candHexes.length !== formColors.length) return false;
  return identitySetKey(candHexes) === identitySetKey(formColors);
}
