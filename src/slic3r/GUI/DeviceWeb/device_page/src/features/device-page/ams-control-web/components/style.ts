import type { CSSProperties } from 'react';
import type { AmsColorType } from '../types';

export function cn(...parts: Array<string | false | null | undefined>) {
  return parts.filter(Boolean).join(' ');
}

interface Rgba {
  r: number;
  g: number;
  b: number;
  a: number;
}

function parseColor(hex: string): Rgba | null {
  const raw = hex.replace('#', '');
  if (raw.length < 6) return null;
  const r = parseInt(raw.slice(0, 2), 16);
  const g = parseInt(raw.slice(2, 4), 16);
  const b = parseInt(raw.slice(4, 6), 16);
  const a = raw.length >= 8 ? parseInt(raw.slice(6, 8), 16) : 255;
  if ([r, g, b, a].some((channel) => Number.isNaN(channel))) return null;
  return { r, g, b, a };
}

// wxColour::GetLuminance weights; compared against 0.6 for light-on-dark text.
function luminance({ r, g, b }: Rgba) {
  return (0.299 * r + 0.587 * g + 0.114 * b) / 255;
}

// C++ AMSLib paints these on the white lib canvas without darkModeColorFor.
const LIB_TEXT_DARK = '#323A3D';
const LIB_TEXT_LIGHT = '#FFFFFF';

export interface SlotContrast {
  textColor: string;
  light: boolean;
}

// Light glyphs when luminance < 0.6, except remain < 50 (colour only fills the
// lower half) and alpha outside 0 / 254 / 255, which stay dark.
export function slotContrast(options: {
  color: string;
  remain: number;
  showRemain: boolean;
  pale?: boolean;
}): SlotContrast {
  const dark: SlotContrast = { textColor: LIB_TEXT_DARK, light: false };
  if (options.pale) return dark;

  const rgba = parseColor(options.color);
  if (!rgba) return dark;

  if (rgba.a === 0) return dark;
  if (rgba.a !== 254 && rgba.a !== 255) return { textColor: '#000000', light: false };

  if (options.showRemain && options.remain < 50) return dark;

  return luminance(rgba) < 0.6 ? { textColor: LIB_TEXT_LIGHT, light: true } : dark;
}

export function slotFillRatio(remain: number, showRemain: boolean) {
  if (!showRemain) return 1;
  const clamped = remain < 0 || remain > 100 ? 100 : remain;
  // Below 10% still draw a 10% sliver so an almost-empty spool stays visible.
  return clamped < 10 ? 0.1 : clamped / 100;
}

function spoolColors(spool: { color: string; colors: string[] }) {
  return spool.colors.length > 0 ? spool.colors : [spool.color];
}

// AMSLib / AMSPreview multi: x = width * i / n, next = width * (i + 1) / n.
// Remainder pixels go to later bands. A CSS gradient on a 9px cube plus
// border-radius clips the last colour to a sliver.
export function colorBandRects(count: number, width: number) {
  const rects: { x: number; width: number }[] = [];
  for (let i = 0; i < count; i++) {
    const x = Math.trunc((width * i) / count);
    const next = Math.trunc((width * (i + 1)) / count);
    rects.push({ x, width: Math.max(0, next - x) });
  }
  return rects;
}

export function slotFill(spool: { color: string; colors: string[]; color_type: AmsColorType }) {
  const colors = spoolColors(spool);
  if (spool.color_type === 0 && colors.length >= 2) {
    const stops = colors.map((color, index) => `${color} ${(index / (colors.length - 1)) * 100}%`);
    return `linear-gradient(90deg, ${stops.join(', ')})`;
  }
  return colors[0] || spool.color;
}

export function previewCubeFill(cube: { color: string; colors: string[]; color_type: AmsColorType }) {
  return spoolColors(cube)[0] || cube.color;
}

export function isPaleColor(hex: string) {
  const rgba = parseColor(hex);
  if (!rgba) return true;
  if (rgba.a === 0) return true;
  return rgba.r >= 238 && rgba.g >= 238 && rgba.b >= 238;
}

export function isTransparentColor(hex: string) {
  const rgba = parseColor(hex);
  return !rgba || rgba.a === 0;
}

// C++ AMSLib: 0 = clear checker bitmap, 254/255 = solid, anything else = tinted checker.
export type FilamentAlphaKind = 'clear' | 'translucent' | 'opaque';

export function filamentAlphaKind(hex: string): FilamentAlphaKind {
  const rgba = parseColor(hex);
  if (!rgba || rgba.a === 0) return 'clear';
  if (rgba.a === 254 || rgba.a === 255) return 'opaque';
  return 'translucent';
}

function blendOverWhite(channel: number, amount: number) {
  return Math.round(channel * amount + 255 * (1 - amount));
}

function rgbCss(r: number, g: number, b: number) {
  return `rgb(${r}, ${g}, ${b})`;
}

// FilamentBitmapUtils::create_transparent_bitmap: white / #D9D9D9, 6px tiles.
export function clearCheckerStyle(): CSSProperties {
  return {
    backgroundImage: 'repeating-conic-gradient(#D9D9D9 0% 25%, #FEFFFE 0% 50%)',
    backgroundSize: '6px 6px',
  };
}

// FilamentBitmapUtils::get_translucent_checker_colors: 0.45 / 0.70 over white.
export function translucentCheckerStyle(hex: string): CSSProperties {
  const rgba = parseColor(hex);
  const light = rgba
    ? rgbCss(blendOverWhite(rgba.r, 0.45), blendOverWhite(rgba.g, 0.45), blendOverWhite(rgba.b, 0.45))
    : '#d9d9d9';
  const dark = rgba
    ? rgbCss(blendOverWhite(rgba.r, 0.7), blendOverWhite(rgba.g, 0.7), blendOverWhite(rgba.b, 0.7))
    : '#ffffff';
  return {
    backgroundImage: `repeating-conic-gradient(${light} 0% 25%, ${dark} 0% 50%)`,
    backgroundSize: '6px 6px',
  };
}
