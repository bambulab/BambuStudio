import { useTranslation } from 'react-i18next';
import type { PointerEvent as ReactPointerEvent } from 'react';
import type { RefillSlotViewModel } from '../types';
import dragHandleIcon from '../assets/drag_handle.svg';
import removeRefillSlotIcon from '../assets/remove_refill_slot.svg';
import { FilamentColorSwatch } from './FilamentColorSwatch';

interface SlotRowProps {
  slot: RefillSlotViewModel;
  // Whether a divider is drawn under the row (all but the last slot).
  divider: boolean;
  // C++-driven: the drag handle only shows when refill relations are editable.
  editable: boolean;
  busy?: boolean;
  dragging?: boolean;
  dropBefore?: boolean;
  dropAfter?: boolean;
  onHandlePointerDown?: (event: ReactPointerEvent<HTMLImageElement>) => void;
  onRemove?: (slot: RefillSlotViewModel) => void;
}

// Compact tray label shown in the pill (e.g. "AMS-A6" -> "A6").
function slotLabel(trayName?: string): string {
  const name = (trayName ?? '').replace(/^AMS[-\s]?/i, '');
  return name || '--';
}

// One tray inside a refill group, rendered in refill-priority order (top =
// used next). The handle starts an in-group reorder when editing is allowed.
export function SlotRow({
  slot,
  divider,
  editable,
  busy,
  dragging,
  dropBefore,
  dropAfter,
  onHandlePointerDown,
  onRemove,
}: SlotRowProps) {
  const { t } = useTranslation();

  const brand = slot.filament_name || slot.sub_brand || slot.filament_type || '--';
  const className = [
    'refill-slot',
    divider ? 'refill-slot--divider' : '',
    dragging ? 'refill-slot--dragging' : '',
    dropBefore ? 'refill-slot--drop-before' : '',
    dropAfter ? 'refill-slot--drop-after' : '',
  ].filter(Boolean).join(' ');

  return (
    <div className={className} data-testid="refill-slot">
      {editable && (
        <img
          className="refill-slot__handle"
          src={dragHandleIcon}
          alt=""
          draggable={false}
          onPointerDown={onHandlePointerDown}
        />
      )}
      <div className="refill-slot__info">
        <div className="refill-slot__brand-row">
          <FilamentColorSwatch color={slot.color} colors={slot.colors} colorType={slot.color_type} />
          <span className="refill-slot__brand" title={slot.has_filament ? brand : undefined}>
            {slot.has_filament ? brand : t('Empty')}
          </span>
          {slot.create_type === 'manual' && (
            <span className="refill-slot__tag">{t('Manual')}</span>
          )}
        </div>
        {slot.has_filament && (
          <div className="refill-slot__meta-row">
            <span>{slotLabel(slot.tray_name)}</span>
            {slot.weight_gram != null && (
              <>
                <span>｜</span>
                <span>{slot.weight_gram}g</span>
              </>
            )}
          </div>
        )}
      </div>
      {editable && slot.create_type === 'manual' && (
        <button
          type="button"
          className="refill-slot__remove"
          aria-label={t('Delete')}
          title={t('Delete')}
          disabled={busy}
          onClick={() => onRemove?.(slot)}
        >
          <img src={removeRefillSlotIcon} alt="" />
        </button>
      )}
    </div>
  );
}
