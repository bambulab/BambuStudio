// STUDIO-17977: hex-string normalisation primitives shared by the list,
// detail, edit dialog and SVG chip. Before this module each call site had
// its own near-duplicate `normalizeHex*` function; the legacy fragmentation
// caused two production bugs:
//   - DetailDialog rendered a blank colour swatch when the persisted
//     color_code was 8-char `RRGGBBAA` (cloud schema); its local helper was
//     missing the slice-to-7 step.
//   - The CSS background path and the SVG `<linearGradient>` path drifted
//     out of sync, producing the "马卡龙 spool icon looks ugly" visual bug.
//
// Anchor invariants (do not weaken without a regression spec):
//   1. Output is exactly 7 chars (`#RRGGBB`) or empty string. Never 8 char,
//      never lowercase, never with surrounding whitespace.
//   2. Empty / non-hex input returns ''. Callers gate their UI on truthy
//      output rather than throwing.
//   3. The function is pure and idempotent: f(f(x)) == f(x).

/**
 * Convert any reasonable hex form into canonical CSS-friendly `#RRGGBB`.
 *
 * Tolerates:
 *   - 6-char `RRGGBB` (legacy MQTT) → prepend `#`.
 *   - 7-char `#RRGGBB` (canonical)  → upper-case only.
 *   - 8-char `RRGGBBAA` / `#RRGGBBAA` (cloud schema) → drop the alpha byte;
 *     CSS / SVG gradients reliably parse 6-char only.
 *   - Mixed-case input (`#aabbcc`)   → upper-case for stable equality.
 *
 * Returns '' for any input that doesn't look like a hex (incl. undefined,
 * null, '', whitespace-only, or strings containing non-hex chars).
 */
export function canonicalizeHex(value: string | undefined | null): string {
  const raw = (value ?? '').trim();
  if (!raw) return '';
  const withHash = raw.startsWith('#') ? raw : `#${raw}`;
  const trimmed = withHash.slice(0, 7).toUpperCase();
  // Light validation. Reject anything that didn't end up as `#` + 6 hex chars
  // so a stray "garbage" input (e.g. "linear-gradient(...)") doesn't sneak
  // past as a faux hex and break a downstream CSS rule.
  if (!/^#[0-9A-F]{6}$/.test(trimmed)) return '';
  return trimmed;
}

/**
 * Case-insensitive hex equality after canonicalisation. Returns false when
 * either side fails to canonicalise.
 */
export function hexEqual(a: string | undefined | null, b: string | undefined | null): boolean {
  const ca = canonicalizeHex(a);
  const cb = canonicalizeHex(b);
  if (!ca || !cb) return false;
  return ca === cb;
}

/**
 * Canonicalise an array of hex strings. Drops anything that doesn't
 * canonicalise to a valid `#RRGGBB`. Stable order (input order preserved).
 */
export function canonicalizeHexList(input: readonly (string | undefined | null)[] | undefined): string[] {
  if (!Array.isArray(input)) return [];
  return input.map(canonicalizeHex).filter((h) => h.length === 7);
}

/**
 * Multiset key used by reverse-lookup matchers. Order-independent and
 * case-independent (canonicalises first), so two SKUs that share a hex pair
 * but persist them in different orders compare equal.
 *
 * Empty input returns ''. Callers using this as a hash key should compare
 * keys for equality rather than membership of an empty string.
 */
export function hexMultisetKey(input: readonly (string | undefined | null)[] | undefined): string {
  return canonicalizeHexList(input).slice().sort().join(',');
}
