import { useTranslation } from 'react-i18next';
import { COLORS, HUMIDITY, HUM_ICON, px } from '../dip';
import type { HumidityView } from '../types';
import {
  humHeatingUrl,
  humLevel1DarkUrl,
  humLevel1NoNumDarkUrl,
  humLevel1NoNumUrl,
  humLevel1Url,
  humLevel2DarkUrl,
  humLevel2NoNumDarkUrl,
  humLevel2NoNumUrl,
  humLevel2Url,
  humLevel3DarkUrl,
  humLevel3NoNumDarkUrl,
  humLevel3NoNumUrl,
  humLevel3Url,
  humLevel4DarkUrl,
  humLevel4NoNumDarkUrl,
  humLevel4NoNumUrl,
  humLevel4Url,
  humLevel5DarkUrl,
  humLevel5NoNumDarkUrl,
  humLevel5NoNumUrl,
  humLevel5Url,
  humSunUrl,
} from '../assets';
import { ThemeImage } from './ThemeImage';

const LEVEL_ICONS = [
  { light: humLevel1Url, dark: humLevel1DarkUrl },
  { light: humLevel2Url, dark: humLevel2DarkUrl },
  { light: humLevel3Url, dark: humLevel3DarkUrl },
  { light: humLevel4Url, dark: humLevel4DarkUrl },
  { light: humLevel5Url, dark: humLevel5DarkUrl },
];

const NO_NUM_ICONS = [
  { light: humLevel1NoNumUrl, dark: humLevel1NoNumDarkUrl },
  { light: humLevel2NoNumUrl, dark: humLevel2NoNumDarkUrl },
  { light: humLevel3NoNumUrl, dark: humLevel3NoNumDarkUrl },
  { light: humLevel4NoNumUrl, dark: humLevel4NoNumDarkUrl },
  { light: humLevel5NoNumUrl, dark: humLevel5NoNumDarkUrl },
];

// Width keys off support_drying, not the current drying state. Level clicks stay
// in-page: DeviceWeb cannot report a native popup anchor.
export function HumidityBar({
  humidity,
  onClick,
}: {
  humidity: HumidityView;
  onClick?: () => void;
}) {
  const { t } = useTranslation();
  const showDry = humidity.support_drying;
  if (humidity.display_type === 'none') return <div style={{ height: px(HUMIDITY.height) }} />;

  const hasPercent = humidity.display_type === 'percent' && humidity.percent >= 0;
  const baseWidth = hasPercent ? HUMIDITY.width : HUMIDITY.widthNoPercent;
  const width = showDry ? baseWidth : baseWidth - HUMIDITY.dryWidth;
  const icon = humidity.display_idx >= 1 && humidity.display_idx <= 5 ? humidity.display_idx - 1 : -1;

  return (
    <button
      type="button"
      data-testid="ams-humidity"
      aria-label={t('Humidity')}
      onClick={onClick}
      disabled={!onClick}
      className="flex items-center justify-center gap-[3px] rounded-full"
      style={{
        width: px(Math.max(width, HUMIDITY.dryWidth)),
        height: px(HUMIDITY.height),
        background: COLORS.blockBg,
      }}
    >
      {icon >= 0 ? (
        humidity.display_type === 'level' ? (
          <ThemeImage
            light={LEVEL_ICONS[icon].light}
            dark={LEVEL_ICONS[icon].dark}
            width={HUM_ICON.numbered}
            height={HUM_ICON.numbered}
          />
        ) : (
          <>
            <ThemeImage
              light={NO_NUM_ICONS[icon].light}
              dark={NO_NUM_ICONS[icon].dark}
              width={HUM_ICON.plain}
              height={HUM_ICON.plain}
            />
            <span className="text-[12px] leading-[16px]" style={{ color: COLORS.gray800 }}>
              {humidity.percent}%
            </span>
          </>
        )
      ) : null}
      {showDry ? (
        <>
          <span className="block w-[1px]" style={{ height: px(20), background: COLORS.roadNub }} />
          {humidity.drying ? (
            <img src={humHeatingUrl} alt="" aria-hidden style={{ width: px(HUM_ICON.heating), height: px(HUM_ICON.heating) }} />
          ) : (
            <img src={humSunUrl} alt="" aria-hidden style={{ width: px(HUM_ICON.sun), height: px(HUM_ICON.sun) }} />
          )}
        </>
      ) : null}
    </button>
  );
}
