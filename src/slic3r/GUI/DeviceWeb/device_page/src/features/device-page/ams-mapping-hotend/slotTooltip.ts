import type { TFunction } from 'i18next';
import type { AmsMappingHotendSlotState } from './types';

// Resolve the hover tooltip text for a non-selectable (or mismatched) hotend
// slot.
//
// Only one tip is ever shown, and it is the most severe reason: `state` is a
// single value already collapsed by the C++ priority chain in
// BuildSlotJudgement (Unavailable > NozzleEmpty > NozzleUnmatch > MaterialEmpty
// > MaterialUnmatch), so when both the nozzle and the material mismatch the
// state is a Nozzle* one and the nozzle copy wins automatically.
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
    case 'Unavailable':
      return t(
        'Cannot select: The nozzle status is abnormal or unknown. Please refresh the nozzle information and try again.',
      );
    case 'NozzleUnmatch':
      return t(
        "Cannot select: The nozzle diameter or flow setting doesn't match with the slicing file. Please make sure the nozzle installed matches with settings in printer, then set the corresponding printer preset when slicing.",
      );
    default:
      return '';
  }
}
