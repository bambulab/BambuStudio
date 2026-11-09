// Placeholder state shown before the C++ host pushes (vite dev server, mock
// E2E). Synthetic data only — no real spool / device identifiers.
import type { FilamentSelectRow, FilamentSelectState } from './types';

function row(
  index: number,
  chipKey: string,
  name: string,
  extra: Partial<FilamentSelectRow> = {},
): FilamentSelectRow {
  return {
    row_key: `r${index}`,
    chip_key: chipKey,
    search_key: `${chipKey.replace('brand:', '')} ${name}`.toLowerCase(),
    name,
    weight_g: 1000,
    swatch: { color_type: 2, colors: [], color_code: '#00AE42' },
    show_bbl_logo: chipKey === 'brand:Bambu Lab',
    tail: { kind: 'none', slot_name: '' },
    selectable: true,
    ...extra,
  };
}

export const mockFilamentSelectState: FilamentSelectState = {
  theme: 'light',
  manager: {
    chips: [
      { key: 'all', kind: 'all', label: '' },
      { key: 'brand:Bambu Lab', kind: 'brand', label: 'Bambu Lab' },
      { key: 'brand:Generic', kind: 'brand', label: 'Generic' },
      { key: 'brand:Polymaker', kind: 'brand', label: 'Polymaker' },
      { key: 'brand:Elegoo', kind: 'brand', label: 'Elegoo' },
      { key: 'brand:eSUN', kind: 'brand', label: 'eSUN' },
      { key: 'brand:Overture', kind: 'brand', label: 'Overture' },
      { key: 'brand:SUNLU', kind: 'brand', label: 'SUNLU' },
      { key: 'other', kind: 'other', label: '' },
      { key: 'unsupported', kind: 'unsupported', label: '' },
    ],
    rows: [
      row(0, 'brand:Bambu Lab', 'PLA Basic', {
        tail: { kind: 'recent', slot_name: '' },
        swatch: { color_type: 2, colors: [], color_code: '#FFFFFF' },
        weight_g: 820,
      }),
      row(1, 'brand:Bambu Lab', 'PLA Silk+', {
        swatch: { color_type: 0, colors: ['#F4C430', '#C0C0C0'], color_code: '#F4C430' },
      }),
      row(2, 'brand:Bambu Lab', 'PLA Basic Gradient', {
        swatch: { color_type: 1, colors: ['#E53935', '#43A047', '#1E88E5'], color_code: '#E53935' },
      }),
      row(3, 'brand:Generic', 'Generic PETG Black', {
        swatch: { color_type: 2, colors: [], color_code: '#000000' },
        weight_g: 640,
      }),
      row(4, 'brand:Polymaker', 'Polymaker PolyTerra PLA Charcoal Black With A Long Name', {
        swatch: { color_type: 2, colors: [], color_code: '#333333' },
      }),
      row(5, 'brand:Elegoo', 'Elegoo PLA Clear', {
        swatch: { color_type: 2, colors: ['#00000000'], color_code: '#00000000' },
      }),
      row(6, 'brand:eSUN', 'eSUN PLA+ Red', {
        swatch: { color_type: 2, colors: [], color_code: '#D32F2F' },
      }),
      row(7, 'brand:Overture', 'Overture PLA White', {
        swatch: { color_type: 2, colors: [], color_code: '#FAFAFA' },
      }),
      row(8, 'brand:SUNLU', 'SUNLU PETG Blue', {
        swatch: { color_type: 2, colors: [], color_code: '#1565C0' },
      }),
      row(9, 'other', 'PLA Orange', {
        swatch: { color_type: 2, colors: [], color_code: '#FB8C00' },
      }),
      row(10, 'brand:Bambu Lab', 'PETG HF', {
        swatch: { color_type: 2, colors: [], color_code: '#9E9E9E' },
        tail: { kind: 'slot', slot_name: 'A1' },
        selectable: false,
      }),
      row(11, 'unsupported', 'Acme PA-CF Black', {
        swatch: { color_type: 2, colors: [], color_code: '#212121' },
        selectable: false,
      }),
    ],
  },
  presets: {
    brands: [
      { key: 'brand:Bambu Lab', kind: 'brand', label: 'Bambu Lab' },
      { key: 'brand:Generic', kind: 'brand', label: 'Generic' },
      { key: 'brand:Polymaker', kind: 'brand', label: 'Polymaker' },
      { key: 'other', kind: 'other', label: '' },
    ],
    aliases: {
      'brand:Bambu Lab': ['Bambu PLA Basic', 'Bambu PLA Matte', 'Bambu PETG HF', 'Bambu ABS'],
      'brand:Generic': ['Generic PLA', 'Generic PETG', 'Generic ABS', 'Generic TPU'],
      'brand:Polymaker': ['PolyLite PLA', 'PolyTerra PLA'],
      other: ['Custom PLA'],
    },
    initial_brand: 'brand:Generic',
    checked_alias: 'Generic PETG',
  },
};
