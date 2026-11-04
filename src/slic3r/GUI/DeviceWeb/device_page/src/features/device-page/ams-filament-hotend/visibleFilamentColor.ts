// A near-white filament colour visually merges with the light page
// background, so both the feed line and the in-nozzle filament can look
// "broken" where a white spool is loaded. Instead of drawing an extra
// outline we mix a little neutral grey into such colours so they stay
// readable while keeping their original hue. Threshold mirrors the
// filament-manager SpoolSvg isLightColor() so both surfaces agree on "light".
const LIGHT_LUMINANCE_THRESHOLD = 0.86;
const BLEND_TARGET = '#808080';
// Keep this subtle — only enough tint to separate the line from a white
// background without making a white spool read as grey.
const BLEND_RATIO = 0.2;

export const STANDBY_COLOR = '#DBDBDB';
const UNKNOWN_SENTINEL = '#D9D9D9';
// Fully transparent filament still needs a visible route, but it must stay
// achromatic so clear / white-translucent spools do not read as green.
const TRANSPARENT_DISPLAY_RGB = '#D9D9D9';
const OPAQUE_LINE_ALPHA = 0x80; // 50%

function parseHex(color: string): [number, number, number] | null {
  const hex = color.trim().replace(/^#/, '').slice(0, 6);
  if (!/^[0-9a-fA-F]{6}$/.test(hex)) return null;
  return [
    parseInt(hex.slice(0, 2), 16),
    parseInt(hex.slice(2, 4), 16),
    parseInt(hex.slice(4, 6), 16),
  ];
}

function rgbHex(color: string): string {
  const rgb = parseHex(color);
  if (!rgb) return color;
  return `#${toHex2(rgb[0])}${toHex2(rgb[1])}${toHex2(rgb[2])}`;
}

function toHex2(v: number): string {
  return Math.round(Math.min(255, Math.max(0, v))).toString(16).padStart(2, '0');
}

function withAlpha(rgb: string, alpha: number): string {
  return `${rgbHex(rgb)}${toHex2(alpha)}`;
}

function hexEqCi(a: string, b: string): boolean {
  return a.trim().toLowerCase() === b.trim().toLowerCase();
}

function isUnknownSentinel(color: string): boolean {
  return hexEqCi(color, UNKNOWN_SENTINEL) || hexEqCi(color, `${UNKNOWN_SENTINEL}FF`);
}

function alphaFromHex(color: string): number | null {
  const trimmed = color.trim();
  if (!trimmed) return null;
  if (trimmed.toLowerCase() === 'transparent') return 0;
  const hex = trimmed.replace(/^#/, '');
  if (/^[0-9a-fA-F]{8}$/.test(hex)) return parseInt(hex.slice(6, 8), 16);
  if (/^[0-9a-fA-F]{6}$/.test(hex)) return 255;
  return null;
}

function isLightColor(color: string): boolean {
  const rgb = parseHex(color);
  if (!rgb) return false;
  const [r, g, b] = rgb.map((v) => v / 255);
  return (0.2126 * r + 0.7152 * g + 0.0722 * b) > LIGHT_LUMINANCE_THRESHOLD;
}

function blendColor(color: string, target: string, ratio: number): string {
  const c = parseHex(color);
  const t = parseHex(target);
  if (!c || !t) return color;
  const mix = c.map((cv, i) => cv + (t[i] - cv) * ratio);
  return `#${toHex2(mix[0])}${toHex2(mix[1])}${toHex2(mix[2])}`;
}

// Display colour for a live filament stroke/fill: light colours get a subtle
// grey tint so they don't disappear on the light background; other colours
// pass through unchanged.
export function visibleFilamentColor(color: string): string {
  return isLightColor(color) ? blendColor(color, BLEND_TARGET, BLEND_RATIO) : color;
}

// Feed-line-only display on the normal Filament tab. Nozzle / batch modes
// keep `visibleFilamentColor`. Handles unknown sentinel, fully transparent
// filament, and near-white (grey tint). Opaque colours keep their own alpha so
// a black spool paints a black line instead of reading as grey.
export function visibleFeedLineColor(color?: string): string {
  if (!color || isUnknownSentinel(color)) return STANDBY_COLOR;

  const alpha = alphaFromHex(color);

  if (alpha === 0) {
    return withAlpha(TRANSPARENT_DISPLAY_RGB, OPAQUE_LINE_ALPHA);
  }

  const tintedRgb = isLightColor(color)
    ? blendColor(color, BLEND_TARGET, BLEND_RATIO)
    : rgbHex(color);

  if (alpha !== null && alpha < 255) {
    return withAlpha(tintedRgb, alpha);
  }

  return tintedRgb;
}
