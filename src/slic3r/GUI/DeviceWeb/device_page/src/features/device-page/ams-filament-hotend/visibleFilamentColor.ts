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

function parseHex(color: string): [number, number, number] | null {
  const hex = color.trim().replace(/^#/, '').slice(0, 6);
  if (!/^[0-9a-fA-F]{6}$/.test(hex)) return null;
  return [
    parseInt(hex.slice(0, 2), 16),
    parseInt(hex.slice(2, 4), 16),
    parseInt(hex.slice(4, 6), 16),
  ];
}

function isLightColor(color: string): boolean {
  const rgb = parseHex(color);
  if (!rgb) return false;
  const [r, g, b] = rgb.map((v) => v / 255);
  return (0.2126 * r + 0.7152 * g + 0.0722 * b) > LIGHT_LUMINANCE_THRESHOLD;
}

function toHex2(v: number): string {
  return Math.round(Math.min(255, Math.max(0, v))).toString(16).padStart(2, '0');
}

// Linearly mix `color` towards `target` by `ratio` (0 keeps color, 1 = target).
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
