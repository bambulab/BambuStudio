import type { AutoRefillRefillInfoViewModel } from './types';

// Standalone sample used before the first C++ push and by the bridge-debug
// route, so the layout is inspectable in a plain browser. Shapes match
// AutoRefillRefillInfoVM::BuildState() and mirror the Figma reference groups.
export const mockAutoRefillRefillInfoState: AutoRefillRefillInfoViewModel = {
  selected_dev_id: 'mock-dev',
  session_seq: 0,
  auto_refill_enabled: true,
  description:
    'The system has created auto-refill groups based on material type, color, and brand.\n'
    + 'When printing, after the current spool runs out, the printer automatically switches to the next compatible spool in the group order and loops back to the first spool after the last one.\n'
    + 'Drag spools to adjust their priority.',
  editable: true,
  can_add_group: true,
  extruders: [
    {
      extruder_id: 0,
      groups: [
        {
          refill_tag_id: 'GFA00_2_1_28F451FF_S_0.4',
          filament_id: 'GFA00',
          filament_type: 'PLA',
          colors: ['#28f451'],
          color_type: 'single',
          nozzle_diameter: 0.4,
          nozzle_flow_type: 'Std',
          slots: [
            { ams_id: 0, slot_id: 0, create_type: 'auto', has_filament: true, tray_name: 'A1', filament_type: 'PLA', sub_brand: 'PLA Basic', color: '#BC2C2CFF', colors: ['#BC2C2CFF'], color_type: 'single', weight_gram: 1000 },
            { ams_id: 1, slot_id: 0, create_type: 'auto', has_filament: true, tray_name: 'B1', filament_type: 'PLA', sub_brand: 'PLA Matte', color: '#FF5F5FFF', colors: ['#FF5F5FFF', '#4E3FFFFF'], color_type: 'gradient', weight_gram: 1000 },
            { ams_id: 1, slot_id: 1, create_type: 'manual', has_filament: true, tray_name: 'B2', filament_type: 'PLA', sub_brand: 'PLA Basic', color: '#5DDC1EFF', colors: ['#5DDC1EFF', '#0DAEF8FF'], color_type: 'multi', weight_gram: 1000 },
          ],
        },
        {
          refill_tag_id: 'GFB00_2_1_F09425FF_S_0.4',
          filament_id: 'GFB00',
          filament_type: 'PETG',
          colors: ['#f09425'],
          color_type: 'single',
          nozzle_diameter: 0.4,
          nozzle_flow_type: 'Std',
          slots: [
            { ams_id: 0, slot_id: 1, create_type: 'auto', has_filament: true, tray_name: 'A1', filament_type: 'PETG', sub_brand: 'PETG Basic', color: '#0A84FFB3', colors: ['#0A84FFB3'], color_type: 'single', weight_gram: 1000 },
            { ams_id: 0, slot_id: 2, create_type: 'auto', has_filament: true, tray_name: 'A2', filament_type: 'PETG', sub_brand: 'PETG Basic', color: '#f09425', colors: ['#f09425'], color_type: 'single', weight_gram: 1000 },
            { ams_id: 0, slot_id: 3, create_type: 'auto', has_filament: true, tray_name: 'A3', filament_type: 'PETG', sub_brand: 'PETG Basic', color: '#f09425', colors: ['#f09425'], color_type: 'single', weight_gram: 1000 },
            { ams_id: 0, slot_id: 4, create_type: 'auto', has_filament: true, tray_name: 'A4', filament_type: 'PETG', sub_brand: 'PETG Basic', color: '#f09425', colors: ['#f09425'], color_type: 'single', weight_gram: 1000 },
            { ams_id: 1, slot_id: 3, create_type: 'manual', has_filament: true, tray_name: 'B1', filament_type: 'PETG', sub_brand: 'PETG Basic', color: '#f09425', colors: ['#f09425'], color_type: 'single', weight_gram: 1000 },
          ],
        },
        {
          refill_tag_id: 'GFC00_2_1_4028F3FF_S_0.4',
          filament_id: 'GFC00',
          filament_type: 'ABS',
          colors: ['#4028f3'],
          color_type: 'single',
          nozzle_diameter: 0.4,
          nozzle_flow_type: 'Std',
          slots: [
            { ams_id: 2, slot_id: 0, create_type: 'auto', has_filament: true, tray_name: 'A1', filament_type: 'ABS', sub_brand: 'ABS', color: '#4028f3', colors: ['#4028f3'], color_type: 'single', weight_gram: 1000 },
            { ams_id: 2, slot_id: 1, create_type: 'auto', has_filament: true, tray_name: 'A1', filament_type: 'ABS', sub_brand: 'ABS', color: '#4028f3', colors: ['#4028f3'], color_type: 'single', weight_gram: 1000 },
            { ams_id: 2, slot_id: 2, create_type: 'auto', has_filament: true, tray_name: 'A1', filament_type: 'ABS', sub_brand: 'ABS', color: '#4028f3', colors: ['#4028f3'], color_type: 'single', weight_gram: 1000 },
            { ams_id: 2, slot_id: 3, create_type: 'auto', has_filament: true, tray_name: 'A1', filament_type: 'ABS', sub_brand: 'ABS', color: '#4028f3', colors: ['#4028f3'], color_type: 'single', weight_gram: 1000 },
            { ams_id: 2, slot_id: 4, create_type: 'auto', has_filament: true, tray_name: 'A1', filament_type: 'ABS', sub_brand: 'ABS', color: '#4028f3', colors: ['#4028f3'], color_type: 'single', weight_gram: 1000 },
          ],
        },
      ],
    },
  ],
};
