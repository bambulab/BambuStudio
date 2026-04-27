export function normalizeColorCode(value?: string): string {
  const raw = (value || '').trim();
  if (!raw) return '';
  const hex = raw.startsWith('#') ? raw : `#${raw}`;
  return hex.slice(0, 7).toUpperCase();
}

export function isPresetColor(value: string, presetColors: string[]): boolean {
  return presetColors.some((c) => c.toUpperCase() === value.toUpperCase());
}

export function commitCustomColorSelection(
  value: string,
  customColors: string[],
  presetColors: string[],
): { colorCode: string; customColors: string[] } {
  const nextColor = normalizeColorCode(value);
  if (!nextColor || isPresetColor(nextColor, presetColors)) {
    return { colorCode: nextColor, customColors };
  }

  if (customColors.some((c) => c.toUpperCase() === nextColor.toUpperCase())) {
    return { colorCode: nextColor, customColors };
  }

  return { colorCode: nextColor, customColors: [...customColors, nextColor] };
}
