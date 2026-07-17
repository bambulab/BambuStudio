import { useTranslation } from 'react-i18next';
import { SpoolGraphic } from '../../ams-filament-hotend/components/SpoolGraphic';
import nozzleIcon from '../../ams-mapping-hotend/assets/nozzle_icon.svg';
import resetIcon from '../../ams-mapping-hotend/assets/reset.svg';
import { SlotRow } from './SlotRow';
import type { RefillGroupViewModel } from '../types';

interface GroupColumnProps {
  group: RefillGroupViewModel;
  // C++-driven: when false the group is display-only and the edit affordances
  // (drag handle, add-compatible, reset) are hidden.
  editable: boolean;
}

// "0.4 Std": diameter trimmed to one decimal, plus the flow label the C++ side
// already formats ("Std" / "HF" / ...). Empty when no nozzle is reported.
function nozzleLabel(group: RefillGroupViewModel): string {
  if (group.nozzle_diameter === undefined) return '';
  const dia = group.nozzle_diameter.toFixed(1);
  return group.nozzle_flow_type ? `${dia} ${group.nozzle_flow_type}` : dia;
}

// One refill group rendered as a vertical column: a spool header describing the
// filament chain, its trays in refill-priority order, then the add/reset row.
// Phase 1 is display-only, so the footer buttons are visual placeholders until
// the device write-back protocol lands.
export function GroupColumn({ group, editable }: GroupColumnProps) {
  const { t } = useTranslation();
  const nozzle = nozzleLabel(group);

  return (
    <div className="refill-group" data-testid="refill-group">
      <div className="refill-group__header">
        <SpoolGraphic
          size={52}
          colors={group.colors}
          colorType={group.color_type}
          remain={100}
        />
        <div className="refill-group__caption">
          <span className="refill-group__type">{group.filament_type}</span>
          {nozzle && (
            <span className="refill-group__nozzle">
              <img className="refill-group__nozzle-icon" src={nozzleIcon} alt="" />
              {nozzle}
            </span>
          )}
        </div>
      </div>

      <div className="refill-group__slots">
        {group.slots.map((slot, index) => (
          <SlotRow
            key={`${slot.ams_id}:${slot.slot_id}`}
            slot={slot}
            divider={index < group.slots.length - 1}
            editable={editable}
          />
        ))}
      </div>

      {editable && (
        <div className="refill-group__footer">
          <button type="button" className="refill-group__add">{t('+ Add similar')}</button>
          <button type="button" className="refill-group__reset" aria-label={t('Reset')}>
            <img src={resetIcon} alt="" />
          </button>
        </div>
      )}
    </div>
  );
}
