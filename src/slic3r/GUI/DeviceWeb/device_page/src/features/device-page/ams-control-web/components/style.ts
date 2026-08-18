import type { AmsColorType } from '../types';

export interface ColorSpec {
  color: string;
  colors: string[];
  color_type: AmsColorType;
}

export function cn(...parts: Array<string | false | null | undefined>) {
  return parts.filter(Boolean).join(' ');
}

export function isLightColor(hex: string) {
  const raw = hex.replace('#', '');
  if (raw.length < 6) return true;
  const r = parseInt(raw.slice(0, 2), 16);
  const g = parseInt(raw.slice(2, 4), 16);
  const b = parseInt(raw.slice(4, 6), 16);
  return (0.299 * r + 0.587 * g + 0.114 * b) / 255 > 0.72;
}

export function slotFill(spec: ColorSpec) {
  const colors = spec.colors.length > 0 ? spec.colors : [spec.color];
  if (spec.color_type === 0 && colors.length >= 2) {
    return `linear-gradient(180deg, ${colors.join(', ')})`;
  }
  if (spec.color_type === 1 && colors.length >= 2) {
    const stops = colors.map((color, index) => {
      const start = (index / colors.length) * 100;
      const end = ((index + 1) / colors.length) * 100;
      return `${color} ${start}% ${end}%`;
    });
    return `linear-gradient(180deg, ${stops.join(', ')})`;
  }
  return colors[0] || '#D9D9D9';
}

// 1..5 humidity icon index (5 = driest) shown as A..E, matching the classic
// panel's five humidity levels.
export function humidityLetter(displayIdx: number) {
  if (displayIdx < 1 || displayIdx > 5) return '';
  return String.fromCharCode('A'.charCodeAt(0) + displayIdx - 1);
}

// A slot card is a fixed width, so a panel only needs room for the cards it holds
// plus the gaps between them and its own padding. That makes a single-slot AMS
// come out exactly as wide as an external spool, and keeps a four-slot unit at the
// 16.5rem it has always been. SLOT_WIDTH_REM mirrors SlotCard's `w-[3.25rem]`.
const SLOT_WIDTH_REM = 3.25;
const SLOT_GAP_REM = 2 / 3;
const PANEL_PADDING_REM = 1.5;

export function panelMinWidth(slotCount: number) {
  const slots = Math.max(slotCount, 1);
  return `${SLOT_WIDTH_REM * slots + SLOT_GAP_REM * (slots - 1) + PANEL_PADDING_REM}rem`;
}

export function remainPercent(remain: number) {
  if (remain < 0 || remain > 100) return 100;
  return remain;
}

// Colour of a filament line: grey while it carries nothing.
export function lineColor(state: string, color: string) {
  if (state === 'idle' || !color) return '#C2C2C2';
  return color;
}
