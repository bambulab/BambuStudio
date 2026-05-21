import type {
  AmsMappingHotendSlotViewModel,
  AmsMappingHotendSubSlotViewModel,
} from '../types';
import { SpoolRing } from './SpoolRing';

interface SubAmsPageProps {
  slot: AmsMappingHotendSlotViewModel;
  onSelect: (sub: AmsMappingHotendSubSlotViewModel) => void;
  onBack: () => void;
}

function SubCard({
  sub,
  onSelect,
}: {
  sub: AmsMappingHotendSubSlotViewModel;
  onSelect: (sub: AmsMappingHotendSubSlotViewModel) => void;
}) {
  const disabled = !sub.is_pickable;
  const multiColors = sub.multi_colors && sub.multi_colors.length > 1 ? sub.multi_colors : undefined;
  const isEmpty = !sub.has_filament;
  const isThird = sub.filament_name === '?';
  const displayName = isEmpty ? 'Empty' : sub.filament_name;

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
        <SpoolRing
          size={52}
          color={sub.color}
          colors={multiColors}
          remain={sub.remain}
          isEmpty={isEmpty}
          isThird={isThird}
          colorType={sub.color_type}
        />
        <div className="ams-map-hotend__sub-card__name">{displayName}</div>
      </div>
    </div>
  );
}

export function SubAmsPage({ slot, onSelect, onBack }: SubAmsPageProps) {
  if (!slot.sub_ams || slot.sub_ams.length === 0) return null;

  // Preload mode: current bind is Ext, preload AMS slot injected as sub_ams
  // with is_preload flag. Renders two cards flat side-by-side: AMS (left) + Ext (right).
  const isPreloadMode = slot.ext && slot.sub_ams?.some(g => g.is_preload);

  const extAsSubSlot: AmsMappingHotendSubSlotViewModel = {
    ams_id:        slot.ams_id,
    slot_id:       slot.slot_id,
    tray_id:       slot.tray_id,
    index:         slot.index,
    ext:           true,
    state:         slot.state,
    filament_type: slot.filament_type,
    filament_name: slot.filament_name,
    color:         slot.color,
    ctype:         slot.ctype,
    color_type:    slot.color_type,
    remain:        slot.remain,
    has_filament:  slot.has_filament,
    is_pickable:   slot.is_pickable,
    checked:       slot.checked,
    multi_colors:  slot.multi_colors,
  };

  const parentEmpty = !slot.has_filament;
  const parentMultiColors = slot.multi_colors && slot.multi_colors.length > 1 ? slot.multi_colors : undefined;
  const parentIsThird = slot.filament_name === '?';
  const parentDisplayName = parentEmpty ? 'Empty' : slot.filament_name;

  return (
    <div className="ams-map-hotend__sub-ams-page" data-testid="ams-mapping-hotend-sub-ams-page">
      {/* Back button */}
      <button
        className="ams-map-hotend__sub-ams-page__back"
        onClick={onBack}
        type="button"
      >
        <svg width="20" height="20" viewBox="0 0 20 20" fill="none">
          <path d="M12 15L7 10L12 5" stroke="#262E30" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round"/>
        </svg>
        <span>返回</span>
      </button>

      {isPreloadMode ? (
        /* Preload mode: flat layout, AMS card left + Ext card right */
        <div className="ams-map-hotend__sub-ams-page__body ams-map-hotend__sub-ams-page__body--flat">
          {slot.sub_ams.map((group) =>
            group.slots.map((sub) => (
              <SubCard
                key={`${sub.ams_id}-${sub.slot_id}`}
                sub={sub}
                onSelect={onSelect}
              />
            ))
          )}
          <SubCard sub={extAsSubSlot} onSelect={onSelect} />
        </div>
      ) : (
        /* Normal sub-AMS mode: parent card left + group containers right */
        <div className="ams-map-hotend__sub-ams-page__body">
          <div className="ams-map-hotend__sub-ams-page__parent">
            <div
              className="ams-map-hotend__sub-card ams-map-hotend__sub-card--parent"
              data-state={slot.state}
              data-checked={slot.checked}
              data-clickable={slot.is_pickable}
              role={slot.is_pickable ? 'button' : undefined}
              tabIndex={slot.is_pickable ? 0 : -1}
              onClick={() => { if (slot.is_pickable) onSelect(extAsSubSlot); }}
              onKeyDown={(e) => {
                if ((e.key === 'Enter' || e.key === ' ') && slot.is_pickable) {
                  e.preventDefault();
                  onSelect(extAsSubSlot);
                }
              }}
            >
              <div className="ams-map-hotend__sub-card__head">
                <div className="ams-map-hotend__sub-card__pill">
                  <span>{slot.index}</span>
                </div>
                <div className="ams-map-hotend__sub-card__radio" data-checked={slot.checked}>
                  {slot.checked && (
                    <svg width="10" height="8" viewBox="0 0 10 8" fill="none">
                      <path d="M1 4L3.5 6.5L9 1" stroke="#fff" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" />
                    </svg>
                  )}
                </div>
              </div>
              <div className="ams-map-hotend__sub-card__spool-area">
                <SpoolRing
                  size={52}
                  color={slot.color}
                  colors={parentMultiColors}
                  remain={slot.remain}
                  isEmpty={parentEmpty}
                  isThird={parentIsThird}
                  colorType={slot.color_type}
                />
                <div className="ams-map-hotend__sub-card__name">{parentDisplayName}</div>
              </div>
            </div>
          </div>

          <div className="ams-map-hotend__sub-ams-page__groups">
            {slot.sub_ams.map((group) => (
              <div key={group.name} className="ams-map-hotend__sub-ams-page__group">
                <div className="ams-map-hotend__sub-ams-page__group-inner">
                  {group.slots.map((sub) => (
                    <SubCard
                      key={`${sub.ams_id}-${sub.slot_id}`}
                      sub={sub}
                      onSelect={onSelect}
                    />
                  ))}
                </div>
                <div className="ams-map-hotend__sub-ams-page__group-name">{group.name}</div>
              </div>
            ))}
          </div>
        </div>
      )}

      {/* Bottom tip */}
      <div className="ams-map-hotend__sub-ams-page__tip">
        <span className="ams-map-hotend__sub-ams-page__tip-label">注意：</span>
        如需了解耗材匹配规则，详细说明请查看Wiki→
      </div>
    </div>
  );
}
