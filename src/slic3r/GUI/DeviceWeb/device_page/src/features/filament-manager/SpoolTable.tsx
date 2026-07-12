import { useCallback, useEffect, useMemo, useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { CandidateColor, Spool } from './types';
import { SpoolColorChip } from './SpoolColorChip';
import { PAGE_SIZES, DEFAULT_PAGE_SIZE, formatSpoolDisplayName, formatSlotLocation } from './constants';
import useStore from '../../store/AppStore';
import {
  cssBackgroundFor,
  hexLabelFor,
  resolveCandidateForSpool,
} from './colors';


function getDisplayedRemainWeight(s: Spool) {
  if (typeof s.net_weight === 'number' && s.net_weight > 0) {
    return Math.round(s.net_weight);
  }
  return Math.round((s.initial_weight || 0) * (s.remain_percent || 0) / 100);
}

function getDisplayedTotalWeight(s: Spool) {
  // STUDIO-17991: show the initial *net* weight (recorded gross weight
  // minus the empty-spool weight) so the "A g / B g" ratio stays in a
  // single unit with the left-hand Remain column, which is also net
  // weight. Using initial_weight directly leaks the gross value — e.g. a
  // Bambu PLA Basic spool stores 1250 g initial + 250 g empty, producing
  // a mismatched "1000 g / 1250 g" against the net Remain. Back-deriving
  // from net_weight / remain_percent was even worse because the device
  // reports remain_percent as an integer percentage, so small remains
  // produced drifted denominators like 1013 g / 1250 g.
  const sw = s.spool_weight || 0;
  return Math.max(0, Math.round((s.initial_weight || 0) - sw));
}

// STUDIO-17977: list-row colour text + reverse-lookup helpers were
// extracted to `./colors`. Surface-specific wrappers (single Spool input)
// kept here so the JSX call sites stay short.
//
// `hexLabelFor` / `cssBackgroundFor` consume a generic ColorRenderInput,
// so we adapt a Spool by pulling its `colors[]` (multi-hex hint),
// `color_code` (single-hex fallback) and `color_type` (rendering tag).
function getDisplayColorInput(s: Spool, matched?: CandidateColor | null) {
  const matchedColors = Array.isArray(matched?.colors) ? matched.colors : [];
  const ownColors = Array.isArray(s.colors) ? s.colors : [];
  if (ownColors.length > 1) {
    return { colorCode: s.color_code, colors: ownColors, colorType: s.color_type };
  }
  if (matchedColors.length > 1) {
    return {
      colorCode: matchedColors[0] || s.color_code,
      colors: matchedColors,
      colorType: matched?.color_type,
    };
  }
  return { colorCode: s.color_code, colors: ownColors, colorType: s.color_type };
}

function getRowHexLabel(s: Spool, matched?: CandidateColor | null): string {
  const display = getDisplayColorInput(s, matched);
  return hexLabelFor({
    hexes: display.colors,
    primaryHex: display.colorCode,
  });
}
// Mini swatch background expression for the list row second line. Adapter
// over the shared `cssBackgroundFor` so the surface-specific call site
// stays compact. When the spool has no usable hex at all we return '' so
// the caller can skip rendering the swatch element entirely (instead of
// painting a fallback `#888` rectangle that would lie about the data).
function getRowSwatchBackground(s: Spool, matched?: CandidateColor | null): string {
  const display = getDisplayColorInput(s, matched);
  const label = hexLabelFor({ hexes: display.colors, primaryHex: display.colorCode });
  if (!label) return '';
  return cssBackgroundFor({
    hexes: display.colors,
    primaryHex: display.colorCode,
    colorType: display.colorType,
  });
}

/* ===== Grouping ===== */
interface SpoolGroup {
  key: string;
  count: number;
  totalWeight: number;
  spools: Spool[];
}

function groupSpools(list: Spool[]): SpoolGroup[] {
  const map: Record<string, SpoolGroup> = {};
  list.forEach((s) => {
    const key = formatSpoolDisplayName(s) || '?';
    if (!map[key]) map[key] = { key, count: 0, totalWeight: 0, spools: [] };
    map[key].count++;
    map[key].totalWeight += getDisplayedRemainWeight(s);
    map[key].spools.push(s);
  });
  return Object.values(map);
}

/* ===== Pagination helper ===== */
function buildPageRange(cur: number, total: number): (number | '...')[] {
  if (total <= 7) return Array.from({ length: total }, (_, i) => i + 1);
  const r: (number | '...')[] = [1];
  if (cur > 3) r.push('...');
  for (let i = Math.max(2, cur - 1); i <= Math.min(total - 1, cur + 1); i++) r.push(i);
  if (cur < total - 2) r.push('...');
  if (total > 1) r.push(total);
  return r;
}

const paginationButtonBase = 'min-w-6 h-6 flex items-center justify-center rounded-sm border text-xs cursor-pointer px-1 disabled:opacity-30 disabled:cursor-default';
const paginationButtonIdle = 'border-transparent bg-transparent text-fm-text-secondary hover:bg-fm-hover hover:text-fm-text-strong';
const paginationButtonActive = 'border-fm-border-focus bg-fm-selected text-fm-text-strong font-medium';
const tableHeaderCellClass = 'text-left px-6 pt-2 pb-[9px] align-middle text-sm font-normal text-fm-text-strong h-12 sticky top-0 bg-[#141414] [html[data-theme=light]_&]:bg-white z-10 select-none border-b border-fm-border';
const tableBodyCellClass = 'text-left px-6 pt-2 pb-[9px] border-b border-fm-border align-middle h-[60px]';

// Compare strings: put non-letter initial characters AFTER 'Z'
function compareFilamentStr(a: string, b: string): number {
  const aLetter = /^[A-Za-z]/.test(a);
  const bLetter = /^[A-Za-z]/.test(b);
  if (aLetter !== bLetter) return aLetter ? -1 : 1;
  return a.localeCompare(b, undefined, { sensitivity: 'base' });
}

/* ===== Sort header ===== */
type SortKey = 'brand' | 'remain_percent';
// Sort key + direction live in a single state so toggling stays atomic. The
// previous design (separate `sortKey` + `sortAsc` plus `setSortAsc(!sortAsc)`)
// captured both values from the render closure. Under React 19's automatic
// batching — combined with the parent re-rendering frequently while the user
// is on a paginated page (selection-sync effect on `filtered`, cloud-pull
// updates to `spools`) — two clicks on the same header could fire against the
// same handler closure before a commit, queuing the same `!sortAsc` value
// twice and silently swallowing one toggle. STUDIO-18126 reported this as
// "偶现分页下余料控件排序切换不使能". A functional `setSort((prev) => …)` reads
// the latest committed value for every queued update, so rapid clicks now
// toggle deterministically.
type SortState = { key: SortKey | ''; asc: boolean };

interface Props {
  spools: Spool[];
  selected: Set<string>;
  onToggleSelect: (id: string) => void;
  onSelectAll: () => void;
  grouped: boolean;
  onDetail: (id: string) => void;
  onAddSimilar: (id: string) => void;
  onEmptyAdd: () => void;
  /** Delete semantics are uniform: confirm -> remove from store -> cloud DELETE. */
  onDelete: (id: string) => void;
  /** Called whenever the sorted list changes, to sync parent-level navigation. */
  onSortedChange?: (sorted: Spool[]) => void;
}

export function SpoolTable({
  spools, selected, onToggleSelect, onSelectAll, grouped,
  onDetail, onAddSimilar, onEmptyAdd, onDelete, onSortedChange,
}: Props) {
  const { t } = useTranslation();
  const [sort, setSort] = useState<SortState>({ key: '', asc: true });
  const [page, setPage] = useState(1);
  const [pageSize, setPageSize] = useState(DEFAULT_PAGE_SIZE);
  const [collapsed, setCollapsed] = useState<Record<string, boolean>>({});

  // STUDIO-17977: per-fila_id candidate cache lives in the global store; the
  // row tail reads it to surface official colour name + BBL fila code for
  // each spool, including legacy spools whose own `color_name` field is
  // empty. The cache is primed on first dialog open by AddEditDialog and on
  // a best-effort prefetch from useFilamentManagerBridge after the spool list
  // loads, so the data is usually warm by the time the table renders.
  const candidatesByFilaId = useStore((s) => s.filament.candidatesByFilaId);

  // Sort
  const sorted = useMemo(() => {
    const { key: sortKey, asc: sortAsc } = sort;
    if (!sortKey) return spools;
    return [...spools].sort((a, b) => {
      if (sortKey === 'brand') {
        const brandCmp = compareFilamentStr(String(a.brand || ''), String(b.brand || ''));
        if (brandCmp !== 0) return sortAsc ? brandCmp : -brandCmp;
        const typeCmp = compareFilamentStr(
          String(a.series || a.material_type || ''),
          String(b.series || b.material_type || ''),
        );
        return sortAsc ? typeCmp : -typeCmp;
      }
      const va = (a as any)[sortKey];
      const vb = (b as any)[sortKey];
      if (typeof va === 'number' && typeof vb === 'number') return sortAsc ? va - vb : vb - va;
      const sa = String(va || ''), sb = String(vb || '');
      return sortAsc ? sa.localeCompare(sb) : sb.localeCompare(sa);
    });
  }, [spools, sort]);

  // Reset page when data changes
  const totalCount = sorted.length;

  // Build rows (flat or grouped)
  type FlatRow = { type: 'spool'; spool: Spool } | { type: 'group'; group: SpoolGroup };

  // Full grouping + spool order used for pagination. Independent of `collapsed`
  // so that folding a group never shifts which spools live on which page.
  const { allGroups, spoolsInGroupOrder } = useMemo(() => {
    if (!grouped) {
      return { allGroups: [] as SpoolGroup[], spoolsInGroupOrder: sorted };
    }
    const groups = groupSpools(sorted);
    const flat: Spool[] = [];
    groups.forEach((g) => g.spools.forEach((s) => flat.push(s)));
    return { allGroups: groups, spoolsInGroupOrder: flat };
  }, [sorted, grouped]);

  // Notify parent of the actual rendered order, for detail dialog navigation
  useEffect(() => {
    onSortedChange?.(spoolsInGroupOrder);
  }, [spoolsInGroupOrder, onSortedChange]);

  // Pagination is always driven by spool count. Group headers never consume
  // page slots.
  const totalSpoolCount = spoolsInGroupOrder.length;
  const pages = Math.max(1, Math.ceil(totalSpoolCount / pageSize));
  const safePage = Math.min(page, pages);
  const startIdx = (safePage - 1) * pageSize;
  const endIdx = Math.min(startIdx + pageSize, totalSpoolCount);

  const pageRows = useMemo<FlatRow[]>(() => {
    if (!grouped) {
      return spoolsInGroupOrder
        .slice(startIdx, endIdx)
        .map((s) => ({ type: 'spool' as const, spool: s }));
    }
    const inPageIds = new Set<string>();
    for (let i = startIdx; i < endIdx; i++) inPageIds.add(spoolsInGroupOrder[i].spool_id);
    const rows: FlatRow[] = [];
    allGroups.forEach((g) => {
      const inPageSpools = g.spools.filter((s) => inPageIds.has(s.spool_id));
      if (inPageSpools.length === 0) return;
      rows.push({ type: 'group', group: g });
      if (!collapsed[g.key]) {
        inPageSpools.forEach((s) => rows.push({ type: 'spool', spool: s }));
      }
    });
    return rows;
  }, [allGroups, spoolsInGroupOrder, grouped, collapsed, startIdx, endIdx]);

  const handleSort = useCallback((key: SortKey) => {
    setSort((prev) => (prev.key === key ? { key, asc: !prev.asc } : { key, asc: true }));
  }, []);

  const toggleGroup = (key: string) => {
    setCollapsed((prev) => ({ ...prev, [key]: !prev[key] }));
  };

  if (totalCount === 0) {
    return (
      <div className="flex-1 min-h-0 flex flex-col items-center justify-center text-fm-text-detail gap-4 bg-fm-inner2 rounded-[16px] border border-fm-border">
        <button
          type="button"
          className="relative w-12 h-12 rounded-lg border border-fm-border bg-fm-inner2 text-fm-text-detail cursor-pointer transition-colors duration-150 hover:bg-fm-hover hover:text-fm-text-primary"
          onClick={onEmptyAdd}
          title={t('Add Filament')}
          aria-label={t('Add Filament')}
        >
          <span className="absolute inset-2 rounded border-2 border-current opacity-55" />
          <span className="absolute left-1/2 top-1/2 h-[2px] w-4 -translate-x-1/2 -translate-y-1/2 rounded-sm bg-current opacity-55" />
          <span className="absolute left-1/2 top-1/2 h-4 w-[2px] -translate-x-1/2 -translate-y-1/2 rounded-sm bg-current opacity-55" />
        </button>
        <p>{t('No Data')}</p>
      </div>
    );
  }

  return (
    <>
      <div className="flex-1 min-h-0 bg-[#141414] [html[data-theme=light]_&]:bg-white rounded-[16px] border border-fm-border p-3 relative overflow-hidden">
        <div className="h-full overflow-auto px-3">
        <table className="w-full border-separate border-spacing-0">
          <colgroup>
            <col style={{ width: 48 }} />
            <col style={{ minWidth: 200 }} />
            <col style={{ width: 180 }} />
            <col style={{ width: 140 }} />
          </colgroup>
          <thead>
            <tr>
              <th className={`${tableHeaderCellClass} !px-4 w-12 text-center`}>
                <input
                  type="checkbox"
                  className="w-4 h-4 cursor-pointer accent-fm-brand"
                  checked={selected.size > 0 && selected.size === spools.length}
                  onChange={onSelectAll}
                />
              </th>
              <ThSort label={t('Filament')} sortKey="brand" current={sort.key} asc={sort.asc} onClick={handleSort} />
              <ThSort label={t('Remain')} sortKey="remain_percent" current={sort.key} asc={sort.asc} onClick={handleSort} />
              <th className={tableHeaderCellClass}>{t('Operation')}</th>
            </tr>
          </thead>
          <tbody>
            {pageRows.map((row) => {
              if (row.type === 'group') {
                const g = row.group;
                const isCollapsed = !!collapsed[g.key];
                return (
                  <tr
                    key={`g-${g.key}`}
                    data-testid="filament-group-row"
                    data-group-key={g.key}
                    data-group-count={g.count}
                    data-group-weight={g.totalWeight}
                    className="cursor-pointer select-none"
                    onClick={() => toggleGroup(g.key)}
                  >
                    <td colSpan={4} className="bg-fm-inner border-b border-fm-border px-6 pt-2 pb-[9px] h-8">
                      <div className="flex items-center gap-3 text-xs text-fm-text-secondary">
                        <span className={`inline-block w-3 transition-transform duration-150${isCollapsed ? ' -rotate-90' : ''}`}>▾</span>
                        <span>{g.key}</span>
                        <span className="bg-fm-input rounded-[10px] px-2 py-[1px] text-[11px]">{g.count}</span>
                        <span className="text-fm-text-detail">{g.totalWeight} g</span>
                      </div>
                    </td>
                  </tr>
                );
              }
              const s = row.spool;
              const remain = s.remain_percent || 0;
              const pClass = remain === 0 ? 'empty' : remain < 20 ? 'low' : '';
              const remainWeight = getDisplayedRemainWeight(s);
              const totalWeight = getDisplayedTotalWeight(s);
              const nameParts = formatSpoolDisplayName(s);
              const matched = resolveCandidateForSpool(s, candidatesByFilaId);
              const displayColor = getDisplayColorInput(s, matched);
              return (
                <tr
                  key={s.spool_id}
                  data-testid={`filament-row-${s.spool_id}`}
                  data-color-code={s.color_code}
                  data-color-type={s.color_type ?? 2}
                  className={`transition-colors duration-100 hover:bg-fm-hover${selected.has(s.spool_id) ? ' bg-fm-selected' : ''}`}
                >
                  <td className={`${tableBodyCellClass} !px-4 w-12 text-center`}>
                    <input
                      type="checkbox"
                      className="w-4 h-4 cursor-pointer accent-fm-brand"
                      checked={selected.has(s.spool_id)}
                      onChange={() => onToggleSelect(s.spool_id)}
                    />
                  </td>
                  <td className={tableBodyCellClass}>
                    <div className="flex items-center gap-3">
                      <div data-testid="filament-row-color" data-color-code={displayColor.colorCode} className="w-10 h-10 shrink-0 relative">
                        <SpoolColorChip
                          colorCode={displayColor.colorCode}
                          colors={displayColor.colors}
                          colorType={displayColor.colorType}
                          amsBadge={s.in_printer === true}
                        />
                      </div>
                      <div className="flex flex-col gap-[2px] min-w-0">
                        <span data-testid="filament-row-name" className="text-sm text-fm-text-primary leading-[22px]">{nameParts || '—'}</span>
                        {/* STUDIO-17977: align with AddEditDialog preview-bar
                            and DetailDialog color field — show the same
                            "[swatch] colorName · #RRGGBB / #RRGGBB" tail so
                            the list row, the detail dialog, and the add/edit
                            preview-bar all read the same.  Even though the
                            left-hand 40×40 SpoolColorChip already conveys the
                            colour via the spool icon, PM feedback is that an
                            independent rectangular swatch in the text tail
                            is what users scan for; the icon shape draws the
                            eye away from the colour itself.  Swatch routes
                            through the same gradient/strip rules as the
                            preview-bar so multi-hex spools render correctly
                            in the row tail too. */}
                        <div className="flex items-center gap-1 text-xs text-fm-text-secondary opacity-70 leading-[19px] min-w-0">
                          <span className="shrink-0">{s.diameter || 1.75} mm</span>
                          {(() => {
                            const hexLabel = getRowHexLabel(s, matched);
                            const swatchBg = getRowSwatchBackground(s, matched);
                            // Prefer the official candidate (matched by hex
                            // multiset against `candidatesByFilaId[setting_id]`)
                            // for both colorName and BBL fila code; this gives
                            // legacy spools the same "蓝粉双色 · 13903" tail
                            // that the AddEditDialog preview-bar shows. Falls
                            // back to the spool's own `color_name` field when
                            // the candidate cache hasn't primed yet (or the
                            // spool was saved without a setting_id).
                            const colorName = (matched?.name || s.color_name || '').trim();
                            const filaCode = (() => {
                              const code = (matched?.color_code || '').trim();
                              // Skip when the candidate "color_code" is itself
                              // a hex (user-owned spool layer): we already
                              // print the hex separately, no point doubling.
                              if (!code) return '';
                              if (code.startsWith('#')) return '';
                              if (/^[0-9A-F]{6,8}$/i.test(code)) return '';
                              return code;
                            })();
                            if (!colorName && !hexLabel && !swatchBg && !filaCode) return null;
                            return (
                              <>
                                <span className="shrink-0">|</span>
                                {swatchBg && (
                                  <span
                                    data-testid="filament-row-color-swatch"
                                    data-swatch-bg={swatchBg}
                                    className="inline-flex shrink-0"
                                  >
                                    <SpoolColorChip
                                      colorCode={displayColor.colorCode}
                                      colors={displayColor.colors}
                                      colorType={displayColor.colorType}
                                      size={14}
                                      radius={3}
                                    />
                                  </span>
                                )}
                                {colorName && (
                                  <span data-testid="filament-row-color-name" className="truncate">{colorName}</span>
                                )}
                                {filaCode && (
                                  <span
                                    data-testid="filament-row-fila-code"
                                    className="text-fm-text-detail shrink-0"
                                    title={t('Bambu Color Code')}
                                  >
                                    · {filaCode}
                                  </span>
                                )}
                                {(colorName || filaCode) && hexLabel && <span className="shrink-0">·</span>}
                                {hexLabel && (
                                  <span data-testid="filament-row-color-hex" className="font-mono tracking-wider truncate">{hexLabel}</span>
                                )}
                                {s.in_printer === true && (() => {
                                  const loc = formatSlotLocation(s.device_name, s.ams_type, s.slot_id, t, s.tray_label);
                                  if (!loc) return null;
                                  return (
                                    <>
                                      <span className="shrink-0">|</span>
                                      <span
                                        data-testid="filament-row-location"
                                        className="text-fm-text-detail shrink-0 font-mono text-[11px]"
                                      >
                                        {loc}
                                      </span>
                                    </>
                                  );
                                })()}
                              </>
                            );
                          })()}
                        </div>
                      </div>
                    </div>
                  </td>
                  <td className={tableBodyCellClass}>
                    <div className="flex flex-col gap-1">
                      <div className="flex items-baseline gap-[2px] whitespace-nowrap">
                        <span className="text-sm text-fm-text-primary leading-[22px]">{remainWeight} g</span>
                        <span className="text-xs text-fm-text-secondary leading-[19px]"> / </span>
                        <span className="text-xs text-fm-text-secondary leading-[19px]">{totalWeight} g</span>
                      </div>
                      <div className="w-[164px] h-[6px] bg-fm-input rounded-sm overflow-hidden">
                        <div className={`h-full rounded-full transition-[width] duration-300${pClass === 'empty' ? ' bg-fm-danger' : pClass === 'low' ? ' bg-fm-warning' : ' bg-fm-brand'}`} style={{ width: `${remain}%` }} />
                      </div>
                    </div>
                  </td>
                  <td className={tableBodyCellClass}>
                    <div className="flex gap-2 items-center">
                      <button data-testid="filament-row-detail" className="w-4 h-4 bg-transparent border-none cursor-pointer text-fm-brand p-0 flex items-center justify-center transition-colors duration-150 hover:text-fm-brand-hover [&>svg]:w-4 [&>svg]:h-4" onClick={() => onDetail(s.spool_id)} title={t('Spool Detail')}>
                        <svg viewBox="0 0 16 16" fill="none">
                          <path d="M3 2.5h6.5L13 6v7.5H3V2.5z" stroke="currentColor" strokeWidth="1.1" />
                          <path d="M9.5 2.5V6H13" stroke="currentColor" strokeWidth="1.1" />
                          <circle cx="7.5" cy="9.5" r="2" stroke="currentColor" strokeWidth="1.1" />
                          <line x1="9" y1="11" x2="11" y2="13" stroke="currentColor" strokeWidth="1.1" />
                        </svg>
                      </button>
                      <button data-testid="filament-row-add-similar" className="w-4 h-4 bg-transparent border-none cursor-pointer text-fm-brand p-0 flex items-center justify-center transition-colors duration-150 hover:text-fm-brand-hover [&>svg]:w-4 [&>svg]:h-4" onClick={() => onAddSimilar(s.spool_id)} title={t('Add')}>
                        <svg viewBox="0 0 16 16" fill="none">
                          <path d="M3 2.5h6.5L13 6v7.5H3V2.5z" stroke="currentColor" strokeWidth="1.1" />
                          <path d="M9.5 2.5V6H13" stroke="currentColor" strokeWidth="1.1" />
                          <line x1="8" y1="7.5" x2="8" y2="11.5" stroke="currentColor" strokeWidth="1.2" />
                          <line x1="6" y1="9.5" x2="10" y2="9.5" stroke="currentColor" strokeWidth="1.2" />
                        </svg>
                      </button>
                      <button data-testid="filament-row-delete" className="w-4 h-4 bg-transparent border-none cursor-pointer text-fm-brand p-0 flex items-center justify-center transition-colors duration-150 hover:text-fm-brand-hover [&>svg]:w-4 [&>svg]:h-4" onClick={() => onDelete(s.spool_id)} title={t('Delete')}>
                        <svg viewBox="0 0 16 16" fill="none">
                          <path d="M4 5h8l-.6 8H4.6L4 5z" stroke="currentColor" strokeWidth="1.1" />
                          <path d="M6 3h4" stroke="currentColor" strokeWidth="1.1" />
                          <path d="M3 5h10" stroke="currentColor" strokeWidth="1.1" />
                          <path d="M6.5 7v4M9.5 7v4" stroke="currentColor" strokeWidth="1" />
                        </svg>
                      </button>
                    </div>
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
        </div>
      </div>

      {/* Pagination — always visible below the list (empty state returned earlier) */}
      <div className="flex items-center justify-end gap-1 py-3 shrink-0">
        <button
          className={`${paginationButtonBase} ${paginationButtonIdle}`}
          disabled={safePage <= 1}
          onClick={() => setPage((p) => Math.max(1, Math.min(p, pages) - 1))}
        >‹</button>
        {buildPageRange(safePage, pages).map((p, i) =>
          p === '...'
            ? <span key={`d${i}`} className="text-fm-text-detail text-xs px-[2px]">…</span>
            : <button
                key={p}
                className={`${paginationButtonBase} ${p === safePage ? paginationButtonActive : paginationButtonIdle}`}
                onClick={() => setPage(p)}
              >{p}</button>
        )}
        <button
          className={`${paginationButtonBase} ${paginationButtonIdle}`}
          disabled={safePage >= pages}
          onClick={() => setPage((p) => Math.min(pages, Math.min(p, pages) + 1))}
        >›</button>
        <select
          className="ml-3 bg-fm-inner2 border-none rounded-sm text-fm-text-primary text-xs px-1 py-[2px] cursor-pointer outline-none"
          value={pageSize}
          onChange={(e) => { setPageSize(Number(e.target.value)); setPage(1); }}
        >
          {PAGE_SIZES.map((s) => (
            <option key={s} value={s}>{s}{t('per page')}</option>
          ))}
        </select>
      </div>
    </>
  );
}

/* Sort header cell */
function ThSort({ label, sortKey, current, asc, onClick }: {
  label: string;
  sortKey: SortKey;
  current: string;
  asc: boolean;
  onClick: (k: SortKey) => void;
}) {
  const cls = current === sortKey ? (asc ? 'sort-asc' : 'sort-desc') : '';
  return (
    <th className={`${tableHeaderCellClass} cursor-pointer hover:text-fm-text-strong ${cls}`} onClick={() => onClick(sortKey)}>
      {label}<span className="fm-sort-icon" />
    </th>
  );
}

