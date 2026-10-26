import { useTranslation } from 'react-i18next';
import type { AmsMappingHotendSlotViewModel } from '../types';
import { resolveSlotTooltip } from '../slotTooltip';
import { SpoolRing } from './SpoolRing';
import { NozzleInfo } from './NozzleInfo';
import { useHoverTip } from './HoverTip';

interface SlotCardProps {
  slot: AmsMappingHotendSlotViewModel;
  // Slicing's expected filament type for the filament being mapped; used to
  // interpolate the material-mismatch tooltip.
  materialIndex: string;
  onClick: () => void;
  onEnterSubAms: () => void;
  onEdit: () => void;
  // Raised when a hotend that cannot talk to the mainboard is clicked; the
  // native side shows the prompt and routes to HMS.
  onNozzleError: () => void;
}

export function SlotCard({ slot, materialIndex, onClick, onEnterSubAms, onEdit, onNozzleError }: SlotCardProps) {
  const { t } = useTranslation();
  const hasRealSubAms = !!slot.sub_ams && slot.sub_ams.some(g => !g.is_preload);
  const hasSubAms = !!slot.sub_ams && slot.sub_ams.length > 0;
  // Pickability is decided in C++ (see ComputePickable). All-ext relaxation
  // and nozzle strictness are already baked in. The card body always uses
  // that select path; a bound sub-AMS list is opened only by the swap button.
  const disabled = !slot.is_pickable;
  // A hotend error is the one non-pickable state that stays clickable: the
  // click is the only entry point to the HMS diagnosis.
  const isNozzleError = slot.state === 'NozzleError';

  const handleClick = () => {
    if (isNozzleError) {
      onNozzleError();
    } else if (!disabled) {
      onClick();
    }
  };

  // Display layer is driven by `has_filament` alone, decoupled from
  // `state` / `is_pickable`. A slot with filament but a missing nozzle
  // still shows the real spool color (it just can't be clicked).
  const isEmpty = !slot.has_filament;
  const displayName = isEmpty ? t('Empty') : slot.filament_name;
  // Mirrors C++ BuildSlotJson(): TrayType::THIRD is rendered with filament_name "?".
  const isThird = slot.filament_name === '?';
  const multiColors = slot.multi_colors && slot.multi_colors.length > 1 ? slot.multi_colors : undefined;

  // Hover hint for non-selectable / mismatched slots. The swap button is a
  // separate control, so a sub-AMS card still explains why the card itself
  // cannot be selected.
  const tooltip = resolveSlotTooltip(t, slot.state, slot.is_pickable, slot.filament_type, materialIndex);
  const { hoverProps, tip } = useHoverTip(tooltip);

  return (
    <div
      className="ams-map-hotend__slot-card"
      data-state={slot.state}
      data-has-sub-ams={hasSubAms}
      data-nozzle-error={isNozzleError}
      data-testid={`ams-mapping-hotend-slot-${slot.ams_id}-${slot.slot_id}`}
      onClick={handleClick}
      {...hoverProps}
      role="button"
      tabIndex={0}
      onKeyDown={(e) => {
        // Swap / edit are nested controls. Ignore keys that originated there
        // so Enter on the swap button does not also select this slot.
        if (e.target !== e.currentTarget) return;
        if (e.key === 'Enter' || e.key === ' ') {
          e.preventDefault();
          handleClick();
        }
      }}
    >
      {/* CardHead: index pill (left) + badge + radio/check (right) */}
      <div className="ams-map-hotend__slot-card__head">
        <div className="ams-map-hotend__slot-card__head-left">
          <div className="ams-map-hotend__slot-card__pill" data-state={slot.state}>
            <span>{slot.index}</span>
            {slot.ext && <span className="ams-map-hotend__slot-card__pill-ext">Ext</span>}
          </div>
          {hasRealSubAms && (
            <div className="ams-map-hotend__sub-ams-badge">
              <svg width="16" height="16" viewBox="0 0 16 16" fill="none">
                <rect x="2" y="2.5" width="12" height="11" rx="1.5" stroke="#a3a3a3" strokeWidth="1"/>
                <rect x="4" y="4.5" width="8" height="1.2" rx="0.4" fill="#a3a3a3"/>
                <rect x="4" y="6.5" width="8" height="1.2" rx="0.4" fill="#a3a3a3"/>
                <rect x="4" y="8.5" width="8" height="1.2" rx="0.4" fill="#a3a3a3"/>
                <rect x="4" y="10.5" width="8" height="1.2" rx="0.4" fill="#a3a3a3"/>
              </svg>
              <span className="ams-map-hotend__sub-ams-badge__count">
                +{slot.sub_ams!.filter(g => !g.is_preload).length}
              </span>
            </div>
          )}
        </div>
        <div
          className="ams-map-hotend__slot-card__radio"
          data-checked={slot.checked}
        >
          {slot.checked && (
            <svg width="10" height="8" viewBox="0 0 10 8" fill="none">
              <path d="M1 4L3.5 6.5L9 1" stroke="#fff" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" />
            </svg>
          )}
        </div>
      </div>

      {/* Spool area: ring + material name */}
      <div className="ams-map-hotend__slot-card__spool-area">
        <div className="ams-map-hotend__slot-card__spool-wrapper">
          <SpoolRing
            size={52}
            color={slot.color}
            colors={multiColors}
            remain={slot.remain}
            isEmpty={isEmpty}
            isThird={isThird}
            colorType={slot.color_type}
          />
          {hasSubAms && (
            <button
              type="button"
              className="ams-map-hotend__slot-card__swap-icon"
              data-testid={`ams-mapping-hotend-swap-${slot.ams_id}-${slot.slot_id}`}
              // Only this control opens the sub-AMS page. Stop the click so
              // the card body keeps its select / nozzle-error behavior.
              onClick={(e) => {
                e.stopPropagation();
                onEnterSubAms();
              }}
              aria-label="Switch filament"
            >
              <svg width="20" height="20" viewBox="0 0 24 24" fill="none">
                <rect width="24" height="24" rx="12" fill="#CECECE"/>
                <g clipPath={`url(#clip_swap_${slot.ams_id}_${slot.slot_id})`}>
                  <path d="M17.6006 13C17.9317 13.0002 18.2001 13.2694 18.2002 13.6006C18.2001 13.9318 17.9318 14.2 17.6006 14.2002H7.97754L10.3984 16.3525C10.6459 16.5726 10.668 16.9516 10.4482 17.1992C10.2281 17.4469 9.84926 17.4691 9.60156 17.249L6.62988 14.6084C5.99759 14.0463 6.39527 13.0001 7.24121 13H17.6006ZM13.9512 6.40136C14.1712 6.15382 14.5502 6.13182 14.7979 6.35156L17.7695 8.99218C18.4018 9.55423 18.0041 10.6005 17.1582 10.6006H6.79883C6.46763 10.6003 6.19922 10.3312 6.19922 9.99999C6.19944 9.66893 6.46777 9.40062 6.79883 9.40039H16.4219L14.001 7.24804C13.7537 7.02784 13.7312 6.6489 13.9512 6.40136Z" fill="#262E30"/>
                </g>
                <defs>
                  <clipPath id={`clip_swap_${slot.ams_id}_${slot.slot_id}`}>
                    <rect width="16" height="16" fill="white" transform="translate(4 4)"/>
                  </clipPath>
                </defs>
              </svg>
            </button>
          )}
        </div>
        <div className="ams-map-hotend__slot-card__material-row">
          <div className="ams-map-hotend__slot-card__material-name">{displayName}</div>
          {slot.can_edit && (
            <button
              type="button"
              className="ams-map-hotend__slot-card__edit-btn"
              data-testid={`ams-mapping-hotend-edit-${slot.ams_id}-${slot.slot_id}`}
              // Editing must stay available even when the card itself is
              // disabled (e.g. an already-edited third-party filament that no
              // longer matches the slicing job). Stop propagation so the
              // card's select/sub-AMS handler doesn't also fire.
              onClick={(e) => {
                e.stopPropagation();
                onEdit();
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

      {/* Divider */}
      <div className="ams-map-hotend__slot-card__divider" />

      {/* Nozzle info */}
      <NozzleInfo nozzle={slot.nozzle} state={slot.state} />
      {tip}
    </div>
  );
}
