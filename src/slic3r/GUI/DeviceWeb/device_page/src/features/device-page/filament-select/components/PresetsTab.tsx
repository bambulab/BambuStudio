import { useTranslation } from 'react-i18next';
import arrowRightUrl from '../assets/chip_arrow_right.svg';
import type { FilamentSelectPresets } from '../types';
import { MaskIcon } from './MaskIcon';

interface PresetsTabProps {
  presets: FilamentSelectPresets;
  selectedBrand: string;
  onSelectBrand: (brandKey: string) => void;
  checkedAlias: string;
  onCheckAlias: (alias: string) => void;
}

export function PresetsTab({ presets, selectedBrand, onSelectBrand, checkedAlias, onCheckAlias }: PresetsTabProps) {
  const { t } = useTranslation();
  const aliases = presets.aliases[selectedBrand] ?? [];

  return (
    <div className="fs-presets" data-testid="filament-select-presets">
      <div className="fs-brand-list">
        {presets.brands.map((brand) => {
          const selected = brand.key === selectedBrand;
          return (
            <div
              key={brand.key}
              className={selected ? 'fs-brand-row fs-brand-row--selected' : 'fs-brand-row'}
              data-testid={`filament-select-brand-${brand.key}`}
              aria-selected={selected}
              role="option"
              onMouseDown={() => onSelectBrand(brand.key)}
            >
              <span className="fs-brand-row__label">{brand.kind === 'other' ? t('Other') : brand.label}</span>
              <MaskIcon src={arrowRightUrl} width={16} height={16} className="fs-brand-row__arrow" />
            </div>
          );
        })}
      </div>
      <div className="fs-divider" />
      <div className="fs-alias-list" role="listbox">
        {aliases.map((alias) => {
          const checked = alias === checkedAlias;
          return (
            <div
              key={alias}
              className={checked ? 'fs-alias-row fs-alias-row--checked' : 'fs-alias-row'}
              data-testid="filament-select-alias"
              aria-selected={checked}
              role="option"
              onMouseDown={() => onCheckAlias(alias)}
            >
              {alias}
            </div>
          );
        })}
      </div>
    </div>
  );
}
