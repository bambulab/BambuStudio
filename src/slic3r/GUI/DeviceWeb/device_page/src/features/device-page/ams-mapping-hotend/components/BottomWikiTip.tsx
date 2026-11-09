import { useTranslation } from 'react-i18next';

interface BottomWikiTipProps {
  // Opens the filament-matching-rules Wiki in the system browser (the WebView
  // cannot navigate away itself, so this is routed to C++ via the bridge).
  onOpenWiki: () => void;
}

// Bottom hint that links to the filament-matching-rules Wiki. Mirrors the wx
// popup tip (AmsMappingPopupUpdate::update_ams_tips) and jumps to the same URL.
export function BottomWikiTip({ onOpenWiki }: BottomWikiTipProps) {
  const { t } = useTranslation();
  return (
    <div
      className="ams-map-hotend__wiki-tip"
      role="button"
      tabIndex={0}
      onClick={onOpenWiki}
      onKeyDown={(e) => {
        if (e.key === 'Enter' || e.key === ' ') {
          e.preventDefault();
          onOpenWiki();
        }
      }}
    >
      <span className="ams-map-hotend__wiki-tip__label">{t('Note:')}</span>
      <span className="ams-map-hotend__wiki-tip__link">
        {t('To learn about the filaments matching rules.')}
        {' \u2192'}
      </span>
    </div>
  );
}
