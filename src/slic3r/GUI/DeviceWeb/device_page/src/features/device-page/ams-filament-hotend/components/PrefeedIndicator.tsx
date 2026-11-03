import type { AmsFilamentHotendColorType } from '../types';
import prefeedUnknownUrl from '../assets/prefeed_unknown.svg';

export function PrefeedIndicator({
  displayIndex,
  colors,
  colorType,
  filaType,
}: {
  displayIndex: number;
  colors?: string[];
  colorType?: AmsFilamentHotendColorType;
  /** Empty / missing means unknown prefeed filament (Figma 1761:76285). */
  filaType?: string;
}) {
  const isUnknown = !filaType;
  const normalizedColors = colors?.filter(Boolean) ?? [];
  if (!isUnknown && normalizedColors.length === 0) return null;

  if (isUnknown) {
    return (
      <img
        className="prefeed-indicator prefeed-indicator--unknown"
        data-testid={`ams-hotend-prefeed-indicator-${displayIndex}`}
        data-unknown="true"
        src={prefeedUnknownUrl}
        width={14}
        height={14}
        alt="AMS pre-feed unknown"
        title="AMS pre-feed"
        draggable={false}
      />
    );
  }

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
