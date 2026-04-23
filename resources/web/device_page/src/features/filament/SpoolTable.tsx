import { useMemo, useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { Spool } from './types';
import { SpoolSvg } from './SpoolSvg';
import { PAGE_SIZES, DEFAULT_PAGE_SIZE, formatSpoolDisplayName } from './constants';

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

/* ===== Grouping ===== */
interface SpoolGroup {
  key: string;
  count: number;
  syncedCount: number;
  totalWeight: number;
  spools: Spool[];
}

function groupSpools(list: Spool[]): SpoolGroup[] {
  const map: Record<string, SpoolGroup> = {};
  list.forEach((s) => {
    const key = `${formatSpoolDisplayName(s) || '?'}/${s.color_name || s.color_code || '?'}`;
    if (!map[key]) map[key] = { key, count: 0, syncedCount: 0, totalWeight: 0, spools: [] };
    map[key].count++;
    if (s.cloud_synced) map[key].syncedCount++;
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

/* ===== Sort header ===== */
type SortKey = 'brand' | 'remain_percent';

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
}

export function SpoolTable({
  spools, selected, onToggleSelect, onSelectAll, grouped,
  onDetail, onAddSimilar, onEmptyAdd, onDelete,
}: Props) {
  const { t } = useTranslation();
  const [sortKey, setSortKey] = useState<SortKey | ''>('');
  const [sortAsc, setSortAsc] = useState(true);
  const [page, setPage] = useState(1);
  const [pageSize, setPageSize] = useState(DEFAULT_PAGE_SIZE);
  const [collapsed, setCollapsed] = useState<Record<string, boolean>>({});

  // Sort
  const sorted = useMemo(() => {
    if (!sortKey) return spools;
    return [...spools].sort((a, b) => {
      const va = (a as any)[sortKey];
      const vb = (b as any)[sortKey];
      if (typeof va === 'number' && typeof vb === 'number') return sortAsc ? va - vb : vb - va;
      const sa = String(va || ''), sb = String(vb || '');
      return sortAsc ? sa.localeCompare(sb) : sb.localeCompare(sa);
    });
  }, [spools, sortKey, sortAsc]);

  // Reset page when data changes
  const totalCount = sorted.length;

  // Build rows (flat or grouped)
  type FlatRow = { type: 'spool'; spool: Spool } | { type: 'group'; group: SpoolGroup };

  const flatRows = useMemo<FlatRow[]>(() => {
    if (!grouped) return sorted.map((s) => ({ type: 'spool' as const, spool: s }));
    const groups = groupSpools(sorted);
    const rows: FlatRow[] = [];
    groups.forEach((g) => {
      rows.push({ type: 'group', group: g });
      if (!collapsed[g.key]) g.spools.forEach((s) => rows.push({ type: 'spool', spool: s }));
    });
    return rows;
  }, [sorted, grouped, collapsed]);

  const totalRows = flatRows.length;
  const pages = Math.max(1, Math.ceil(totalRows / pageSize));
  const safePage = Math.min(page, pages);
  const pageRows = flatRows.slice((safePage - 1) * pageSize, safePage * pageSize);

  const handleSort = (key: SortKey) => {
    if (sortKey === key) setSortAsc(!sortAsc);
    else { setSortKey(key); setSortAsc(true); }
  };

  const toggleGroup = (key: string) => {
    setCollapsed((prev) => ({ ...prev, [key]: !prev[key] }));
  };

  if (totalCount === 0) {
    return (
      <div className="flex flex-col items-center justify-center py-20 text-fm-text-detail gap-4">
        <button
          type="button"
          className="w-12 h-12 rounded-lg border border-fm-border bg-fm-inner2 text-fm-text-detail cursor-pointer transition-colors duration-150 hover:bg-fm-hover hover:text-fm-text-primary"
          onClick={onEmptyAdd}
          title={t('Add Filament')}
          aria-label={t('Add Filament')}
        >
          <svg width="48" height="48" viewBox="0 0 48 48" fill="none" opacity="0.55">
            <rect x="8" y="8" width="32" height="32" rx="4" stroke="currentColor" strokeWidth="2" />
            <path d="M16 24h16M24 16v16" stroke="currentColor" strokeWidth="2" />
          </svg>
        </button>
        <p>{t('No Data')}</p>
      </div>
    );
  }

  return (
    <>
      <div className="flex-1 overflow-auto bg-fm-inner rounded-lg px-6 relative">
        <table className="w-full border-collapse">
          <colgroup>
            <col style={{ width: 48 }} />
            <col style={{ minWidth: 200 }} />
            <col style={{ width: 180 }} />
            <col style={{ width: 140 }} />
          </colgroup>
          <thead>
            <tr>
              <th className="text-left p-2 border-b border-[#292929] align-middle text-sm font-medium text-fm-text-primary h-12 sticky top-0 bg-fm-inner z-[2] select-none w-12 text-center">
                <input
                  type="checkbox"
                  className="w-4 h-4 cursor-pointer accent-fm-brand"
                  checked={selected.size > 0 && selected.size === spools.length}
                  onChange={onSelectAll}
                />
              </th>
              <ThSort label={t('Filament')} sortKey="brand" current={sortKey} asc={sortAsc} onClick={handleSort} />
              <ThSort label={t('Remain')} sortKey="remain_percent" current={sortKey} asc={sortAsc} onClick={handleSort} />
              <th className="text-left p-2 border-b border-[#292929] align-middle text-sm font-medium text-fm-text-primary h-12 sticky top-0 bg-fm-inner z-[2] select-none">{t('Operation')}</th>
            </tr>
          </thead>
          <tbody>
            {pageRows.map((row) => {
              if (row.type === 'group') {
                const g = row.group;
                const isCollapsed = !!collapsed[g.key];
                return (
                  <tr key={`g-${g.key}`} className="cursor-pointer select-none" onClick={() => toggleGroup(g.key)}>
                    <td colSpan={4} className="bg-white/[0.02] border-b border-fm-border p-2 h-10">
                      <div className="flex items-center gap-3 text-xs text-fm-text-secondary">
                        <span className={`inline-block w-3 transition-transform duration-150${isCollapsed ? ' -rotate-90' : ''}`}>▾</span>
                        <span>{g.key}</span>
                        <span className="bg-fm-input rounded-[10px] px-2 py-[1px] text-[11px]">{g.count}</span>
                        <span className="text-fm-text-detail">{g.totalWeight} g</span>
                        {/* F2.1: cloud-sync summary for the group — mirrors the
                            per-row green dot (figma: 已同步=绿点, 未同步=无). */}
                        {g.syncedCount > 0 && (
                          <span
                            className="inline-flex items-center gap-[4px] text-[11px] text-fm-text-detail"
                            title={t('Cloud · synced')}
                          >
                            <span
                              className="inline-block w-[6px] h-[6px] rounded-full border border-black/40"
                              style={{ background: '#50e81d' }}
                            />
                            {g.syncedCount}/{g.count}
                          </span>
                        )}
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
              return (
                <tr key={s.spool_id} className={`transition-colors duration-100 hover:bg-fm-hover${selected.has(s.spool_id) ? ' bg-fm-selected' : ''}`}>
                  <td className="text-left p-2 border-b border-[#292929] align-middle w-12 text-center">
                    <input
                      type="checkbox"
                      className="w-4 h-4 cursor-pointer accent-fm-brand"
                      checked={selected.has(s.spool_id)}
                      onChange={() => onToggleSelect(s.spool_id)}
                    />
                  </td>
                  <td className="text-left p-2 border-b border-[#292929] align-middle">
                    <div className="flex items-center gap-3">
                      <div
                        className="w-10 h-10 shrink-0 relative"
                        title={s.cloud_synced ? t('Cloud · synced') : t('No sync yet')}
                      >
                        <SpoolSvg color={s.color_code} />
                        {/* F2.1: cloud-sync indicator — only rendered when
                            `cloud_synced==true`; absence of the dot means
                            "not synced" (matches figma node 23669:75824). */}
                        {s.cloud_synced && (
                          <div
                            className="absolute top-[1px] left-[34px] w-[6px] h-[6px] rounded-full border border-black/40 z-[1]"
                            style={{ background: '#50e81d' }}
                            aria-label={t('Cloud · synced')}
                          />
                        )}
                      </div>
                      <div className="flex flex-col gap-[2px]">
                        <span className="text-sm text-fm-text-primary leading-[22px]">{nameParts || '—'}</span>
                        <div className="text-xs text-fm-text-secondary opacity-70 leading-[19px]">
                          {s.diameter || 1.75} mm{s.color_name ? ` | ${s.color_name}` : ''}
                        </div>
                      </div>
                    </div>
                  </td>
                  <td className="text-left p-2 border-b border-[#292929] align-middle">
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
                  <td className="text-left p-2 border-b border-[#292929] align-middle">
                    <div className="flex gap-2 items-center">
                      <button className="w-4 h-4 bg-transparent border-none cursor-pointer text-fm-brand p-0 flex items-center justify-center transition-colors duration-150 hover:text-fm-brand-hover [&>svg]:w-4 [&>svg]:h-4" onClick={() => onDetail(s.spool_id)} title={t('Spool Detail')}>
                        <svg viewBox="0 0 16 16" fill="none">
                          <path d="M3 2.5h6.5L13 6v7.5H3V2.5z" stroke="currentColor" strokeWidth="1.1" />
                          <path d="M9.5 2.5V6H13" stroke="currentColor" strokeWidth="1.1" />
                          <circle cx="7.5" cy="9.5" r="2" stroke="currentColor" strokeWidth="1.1" />
                          <line x1="9" y1="11" x2="11" y2="13" stroke="currentColor" strokeWidth="1.1" />
                        </svg>
                      </button>
                      <button className="w-4 h-4 bg-transparent border-none cursor-pointer text-fm-brand p-0 flex items-center justify-center transition-colors duration-150 hover:text-fm-brand-hover [&>svg]:w-4 [&>svg]:h-4" onClick={() => onAddSimilar(s.spool_id)} title={t('Add')}>
                        <svg viewBox="0 0 16 16" fill="none">
                          <path d="M3 2.5h6.5L13 6v7.5H3V2.5z" stroke="currentColor" strokeWidth="1.1" />
                          <path d="M9.5 2.5V6H13" stroke="currentColor" strokeWidth="1.1" />
                          <line x1="8" y1="7.5" x2="8" y2="11.5" stroke="currentColor" strokeWidth="1.2" />
                          <line x1="6" y1="9.5" x2="10" y2="9.5" stroke="currentColor" strokeWidth="1.2" />
                        </svg>
                      </button>
                      <button className="w-4 h-4 bg-transparent border-none cursor-pointer text-fm-brand p-0 flex items-center justify-center transition-colors duration-150 hover:text-fm-brand-hover [&>svg]:w-4 [&>svg]:h-4" onClick={() => onDelete(s.spool_id)} title={t('Delete')}>
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

      {/* Pagination */}
      {totalRows > pageSize && (
        <div className="flex items-center justify-end gap-1 py-3 shrink-0">
          <button
            className="min-w-6 h-6 flex items-center justify-center rounded-sm border-none bg-transparent text-fm-text-secondary text-xs cursor-pointer px-1 hover:bg-fm-hover hover:text-fm-text-strong disabled:opacity-30 disabled:cursor-default"
            disabled={safePage <= 1}
            onClick={() => setPage(safePage - 1)}
          >‹</button>
          {buildPageRange(safePage, pages).map((p, i) =>
            p === '...'
              ? <span key={`d${i}`} className="text-fm-text-detail text-xs px-[2px]">…</span>
              : <button
                  key={p}
                  className={`min-w-6 h-6 flex items-center justify-center rounded-sm border-none bg-transparent text-fm-text-secondary text-xs cursor-pointer px-1 hover:bg-fm-hover hover:text-fm-text-strong disabled:opacity-30 disabled:cursor-default${p === safePage ? ' bg-fm-brand text-white' : ''}`}
                  onClick={() => setPage(p)}
                >{p}</button>
          )}
          <button
            className="min-w-6 h-6 flex items-center justify-center rounded-sm border-none bg-transparent text-fm-text-secondary text-xs cursor-pointer px-1 hover:bg-fm-hover hover:text-fm-text-strong disabled:opacity-30 disabled:cursor-default"
            disabled={safePage >= pages}
            onClick={() => setPage(safePage + 1)}
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
      )}
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
    <th className={`text-left p-2 border-b border-[#292929] align-middle text-sm font-medium text-fm-text-primary h-12 sticky top-0 bg-fm-inner z-[2] select-none cursor-pointer hover:text-fm-text-strong ${cls}`} onClick={() => onClick(sortKey)}>
      {label}<span className="fm-sort-icon" />
    </th>
  );
}

