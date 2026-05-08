import { useMemo, useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { DebugLogEntry, DebugLogFilter } from './types';

const FILTERS: Array<{ id: DebugLogFilter; label: string }> = [
  { id: 'all', label: 'All' },
  { id: 'data', label: 'Data' },
  { id: 'bridge', label: 'C++↔Web' },
  { id: 'http', label: 'HTTP' },
];

function formatTime(ts: number) {
  try {
    return new Date(ts).toLocaleTimeString();
  } catch {
    return String(ts);
  }
}

function levelClass(level: DebugLogEntry['level']) {
  if (level === 'error') return 'text-[#ff6b6b] border-[#ff6b6b]/40 bg-[#ff6b6b]/10';
  if (level === 'warn') return 'text-[#f5b84b] border-[#f5b84b]/40 bg-[#f5b84b]/10';
  return 'text-fm-text-secondary border-fm-border bg-fm-hover';
}

export function DebugLogPanel({
  logs,
  filter,
  onFilterChange,
  onClear,
}: {
  logs: DebugLogEntry[];
  filter: DebugLogFilter;
  onFilterChange: (filter: DebugLogFilter) => void;
  onClear: () => void;
}) {
  const { t } = useTranslation();
  const [expanded, setExpanded] = useState<Set<number>>(new Set());
  const pageInfo = useMemo(() => {
    if (typeof window === 'undefined') return null;
    const { href, origin, pathname, search, hash } = window.location;
    const hashPath = hash.startsWith('#') ? hash.slice(1) : '';
    const currentPathname = hashPath ? hashPath.split('?')[0] || '/' : pathname;
    return {
      href,
      origin,
      pathname,
      search,
      hash,
      currentPathname,
      filamentUrl: `${origin}${pathname}${search}#/filament`,
    };
  }, []);

  const visibleLogs = useMemo(
    () => {
      const filtered = filter === 'all' ? logs : logs.filter((item) => item.category === filter);
      if ((!pageInfo?.href && !pageInfo?.filamentUrl) || (filter !== 'all' && filter !== 'bridge')) return filtered;

      const entries: DebugLogEntry[] = [];

      if (pageInfo?.filamentUrl) {
        entries.push({
          id: -2,
          ts: Date.now(),
          category: 'bridge',
          level: pageInfo.currentPathname === '/filament' ? 'info' : 'warn',
          title: 'Filament Page URL',
          summary: pageInfo.filamentUrl,
          detail: {
            href: pageInfo.filamentUrl,
            targetRoute: '/filament',
            currentPathname: pageInfo.currentPathname,
            currentHash: pageInfo.hash,
          },
        });
      }

      if (pageInfo?.href) {
        const urlEntry: DebugLogEntry = {
          id: -1,
          ts: Date.now(),
          category: 'bridge',
          level: 'info',
          title: 'Current Page URL',
          summary: pageInfo.href,
          detail: {
            href: pageInfo.href,
            origin: pageInfo.origin,
            pathname: pageInfo.pathname,
            search: pageInfo.search,
            hash: pageInfo.hash,
          },
        };
        entries.push(urlEntry);
      }

      return [...entries, ...filtered];
    },
    [logs, filter, pageInfo]
  );

  const handleToggle = (id: number) => {
    setExpanded((prev) => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  };

  const handleCopyAll = async () => {
    const text = visibleLogs.map((item) => {
      const detail = item.detail === undefined ? '' : `\n${JSON.stringify(item.detail, null, 2)}`;
      return `[${formatTime(item.ts)}] [${item.category}] [${item.level}] ${item.title}\n${item.summary}${detail}`;
    }).join('\n\n');

    try {
      await navigator.clipboard.writeText(text);
    } catch {
      // Best-effort only for debug-only UI.
    }
  };

  return (
    <div className="shrink-0 rounded-xl border border-fm-border bg-fm-sidebar overflow-hidden">
      <div className="flex items-center justify-between gap-4 px-4 py-3 border-b border-fm-border">
        <div className="flex items-center gap-3">
          <div className="text-sm font-medium text-fm-text-primary">{t('Debug Log')}</div>
          <div className="flex gap-2">
            {FILTERS.map((item) => (
              <button
                key={item.id}
                className={`h-7 px-3 rounded-md border cursor-pointer text-xs transition-colors duration-150 ${
                  filter === item.id
                    ? 'bg-fm-brand/15 border-fm-brand text-fm-brand'
                    : 'bg-fm-inner2 border-fm-border text-fm-text-secondary hover:bg-fm-hover'
                }`}
                onClick={() => onFilterChange(item.id)}
              >
                {t(item.label)}
              </button>
            ))}
          </div>
        </div>

        <div className="flex items-center gap-2">
          <button
            className="h-7 px-3 rounded-md border border-fm-border bg-fm-inner2 cursor-pointer text-xs text-fm-text-secondary hover:bg-fm-hover"
            onClick={handleCopyAll}
          >
            {t('Copy All')}
          </button>
          <button
            className="h-7 px-3 rounded-md border border-fm-border bg-fm-inner2 cursor-pointer text-xs text-fm-text-secondary hover:bg-fm-hover"
            onClick={onClear}
          >
            {t('Clear')}
          </button>
        </div>
      </div>

      <div className="h-[220px] overflow-auto px-3 py-2 flex flex-col gap-2">
        {visibleLogs.length === 0 ? (
          <div className="h-full flex items-center justify-center text-xs text-fm-text-detail">
            No debug logs yet.
          </div>
        ) : (
          visibleLogs.map((item) => {
            const isOpen = expanded.has(item.id);
            return (
              <div
                key={item.id}
                className="rounded-lg border border-fm-border bg-fm-base/70 px-3 py-2"
              >
                <div className="flex items-start justify-between gap-3">
                  <div className="min-w-0 flex-1">
                    <div className="flex items-center gap-2 flex-wrap">
                      <span className="text-[11px] text-fm-text-detail">{formatTime(item.ts)}</span>
                      <span className={`inline-flex items-center h-5 px-2 rounded-full border text-[11px] ${levelClass(item.level)}`}>
                        {item.category} / {item.level}
                      </span>
                      <span className="text-xs text-fm-text-primary font-medium">{item.title}</span>
                    </div>
                    <div className="mt-1 text-xs text-fm-text-secondary break-words">{item.summary}</div>
                  </div>
                  {item.detail !== undefined && (
                    <button
                      className="shrink-0 h-6 px-2 rounded-md border border-fm-border bg-fm-inner2 cursor-pointer text-[11px] text-fm-text-secondary hover:bg-fm-hover"
                      onClick={() => handleToggle(item.id)}
                    >
                      {isOpen ? 'Hide' : 'Detail'}
                    </button>
                  )}
                </div>

                {isOpen && item.detail !== undefined && (
                  <pre className="mt-2 rounded-md bg-[#121212] border border-fm-border p-3 text-[11px] leading-4 text-[#d6d6d6] overflow-auto whitespace-pre-wrap break-all">
                    {typeof item.detail === 'string' ? item.detail : JSON.stringify(item.detail, null, 2)}
                  </pre>
                )}
              </div>
            );
          })
        )}
      </div>
    </div>
  );
}
