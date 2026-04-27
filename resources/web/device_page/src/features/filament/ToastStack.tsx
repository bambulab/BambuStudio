import { useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import type { CloudToast } from './types';

interface Props {
  toasts: CloudToast[];
  onDismiss: (id: number) => void;
  /** Auto-dismiss interval in ms (0 = never). */
  autoDismissMs?: number;
}

export function ToastStack({ toasts, onDismiss, autoDismissMs = 5000 }: Props) {
  const { t } = useTranslation();

  useEffect(() => {
    if (!autoDismissMs) return;
    if (toasts.length === 0) return;
    const timers = toasts.map((toast) =>
      window.setTimeout(() => onDismiss(toast.id), autoDismissMs),
    );
    return () => timers.forEach((id) => window.clearTimeout(id));
  }, [toasts, autoDismissMs, onDismiss]);

  if (toasts.length === 0) return null;

  return (
    <div className="fixed bottom-6 right-6 flex flex-col gap-2 z-[2000] pointer-events-none">
      {toasts.map((toast) => {
        const color = toast.level === 'error' ? '#ff6b6b'
          : toast.level === 'warn' ? '#ffb84d'
          : '#50e81d';
        return (
          <div
            key={toast.id}
            className="pointer-events-auto flex items-start gap-3 max-w-[360px] bg-fm-sidebar border border-fm-border rounded-lg px-4 py-3 shadow-[0_4px_20px_rgba(0,0,0,0.25)] text-fm-text-primary text-xs leading-[19px]"
          >
            <span
              className="mt-[4px] inline-block size-[8px] rounded-full shrink-0"
              style={{ background: color }}
            />
            <span className="flex-1 break-words">{toast.text}</span>
            <button
              className="shrink-0 size-[18px] rounded-[6px] bg-transparent border-none text-fm-text-detail cursor-pointer hover:text-fm-text-strong"
              onClick={() => onDismiss(toast.id)}
              aria-label={t('Dismiss')}
            >
              <svg width="10" height="10" viewBox="0 0 10 10" fill="none">
                <path d="M2 2l6 6M8 2l-6 6" stroke="currentColor" strokeWidth="1.2" />
              </svg>
            </button>
          </div>
        );
      })}
    </div>
  );
}
