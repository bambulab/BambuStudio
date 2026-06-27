// STUDIO-17977: flat rounded-square colour chip used everywhere a spool's
// colour is shown — list row icon, list row tail mini, detail dialog,
// AMS slot card, AMS slot mini, AddEditDialog candidate panel / preview
// bar / custom swatch. Single source of truth for the rendering rules.
//
// Why not a CSS-only `linear-gradient` swatch?
//   - For multicolor (colorType === 1) intent, we want a hard pixel
//     boundary between adjacent colours.  A linear-gradient with two
//     stops at the same offset still anti-aliases the seam in WebView2,
//     producing a visible 1-2 px blur on small chips (12-16 px). We draw
//     multicolor chips as block rectangles inside one clipped wrapper so
//     the top/bottom edges are shared visually, not just geometrically.
//   - Gradient (colorType !== 1 multi-hex) keeps using `linear-gradient`
//     because that mode *wants* a smooth blend; CSS gradient is fine.
//   - Single colour is a flat background.
//
// Why `box-shadow inset` instead of `border`?
//   - `border` + `border-radius` + `background` produces 1 px sub-pixel
//     gaps at the rounded corners in some WebView2 zoom levels (Chromium
//     issue 1179970).  `box-shadow: inset 0 0 0 1px ...` paints the ring
//     on top of the fill so corners stay seamless.
//   - The shadow also lets the 1 px ring use a soft alpha (10 %) instead
//     of the harsh `border-white/20` that produced the bright outlines
//     PM flagged as too loud against dark slot card backgrounds.
//   - The ring color is provided by the theme token `--color-fm-chip-ring`,
//     which has its own light / dark value pair in `styles.css`. A white /
//     near-white chip fill therefore still keeps a visible outline on the
//     light-theme white surface without darkening the dark-theme look.

import type { CSSProperties } from 'react';
import { canonicalizeHex, canonicalizeHexList } from './colors';

export interface SpoolColorChipProps {
  /** Single-color fallback hex (matches FilamentSpool.color_code). */
  colorCode?: string;
  /** Multi-color hex list; renders per colorType when length > 1. */
  colors?: string[];
  /** 0 = gradient, 1 = multicolor, 2 = single (default 2). */
  colorType?: 0 | 1 | 2;
  /** Pixel size, keeps the legacy 40 default so existing call sites do
   * not need to pass an explicit size. */
  size?: number;
  /** Render an inset 1px ring (default true). Caller can disable when
   * the wrapping element (e.g. a button with its own selection ring)
   * already provides one and a second ring would visually double up. */
  border?: boolean;
  /** Override the auto-scaled border radius (in px). Default scales with
   * `size` so a 40 px chip is clearly rounded and a 12 px mini stays a
   * tight squarelet. */
  radius?: number;
}

export function SpoolColorChip({
  colorCode,
  colors,
  colorType,
  size = 40,
  border = true,
  radius,
}: SpoolColorChipProps) {
  const hexList = canonicalizeHexList(colors);
  const fallback = canonicalizeHex(colorCode) || '#888';
  const r = radius ?? Math.max(2, Math.round(size / 6));

  // Soft alpha + box-shadow inset; see file header for why. The color
  // comes from a theme token so light vs dark stays in sync with the
  // surrounding surface.
  const ring = border ? 'inset 0 0 0 1px var(--color-fm-chip-ring)' : undefined;

  // Common chip wrapper. `overflow: hidden` is mandatory for the
  // multicolor flex-strip mode so the strips inherit the rounded
  // corners; harmless for single / gradient modes.
  const chipStyle: CSSProperties = {
    width: size,
    height: size,
    borderRadius: r,
    overflow: 'hidden',
    boxShadow: ring,
    boxSizing: 'border-box',
  };

  // Multi-hex with explicit multicolor intent: paint the colour strips
  // edge-to-edge inside the rounded chip frame and let `overflow: hidden`
  // clip the rounded corners.
  //
  // An earlier revision inset the strips by 1-2 px and filled the gap
  // with a soft wrapper background so the shared top / bottom edges
  // would visually stay straight at rounded corners. That extra inset
  // was invisible when the chip ring was a near-transparent white, but
  // STUDIO-18338 made the ring theme-aware (light theme uses a darker
  // ring), at which point the inset became a clearly visible "ring
  // floating away from the strips" artefact. Filling the gap with a
  // theme-matched colour would still leave a perceptible band between
  // ring and strips on small chips, so the cleaner fix is to drop the
  // inset entirely. The corner anti-alias loss is negligible at the
  // sizes we render (14-40 px).
  if (hexList.length > 1 && colorType === 1) {
    const n = hexList.length;
    const baseStrip = Math.floor(size / n);
    const widths = hexList.map((_, i) =>
      i === n - 1 ? size - baseStrip * (n - 1) : baseStrip,
    );
    return (
      <span
        className="inline-flex shrink-0 align-middle"
        style={{
          ...chipStyle,
          display: 'inline-flex',
          lineHeight: 0,
          fontSize: 0,
        }}
      >
        {hexList.map((hex, i) => (
          <span
            key={`${i}-${hex}`}
            style={{
              display: 'block',
              flex: `0 0 ${widths[i]}px`,
              width: widths[i],
              height: size,
              background: hex,
            }}
          />
        ))}
      </span>
    );
  }

  // Multi-hex with gradient intent (or unspecified): smooth CSS gradient.
  // CSS handles smooth interpolation cleanly so no seam workaround is
  // required here.
  if (hexList.length > 1) {
    const stops = hexList.join(', ');
    return (
      <span
        className="inline-block shrink-0"
        style={{
          ...chipStyle,
          background: `linear-gradient(90deg, ${stops})`,
        }}
      />
    );
  }

  // Single colour.
  const single = hexList[0] || fallback;
  return (
    <span
      className="inline-block shrink-0"
      style={{
        ...chipStyle,
        background: single,
      }}
    />
  );
}
