// STUDIO-17977: shared CSS background + hex-label expressions for every
// surface that paints a spool / candidate / AMS-slot colour swatch. Single
// source of truth for the colour rendering rules so list row / detail
// dialog / preview-bar / candidate panel all behave identically and a fix
// in one place propagates everywhere.
//
// Rendering rule (matches the SpoolColorChip SVG path):
//   - hexes.length > 1 && colorType === 1 → hard-transition strips (each
//     hex paints an equal-width segment).  This is the "multicolor" intent
//     in the swagger schema.
//   - hexes.length > 1 && colorType !== 1 → smooth gradient (default for
//     any multi-hex payload arriving without explicit colorType, e.g. AMS
//     MQTT that omits the `cols`/`ctype` pair).  This is the "gradient"
//     intent.
//   - otherwise → flat single hex (fallback to `'#888'` only when no hex
//     is available at all, never to obscure a bad input silently).

import type { CSSProperties } from 'react';
import { canonicalizeHex, canonicalizeHexList, hasPartialTransparency, isTransparentHex } from './hex';

export interface ColorRenderInput {
  /**
   * Hex list (any tolerated form). Will be canonicalised + validated; bad
   * entries are dropped before rendering decisions are made.
   */
  hexes?: readonly (string | undefined | null)[];
  /**
   * Single-colour fallback hex used when `hexes` collapses to ≤1 entry.
   * Tolerates the same input forms as `canonicalizeHex`. Optional — when
   * omitted, single-colour mode falls back to `'#888'` so the swatch is at
   * least visible rather than transparent.
   */
  primaryHex?: string;
  /**
   * Swagger semantics: 0 = gradient, 1 = multicolor, 2 = single. Optional
   * because multi-hex AMS payloads frequently arrive without it; we default
   * to gradient (0) so the visible hex list is honoured rather than hiding
   * the second hex behind an unexplained "single" tag.
   */
  colorType?: 0 | 1 | 2;
}

interface NormalisedRenderInput {
  hexes: string[];
  primary: string;
  colorType?: 0 | 1 | 2;
}

function normalise(input: ColorRenderInput): NormalisedRenderInput {
  const hexes = canonicalizeHexList(input.hexes);
  const primary = canonicalizeHex(input.primaryHex);
  return { hexes, primary, colorType: input.colorType };
}

/**
 * CSS checkered pattern painted for fully-transparent colours (alpha byte
 * == 0x00 in `#RRGGBBAA`). A transparent spool (e.g. Bambu PC
 * "Transparent" `#00000000`, PETG "Clear" `#00000000`) must NOT render as
 * solid black — the checkered pattern reads as "clear / no colour".
 * Exported so SpoolColorChip (which builds its own DOM) reuses the exact
 * same expression and the two paths can never drift out of sync.
 */
export const TRANSPARENT_CHECKERED_BG =
  'repeating-conic-gradient(#ccc 0% 25%, #fff 0% 50%) 0 0 / 8px 8px';

/**
 * True when the colour to render is fully transparent. A single-colour
 * spool carries its hex in `primaryHex` (cloud `color_code`) with an EMPTY
 * `hexes`/`colors` array, so both sources must be checked — checking only
 * `hexes[0]` was the bug that left transparent single spools rendering as
 * `#000000` (black) after canonicalisation dropped the alpha byte.
 */
export function isTransparentRenderInput(input: ColorRenderInput): boolean {
  return isTransparentHex(input.hexes?.[0]) || isTransparentHex(input.primaryHex);
}

/**
 * CSS `background` expression for a colour swatch / chip / preview bar.
 * Always returns a non-empty string so the caller can safely set
 * `style={{ background: cssBackgroundFor(...) }}` without conditional
 * fallback elsewhere.
 */
