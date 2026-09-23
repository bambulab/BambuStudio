import humidityIconUrl from '../../ams-filament-hotend/assets/ams_humidity.svg';
import type { ConnectedSlotsUnitInfo } from '../types';

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
 * Humidity % and drying status pill on an AMS unit board. Like the dryer
 * readout on the filament page, a unit that can dry always shows its status,
 * idle included.
 */
export function AmsClimateStatus({
  unit,
  onDry,
}: {
  unit: ConnectedSlotsUnitInfo;
  onDry?: (amsId: string) => void;
}) {
  const humidity = typeof unit.humidity_raw === 'number' && unit.humidity_raw >= 0
    ? unit.humidity_raw
    : null;
  const drying = isActiveDryStatus(unit.dry_status);
  const dry = unit.dry_status_text || (drying || unit.can_dry ? unit.dry_status || 'Idle' : '');
  if (humidity === null && !dry) return null;

  const content = (
    <>
      {humidity !== null && (
        <span className="cs-ams-climate-status__item">
          <HumidityIcon />
          <span>{humidity} %</span>
        </span>
      )}
      {humidity !== null && dry && <i className="cs-ams-climate-status__split" />}
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
