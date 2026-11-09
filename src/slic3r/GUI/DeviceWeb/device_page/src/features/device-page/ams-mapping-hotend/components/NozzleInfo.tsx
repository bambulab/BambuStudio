import type { ReactNode } from 'react';
import { useTranslation } from 'react-i18next';
import type { AmsMappingHotendNozzleInfo, AmsMappingHotendSlotState } from '../types';
import { isNozzleFaultState } from '../types';
import nozzleIcon from '../assets/nozzle_icon.svg';
import cutterIcon from '../assets/cutter_icon.svg';
import penIcon from '../assets/pen_icon.svg';

interface NozzleInfoProps {
  nozzle?: AmsMappingHotendNozzleInfo;
  state: AmsMappingHotendSlotState;
}

// Red exclamation overlay for the four device-reported hotend faults. It sits
// on the nozzle glyph rather than taking a row of its own so the 104x140 card
// keeps the same footer height in every state.
function FaultBadge() {
  return (
    <svg
      className="ams-map-hotend__nozzle-info__badge"
      width="14"
      height="14"
      viewBox="0 0 14 14"
      fill="none"
      aria-hidden="true"
    >
      <circle cx="7" cy="7" r="6" fill="#d01b1b" stroke="#ffffff" strokeWidth="1" />
      <rect x="6.25" y="3.4" width="1.5" height="4.6" rx="0.75" fill="#ffffff" />
      <circle cx="7" cy="10.1" r="0.9" fill="#ffffff" />
    </svg>
  );
}

export function NozzleInfo({ nozzle, state }: NozzleInfoProps) {
  const { t } = useTranslation();
  const isFault = isNozzleFaultState(state);

  // Cutter / pen tools are not printing nozzles: they carry their own icon and
  // a single-line label instead of the dia / flow rows. The backend already
  // forces these slots into the non-pickable NozzleUnmatch state, so the card
  // dimming is handled by CSS the same way as any other mismatch.
  const isTool = !!nozzle && (nozzle.tool_kind === 'cutter' || nozzle.tool_kind === 'pen');
  const isPen = nozzle?.tool_kind === 'pen';

  const renderLabel = (): ReactNode => {
    // The device cannot describe a hotend that is erroring or unread, so there
    // is no spec to print. NozzleLost / NozzleUncalibrated deliberately fall
    // through to the dia / flow rows: the last reported spec still tells the
    // user which hotend to reinstall or calibrate.
    if (state === 'NozzleError') {
      return <div className="ams-map-hotend__nozzle-info__fault">{t('Error')}</div>;
    }
    if (state === 'NozzleUnknown') {
      return <div className="ams-map-hotend__nozzle-info__fault">{t('Unknown')}</div>;
    }
    if (isTool) {
      return <div className="ams-map-hotend__nozzle-info__tool">{isPen ? t('Pen') : t('Cutter')}</div>;
    }
    // No rack nozzle on this slot (e.g. NozzleEmpty). Keep the icon + label
    // layout so card height stays aligned with Normal / Unmatch siblings; the
    // single-line "Empty" replaces the dia / flow rows.
    if (!nozzle) {
      return <div className="ams-map-hotend__nozzle-info__empty">{t('Empty')}</div>;
    }
    return (
      <>
        <div className="ams-map-hotend__nozzle-info__dia">{nozzle.dia}</div>
        <div className="ams-map-hotend__nozzle-info__flow">{nozzle.flow_type}</div>
      </>
    );
  };

  return (
    <div className="ams-map-hotend__nozzle-info">
      <div className="ams-map-hotend__nozzle-info__glyph">
        <img
          className={
            isTool
              ? 'ams-map-hotend__nozzle-info__icon ams-map-hotend__nozzle-info__icon--tool'
              : 'ams-map-hotend__nozzle-info__icon'
          }
          src={isTool ? (isPen ? penIcon : cutterIcon) : nozzleIcon}
          alt=""
        />
        {isFault && <FaultBadge />}
      </div>
      <div className="ams-map-hotend__nozzle-info__text">{renderLabel()}</div>
    </div>
  );
}
