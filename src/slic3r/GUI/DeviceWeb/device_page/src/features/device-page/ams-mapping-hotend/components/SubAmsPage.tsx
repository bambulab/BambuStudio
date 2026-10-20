import { useTranslation } from 'react-i18next';
import type { TFunction } from 'i18next';
import type {
  AmsMappingHotendSlotViewModel,
  AmsMappingHotendSubSlotViewModel,
} from '../types';
import { resolveFeedBlockTooltip, resolveSlotTooltip } from '../slotTooltip';
import { SpoolRing } from './SpoolRing';
import { BottomWikiTip } from './BottomWikiTip';
import { useHoverTip } from './HoverTip';

interface SubAmsPageProps {
  slot: AmsMappingHotendSlotViewModel;
  // Slicing's expected filament type, forwarded to the sub-card tooltips.
  materialIndex: string;
  onSelect: (sub: AmsMappingHotendSubSlotViewModel) => void;
  onEdit: (sub: AmsMappingHotendSubSlotViewModel) => void;
  onBack: () => void;
  onOpenWiki: () => void;
}

// The feed block wins when present: it is only surfaced for `ext_spool`, which
// disables the card outright and therefore outranks any state tip.
function resolveCardTooltip(
  t: TFunction,
  sub: AmsMappingHotendSubSlotViewModel,
  materialIndex: string,
): string {
  const blockTip = resolveFeedBlockTooltip(t, sub.feed_block);
  return blockTip || resolveSlotTooltip(t, sub.state, sub.is_pickable, sub.filament_type, materialIndex);
}

function SubCard({
  sub,
  materialIndex,
  onSelect,
  onEdit,
}: {
  sub: AmsMappingHotendSubSlotViewModel;
  materialIndex: string;
  onSelect: (sub: AmsMappingHotendSubSlotViewModel) => void;
  onEdit: (sub: AmsMappingHotendSubSlotViewModel) => void;
}) {
  const { t } = useTranslation();
  const disabled = !sub.is_pickable;
  const tooltip = resolveCardTooltip(t, sub, materialIndex);
  const { hoverProps, tip } = useHoverTip(tooltip);
  const multiColors = sub.multi_colors && sub.multi_colors.length > 1 ? sub.multi_colors : undefined;
  const isEmpty = !sub.has_filament;
  const isThird = sub.filament_name === '?';
  const displayName = isEmpty ? t('Empty') : sub.filament_name;

  return (
    <div
      className="ams-map-hotend__sub-card"
      data-state={sub.state}
      data-checked={sub.checked}
      data-pickable={sub.is_pickable}
      onClick={() => { if (!disabled) onSelect(sub); }}
      {...hoverProps}
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
                <path d="M2.56734 10.5956L1.99833 12.5277L1.69156 13.5717C1.62971 13.782 1.68661 14.0072 1.84 14.1605C1.99338 14.3139 2.21851 14.3708 2.42633 14.3115L3.47281 14.0022L5.40497 13.4332C5.71174 13.3441 5.9913 13.1883 6.23127 12.978C6.26591 12.9483 6.29807 12.9161 6.33023 12.884L13.856 5.36068C14.3978 4.81888 14.4646 3.98516 14.0589 3.36914C14.002 3.28255 13.9327 3.19844 13.856 3.12174L12.8813 2.14453C12.2628 1.52604 11.2608 1.52604 10.6423 2.14453L3.11656 9.67031C2.85927 9.9276 2.67125 10.2467 2.56734 10.5956ZM3.70536 10.9296C3.74742 10.791 3.81669 10.6624 3.91318 10.5535C3.92802 10.5387 3.94039 10.5238 3.95523 10.509L9.60823 4.85846L11.1421 6.39232L5.49156 12.0453C5.37528 12.1616 5.22932 12.2482 5.07099 12.2952L4.49208 12.4659L3.13635 12.8642L3.53466 11.5085L3.70536 10.9296Z" fill="currentColor"/>
              </svg>
            </button>
          )}
        </div>
      </div>
      {tip}
    </div>
  );
}

