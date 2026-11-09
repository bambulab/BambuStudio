import type { TFunction } from 'i18next';
import type { AmsMappingHotendFeedBlock, AmsMappingHotendSlotState } from './types';

// Resolve the hover tooltip text for a non-selectable (or mismatched) hotend
// slot.
//
// Only one tip is ever shown, and it is the most severe reason: `state` is a
// single value already collapsed by the C++ priority chain in
// BuildSlotJudgement (NozzleLost > NozzleEmpty > NozzleError > NozzleUnknown >
// NozzleUncalibrated > NozzleUnmatch > MaterialEmpty > MaterialUnmatch), so
// when both the nozzle and the material mismatch the state is a nozzle-side
// one and the nozzle copy wins automatically.
//
// Material copy mirrors the legacy wx popup tooltips in
// `AmsMappingPopup.cpp` MappingItem::set_data. Nozzle copy reuses the wording
// of the rack-nozzle status messages in `SelectMachine.cpp` (S_RACK_NOZZLE_*
// and the diameter / flow mismatch messages) so rack machines stay consistent.
//
// Returns an empty string when no tooltip should be shown (e.g. Normal slots,
// including unrecognized third-party filament which the backend resolves to
// Normal on purpose).
export function resolveSlotTooltip(
  t: TFunction,
  state: AmsMappingHotendSlotState,
  isPickable: boolean,
  slotType: string,
  targetType: string,
): string {
  switch (state) {
    case 'MaterialUnmatch':
      // External spool / all-ext relaxation keeps the slot pickable: surface a
      // softer "Tips:" hint instead of the hard "Cannot select:" wording.
      return isPickable
        ? t(
            "Tips: the filament type({{slot}}) does not match with the filament type({{target}}) in the slicing file. If you want to use this slot, you can install {{target}} instead of {{slot}} and change slot information on the 'Device' page.",
            { slot: slotType, target: targetType },
          )
        : t(
            "Cannot select: the filament type({{slot}}) does not match with the filament type({{target}}) in the slicing file. If you want to use this slot, you can install {{target}} instead of {{slot}} and change slot information on the 'Device' page.",
            { slot: slotType, target: targetType },
          );
    case 'MaterialEmpty':
      return t('Cannot select: No filament loaded in current slot.');
    case 'NozzleEmpty':
      return t(
        'Cannot select: No hotends available. Please complete the hotend rack setup and try again.',
      );
    case 'NozzleLost':
      return t('Cannot select: The hotend is detached. Please install it before printing.');
    case 'NozzleUncalibrated':
      return t(
        'Cannot select: The toolhead position is not calibrated yet. Please go to the printer and run the toolhead rack position calibration, then try again.',
      );
    case 'NozzleUnknown':
      return t(
        'Cannot select: The hotend type is unknown. Please go to the printer and edit the hotend type, then try again.',
      );
    // NozzleError has no hover tooltip on purpose: clicking the card opens a
    // dialog that routes to HMS, which carries the full diagnosis.
    case 'NozzleError':
      return '';
    case 'NozzleUnmatch':
      return t(
        "Cannot select: The nozzle diameter or flow setting doesn't match with the slicing file. Please make sure the nozzle installed matches with settings in printer, then set the corresponding printer preset when slicing.",
      );
    default:
      return '';
  }
}

// Tooltip explaining why the printer cannot feed a slot on its own. Only
// `ext_spool` is surfaced here because it also disables the card; the other
// blocks stay selectable and are raised as send-page pre-print warnings.
export function resolveFeedBlockTooltip(
  t: TFunction,
  feedBlock: AmsMappingHotendFeedBlock | undefined,
): string {
  switch (feedBlock) {
    case 'ext_spool':
      // The printer cannot retract an engaged external spool, so every other
      // source of this hotend stays display-only until the user pulls it out.
      return t('Unload the external filament manually before feeding to enable selection.');
    default:
      return '';
  }
}
