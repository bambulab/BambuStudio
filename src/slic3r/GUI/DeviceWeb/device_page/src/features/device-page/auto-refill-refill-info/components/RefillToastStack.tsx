import { useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import toastSuccessIcon from '../assets/toast_success.svg';
import type { RefillToast } from '../types';

interface RefillToastStackProps {
  toasts: RefillToast[];
  onDismiss: (id: number) => void;
  // The design specifies a 3s auto-dismiss.
  autoDismissMs?: number;
}

// Success feedback for the refill write-backs (add / remove / group removed).
// The text is already localized by the caller through `t()`.
export function RefillToastStack({ toasts, onDismiss, autoDismissMs = 3000 }: RefillToastStackProps) {
  const { t } = useTranslation();

  useEffect(() => {
    if (toasts.length === 0) return;
    const timers = toasts.map((toast) => window.setTimeout(() => onDismiss(toast.id), autoDismissMs));
    return () => timers.forEach((timer) => window.clearTimeout(timer));
  }, [toasts, autoDismissMs, onDismiss]);

  if (toasts.length === 0) return null;

  return (
    <div className="refill-toasts" role="status" aria-live="polite">
      {toasts.map((toast) => (
        <div key={toast.id} className="refill-toast">
          <img className="refill-toast__icon" src={toastSuccessIcon} alt="" />
          <span className="refill-toast__text">{t(toast.text)}</span>
        </div>
      ))}
    </div>
  );
}
