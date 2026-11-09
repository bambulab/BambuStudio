// Defensive normalisation of the C++ payload: a missing or malformed field
// degrades to an empty value instead of crashing the page.
import type {
  FilamentSelectChip,
  FilamentSelectChipKind,
  FilamentSelectRow,
  FilamentSelectState,
  FilamentSelectTailKind,
} from './types';

type Loose = Record<string, unknown>;

const CHIP_KINDS: readonly FilamentSelectChipKind[] = ['all', 'brand', 'other', 'unsupported'];
const TAIL_KINDS: readonly FilamentSelectTailKind[] = ['slot', 'recent', 'none'];

function asObject(v: unknown): Loose {
  return v && typeof v === 'object' && !Array.isArray(v) ? (v as Loose) : {};
}

function asString(v: unknown): string {
  return typeof v === 'string' ? v : '';
}

function asArray(v: unknown): unknown[] {
  return Array.isArray(v) ? v : [];
}

function normalizeChip(raw: unknown): FilamentSelectChip {
  const o = asObject(raw);
  const kind = CHIP_KINDS.includes(o.kind as FilamentSelectChipKind)
    ? (o.kind as FilamentSelectChipKind)
    : 'brand';
  return { key: asString(o.key), kind, label: asString(o.label) };
}

function normalizeRow(raw: unknown): FilamentSelectRow {
  const o = asObject(raw);
  const swatch = asObject(o.swatch);
  const tail = asObject(o.tail);
  const colorType = swatch.color_type === 0 || swatch.color_type === 1 ? swatch.color_type : 2;
  return {
    row_key: asString(o.row_key),
    chip_key: asString(o.chip_key),
    search_key: asString(o.search_key),
    name: asString(o.name),
    weight_g: typeof o.weight_g === 'number' && Number.isFinite(o.weight_g) ? o.weight_g : 0,
    swatch: {
      color_type: colorType,
      colors: asArray(swatch.colors).filter((c): c is string => typeof c === 'string'),
      color_code: asString(swatch.color_code),
    },
    show_bbl_logo: o.show_bbl_logo === true,
    tail: {
      kind: TAIL_KINDS.includes(tail.kind as FilamentSelectTailKind) ? (tail.kind as FilamentSelectTailKind) : 'none',
      slot_name: asString(tail.slot_name),
    },
    selectable: o.selectable === true,
  };
}

export function normalizeFilamentSelectState(raw: unknown): FilamentSelectState {
  const o = asObject(raw);
  const manager = asObject(o.manager);
  const presets = asObject(o.presets);
  const aliasesRaw = asObject(presets.aliases);
  const aliases: Record<string, string[]> = {};
  for (const [key, list] of Object.entries(aliasesRaw)) {
    aliases[key] = asArray(list).filter((a): a is string => typeof a === 'string');
  }
  return {
    theme: o.theme === 'dark' ? 'dark' : 'light',
    manager: {
      chips: asArray(manager.chips).map(normalizeChip),
      rows: asArray(manager.rows).map(normalizeRow),
    },
    presets: {
      brands: asArray(presets.brands).map(normalizeChip),
      aliases,
      initial_brand: asString(presets.initial_brand),
      checked_alias: asString(presets.checked_alias),
    },
  };
}
