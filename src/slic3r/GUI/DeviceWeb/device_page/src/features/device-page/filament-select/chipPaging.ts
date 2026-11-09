// Pure list filtering and brand-chip paging of the Select Filament dialog.
// Mirrors FilamentSelectDialog::apply_filters() / refresh_chip_visibility()
// so the Web dialog pages the chips exactly like the native one.
import type { FilamentSelectChip, FilamentSelectRow } from './types';

/** Native: GetValue().Lower().Trim(false).Trim(true). */
export function normalizeQuery(query: string): string {
  return query.trim().toLowerCase();
}

export function rowMatchesSearch(row: FilamentSelectRow, normalizedQuery: string): boolean {
  return normalizedQuery === '' || row.search_key.includes(normalizedQuery);
}

/** A row is shown when it matches both the active chip and the search text. */
export function isRowVisible(row: FilamentSelectRow, activeChipKey: string, normalizedQuery: string): boolean {
  const chipOk = activeChipKey === '' || activeChipKey === 'all' || row.chip_key === activeChipKey;
  return chipOk && rowMatchesSearch(row, normalizedQuery);
}

/**
 * While searching, every chip except "All" is kept only if at least one row
 * under it matches the search text.
 */
export function chipsWithResults(
  chips: FilamentSelectChip[],
  rows: FilamentSelectRow[],
  normalizedQuery: string,
): boolean[] {
  if (normalizedQuery === '') return chips.map(() => true);
  return chips.map((chip) => chip.kind === 'all'
    || rows.some((row) => row.chip_key === chip.key && rowMatchesSearch(row, normalizedQuery)));
}

export interface ChipPagingInput {
  /** Width of every chip including its trailing gap, in px. */
  widths: number[];
  /** Index of the pinned "All" chip, -1 when absent. */
  allIndex: number;
  /** chipsWithResults() output. */
  hasResults: boolean[];
  /** First brand chip shown, in units of the surviving (non-All) chip list. */
  offset: number;
  /** Width available to the chip strip, in px. */
  available: number;
}

export interface ChipPagingResult {
  visible: boolean[];
  /** Offset clamped into the surviving chip list. */
  offset: number;
  canLeft: boolean;
  canRight: boolean;
  /** Indices of the non-"All" chips that survive the search filter, in order. */
  surviving: number[];
  /** Last surviving-list position inside the window (-1 when none). */
  last: number;
  /** false until widths and the available width are known. */
  measured: boolean;
}

export function pageChips(input: ChipPagingInput): ChipPagingResult {
  const { widths, allIndex, hasResults, available } = input;
  const count = widths.length;

  // Surviving non-"All" chips; offset and arrows operate on this list only.
  const surviving: number[] = [];
  for (let i = 0; i < count; ++i) {
    if (i !== allIndex && hasResults[i]) surviving.push(i);
  }

  // Not laid out yet: show everything, arrows off (native returns early).
  if (available <= 0 || count === 0) {
    return {
      visible: widths.map(() => true),
      offset: 0,
      canLeft: false,
      canRight: false,
      surviving,
      last: surviving.length - 1,
      measured: false,
    };
  }

  // "All" is always visible; reserve its width.
  const allWidth = allIndex >= 0 ? widths[allIndex] : 0;

  const offset = Math.max(0, Math.min(input.offset, surviving.length - 1));

  // Greedy from the offset until the next chip no longer fits; always at least one.
  const brandAvailable = available - allWidth;
  let used = 0;
  let last = offset - 1;
  for (let k = offset; k < surviving.length; ++k) {
    const w = widths[surviving[k]];
    if (used + w > brandAvailable && last >= offset) break;
    used += w;
    last = k;
  }

  const visible = widths.map(() => false);
  if (allIndex >= 0) visible[allIndex] = true;
  for (let k = offset; k <= last; ++k) visible[surviving[k]] = true;

  return {
    visible,
    offset,
    canLeft: offset > 0,
    canRight: last < surviving.length - 1,
    surviving,
    last,
    measured: true,
  };
}

export interface ChipWindowGeometry {
  /** How far the chip track is shifted left: widths of the chips paged out on the left. */
  trackShift: number;
  /** Width of the clipping window: widths of the chips inside it (whole chips only). */
  windowWidth: number;
}

/**
 * Pixel geometry of the sliding chip strip for a pageChips() result. Widths
 * include each chip's trailing gap, matching how the track lays them out.
 */
export function chipWindowGeometry(
  widths: number[],
  surviving: number[],
  offset: number,
  last: number,
): ChipWindowGeometry {
  let trackShift = 0;
  for (let k = 0; k < offset && k < surviving.length; ++k) trackShift += widths[surviving[k]] ?? 0;
  let windowWidth = 0;
  for (let k = Math.max(0, offset); k <= last && k < surviving.length; ++k) windowWidth += widths[surviving[k]] ?? 0;
  return { trackShift, windowWidth };
}
