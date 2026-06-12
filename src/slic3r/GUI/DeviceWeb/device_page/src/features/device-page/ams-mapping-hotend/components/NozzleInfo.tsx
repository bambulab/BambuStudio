import type { AmsMappingHotendNozzleInfo, AmsMappingHotendSlotState } from '../types';
import nozzleIcon from '../assets/nozzle_icon.svg';
import cutterIcon from '../assets/cutter_icon.svg';
import penIcon from '../assets/pen_icon.svg';

interface NozzleInfoProps {
  nozzle?: AmsMappingHotendNozzleInfo;
  state: AmsMappingHotendSlotState;
}

export function NozzleInfo({ nozzle, state }: NozzleInfoProps) {
  if (state === 'Unavailable') {
    return <div className="ams-map-hotend__nozzle-na">Nozzle N/A</div>;
  }

  // Cutter / pen tools are not printing nozzles: render their own icon and a
  // single-line label instead of the dia / flow rows. The backend already
  // forces these slots into the non-pickable NozzleUnmatch state, so the card
  // dimming is handled by CSS the same way as any other mismatch.
  if (nozzle && (nozzle.tool_kind === 'cutter' || nozzle.tool_kind === 'pen')) {
    const isPen = nozzle.tool_kind === 'pen';
    return (
      <div className="ams-map-hotend__nozzle-info">
        <img
          className="ams-map-hotend__nozzle-info__icon ams-map-hotend__nozzle-info__icon--tool"
          src={isPen ? penIcon : cutterIcon}
          alt=""
        />
        <div className="ams-map-hotend__nozzle-info__text">
          <div className="ams-map-hotend__nozzle-info__tool">{isPen ? 'Pen' : 'Cutter'}</div>
        </div>
      </div>
    );
  }

  // No rack nozzle on this slot (e.g. NozzleEmpty). Keep the icon + label
  // layout so card height stays aligned with Normal / Unmatch siblings; the
  // single-line "Empty" replaces the dia / flow rows.
  if (!nozzle) {
    return (
      <div className="ams-map-hotend__nozzle-info">
        <img className="ams-map-hotend__nozzle-info__icon" src={nozzleIcon} alt="" />
        <div className="ams-map-hotend__nozzle-info__text">
          <div className="ams-map-hotend__nozzle-info__empty">Empty</div>
        </div>
      </div>
    );
  }

  return (
    <div className="ams-map-hotend__nozzle-info">
      <img className="ams-map-hotend__nozzle-info__icon" src={nozzleIcon} alt="" />
      <div className="ams-map-hotend__nozzle-info__text">
        <div className="ams-map-hotend__nozzle-info__dia">{nozzle.dia}</div>
        <div className="ams-map-hotend__nozzle-info__flow">{nozzle.flow_type}</div>
      </div>
    </div>
  );
}
