import { useEffect } from 'react';
import { useTranslation } from 'react-i18next';

interface Props {
  open: boolean;
  title: string;
  message?: string;
  confirmText?: string;
  cancelText?: string;
  /** Render confirm button in destructive red style. */
  danger?: boolean;
  onConfirm: () => void | Promise<unknown>;
  onCancel: () => void;
}

/**
 * Reusable modal confirmation dialog that replaces the native `window.confirm()`.
 * The native dialog leaks the page URL (file:// or http://) in its title bar when
 * running inside a WebView2 / CEF host, which looks broken for end users.
 */
export function ConfirmDialog({
  open, title, message,
  confirmText, cancelText,
  danger = false,
  onConfirm, onCancel,
}: Props) {
  const { t } = useTranslation();

  const handleConfirm = () => {
    void onConfirm();
  };

  useEffect(() => {
    if (!open) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onCancel();
      else if (e.key === 'Enter') void onConfirm();
    };
    document.addEventListener('keydown', onKey);
    return () => document.removeEventListener('keydown', onKey);
  }, [open, onCancel, onConfirm]);

  if (!open) return null;

  return (
    <div
      className="fixed inset-0 bg-black/50 flex items-center justify-center z-[1100]"
    >
      <div
        className="w-[360px] bg-fm-sidebar rounded-xl shadow-[0_8px_24px_rgba(0,0,0,0.4)] overflow-hidden"
        onClick={(e) => e.stopPropagation()}
        role="dialog"
        aria-modal="true"
      >
        <div className="px-6 pt-5 pb-2">
          <h3 className="text-sm font-medium text-fm-text-strong leading-5 m-0">{title}</h3>
        </div>
        {message ? (
          <div className="px-6 pb-5 text-xs text-fm-text-secondary leading-5 whitespace-pre-line">
            {message}
          </div>
        ) : (
          <div className="h-2" />
        )}
        <div className="flex gap-2 justify-end px-6 pb-5">
          <button
            type="button"
            onClick={onCancel}
            className="inline-flex items-center justify-center h-[30px] px-4 rounded-lg border border-fm-border-focus/50 bg-fm-inner text-fm-text-primary text-xs cursor-pointer transition-colors duration-150 hover:bg-fm-hover"
          >
            {cancelText ?? t('Cancel')}
          </button>
          <button
            type="button"
            onClick={handleConfirm}
            className={`inline-flex items-center justify-center h-[30px] px-4 rounded-lg border-none text-xs font-medium cursor-pointer transition-colors duration-150 ${
              danger
                ? 'bg-[#d9373b] text-white hover:bg-[#bf2f33]'
                : 'bg-fm-brand text-white hover:bg-fm-brand-hover'
            }`}
          >
            {confirmText ?? t('OK')}
          </button>
        </div>
      </div>
    </div>
  );
}
