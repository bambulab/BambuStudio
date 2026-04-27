import { useTranslation } from 'react-i18next';
import type { CloudSyncState } from './types';

interface Props {
  state: CloudSyncState;
  onPullClick: () => void;
}

/**
 * Compact sync button in the filament toolbar.
 *
 * Cloud is the single source of truth. Local writes are pushed immediately
 * from the C++ side and the dispatcher auto-pulls after each successful
 * push, so this button is a pure "pull all from cloud" action rather than a
 * reconcile.  The red-dot alert is therefore transient: it only shows while
 * the last push / pull is reporting an error, never as a persistent "you
 * have unsynced local edits" marker.
 */
export function CloudBadge({ state, onPullClick }: Props) {
  const { t } = useTranslation();
  const { logged_in, is_syncing, is_pulling, last_synced_at, last_error } = state;

  const hasError = !!last_error.code;
  const isTransient = is_syncing || is_pulling;
  const showAlert = logged_in && !isTransient && hasError;

  const tooltip = (() => {
    if (!logged_in) return t('Not logged in — cloud sync disabled');
    if (hasError) return t('Filament operation failed. This feature currently requires a network connection.');
    if (isTransient) return t('Syncing filament info...');
    if (last_synced_at) {
      try {
        const d = new Date(last_synced_at);
        return t('Last synced: {{time}}', { time: d.toLocaleString() });
      } catch {
        return last_synced_at;
      }
    }
    return t('Sync filament info');
  })();

  const clickable = logged_in && !isTransient;
  const colorClass = !logged_in
    ? 'text-fm-text-detail'
    : (showAlert ? 'text-[#ff6b6b]' : 'text-fm-text-secondary');
  const bgClass = !logged_in
    ? 'bg-transparent'
    : (showAlert ? 'bg-fm-hover' : 'bg-fm-inner');
  const borderClass = !logged_in
    ? 'border-dashed border-fm-border/70'
    : 'border-fm-border-focus/50';

  return (
    <button
      className={`inline-flex items-center justify-center h-[30px] w-[30px] rounded-lg border transition-colors duration-150 ${bgClass} ${colorClass} ${borderClass} ${clickable ? 'cursor-pointer hover:bg-fm-hover' : 'cursor-default'} ${!logged_in ? 'opacity-55' : ''}`}
      title={tooltip}
      aria-label={tooltip}
      onClick={() => { if (clickable) onPullClick(); }}
      data-offline={!logged_in ? 'true' : undefined}
    >
      {isTransient ? (
        <svg className="animate-spin" width="18" height="18" viewBox="0 0 18 18" fill="none" aria-hidden="true">
          <circle cx="9" cy="9" r="6.5" stroke="currentColor" strokeWidth="1.2" opacity="0.28" />
          <path d="M15.5 9A6.5 6.5 0 0 0 9 2.5" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" />
        </svg>
      ) : !logged_in ? (
        // Offline / not-signed-in glyph: cloud + diagonal slash — an unmistakable
        // visual cue that cloud sync is disabled. Keeps the same footprint as
        // the active state so the toolbar layout does not shift.
        <svg width="18" height="18" viewBox="0 0 18 18" fill="none" aria-hidden="true">
          <path d="M5 12.5h7.5a2.75 2.75 0 0 0 .4-5.47A4 4 0 0 0 5.2 7.2a2.65 2.65 0 0 0-.2 5.3Z"
                stroke="currentColor" strokeWidth="1.2" strokeLinejoin="round" opacity="0.9" />
          <path d="M3.25 3.25l11.5 11.5" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" />
        </svg>
      ) : (
        <span className="relative inline-flex items-center justify-center size-[18px]" aria-hidden="true">
          <svg width="18" height="18" viewBox="0 0 18 18" fill="none">
            <path d="M4.25 9.25A4.75 4.75 0 0 1 12.6 6.12" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" strokeLinejoin="round" />
            <path d="M12.6 4.25v1.87h-1.87" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" strokeLinejoin="round" />
            <path d="M13.75 8.75A4.75 4.75 0 0 1 5.4 11.88" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" strokeLinejoin="round" />
            <path d="M5.4 13.75v-1.87h1.87" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" strokeLinejoin="round" />
          </svg>
          {showAlert && (
            <span className="absolute -top-[1px] -right-[1px] size-[5px] rounded-full bg-[#ff6b6b]" />
          )}
        </span>
      )}
    </button>
  );
}
