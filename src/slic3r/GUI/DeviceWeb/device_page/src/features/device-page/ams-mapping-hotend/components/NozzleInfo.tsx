import type { AmsMappingHotendNozzleInfo, AmsMappingHotendSlotState } from '../types';
import nozzleIcon from '../assets/nozzle_icon.svg';

interface NozzleInfoProps {
  nozzle?: AmsMappingHotendNozzleInfo;
  state: AmsMappingHotendSlotState;
}

export function NozzleInfo({ nozzle, state }: NozzleInfoProps) {
  if (state === 'Unavailable') {
    return <div className="ams-map-hotend__nozzle-na">Nozzle N/A</div>;
  }

  if (!nozzle) return null;

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
