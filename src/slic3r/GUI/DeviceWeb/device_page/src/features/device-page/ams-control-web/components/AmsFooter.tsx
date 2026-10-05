import { useTranslation } from 'react-i18next';
import { CONTENT_WIDTH, EXTRUDER, FOOTER_BUTTON, FOOTER_GAP, FOOTER_LEFT_INSET, FOOTER_SIDE_WIDTH, SETTINGS_ICON, px } from '../dip';
import { extruderIconName, extrudersLeftToRight } from '../displayAdapters';
import { nozzleIconLefts } from '../geometry';
import type { AmsControlActions, ExtruderArea } from '../types';
import { amsSettingHoverUrl, amsSettingNormalUrl, amsSettingPressUrl } from '../assets';
import { ExtruderIcon } from './ExtruderIcon';

const buttonStyle = {
  width: px(FOOTER_BUTTON.width),
  height: px(FOOTER_BUTTON.height),
} as const;

export function AmsFooter({
  actions,
  extruderArea,
  onSettings,
  onAutoRefill,
  onLoad,
  onUnload,
  showDebug,
  debugOpening,
  onDebug,
}: {
  actions: AmsControlActions;
  extruderArea: ExtruderArea;
  onSettings: () => void;
  onAutoRefill: () => void;
  onLoad: () => void;
  onUnload: () => void;
  showDebug: boolean;
  debugOpening: boolean;
  onDebug: () => void;
}) {
  const { t } = useTranslation();
  const extruders = extrudersLeftToRight(extruderArea.extruders);
  const iconNames = extruders.map((extruder) => extruderIconName(extruderArea.extruders, extruder));
  const iconLefts = nozzleIconLefts(iconNames);

  return (
    <div className="relative" style={{ width: px(CONTENT_WIDTH), minHeight: px(EXTRUDER.height) }}>
      {/* minWidth, not width: the internal-build Debug button may push this group past
          FOOTER_SIDE_WIDTH, and the shipped controls must not be squeezed to fit. */}
      <div
        className="absolute top-0 flex items-center"
        style={{ left: px(FOOTER_LEFT_INSET), minWidth: px(FOOTER_SIDE_WIDTH), gap: px(FOOTER_GAP) }}
      >
        {actions.show_auto_refill ? (
          <button
            type="button"
            data-testid="ams-auto-refill"
            className="ams-btn-white rounded-[4px] border text-[13px]"
            style={buttonStyle}
            onClick={onAutoRefill}
          >
            {t('Auto-refill')}
          </button>
        ) : null}
        <button
          type="button"
          data-testid="ams-settings"
          aria-label={t('AMS Settings')}
          className="ams-setting-btn ams-icon-swap ams-icon-swap-press relative flex shrink-0 items-center justify-center"
          style={{ width: px(SETTINGS_ICON), height: px(SETTINGS_ICON) }}
          onClick={onSettings}
        >
          <img src={amsSettingNormalUrl} alt="" className="ams-icon-base absolute inset-0 m-auto size-full" />
          <img src={amsSettingHoverUrl} alt="" className="ams-icon-hover absolute inset-0 m-auto size-full" />
          <img src={amsSettingPressUrl} alt="" className="ams-icon-active absolute inset-0 m-auto size-full" />
        </button>
        {showDebug ? (
          <button
            type="button"
            data-testid="ams-control-web-debug"
            disabled={debugOpening}
            className="h-6 shrink-0 rounded-full border border-[#C2C2C2] px-2.5 text-xs hover:bg-[#F5F5F5] disabled:opacity-60"
            onClick={onDebug}
          >
            {debugOpening ? 'Opening...' : 'Debug'}
          </button>
        ) : null}
      </div>

      {extruderArea.visible
        ? extruders.map((extruder, index) => (
            <div key={extruder.id} className="absolute top-0" style={{ left: px(iconLefts[index]) }}>
              <ExtruderIcon
                extruder={extruder}
                icon={extruderIconName(extruderArea.extruders, extruder)}
              />
            </div>
          ))
        : null}

      <div
        className="absolute right-0 top-0 flex items-center justify-end"
        style={{ width: px(FOOTER_SIDE_WIDTH), gap: px(FOOTER_GAP) }}
      >
        <button
          type="button"
          data-testid="ams-unload"
          disabled={!actions.can_unload}
          title={actions.unload_tips}
          className="ams-btn-white rounded-[4px] border text-[13px]"
          style={buttonStyle}
          onClick={onUnload}
        >
          {t('Unload')}
        </button>
        <button
          type="button"
          data-testid="ams-load"
          disabled={!actions.can_load}
          title={actions.load_tips}
          className="ams-btn-green rounded-[4px] text-[13px]"
          style={buttonStyle}
          onClick={onLoad}
        >
          {t('Load')}
        </button>
      </div>
    </div>
  );
}
