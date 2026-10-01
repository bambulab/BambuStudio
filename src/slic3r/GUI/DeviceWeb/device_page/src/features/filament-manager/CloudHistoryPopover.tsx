import { useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import useStore from '../../store/AppStore';
import type { CloudSyncHistoryEntry } from './types';

interface Props {
  open: boolean;
  onClose: () => void;
}

function formatTs(ts: number): string {
  try {
    return new Date(ts).toLocaleString();
  } catch {
    return String(ts);
  }
}

function rowColorClass(entry: CloudSyncHistoryEntry): string {
  if (entry.status === 'error') return 'text-[#ff6b6b]';
  return entry.kind === 'pull' ? 'text-fm-brand' : 'text-fm-text-primary';
}

/**
 * Modal list of recent cloud sync events (pulls, pushes, failures) with
 * timestamps. Sourced from `filament.cloudSyncHistory`, which the bridge
 * fills from every `sync/pull_done`, `sync/push_done`, `sync/push_failed`
 * report — independent of the debug log switch, so it also works in
 * Release builds.
 */
export function CloudHistoryPopover({ open, onClose }: Props) {
  const { t } = useTranslation();
  const history = useStore((s) => s.filament.cloudSyncHistory);
  const clearHistory = useStore((s) => s.filament.clearCloudSyncHistory);

  useEffect(() => {
    if (!open) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onClose();
    };
    document.addEventListener('keydown', onKey);
    return () => document.removeEventListener('keydown', onKey);
  }, [open, onClose]);

  if (!open) return null;

  // Newest first for display while keeping the underlying store in arrival order.
  const rows = [...history].reverse();

  return (
    <div
      className="fixed inset-0 bg-black/50 flex items-center justify-center z-[1100]"
      onClick={onClose}
    >
      <div
        className="w-[520px] max-w-[90vw] max-h-[80vh] bg-fm-sidebar rounded-xl shadow-[0_8px_24px_rgba(0,0,0,0.4)] overflow-hidden flex flex-col"
        onClick={(e) => e.stopPropagation()}
        role="dialog"
        aria-modal="true"
      >
        <div className="flex items-center justify-between px-5 pt-4 pb-3 border-b border-fm-border-focus/30">
          <h3 className="text-sm font-medium text-fm-text-strong leading-5 m-0">
            {t('Sync History')}
          </h3>
          <div className="flex items-center gap-2">
            <button
              type="button"
              onClick={clearHistory}
              disabled={rows.length === 0}
              className="inline-flex items-center justify-center h-[26px] px-3 rounded-lg border border-fm-border-focus/50 bg-fm-inner text-fm-text-secondary text-xs cursor-pointer transition-colors duration-150 hover:bg-fm-hover disabled:opacity-40 disabled:cursor-default"
            >
              {t('Clear')}
            </button>
            <button
              type="button"
              onClick={onClose}
              className="inline-flex items-center justify-center h-[26px] px-3 rounded-lg border border-fm-border-focus/50 bg-fm-inner text-fm-text-primary text-xs cursor-pointer transition-colors duration-150 hover:bg-fm-hover"
            >
              {t('Close')}
            </button>
          </div>
        </div>

        <div className="flex-1 overflow-y-auto">
          {rows.length === 0 ? (
            <div className="flex items-center justify-center h-[200px] text-xs text-fm-text-secondary">
              {t('No sync history yet.')}
            </div>
          ) : (
            <ul className="m-0 p-0 list-none">
              {rows.map((entry) => (
                <li
                  key={entry.id}
                  className="flex items-start gap-3 px-5 py-2.5 border-b border-fm-border-focus/15 last:border-b-0"
                >
                  <span className="shrink-0 mt-[1px] text-[11px] font-mono text-fm-text-secondary tabular-nums">
                    {formatTs(entry.ts)}
                  </span>
                  <span
                    className={`shrink-0 mt-[1px] inline-flex items-center justify-center h-[18px] px-2 rounded-full text-[11px] ${
                      entry.status === 'error'
                        ? 'bg-[#ff6b6b]/15 text-[#ff6b6b]'
                        : entry.kind === 'pull'
                          ? 'bg-fm-brand/15 text-fm-brand'
                          : 'bg-fm-inner text-fm-text-secondary'
                    }`}
                  >
                    {entry.status === 'error'
                      ? t('Failed')
                      : entry.kind === 'pull'
                        ? t('Pull')
                        : t('Push')}
                  </span>
                  <span className={`flex-1 text-xs leading-5 break-words ${rowColorClass(entry)}`}>
                    {entry.summary}
                  </span>
                </li>
              ))}
            </ul>
          )}
        </div>
      </div>
    </div>
  );
}
