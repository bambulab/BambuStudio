import { useEffect, useRef, useMemo, useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { Spool } from './types';
import { SpoolSvg } from './SpoolSvg';

const PIE_COLORS = ['#8BC34A','#4CAF50','#009688','#3F51B5','#FF9800','#F44336','#9C27B0','#00BCD4','#FFC107','#795548'];

interface Props {
  spools: Spool[];
  onOpenDetail: (id: string) => void;
}

export function StatsView({ spools, onOpenDetail }: Props) {
  const { t } = useTranslation();
  const activeSpools = useMemo(() => spools.filter((s) => s.status !== 'archived'), [spools]);

  // Summary cards
  const totalValue = useMemo(() => {
    let v = 0;
    activeSpools.forEach((s) => {
      const nw = (s.initial_weight || 0) - (s.spool_weight || 0);
      v += (s.unit_price || 0) * Math.max(0, nw) / 1000;
    });
    return v;
  }, [activeSpools]);

  const colorCount = useMemo(() => {
    const set = new Set<string>();
    activeSpools.forEach((s) => { if (s.color_name) set.add(s.color_name); });
    return set.size;
  }, [activeSpools]);

  // Pie data
  const typeData = useMemo(() => countBy(activeSpools, 'material_type'), [activeSpools]);
  const colorData = useMemo(() => countByColor(activeSpools), [activeSpools]);

  // Reminders
  const [reminderTab, setReminderTab] = useState<'low' | 'dry' | 'empty'>('low');
  const reminderItems = useMemo(() => {
    if (reminderTab === 'empty') return activeSpools.filter((s) => (s.remain_percent || 0) === 0);
    if (reminderTab === 'low') return activeSpools.filter((s) => { const r = s.remain_percent || 0; return r > 0 && r <= 20; });
    // dry
    return activeSpools.filter((s) => {
      if (!s.dry_reminder_days || !s.dry_date) return false;
      const diff = (Date.now() - new Date(s.dry_date).getTime()) / 86400000;
      return diff >= s.dry_reminder_days;
    });
  }, [activeSpools, reminderTab]);

  return (
    <div className="flex-1 overflow-y-auto p-5 flex flex-col gap-4">
      {/* Top cards */}
      <div className="flex gap-4">
        <div className="flex-1 bg-fm-sidebar rounded-lg px-6 py-5">
          <div className="text-[28px] font-bold text-fm-text-strong leading-[1.3]">$ {totalValue.toFixed(2)}</div>
          <div className="flex items-center gap-2 mt-2 text-xs">
            <span className="text-fm-text-secondary">{t('Total Value')}</span>
          </div>
        </div>
        <div className="flex-1 bg-fm-sidebar rounded-lg px-6 py-5">
          <div className="text-[28px] font-bold text-fm-text-strong leading-[1.3]">{activeSpools.length}</div>
          <div className="flex items-center gap-2 mt-2 text-xs">
            <span className="text-fm-text-secondary">{t('Total Quantity')}</span>
          </div>
        </div>
        <div className="flex-1 bg-fm-sidebar rounded-lg px-6 py-5">
          <div className="text-[28px] font-bold text-fm-text-strong leading-[1.3]">{colorCount}</div>
          <div className="flex items-center gap-2 mt-2 text-xs">
            <span className="text-fm-text-secondary">{t('Color Varieties')}</span>
          </div>
        </div>
      </div>

      {/* Pie charts + reminders */}
      <div className="flex gap-4">
        <div className="flex-1 flex flex-col gap-4 min-w-0">
          <div className="bg-fm-sidebar rounded-lg overflow-hidden">
            <div className="flex items-center justify-between px-5 py-[14px] text-sm font-medium text-fm-text-strong">{t('Distribution')}</div>
            <div className="px-5 pb-5">
              <div className="flex gap-4">
                <PieBlock data={typeData} label={t('By Type')} />
                <PieBlock data={colorData} label={t('By Color')} />
              </div>
            </div>
          </div>
        </div>

        {/* Reminders */}
        <div className="w-60 shrink-0 bg-fm-sidebar rounded-lg flex flex-col overflow-hidden">
          <div className="flex items-center justify-between px-5 py-[14px] text-sm font-medium text-fm-text-strong">{t('Reminders')}</div>
          <div className="flex gap-0 px-5 border-b border-fm-border">
            {(['low', 'dry', 'empty'] as const).map((rt) => (
              <div
                key={rt}
                className={`px-3 py-2 text-xs text-fm-text-secondary cursor-pointer border-b-2 border-transparent transition-colors duration-150 hover:text-fm-text-primary${reminderTab === rt ? ' text-fm-brand border-fm-brand' : ''}`}
                onClick={() => setReminderTab(rt)}
              >
                {rt === 'low' ? t('Low Remain') : rt === 'dry' ? t('Needs Drying') : t('Exhausted')}
              </div>
            ))}
          </div>
          <div className="flex-1 overflow-y-auto px-3 py-2">
            {reminderItems.length === 0 ? (
              <div className="flex items-center justify-center py-10 text-fm-text-detail text-sm" style={{ padding: '24px 0' }}>{t('No Records')}</div>
            ) : (
              reminderItems.map((s) => (
                <div key={s.spool_id} className="flex items-center gap-[10px] p-2 rounded-md cursor-pointer transition-colors duration-150 hover:bg-fm-hover" onClick={() => onOpenDetail(s.spool_id)}>
                  <div className="w-10 h-10 shrink-0 relative" style={{ width: 32, height: 32 }}>
                    <SpoolSvg color={s.color_code} size={32} />
                  </div>
                  <div className="flex flex-col gap-[1px] flex-1 min-w-0">
                    <div className="text-xs text-fm-text-strong font-medium whitespace-nowrap overflow-hidden text-ellipsis">
                      {s.material_type}{s.series ? ' ' + s.series : ''}
                    </div>
                    <div className="text-[11px] text-fm-text-detail">
                      {reminderTab === 'low' || reminderTab === 'empty'
                        ? t('Remain {{percent}}%', { percent: s.remain_percent || 0 })
                        : t('Drying: {{date}}', { date: s.dry_date })}
                    </div>
                  </div>
                </div>
              ))
            )}
          </div>
        </div>
      </div>

      {/* Heatmap placeholder */}
      <div className="bg-fm-sidebar rounded-lg overflow-hidden">
        <div className="flex items-center justify-between px-5 py-[14px] text-sm font-medium text-fm-text-strong">{t('Usage Heatmap')}</div>
        <div className="px-5 pb-5">
          <Heatmap />
        </div>
      </div>
    </div>
  );
}

/* ===== Pie Chart ===== */
interface PieItem { name: string; value: number; color?: string; _renderedColor?: string }

function countBy(arr: Spool[], key: keyof Spool): PieItem[] {
  const map: Record<string, number> = {};
  arr.forEach((s) => { const v = (s[key] as string) || 'Other'; map[v] = (map[v] || 0) + 1; });
  return Object.entries(map).map(([name, value]) => ({ name, value })).sort((a, b) => b.value - a.value);
}

function countByColor(arr: Spool[]): PieItem[] {
  const map: Record<string, { count: number; color: string }> = {};
  arr.forEach((s) => {
    const name = s.color_name || 'Other';
    if (!map[name]) map[name] = { count: 0, color: s.color_code || '#888' };
    map[name].count++;
  });
  return Object.entries(map)
    .map(([name, { count, color }]) => ({ name, value: count, color }))
    .sort((a, b) => b.value - a.value);
}

function PieBlock({ data, label }: { data: PieItem[]; label: string }) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const w = canvas.width, h = canvas.height;
    const cx = w / 2, cy = h / 2, r = Math.min(w, h) / 2 - 8, ri = r * 0.55;
    ctx.clearRect(0, 0, w, h);

    const total = data.reduce((s, d) => s + d.value, 0);
    if (total === 0) return;

    let angle = -Math.PI / 2;
    data.forEach((d, i) => {
      const slice = (d.value / total) * Math.PI * 2;
      ctx.beginPath();
      ctx.moveTo(cx + ri * Math.cos(angle), cy + ri * Math.sin(angle));
      ctx.arc(cx, cy, r, angle, angle + slice);
      ctx.arc(cx, cy, ri, angle + slice, angle, true);
      ctx.closePath();
      ctx.fillStyle = d.color || PIE_COLORS[i % PIE_COLORS.length];
      ctx.fill();
      d._renderedColor = ctx.fillStyle;
      angle += slice;
    });
  }, [data]);

  const total = data.reduce((s, d) => s + d.value, 0);

  return (
    <div className="flex items-center gap-3 flex-1 min-w-0">
      <canvas ref={canvasRef} width={120} height={120} style={{ width: 120, height: 120 }} />
      <div className="flex flex-col gap-1 text-xs">
        <div style={{ fontSize: 12, marginBottom: 4, opacity: 0.6 }}>{label}</div>
        {data.slice(0, 6).map((d, i) => (
          <div key={d.name} className="flex items-center gap-[6px]">
            <span className="w-2 h-2 rounded-full shrink-0" style={{ background: d.color || PIE_COLORS[i % PIE_COLORS.length] }} />
            <span className="text-fm-text-secondary min-w-12">{d.name}</span>
            <span className="text-fm-text-detail">{total ? ((d.value / total) * 100).toFixed(1) : 0}%</span>
          </div>
        ))}
      </div>
    </div>
  );
}


