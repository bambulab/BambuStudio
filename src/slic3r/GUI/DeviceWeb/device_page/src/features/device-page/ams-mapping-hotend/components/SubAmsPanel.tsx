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
                <path d="M11.2 2.8a1.13 1.13 0 0 1 1.6 1.6L5.3 11.9l-2.3.7.7-2.3 7.5-7.5Z" stroke="currentColor" strokeWidth="1.2" strokeLinejoin="round"/>
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
