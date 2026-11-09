import { useTranslation } from 'react-i18next';

interface FooterProps {
  onConfirm: () => void;
  onClose: () => void;
}

export function Footer({ onConfirm, onClose }: FooterProps) {
  const { t } = useTranslation();
  return (
    <div className="fs-footer">
      <button type="button" className="fs-btn fs-btn--confirm" data-testid="filament-select-confirm" onClick={onConfirm}>
        {t('Confirm')}
      </button>
      <button type="button" className="fs-btn fs-btn--close" data-testid="filament-select-close" onClick={onClose}>
        {t('Close')}
      </button>
    </div>
  );
}
