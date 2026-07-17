import type { CSSProperties } from 'react';
import type { RefillColorType } from '../types';

interface FilamentColorSwatchProps {
  color?: string;
  colors?: string[];
  colorType?: RefillColorType;
}

const FALLBACK_COLOR = '#dbdbdb';
const CHECKER_PATTERN =
  'conic-gradient(#d9d9d9 25%, #ffffff 0 50%, #d9d9d9 0 75%, #ffffff 0)';

function normalizeColor(value?: string): string {
  if (!value) return FALLBACK_COLOR;
  if (value === 'transparent' || value.startsWith('#')) return value;
  return `#${value}`;
}

function hasTransparency(color: string): boolean {
  if (color === 'transparent') return true;
  return /^#[0-9a-f]{8}$/i.test(color) && Number.parseInt(color.slice(7, 9), 16) < 255;
}

function gradientStops(colors: string[]): string {
  if (colors.length === 1) return `${colors[0]} 0%, ${colors[0]} 100%`;
  return colors
    .map((color, index) => `${color} ${(index / (colors.length - 1)) * 100}%`)
    .join(', ');
}

function multiColorStops(colors: string[]): string {
  const step = 100 / colors.length;
  return colors
    .flatMap((color, index) => [`${color} ${index * step}%`, `${color} ${(index + 1) * step}%`])
    .join(', ');
}

function materialFill(colors: string[], colorType?: RefillColorType): string {
  if (colorType === 'gradient' && colors.length > 1) {
    return `radial-gradient(circle, ${gradientStops(colors)})`;
  }
  if (colorType === 'multi' && colors.length > 1) {
    return `linear-gradient(to bottom, ${multiColorStops(colors)})`;
  }
  return `linear-gradient(${colors[0]}, ${colors[0]})`;
}

function swatchStyle(color?: string, sourceColors?: string[], colorType?: RefillColorType): CSSProperties {
  const colors = (sourceColors?.length ? sourceColors : [color]).map(normalizeColor);
  const fill = materialFill(colors, colorType);

  if (colors.some(hasTransparency)) {
    return {
      backgroundColor: '#ffffff',
      backgroundImage: `${fill}, ${CHECKER_PATTERN}`,
      backgroundPosition: 'center, center',
      backgroundSize: '100% 100%, 6px 6px',
    };
  }

  return { backgroundImage: fill };
}

export function FilamentColorSwatch({ color, colors, colorType }: FilamentColorSwatchProps) {
  return (
    <span
      className="refill-slot__swatch"
      style={swatchStyle(color, colors, colorType)}
      aria-hidden="true"
    />
  );
}
