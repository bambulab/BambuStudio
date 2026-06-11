import type { AmsFilamentHotendColorType } from '../types';

export function PrefeedIndicator({
  displayIndex,
  colors,
  colorType,
}: {
  displayIndex: number;
  colors?: string[];
  colorType?: AmsFilamentHotendColorType;
}) {
  const normalizedColors = colors?.filter(Boolean) ?? [];
  if (normalizedColors.length === 0) return null;

  return (
    <span
      className="prefeed-indicator"
      data-testid={`ams-hotend-prefeed-indicator-${displayIndex}`}
      data-color-type={colorType ?? 'single'}
      style={{ background: colorBackground(normalizedColors, colorType) }}
      title="AMS pre-feed"
      aria-label={`ams-prefeed-${displayIndex}`}
    />
  );
}

function colorBackground(colors: string[], colorType?: AmsFilamentHotendColorType) {
  if (colors.length === 1) return colors[0];
  if (colorType === 'gradient') {
    return `linear-gradient(135deg, ${colors.join(', ')})`;
  }

  const step = 100 / colors.length;
  const stops = colors.flatMap((color, index) => {
    const start = Math.round(index * step);
    const end = Math.round((index + 1) * step);
    return [`${color} ${start}%`, `${color} ${end}%`];
  });
  return `conic-gradient(${stops.join(', ')})`;
}
