import { useTranslation } from 'react-i18next';
import type { RefillSlotViewModel } from '../types';
import dragHandleIcon from '../assets/drag_handle.svg';
import { FilamentColorSwatch } from './FilamentColorSwatch';

interface SlotRowProps {
  slot: RefillSlotViewModel;
  // Whether a divider is drawn under the row (all but the last slot).
  divider: boolean;
  // C++-driven: the drag handle only shows when refill relations are editable.
  editable: boolean;
}

// Compact tray label shown in the pill (e.g. "AMS-A6" -> "A6").
function slotLabel(trayName?: string): string {
  const name = (trayName ?? '').replace(/^AMS[-\s]?/i, '');
  return name || '--';
}

// One tray inside a refill group, rendered in refill-priority order (top =
// used next). Phase 1 is display-only: the drag handle is a visual affordance;
// reordering lands with the device write-back protocol in a later phase.
export function SlotRow({ slot, divider, editable }: SlotRowProps) {
  const { t } = useTranslation();

  const brand = slot.sub_brand || slot.filament_type || '--';

  return (
    <div className={`refill-slot${divider ? ' refill-slot--divider' : ''}`} data-testid="refill-slot">
      {editable && <img className="refill-slot__handle" src={dragHandleIcon} alt="" />}
      <div className="refill-slot__info">
        <div className="refill-slot__brand-row">
          <FilamentColorSwatch color={slot.color} colors={slot.colors} colorType={slot.color_type} />
          <span className="refill-slot__brand">{slot.has_filament ? brand : t('Empty')}</span>
          {slot.create_type === 'manual' && (
            <span className="refill-slot__tag">{t('Manual')}</span>
          )}
        </div>
        {slot.has_filament && (
          <div className="refill-slot__meta-row">
            <span className="refill-slot__pill">{slotLabel(slot.tray_name)}</span>
            {slot.weight_gram != null && (
              <span className="refill-slot__weight">{slot.weight_gram}g</span>
            )}
          </div>
        )}
      </div>
    </div>
  );
}
