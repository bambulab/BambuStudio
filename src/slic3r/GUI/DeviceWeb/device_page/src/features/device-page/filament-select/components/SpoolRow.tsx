import { useTranslation } from 'react-i18next';
import { SpoolColorChip } from '../../../filament-manager/SpoolColorChip';
import logoUrl from '../assets/BambuStudioBlack.svg';
import type { FilamentSelectRow } from '../types';
import { MaskIcon } from './MaskIcon';

interface SpoolRowProps {
  row: FilamentSelectRow;
  selected: boolean;
  onSelect: (rowKey: string) => void;
}

// Same 28 px swatch as the native make_spool_color_chip(). A single-colour
// spool may still carry several hexes; the native dialog then paints only the
// first one, so only multicolour / gradient spools pass the full list.
function swatchColors(row: FilamentSelectRow): string[] {
  return row.swatch.color_type === 2 ? row.swatch.colors.slice(0, 1) : row.swatch.colors;
}

export function SpoolRow({ row, selected, onSelect }: SpoolRowProps) {
  const { t } = useTranslation();
  const dimmed = !row.selectable;
  const className = [
    'fs-row',
    dimmed ? 'fs-row--dimmed' : '',
    selected ? 'fs-row--selected' : '',
  ].filter(Boolean).join(' ');

  return (
    <div
      className={className}
      role="option"
      aria-selected={selected}
      aria-disabled={dimmed}
      data-testid={`filament-select-row-${row.row_key}`}
      onMouseDown={dimmed ? undefined : () => onSelect(row.row_key)}
    >
      <span className="fs-row__swatch">
        <SpoolColorChip
          size={28}
          colorCode={row.swatch.color_code}
          colors={swatchColors(row)}
          colorType={row.swatch.color_type}
        />
      </span>
      {row.show_bbl_logo && <MaskIcon src={logoUrl} width={13} height={16} className="fs-row__logo" />}
      <span className="fs-row__name" title={row.name}>{row.name}</span>
      <span className="fs-row__weight">{`${row.weight_g}g`}</span>
      <span className="fs-row__tail">
        {row.tail.kind === 'slot' && (
          <span className="fs-row__slot" data-testid="filament-select-row-slot">
            {t('{{slot}} slot', { slot: row.tail.slot_name })}
          </span>
        )}
        {row.tail.kind === 'recent' && !dimmed && (
          <span className="fs-row__badge" data-testid="filament-select-row-recent">{t('Recently used')}</span>
        )}
      </span>
    </div>
  );
}
