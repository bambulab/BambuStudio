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

  // Soft alpha + box-shadow inset; see file header for why.
  const ring = border ? 'inset 0 0 0 1px rgba(255, 255, 255, 0.10)' : undefined;

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

  // Multi-hex with explicit multicolor intent: render an outer rounded
  // chip frame, then inset the actual colour strips as plain rectangles.
  // Human perception reads clipped rounded corners as vertical imbalance:
  // the edge colour loses anti-aliased corner pixels while the inner seam
  // stays full-height. Insetting the colour body keeps the shared top and
  // bottom edges visually straight while preserving the rounded square
  // language of the rest of the UI.
  if (hexList.length > 1 && colorType === 1) {
    const n = hexList.length;
    const inset = size >= 24 ? 2 : 1;
    const bodySize = Math.max(1, size - inset * 2);
    const baseStrip = Math.floor(bodySize / n);
    const widths = hexList.map((_, i) =>
      i === n - 1 ? bodySize - baseStrip * (n - 1) : baseStrip,
    );
    return (
      <span
        className="inline-flex shrink-0 align-middle"
        style={{
          ...chipStyle,
          display: 'inline-flex',
          alignItems: 'center',
          justifyContent: 'center',
          lineHeight: 0,
          fontSize: 0,
          background: 'rgba(255, 255, 255, 0.06)',
        }}
      >
        <span
          style={{
            display: 'inline-flex',
            width: bodySize,
            height: bodySize,
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
                height: bodySize,
                background: hex,
              }}
            />
          ))}
        </span>
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
