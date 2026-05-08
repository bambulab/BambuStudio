import { useState, useEffect, useRef, useCallback } from 'react';
import { useTranslation } from 'react-i18next';
import type { Spool } from './types';
import { SpoolSvg } from './SpoolSvg';
import { ENTRY_METHOD_LABELS, formatSpoolDisplayName } from './constants';

interface Props {
  open: boolean;
  spool: Spool | null;
  filteredSpools: Spool[];
  onClose: () => void;
  onEdit: (spool: Spool) => void;
  onNavigate: (spoolId: string) => void;
}

export function DetailDialog({ open, spool, filteredSpools, onClose, onEdit, onNavigate }: Props) {
  const { t } = useTranslation();

  // F4.10 parity: drag support for this dialog as well.
  const [dragOffset, setDragOffset] = useState<{ x: number; y: number }>({ x: 0, y: 0 });
  const dragState = useRef<{ startX: number; startY: number; baseX: number; baseY: number } | null>(null);
  useEffect(() => { if (open) setDragOffset({ x: 0, y: 0 }); }, [open]);
  const handleDragMove = useCallback((e: MouseEvent) => {
    const st = dragState.current;
    if (!st) return;
    setDragOffset({ x: st.baseX + (e.clientX - st.startX), y: st.baseY + (e.clientY - st.startY) });
  }, []);
  const handleDragEnd = useCallback(() => {
    dragState.current = null;
    window.removeEventListener('mousemove', handleDragMove);
    window.removeEventListener('mouseup', handleDragEnd);
    document.body.style.userSelect = '';
  }, [handleDragMove]);
  const handleDragStart = useCallback((e: React.MouseEvent) => {
    const target = e.target as HTMLElement;
    if (target.closest('button, input, select, textarea, a')) return;
    dragState.current = { startX: e.clientX, startY: e.clientY, baseX: dragOffset.x, baseY: dragOffset.y };
    window.addEventListener('mousemove', handleDragMove);
    window.addEventListener('mouseup', handleDragEnd);
    document.body.style.userSelect = 'none';
    e.preventDefault();
  }, [dragOffset, handleDragMove, handleDragEnd]);
  const resetDragOffset = useCallback(() => setDragOffset({ x: 0, y: 0 }), []);

  if (!open || !spool) return null;

  const idx = filteredSpools.findIndex((s) => s.spool_id === spool.spool_id);
  const hasPrev = idx > 0;
  const hasNext = idx >= 0 && idx < filteredSpools.length - 1;

  const nameParts = formatSpoolDisplayName(spool);
  // Material Type 字段语义 = 云端 filamentName（本地 series 字段对齐）。
  // 直接展示 series；series 缺失时退回到 material_type。不再做 type+series
  // 前缀拼接 —— 历史上那种拼接会把 series="Support For PA/PET" + type="PA"
  // 渲染成 "PA Support For PA/PET"，和 filamentName 真实值不符。
  const typeSeriesFull = (spool.series || '').trim() || (spool.material_type || '').trim();
  // STUDIO-17991: align the Weight block with AddEditDialog's two-field
  // swagger model (netWeight / totalNetWeight). Drop 毛重/料盘/净重 三列
  // — the cloud schema never persisted 料盘 anyway, so exposing it here
  // only drifted out of sync with the list Remain column.
  //
  // Legacy rows stored initial_weight as 毛重 with spool_weight > 0; new
  // rows store initial_weight as the pure 整卷净重 with spool_weight == 0.
  // `initial - spool` collapses both shapes to 整卷净重 transparently.
  const legacySpool = spool.spool_weight || 0;
  const totalNet = Math.max(0, (spool.initial_weight || 0) - legacySpool);
  const currentNet = (typeof spool.net_weight === 'number' && spool.net_weight > 0)
    ? Math.round(spool.net_weight)
    : Math.round(totalNet * (spool.remain_percent || 0) / 100);

  return (
    <div className="fixed inset-0 bg-black/50 flex items-start justify-center pt-5 z-[1000]">
      <div
        className="w-[480px] max-h-[calc(100vh-40px)] bg-fm-sidebar rounded-xl flex flex-col overflow-hidden"
        style={{ transform: `translate(${dragOffset.x}px, ${dragOffset.y}px)` }}
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header — drag handle (F4.10). Double-click resets position. */}
        <div
          className="flex items-center px-6 py-4 border-b border-[#424242] gap-3 cursor-move select-none"
          onMouseDown={handleDragStart}
          onDoubleClick={resetDragOffset}
          title={t('Drag to move')}
        >
          <h3 className="flex-1 text-base font-medium text-fm-text-strong whitespace-nowrap overflow-hidden text-ellipsis">{t('Spool Detail')}</h3>
          <div className="flex items-center gap-[13px] shrink-0">
            <button
              className="flex items-center gap-1 bg-transparent border-none cursor-pointer text-fm-text-secondary text-xs px-2 pl-[6px] py-[2px] h-6 rounded-md transition-colors duration-150 hover:text-fm-text-strong hover:bg-fm-hover disabled:text-fm-text-gray disabled:cursor-default disabled:hover:bg-transparent [&>svg]:shrink-0"
              disabled={!hasPrev}
              onClick={() => hasPrev && onNavigate(filteredSpools[idx - 1].spool_id)}
            >
              <svg width="12" height="12" viewBox="0 0 12 12" fill="none">
                <path d="M7 2L3 6l4 4" stroke="currentColor" strokeWidth="1.2" />
              </svg>
              {t('Previous')}
            </button>
            <button
              className="flex items-center gap-1 bg-transparent border-none cursor-pointer text-fm-text-secondary text-xs px-2 pl-[6px] py-[2px] h-6 rounded-md transition-colors duration-150 hover:text-fm-text-strong hover:bg-fm-hover disabled:text-fm-text-gray disabled:cursor-default disabled:hover:bg-transparent [&>svg]:shrink-0"
              disabled={!hasNext}
              onClick={() => hasNext && onNavigate(filteredSpools[idx + 1].spool_id)}
            >
              {t('Next')}
              <svg width="12" height="12" viewBox="0 0 12 12" fill="none">
                <path d="M5 2l4 4-4 4" stroke="currentColor" strokeWidth="1.2" />
              </svg>
            </button>
          </div>
          <button className="bg-transparent border-none text-fm-text-detail text-xl cursor-pointer leading-none hover:text-fm-text-strong" onClick={onClose}>×</button>
        </div>

        {/* Spool banner */}
        <div className="flex items-center gap-3 px-6">
          <div className="w-10 h-10 shrink-0 relative">
            <SpoolSvg color={spool.color_code} />
          </div>
          <div className="flex flex-col">
            <div className="flex items-center gap-1 text-sm font-medium text-fm-text-strong leading-[22px] [&>svg]:text-fm-text-secondary [&>svg]:shrink-0">
              {nameParts || '—'}
              <span className="inline-flex items-center justify-center px-1 h-4 rounded-sm bg-fm-input text-[11px] text-fm-text-secondary leading-4 empty:hidden">
                {ENTRY_METHOD_LABELS[spool.entry_method] ? t(ENTRY_METHOD_LABELS[spool.entry_method]) : ''}
              </span>
            </div>
            <div className="text-xs text-fm-text-secondary opacity-70 leading-[19px]">
              {spool.diameter || 1.75} mm｜{spool.color_name || '—'}
            </div>
          </div>
        </div>

        {/* Body — layout mirrors figma node 7460:18424 (耗材信息 / 基础信息 / 备注).
            "Usage Records" tab removed (F3.1): the cloud API does not yet
            expose any usage history, and the local-only tab was always empty. */}
        <div className="flex-1 overflow-y-auto px-6 py-5">
          {/* 耗材信息 — aligned with AddEditDialog (F4.3): collapse the
              standalone "Series" field into the combined Material Type
              so the detail panel reads the same way as the edit form
              (e.g. "PLA Basic" / "PLA Matte"). Layout: row 1 品牌 | 类型,
              row 2 颜色. */}
              <div className="fm-section-bar flex items-center gap-[6px] text-sm font-normal text-fm-text-primary mt-4 mb-2 leading-[22px] first:mt-0">{t('Filament Info')}</div>
              <div className="flex gap-3">
                <DetField label={t('Brand')} value={spool.brand || '—'} />
                <DetField
                  label={t('Material Type')}
                  value={typeSeriesFull || '—'}
                />
              </div>
              <div className="flex gap-3">
                <div className="flex flex-col gap-1 mb-4 flex-1">
                  <label className="text-xs text-fm-text-secondary leading-[19px]">{t('Color')}</label>
                  <div className="h-8 flex items-center">
                    <div className="w-6 h-6 rounded-sm border border-white/[0.16]" style={{ background: spool.color_code || '#888' }} />
                  </div>
                </div>
                <div className="flex-1" />
              </div>

              <div className="fm-section-bar flex items-center gap-[6px] text-sm font-normal text-fm-text-primary mt-4 mb-2 leading-[22px]">{t('Weight')}</div>
              <div className="flex items-end gap-2 bg-fm-inner rounded-md p-2 opacity-30">
                <div className="flex flex-col gap-1 flex-1">
                  <span className="text-[11px] text-fm-text-secondary leading-4">{t('Current Net Weight')}</span>
                  <span className="text-sm text-fm-text-strong leading-[22px] font-medium">{currentNet} g</span>
                </div>
                <div className="flex flex-col gap-1 flex-1">
                  <span className="text-[11px] text-fm-text-secondary leading-4">{t('Total Net Weight')}</span>
                  <span className="text-sm text-fm-text-strong leading-[22px] font-medium">{totalNet} g</span>
                </div>
              </div>

              {/* 基础信息 — figma 参数1: 显示 preset 组合名 */}
              <div className="fm-section-bar flex items-center gap-[6px] text-sm font-normal text-fm-text-primary mt-4 mb-2 leading-[22px]">{t('Basic Info')}</div>
              <DetField
                label={t('Parameter') + ' 1'}
                value={formatSpoolDisplayName(spool) || '—'}
              />

              {/* 备注（云端唯一支持的扩展字段） */}
          <div className="fm-section-bar flex items-center gap-[6px] text-sm font-normal text-fm-text-primary mt-4 mb-2 leading-[22px]">{t('Note')}</div>
          <DetField label={t('Note')} value={spool.note || '—'} />
        </div>

        {/* Footer */}
        <div className="flex items-center justify-end px-6 py-3 border-t border-fm-border">
          <button className="inline-flex items-center gap-1 h-[30px] px-8 rounded-lg border-none cursor-pointer text-xs whitespace-nowrap transition-colors duration-150 bg-fm-brand text-white font-medium hover:bg-fm-brand-hover" onClick={() => onEdit(spool)}>{t('Edit Filament Info')}</button>
        </div>
      </div>
    </div>
  );
}

function DetField({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex flex-col gap-1 mb-4 flex-1">
      <label className="text-xs text-fm-text-secondary leading-[19px]">{label}</label>
      <div className="text-xs text-fm-text-primary leading-[19px] min-h-8 flex items-center bg-fm-inner2 rounded-md px-2 py-[6px]">{value}</div>
    </div>
  );
}
