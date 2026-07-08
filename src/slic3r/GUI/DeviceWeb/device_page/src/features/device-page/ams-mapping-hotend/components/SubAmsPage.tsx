import { useTranslation } from 'react-i18next';
import type {
  AmsMappingHotendSlotViewModel,
  AmsMappingHotendSubSlotViewModel,
} from '../types';
import { resolvePreloadLockedTooltip, resolveSlotTooltip } from '../slotTooltip';
import { SpoolRing } from './SpoolRing';
import { BottomWikiTip } from './BottomWikiTip';

interface SubAmsPageProps {
  slot: AmsMappingHotendSlotViewModel;
  // Slicing's expected filament type, forwarded to the sub-card tooltips.
  materialIndex: string;
  onSelect: (sub: AmsMappingHotendSubSlotViewModel) => void;
  onEdit: (sub: AmsMappingHotendSubSlotViewModel) => void;
  onBack: () => void;
  onOpenWiki: () => void;
}

function SubCard({
  sub,
  materialIndex,
  isPreload = false,
  onSelect,
  onEdit,
}: {
  sub: AmsMappingHotendSubSlotViewModel;
  materialIndex: string;
  isPreload?: boolean;
  onSelect: (sub: AmsMappingHotendSubSlotViewModel) => void;
  onEdit: (sub: AmsMappingHotendSubSlotViewModel) => void;
}) {
  const { t } = useTranslation();
  const disabled = !sub.is_pickable;
  // A locked preload card (pre-fed AMS behind an external spool feed) uses a
  // dedicated hint telling the user to unload the external spool first.
  const tooltip = isPreload && !sub.is_pickable
    ? resolvePreloadLockedTooltip(t)
    : resolveSlotTooltip(t, sub.state, sub.is_pickable, sub.filament_type, materialIndex);
  const multiColors = sub.multi_colors && sub.multi_colors.length > 1 ? sub.multi_colors : undefined;
  const isEmpty = !sub.has_filament;
  const isThird = sub.filament_name === '?';
  const displayName = isEmpty ? 'Empty' : sub.filament_name;

  return (
    <div
      className="ams-map-hotend__sub-card"
      data-state={sub.state}
      data-checked={sub.checked}
      data-pickable={sub.is_pickable}
      title={tooltip || undefined}
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
          {sub.ext && <span className="ams-map-hotend__sub-card__pill-ext">Ext</span>}
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
        <div className="ams-map-hotend__sub-card__name-row">
          <div className="ams-map-hotend__sub-card__name">{displayName}</div>
          {sub.can_edit && (
            <button
              type="button"
              className="ams-map-hotend__sub-card__edit-btn"
              data-testid={`ams-mapping-hotend-sub-edit-${sub.ams_id}-${sub.slot_id}`}
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

export function SubAmsPage({ slot, materialIndex, onSelect, onEdit, onBack, onOpenWiki }: SubAmsPageProps) {
  const { t } = useTranslation();
  if (!slot.sub_ams || slot.sub_ams.length === 0) return null;

  // Preload mode: current bind is Ext, preload AMS slot injected as sub_ams
  // with is_preload flag. The Ext source (the slot itself) sits in the bottom
  // section, the preloaded AMS slot(s) in the top section.
  const isPreloadMode = slot.ext && slot.sub_ams?.some(g => g.is_preload);

  // The slot itself, projected as a sub-slot so it can be rendered with the
  // shared SubCard. `ext` mirrors the real slot value (not hard-coded) so the
  // top/bottom partition and the Ext tag stay correct.
  const extAsSubSlot: AmsMappingHotendSubSlotViewModel = {
    ams_id:        slot.ams_id,
    slot_id:       slot.slot_id,
    tray_id:       slot.tray_id,
    index:         slot.index,
    ext:           slot.ext,
    state:         slot.state,
    filament_type: slot.filament_type,
    filament_name: slot.filament_name,
    color:         slot.color,
    ctype:         slot.ctype,
    color_type:    slot.color_type,
    remain:        slot.remain,
    has_filament:  slot.has_filament,
    is_pickable:   slot.is_pickable,
    can_edit:      slot.can_edit,
    checked:       slot.checked,
    multi_colors:  slot.multi_colors,
  };

  // When the currently selected (checked) source is a non-Ext (AMS) source,
  // warn the user they must manually pull the external filament to change it.
  const bannerVisible =
    (slot.checked && !slot.ext) ||
    slot.sub_ams.some((g) => g.slots.some((s) => s.checked && !s.ext));

  const parentEmpty = !slot.has_filament;
  const parentMultiColors = slot.multi_colors && slot.multi_colors.length > 1 ? slot.multi_colors : undefined;
  const parentIsThird = slot.filament_name === '?';
  const parentDisplayName = parentEmpty ? 'Empty' : slot.filament_name;
  const parentTooltip = resolveSlotTooltip(t, slot.state, slot.is_pickable, slot.filament_type, materialIndex);

  const warningBanner = bannerVisible ? (
    <div className="ams-map-hotend__sub-ams-page__warning">
      <svg width="16" height="16" viewBox="0 0 16 16" fill="none">
        <circle cx="8" cy="8" r="6.5" stroke="#ea6e00" strokeWidth="1.2" />
        <rect x="7.3" y="6.8" width="1.4" height="5" rx="0.7" fill="#ea6e00" />
        <circle cx="8" cy="4.6" r="0.9" fill="#ea6e00" />
      </svg>
      <span>打印过程中需要手动抽出外挂耗材进行换料操作</span>
    </div>
  ) : null;

  const backButton = (
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
  );

  const bottomTip = <BottomWikiTip onOpenWiki={onOpenWiki} />;

  if (isPreloadMode) {
    // Top: preloaded AMS source(s). Bottom: the Ext source.
    return (
      <div className="ams-map-hotend__sub-ams-page" data-testid="ams-mapping-hotend-sub-ams-page">
        {warningBanner}
        {backButton}

        <div className="ams-map-hotend__sub-ams-page__body ams-map-hotend__sub-ams-page__body--flat">
          {slot.sub_ams.map((group) =>
            group.slots.map((sub) => (
              <SubCard
                key={`${sub.ams_id}-${sub.slot_id}`}
                sub={sub}
                materialIndex={materialIndex}
                isPreload={group.is_preload}
                onSelect={onSelect}
                onEdit={onEdit}
              />
            ))
          )}
        </div>

        <div className="ams-map-hotend__sub-ams-page__section-divider" />

        <div className="ams-map-hotend__sub-ams-page__ext-section">
          <SubCard sub={extAsSubSlot} materialIndex={materialIndex} onSelect={onSelect} onEdit={onEdit} />
        </div>

        {bottomTip}
      </div>
    );
  }

  // Normal sub-AMS mode: parent card (left) + AMS group containers (right) in
  // the top section. When the slot itself is an Ext source it is pushed to the
  // bottom section below a divider.
  return (
    <div className="ams-map-hotend__sub-ams-page" data-testid="ams-mapping-hotend-sub-ams-page">
      {warningBanner}
      {backButton}

      <div className="ams-map-hotend__sub-ams-page__body">
        {!slot.ext && (
          <div className="ams-map-hotend__sub-ams-page__parent">
            <div
              className="ams-map-hotend__sub-card ams-map-hotend__sub-card--parent"
              data-state={slot.state}
              data-checked={slot.checked}
              data-clickable={slot.is_pickable}
              title={parentTooltip || undefined}
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
                <div className="ams-map-hotend__sub-card__name-row">
                  <div className="ams-map-hotend__sub-card__name">{parentDisplayName}</div>
                  {slot.can_edit && (
                    <button
                      type="button"
                      className="ams-map-hotend__sub-card__edit-btn"
                      data-testid={`ams-mapping-hotend-sub-edit-${slot.ams_id}-${slot.slot_id}`}
                      onClick={(e) => {
                        e.stopPropagation();
                        onEdit(extAsSubSlot);
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
          </div>
        )}

        <div className="ams-map-hotend__sub-ams-page__groups">
          {slot.sub_ams.map((group) => (
            <div key={group.name} className="ams-map-hotend__sub-ams-page__group">
              <div className="ams-map-hotend__sub-ams-page__group-inner">
                {group.slots.map((sub) => (
                  <SubCard
                    key={`${sub.ams_id}-${sub.slot_id}`}
                    sub={sub}
                    materialIndex={materialIndex}
                    isPreload={group.is_preload}
                    onSelect={onSelect}
                    onEdit={onEdit}
                  />
                ))}
              </div>
              <div className="ams-map-hotend__sub-ams-page__group-name">{group.name}</div>
            </div>
          ))}
        </div>
      </div>

      {slot.ext && (
        <>
          <div className="ams-map-hotend__sub-ams-page__section-divider" />
          <div className="ams-map-hotend__sub-ams-page__ext-section">
            <SubCard sub={extAsSubSlot} materialIndex={materialIndex} onSelect={onSelect} onEdit={onEdit} />
          </div>
        </>
      )}

      {bottomTip}
    </div>
  );
}
