import type {
  AmsMappingHotendSlotViewModel,
  AmsMappingHotendSubSlotViewModel,
} from '../types';
import { SpoolRing } from './SpoolRing';

interface SubAmsPanelProps {
  slot: AmsMappingHotendSlotViewModel;
  onSelect: (sub: AmsMappingHotendSubSlotViewModel) => void;
  onEdit?: (sub: AmsMappingHotendSubSlotViewModel) => void;
}

function SubCard({
  sub,
  onSelect,
  onEdit,
}: {
  sub: AmsMappingHotendSubSlotViewModel;
  onSelect: (sub: AmsMappingHotendSubSlotViewModel) => void;
  onEdit?: (sub: AmsMappingHotendSubSlotViewModel) => void;
}) {
  const disabled = !sub.is_pickable;
  const multiColors = sub.multi_colors && sub.multi_colors.length > 1 ? sub.multi_colors : undefined;
  const isEmpty = !sub.has_filament;
  const displayName = isEmpty ? '-' : sub.filament_name;

  return (
    <div
      className="ams-map-hotend__sub-card"
      data-state={sub.state}
      data-checked={sub.checked}
      onClick={() => { if (!disabled) onSelect(sub); }}
      role="button"
      tabIndex={disabled ? -1 : 0}
      onKeyDown={(e) => {
        if ((e.key === 'Enter' || e.key === ' ') && !disabled) {
          e.preventDefault();
          onSelect(sub);
        }
      }}
    >
      <div className="ams-map-hotend__sub-card__head">
        <div className="ams-map-hotend__sub-card__pill">
          <span>{sub.index}</span>
        </div>
        <div className="ams-map-hotend__sub-card__radio" data-checked={sub.checked}>
          {sub.checked && (
            <svg width="10" height="8" viewBox="0 0 10 8" fill="none">
              <path d="M1 4L3.5 6.5L9 1" stroke="#fff" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" />
            </svg>
          )}
        </div>
      </div>
      <div className="ams-map-hotend__sub-card__spool-area">
        <SpoolRing size={52} color={sub.color} colors={multiColors} remain={sub.remain} isEmpty={isEmpty} />
        <div className="ams-map-hotend__sub-card__name-row">
          <div className="ams-map-hotend__sub-card__name">{displayName}</div>
          {sub.can_edit && onEdit && (
            <button
              type="button"
              className="ams-map-hotend__sub-card__edit-btn"
              onClick={(e) => {
                e.stopPropagation();
                onEdit(sub);
              }}
              aria-label="Edit filament"
            >
              <svg width="17" height="17" viewBox="0 0 16 16" fill="none">
                <path d="M2.56734 10.5956L1.99833 12.5277L1.69156 13.5717C1.62971 13.782 1.68661 14.0072 1.84 14.1605C1.99338 14.3139 2.21851 14.3708 2.42633 14.3115L3.47281 14.0022L5.40497 13.4332C5.71174 13.3441 5.9913 13.1883 6.23127 12.978C6.26591 12.9483 6.29807 12.9161 6.33023 12.884L13.856 5.36068C14.3978 4.81888 14.4646 3.98516 14.0589 3.36914C14.002 3.28255 13.9327 3.19844 13.856 3.12174L12.8813 2.14453C12.2628 1.52604 11.2608 1.52604 10.6423 2.14453L3.11656 9.67031C2.85927 9.9276 2.67125 10.2467 2.56734 10.5956ZM3.70536 10.9296C3.74742 10.791 3.81669 10.6624 3.91318 10.5535C3.92802 10.5387 3.94039 10.5238 3.95523 10.509L9.60823 4.85846L11.1421 6.39232L5.49156 12.0453C5.37528 12.1616 5.22932 12.2482 5.07099 12.2952L4.49208 12.4659L3.13635 12.8642L3.53466 11.5085L3.70536 10.9296Z" fill="currentColor"/>
              </svg>
            </button>
          )}
        </div>
      </div>
    </div>
  );
}

export function SubAmsPanel({ slot, onSelect }: SubAmsPanelProps) {
  if (!slot.sub_ams || slot.sub_ams.length === 0) return null;

  const totalSubSlots = slot.sub_ams.reduce((sum, g) => sum + g.slots.length, 0);
  if (totalSubSlots === 0) return null;

  const parentEmpty = !slot.has_filament;
  const parentMultiColors = slot.multi_colors && slot.multi_colors.length > 1 ? slot.multi_colors : undefined;
  const parentDisplayName = parentEmpty ? '-' : slot.filament_name;

  return (
    <div className="ams-map-hotend__sub-ams-panel" data-testid="ams-mapping-hotend-sub-ams">
      {/* Triangle indicator */}
      <div className="ams-map-hotend__sub-ams-panel__arrow" />

      <div className="ams-map-hotend__sub-ams-panel__content">
        {/* Left: parent slot card (104x112, no nozzle) */}
        <div
          className="ams-map-hotend__sub-card ams-map-hotend__sub-card--parent"
          data-state={slot.state}
          data-checked={false}
        >
          <div className="ams-map-hotend__sub-card__head">
            <div className="ams-map-hotend__sub-card__pill">
              <span>{slot.index}</span>
            </div>
            <div className="ams-map-hotend__sub-card__radio" data-checked={false} />
          </div>
          <div className="ams-map-hotend__sub-card__spool-area">
            <SpoolRing size={52} color={slot.color} colors={parentMultiColors} remain={slot.remain} isEmpty={parentEmpty} />
            <div className="ams-map-hotend__sub-card__name">{parentDisplayName}</div>
          </div>
        </div>

        {/* Right: AMS groups */}
        <div className="ams-map-hotend__sub-ams-groups">
          {slot.sub_ams.map((group) => (
            <div key={group.name} className="ams-map-hotend__sub-ams-group">
              <div className="ams-map-hotend__sub-ams-group__cards">
                {group.slots.map((sub) => (
                  <SubCard
                    key={`${sub.ams_id}-${sub.slot_id}`}
                    sub={sub}
                    onSelect={onSelect}
                  />
                ))}
              </div>
              <div className="ams-map-hotend__sub-ams-group__title">{group.name}</div>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
