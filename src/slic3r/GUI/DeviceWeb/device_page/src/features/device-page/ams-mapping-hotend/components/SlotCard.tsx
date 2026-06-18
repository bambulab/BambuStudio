import { useTranslation } from 'react-i18next';
import type { AmsMappingHotendSlotViewModel } from '../types';
import { resolveSlotTooltip } from '../slotTooltip';
import { SpoolRing } from './SpoolRing';
import { NozzleInfo } from './NozzleInfo';

interface SlotCardProps {
  slot: AmsMappingHotendSlotViewModel;
  // Slicing's expected filament type for the filament being mapped; used to
  // interpolate the material-mismatch tooltip.
  materialIndex: string;
  onClick: () => void;
  onEnterSubAms: () => void;
  onEdit: () => void;
}

export function SlotCard({ slot, materialIndex, onClick, onEnterSubAms, onEdit }: SlotCardProps) {
  const { t } = useTranslation();
  const hasRealSubAms = !!slot.sub_ams && slot.sub_ams.some(g => !g.is_preload);
  const hasSubAms = !!slot.sub_ams && slot.sub_ams.length > 0;
  // Pickability is decided in C++ (see ComputePickable). All-ext relaxation
  // and nozzle strictness are already baked in.
  const disabled = !slot.is_pickable;

  const handleClick = () => {
    if (hasSubAms) {
      onEnterSubAms();
    } else if (!disabled) {
      onClick();
    }
  };

  // Display layer is driven by `has_filament` alone, decoupled from
  // `state` / `is_pickable`. A slot with filament but a missing nozzle
  // still shows the real spool color (it just can't be clicked).
  const isEmpty = !slot.has_filament;
  const displayName = isEmpty ? 'Empty' : slot.filament_name;
  // Mirrors C++ BuildSlotJson(): TrayType::THIRD is rendered with filament_name "?".
  const isThird = slot.filament_name === '?';
  const multiColors = slot.multi_colors && slot.multi_colors.length > 1 ? slot.multi_colors : undefined;

  // Hover hint for non-selectable / mismatched slots. Slots that can enter a
  // sub-AMS expand stay actionable, so we suppress the tooltip for them.
  const tooltip = hasSubAms
    ? ''
    : resolveSlotTooltip(t, slot.state, slot.is_pickable, slot.filament_type, materialIndex);

  return (
    <div
      className="ams-map-hotend__slot-card"
      data-state={slot.state}
      data-has-sub-ams={hasSubAms}
      data-testid={`ams-mapping-hotend-slot-${slot.ams_id}-${slot.slot_id}`}
      title={tooltip || undefined}
      onClick={handleClick}
      role="button"
      tabIndex={0}
      onKeyDown={(e) => {
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
            <svg className="ams-map-hotend__slot-card__swap-icon" width="20" height="20" viewBox="0 0 24 24" fill="none">
              <rect width="24" height="24" rx="12" fill="#CECECE"/>
              <g clipPath="url(#clip_swap)">
                <path d="M17.6006 13C17.9317 13.0002 18.2001 13.2694 18.2002 13.6006C18.2001 13.9318 17.9318 14.2 17.6006 14.2002H7.97754L10.3984 16.3525C10.6459 16.5726 10.668 16.9516 10.4482 17.1992C10.2281 17.4469 9.84926 17.4691 9.60156 17.249L6.62988 14.6084C5.99759 14.0463 6.39527 13.0001 7.24121 13H17.6006ZM13.9512 6.40136C14.1712 6.15382 14.5502 6.13182 14.7979 6.35156L17.7695 8.99218C18.4018 9.55423 18.0041 10.6005 17.1582 10.6006H6.79883C6.46763 10.6003 6.19922 10.3312 6.19922 9.99999C6.19944 9.66893 6.46777 9.40062 6.79883 9.40039H16.4219L14.001 7.24804C13.7537 7.02784 13.7312 6.6489 13.9512 6.40136Z" fill="#262E30"/>
              </g>
              <defs>
                <clipPath id="clip_swap">
                  <rect width="16" height="16" fill="white" transform="translate(4 4)"/>
                </clipPath>
              </defs>
            </svg>
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
                <path d="M11.2 2.8a1.13 1.13 0 0 1 1.6 1.6L5.3 11.9l-2.3.7.7-2.3 7.5-7.5Z" stroke="currentColor" strokeWidth="1.2" strokeLinejoin="round"/>
              </svg>
            </button>
          )}
        </div>
      </div>

      {/* Divider */}
      <div className="ams-map-hotend__slot-card__divider" />

      {/* Nozzle info */}
      <NozzleInfo nozzle={slot.nozzle} state={slot.state} />

    </div>
  );
}