/* ===== Heatmap (placeholder random data like old version) ===== */
function Heatmap() {
  const { t } = useTranslation();
  const days = [t('day_Sun'), t('day_Mon'), t('day_Tue'), t('day_Wed'), t('day_Thu'), t('day_Fri'), t('day_Sat')];
  const cells = useMemo(() => {
    return Array.from({ length: 35 }, () => Math.floor(Math.random() * 5)); // 0-4
  }, []);

  const levelCls = (v: number) => {
    if (v >= 4) return 'bg-[rgba(80,232,29,0.75)]';
    if (v === 3) return 'bg-[rgba(80,232,29,0.5)]';
    if (v === 2) return 'bg-[rgba(80,232,29,0.3)]';
    if (v === 1) return 'bg-[rgba(80,232,29,0.15)]';
    return '';
  };

  return (
    <div className="flex flex-col gap-1">
      <div className="flex gap-0 text-[11px] text-fm-text-detail [&>span]:flex-1 [&>span]:text-center">
        {days.map((d) => <span key={d}>{d}</span>)}
      </div>
      <div className="grid grid-cols-7 gap-[3px]">
        {cells.map((v, i) => (
          <div key={i} className={`w-full aspect-square max-w-8 rounded-[3px] bg-white/[0.04] transition-colors duration-150 ${levelCls(v)}`} />
        ))}
      </div>
    </div>
  );
}