// Left-hand card of the sub-AMS page: the hotend's own rack AMS slot. It
// deliberately carries no border container and no unit label, unlike the
// sub-AMS blocks on the right.
function ParentCard({
  sub,
  materialIndex,
  onSelect,
  onEdit,
}: {
  sub: AmsMappingHotendSubSlotViewModel;
  materialIndex: string;
  onSelect: (sub: AmsMappingHotendSubSlotViewModel) => void;
  onEdit: (sub: AmsMappingHotendSubSlotViewModel) => void;
}) {
  const { t } = useTranslation();
  const tooltip = resolveCardTooltip(t, sub, materialIndex);
  const { hoverProps, tip } = useHoverTip(tooltip);
  const multiColors = sub.multi_colors && sub.multi_colors.length > 1 ? sub.multi_colors : undefined;
  const isEmpty = !sub.has_filament;
  const isThird = sub.filament_name === '?';
  const displayName = isEmpty ? t('Empty') : sub.filament_name;

  return (
    <div className="ams-map-hotend__sub-ams-page__parent">
      <div
        className="ams-map-hotend__sub-card ams-map-hotend__sub-card--parent"
        data-state={sub.state}
        data-checked={sub.checked}
        data-clickable={sub.is_pickable}
        // A card locked behind an engaged external spool keeps state Normal, so
        // the dim has to be keyed off pickability just like the sub-cards do.
        data-pickable={sub.is_pickable}
        role={sub.is_pickable ? 'button' : undefined}
        tabIndex={sub.is_pickable ? 0 : -1}
        onClick={() => { if (sub.is_pickable) onSelect(sub); }}
        {...hoverProps}
        onKeyDown={(e) => {
          if ((e.key === 'Enter' || e.key === ' ') && sub.is_pickable) {
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
                  <path d="M2.56734 10.5956L1.99833 12.5277L1.69156 13.5717C1.62971 13.782 1.68661 14.0072 1.84 14.1605C1.99338 14.3139 2.21851 14.3708 2.42633 14.3115L3.47281 14.0022L5.40497 13.4332C5.71174 13.3441 5.9913 13.1883 6.23127 12.978C6.26591 12.9483 6.29807 12.9161 6.33023 12.884L13.856 5.36068C14.3978 4.81888 14.4646 3.98516 14.0589 3.36914C14.002 3.28255 13.9327 3.19844 13.856 3.12174L12.8813 2.14453C12.2628 1.52604 11.2608 1.52604 10.6423 2.14453L3.11656 9.67031C2.85927 9.9276 2.67125 10.2467 2.56734 10.5956ZM3.70536 10.9296C3.74742 10.791 3.81669 10.6624 3.91318 10.5535C3.92802 10.5387 3.94039 10.5238 3.95523 10.509L9.60823 4.85846L11.1421 6.39232L5.49156 12.0453C5.37528 12.1616 5.22932 12.2482 5.07099 12.2952L4.49208 12.4659L3.13635 12.8642L3.53466 11.5085L3.70536 10.9296Z" fill="currentColor"/>
                </svg>
              </button>
            )}
          </div>
        </div>
        {tip}
      </div>
    </div>
  );
}

function WarningBanner({ text }: { text: string }) {
  return (
    <div className="ams-map-hotend__sub-ams-page__warning">
      <svg width="16" height="16" viewBox="0 0 16 16" fill="none">
        <circle cx="8" cy="8" r="6.5" stroke="#ea6e00" strokeWidth="1.2" />
        <rect x="7.3" y="6.8" width="1.4" height="5" rx="0.7" fill="#ea6e00" />
        <circle cx="8" cy="4.6" r="0.9" fill="#ea6e00" />
      </svg>
      <span>{text}</span>
    </div>
  );
}

export function SubAmsPage({ slot, materialIndex, onSelect, onEdit, onBack, onOpenWiki }: SubAmsPageProps) {
  const { t } = useTranslation();
  if (!slot.sub_ams || slot.sub_ams.length === 0) return null;

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

  // The left card is always the hotend's own rack AMS slot. When the hotend is
  // currently fed by the external spool the main card is that spool, and the
  // rack slot arrives as the synthetic preload group instead; promote it out of
  // the group list so the right-hand side only ever holds real sub-AMS units.
  const preloadGroup = slot.sub_ams.find((g) => g.is_preload);
  const rackSlot = slot.ext ? preloadGroup?.slots[0] ?? null : extAsSubSlot;
  const subGroups = slot.ext && preloadGroup
    ? slot.sub_ams.filter((g) => !g.is_preload)
    : slot.sub_ams;

  // Say it once at the top instead of relying on the user hovering each card.
  // Empty string when nothing is in the way.
  const bannerText = resolveFeedBlockTooltip(t, slot.hotend_feed_block);
  const warningBanner = bannerText ? <WarningBanner text={bannerText} /> : null;

  const backButton = (
    <button
      className="ams-map-hotend__sub-ams-page__back"
      onClick={onBack}
      type="button"
    >
      <svg width="20" height="20" viewBox="0 0 20 20" fill="none">
        <path d="M12 15L7 10L12 5" stroke="#262E30" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round"/>
      </svg>
      <span>{t('Back')}</span>
    </button>
  );

  const bottomTip = <BottomWikiTip onOpenWiki={onOpenWiki} />;

  // Parent card (left) + sub-AMS group containers (right) in the top section.
  // When the slot itself is an Ext source it is pushed to the bottom section
  // below a divider.
  return (
    <div className="ams-map-hotend__sub-ams-page" data-testid="ams-mapping-hotend-sub-ams-page">
      {warningBanner}
      {backButton}

      <div className="ams-map-hotend__sub-ams-page__body">
        {rackSlot && (
          <ParentCard
            sub={rackSlot}
            materialIndex={materialIndex}
            onSelect={onSelect}
            onEdit={onEdit}
          />
        )}

        <div className="ams-map-hotend__sub-ams-page__groups">
          {subGroups.map((group) => (
            <div
              key={group.name}
              className="ams-map-hotend__sub-ams-page__group"
              // A multi-slot unit owns its row; single-slot units (AMS HT) pack
              // three to a row, each keeping its own border and label.
              data-single-slot={group.slots.length === 1}
            >
              <div className="ams-map-hotend__sub-ams-page__group-inner">
                {group.slots.map((sub) => (
                  <SubCard
                    key={`${sub.ams_id}-${sub.slot_id}`}
                    sub={sub}
                    materialIndex={materialIndex}
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
