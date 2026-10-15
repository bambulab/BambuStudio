import type { RefillColorType } from '../types';
import { SpoolColorChip } from '../../../filament-manager/SpoolColorChip';

interface FilamentColorSwatchProps {
  color?: string;
  colors?: string[];
  colorType?: RefillColorType;
}

const COLOR_TYPE_MAP: Record<RefillColorType, 0 | 1 | 2> = { gradient: 0, multi: 1, single: 2 };

export function FilamentColorSwatch({ color, colors, colorType }: FilamentColorSwatchProps) {
  return (
    <span className="refill-slot__swatch" aria-hidden="true">
      <SpoolColorChip
        colorCode={color}
        colors={colors}
        colorType={colorType ? COLOR_TYPE_MAP[colorType] : undefined}
        size={16}
        radius={4}
        border={false}
      />
    </span>
  );
}
