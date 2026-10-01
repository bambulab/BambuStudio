// STUDIO-17977: candidate-colour identification + reverse-lookup primitives
// shared by the list row tail (`SpoolTable.resolveCandidateForSpool`) and
// the add/edit dialog candidate-selection highlight
// (`AddEditDialog.isCandidateSelected`).
//
// Background:
//   - `AddEditDialog::effectiveCandidates` merges three layers into the
//     visible candidate panel:
//       (1) official BBL candidates from FilamentColorCodeQuery,
//       (2) aggregated material-level fallbacks (still official),
//       (3) user-owned candidates synthesised from existing spools, where
//           `color_code` is set to the spool hex itself and `name` is
//           usually empty.
//   - Reverse lookup must prefer the official layer, otherwise a user-owned
//     spool eats its own self-synthesised candidate and the list row keeps
//     showing an empty colour name even though the official catalogue
//     would yield e.g. "柠檬黄 / 11400".
//
// The four-stage fallback (own→cross-fila→user-owned) used to live inline
// in SpoolTable; centralised here so future surfaces (DetailDialog,
// AddEditDialog, AMS slot tooltips) can reuse the same chain.

import type { Spool, CandidateColor } from '../types';
import { canonicalizeHex, canonicalizeHexList, hexEqual, hexMultisetKey } from './hex';

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
  const target = canonicalizeHexList(targetHexes);
  if (target.length === 0) return null;
  const targetKey = hexMultisetKey(target);
  for (const c of list) {
    if (options.officialOnly && !isOfficialCandidate(c)) continue;
    const cHexes = canonicalizeHexList(c.colors);
    if (options.singleHexOnly && cHexes.length !== 1) continue;
    if (cHexes.length !== target.length) continue;
    if (hexMultisetKey(cHexes) === targetKey) return c;
  }
  return null;
}

function matchOfficialContainingHex(
  list: readonly CandidateColor[] | undefined,
  hex: string,
): CandidateColor | null {
  if (!Array.isArray(list) || list.length === 0) return null;
  const target = canonicalizeHex(hex);
  if (!target) return null;
  for (const c of list) {
    if (!isOfficialCandidate(c)) continue;
    const cHexes = canonicalizeHexList(c.colors);
    if (cHexes.some((item) => hexEqual(item, target))) return c;
  }
  return null;
}

/**
 * Four-stage fallback for the list row tail "official colour name + BBL
 * fila code" lookup. Stages (acceptance feedback 2026-05-09):
 *
 *   1. own fila_id, official only, full `spool.colors[]` multiset.
 *      Catches AMS-imported multi-hex SKUs (e.g. PLA Silk 13906 "马卡龙").
 *   2. own fila_id, official only, fallback to `spool.color_code` single
 *      hex. Catches AMS-derived single-colour SKUs and legacy spools whose
 *      `colors[]` field is empty.
 *   3. cross-fila_id, official only, single hex. Same hex often exists in
 *      multiple BBL series (e.g. "11400" 柠檬黄 in PLA Basic / Matte /
 *      Lite); a same-hex match in another fila_id still surfaces the
 *      right *colour name*. Multi-hex stage 3 is intentionally omitted —
 *      borrowing a multi-colour SKU name from a different series would
 *      mislead.
 *   4. own fila_id (last resort), any layer including user-owned. Lets us
 *      still pick up *something* when the spool's hex is genuinely
 *      off-catalogue (e.g. user-pasted custom hex). Typically yields a
 *      candidate with empty name + hex code, which the caller treats as
 *      "no official name available" and renders only the hex tail.
 */
export function resolveCandidateForSpool(
  s: Spool,
  cache: Record<string, CandidateColor[]>,
): CandidateColor | null {
  const filaId = (s.setting_id || '').trim();
  const ownList = filaId ? cache[filaId] : undefined;
  const colorsList = canonicalizeHexList(s.colors);
  const single = canonicalizeHex(s.color_code);

  // Stage 1: own fila_id, official only, full colors[] multiset.
  if (ownList) {
    const m = matchHexMultiset(ownList, colorsList, { officialOnly: true });
    if (m) return m;
  }
  // Stage 2: own fila_id, official only, single color_code.
  if (ownList && single) {
    const m = matchHexMultiset(ownList, [single], { officialOnly: true });
    if (m) return m;
  }
  // Stage 2b: own fila_id, official only, containing-hex fallback.
  // Cloud/list legacy rows can lose `colors[]` and keep only the primary
  // `color_code` even though the official SKU is multi-hex. Matching by
  // contained hex within the same fila_id lets the list recover display-only
  // multicolor information even when AMS / BBL order differs.
  if (ownList && single) {
    const m = matchOfficialContainingHex(ownList, single);
    if (m) return m;
  }
  // Stage 3: cross-fila_id, official only, single hex only.
  if (single) {
    for (const [otherFilaId, list] of Object.entries(cache)) {
      if (otherFilaId === filaId) continue;
      const m = matchHexMultiset(list, [single], { officialOnly: true, singleHexOnly: true });
      if (m) return m;
    }
  }
  // Stage 4: own fila_id, any layer (incl. user-owned), colors[] then single.
  if (ownList) {
    const m = matchHexMultiset(ownList, colorsList);
    if (m) return m;
  }
  if (ownList && single) {
    return matchHexMultiset(ownList, [single]);
  }
  return null;
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
  const formCode = canonicalizeHex(formColorCode);
  if (!formCode) return false;
  const candPrimary = canonicalizeHex((candidate.colors ?? [])[0]);
  if (candidate.color_type === 2) {
    // A single-hex candidate matches any single-hex form state.  The
    // canonical invariant is `formColors=[]` for single, but we tolerate
    // `formColors=[hex]` (length 1) for older AMS payloads / defensive
    // copies. Reject only when the form has 2+ hexes.
    if (formColors.length > 1) return false;
    return hexEqual(formCode, candPrimary);
  }
  // Multi-hex candidate (color_type 0=gradient or 1=multicolor). Treat
  // colors[] as a multiset (ignoring colorType) — same SKU may persist
  // hexes in different orders across AMS / BBL preset registry.
  if (formColors.length < 2) return false;
  const candHexes = canonicalizeHexList(candidate.colors);
  if (candHexes.length !== formColors.length) return false;
  return hexMultisetKey(candHexes) === hexMultisetKey(formColors);
}
