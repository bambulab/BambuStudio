import type { Spool } from './types';

export function effectiveSpool(spool: Spool): Spool {
  const original = spool.original_spool || spool;
  const assignment = original.rfid_override;
  if (!assignment) return original;
  return {
    ...original,
    ...(assignment.identity || { brand: '', series: Object.values(assignment.profiles)[0]?.split('@')[0].trim() || 'Assigned filament' }),
    color_code: assignment.color,
    colors: [assignment.color],
    color_type: 2,
    color_name: '',
    // The original product code does not describe the replacement filament.
    tray_id_name: '',
    setting_id: '',
    original_spool: original,
  };
}