export function cssBackgroundFor(input: ColorRenderInput): string {
  // Fully-transparent colour → checkered pattern instead of solid black.
  // Checked before canonicalisation because canonicalise drops the alpha
  // byte (#00000000 → #000000), hiding the transparency.
  if (isTransparentRenderInput(input)) return TRANSPARENT_CHECKERED_BG;
  const { hexes, primary, colorType } = normalise(input);
  if (hexes.length > 1) {
    if (colorType === 1) {
      // Hard-transition strips: each hex paints `[i/n, (i+1)/n]` of the
      // gradient, so adjacent stops at the same offset flip the colour
      // instantly and produce equal-width vertical strips.
      const step = 100 / hexes.length;
      const stops = hexes
        .map((hex, i) => `${hex} ${i * step}%, ${hex} ${(i + 1) * step}%`)
        .join(', ');
      return `linear-gradient(90deg, ${stops})`;
    }
    // Smooth gradient: just hand the hex list to CSS and let it
    // distribute the stops evenly.
    return `linear-gradient(90deg, ${hexes.join(', ')})`;
  }
  if (hexes.length === 1) return hexes[0];
  return primary || '#888';
}

/**
 * Checkerboard tile pattern shared by partially-transparent swatches.
 * Same 4-stop conic-gradient used by FilamentColorSwatch in the auto-refill
 * feature so all translucent swatches in the app render identically.
 */
const CHECKER_PATTERN_TRANSLUCENT =
  'conic-gradient(#d9d9d9 25%, #ffffff 0 50%, #d9d9d9 0 75%, #ffffff 0)';

/**
 * CSS background properties for a partially-transparent colour swatch.
 * Layers the RGBA colour on top of a checkerboard so the chip reads as
 * "translucent" rather than as a solid opaque fill — matching the C++
 * prepare-page rendering (ImGuiWrapper::draw_single_color_swatch).
 *
 * The caller must spread the returned object into `style={{ ... }}`:
 *   <span style={{ ...chipStyle, ...translucentBackground(rawHex) }} />
 *
 * `hex8` must be an 8-char `#RRGGBBAA` or `RRGGBBAA` value. Browsers
 * supporting CSS Color Level 4 (all modern Chromium / WebView2) parse
 * 8-char hex natively in `linear-gradient` colour stops.
 */
export function translucentBackground(hex8: string): CSSProperties {
  const rgba = hex8.startsWith('#') ? hex8 : `#${hex8}`;
  return {
    backgroundImage: `linear-gradient(${rgba} 0%, ${rgba} 100%), ${CHECKER_PATTERN_TRANSLUCENT}`,
    backgroundSize: '100% 100%, 8px 8px',
  };
}

/**
 * True when the primary colour input (before canonicalisation strips alpha)
 * is partially transparent (alpha 1–254). Used by SpoolColorChip to gate
 * the translucent-checkerboard rendering path.
 */
export function isPartiallyTransparentRenderInput(input: ColorRenderInput): boolean {
  return (
    hasPartialTransparency(input.hexes?.[0]) ||
    hasPartialTransparency(input.primaryHex)
  );
}

/**
 * Plain text representation of a colour for the row tail / detail label /
 * preview-bar hex list.
 *
 *   - multi-hex → "#A / #B / #C"
 *   - single   → "#RRGGBB"
 *   - empty    → ''  (caller decides whether to render anything at all)
 */
export function hexLabelFor(input: ColorRenderInput): string {
  const { hexes, primary } = normalise(input);
  if (hexes.length > 1) return hexes.join(' / ');
  if (hexes.length === 1) return hexes[0];
  return primary;
}

/**
 * Text label combining colour name and hex, e.g. "柠檬黄 · #F7D959 / #00918B".
 * Pure presentation helper — for surfaces (DetailDialog, list row tail)
 * that want the same "name · hex" format. Falls back to hex when name is
 * empty, '' when nothing is available.
 */
export function colorNameWithHexLabel(input: ColorRenderInput & { name?: string }): string {
  const hex = hexLabelFor(input);
  const name = (input.name ?? '').trim();
  if (name && hex) return `${name}  ·  ${hex}`;
  return name || hex;
}
