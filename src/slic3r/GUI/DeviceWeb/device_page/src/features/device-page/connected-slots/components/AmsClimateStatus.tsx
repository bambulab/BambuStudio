import humidityIconUrl from '../../ams-filament-hotend/assets/ams_humidity.svg';
import {
  humLevel1Url,
  humLevel2Url,
  humLevel3Url,
  humLevel4Url,
  humLevel5Url,
  humLevel1DarkUrl,
  humLevel2DarkUrl,
  humLevel3DarkUrl,
  humLevel4DarkUrl,
  humLevel5DarkUrl,
} from '../../ams-control-web/assets';
import useStore from '../../../../store/AppStore';
import type { ConnectedSlotsUnitInfo } from '../types';

// Same numbered glyphs as the AmsControlWeb humidity bar.
const LEVEL_ICONS = [
  { light: humLevel1Url, dark: humLevel1DarkUrl },
  { light: humLevel2Url, dark: humLevel2DarkUrl },
  { light: humLevel3Url, dark: humLevel3DarkUrl },
  { light: humLevel4Url, dark: humLevel4DarkUrl },
  { light: humLevel5Url, dark: humLevel5DarkUrl },
];

// Same design-exported glyphs as the AmsFilamentHotend dryer readout: the
// droplet keeps its own palette, the drying sun is masked with currentColor.
function HumidityIcon() {
  return <img className="cs-ams-climate-status__icon" src={humidityIconUrl} alt="" aria-hidden="true" />;
}

function DryingIcon() {
  return <span className="cs-ams-climate-status__icon cs-ams-climate-status__icon--drying" aria-hidden="true" />;
}

function isActiveDryStatus(status?: string) {
  return !!status && status !== 'Idle' && status !== 'Off';
}

/**
 * Humidity and drying-status pill on an AMS unit board. A unit that can dry
 * always shows its status, idle included; others are a static readout.
 */
export function AmsClimateStatus({
  unit,
  onDry,
}: {
  unit: ConnectedSlotsUnitInfo;
  onDry?: (amsId: string) => void;
}) {
  const theme = useStore((s) => s.filament.theme);
  // Old pushers have no humidity_display_type; fall back to the percent field.
  const displayType = unit.humidity_display_type
    ?? (typeof unit.humidity_raw === 'number' && unit.humidity_raw >= 0 ? 'percent' : 'none');

  const percent = displayType === 'percent'
    && typeof unit.humidity_raw === 'number'
    && unit.humidity_raw >= 0
    ? unit.humidity_raw
    : null;
  const level = displayType === 'level'
    && typeof unit.humidity_level === 'number'
    && unit.humidity_level >= 1
    && unit.humidity_level <= 5
    ? unit.humidity_level
    : null;
  const hasHumidity = percent !== null || level !== null;
  const drying = isActiveDryStatus(unit.dry_status);
  const dry = unit.dry_status_text || (drying || unit.can_dry ? unit.dry_status || 'Idle' : '');
  if (!hasHumidity && !dry) return null;

  const content = (
    <>
      {percent !== null && (
        <span className="cs-ams-climate-status__item">
          <HumidityIcon />
          <span>{percent} %</span>
        </span>
      )}
      {level !== null && (
        <span className="cs-ams-climate-status__item">
          <img
            className="cs-ams-climate-status__icon cs-ams-climate-status__icon--level"
            src={theme === 'dark' ? LEVEL_ICONS[level - 1].dark : LEVEL_ICONS[level - 1].light}
            alt=""
            aria-hidden="true"
          />
        </span>
      )}
      {hasHumidity && dry && <i className="cs-ams-climate-status__split" />}
      {dry && (
        <span className={`cs-ams-climate-status__item${drying ? ' cs-ams-climate-status__item--drying' : ''}`}>
          <DryingIcon />
          <span className="cs-ams-climate-status__text" title={dry}>{dry}</span>
        </span>
      )}
    </>
  );

  if (unit.can_dry && onDry) {
    return (
      <button
        type="button"
        className="cs-ams-climate-status cs-ams-climate-status--interactive"
        data-testid={`ams-climate-status-${unit.ams_id}`}
        aria-label={`Open drying controls for ${unit.display_name || unit.ams_id}`}
        onClick={() => onDry(unit.ams_id)}
      >
        {content}
      </button>
    );
  }

  return <span className="cs-ams-climate-status" data-testid={`ams-climate-status-${unit.ams_id}`}>{content}</span>;
}
