// Shared golden ViewModel used to drive BOTH front-ends from an identical
// state during visual comparison.
//
// Transcribed from device_page/src/features/device-page/ams-filament-hotend/
// mockData.ts. That file is TypeScript and pulls in the whole Vite graph, so
// rather than transpiling it at test time the builder logic is mirrored here.
// Keep the two in sync when the mock changes - compare-pages.mjs is only
// meaningful while both sides start from the same payload.

const lineFor = (state) => {
  switch (state) {
    case 'Loading': return 'loading';
    case 'Unloading': return 'unloading';
    case 'ReadyToUse': return 'loaded';
    default: return 'no_filament';
  }
};

const nozzleFor = (state) => {
  switch (state) {
    case 'Loading':
    case 'Unloading':
    case 'ReadyToUse': return 'Active';
    case 'Error': return 'Error';
    default: return 'Normal';
  }
};

const buildSlot = (seed) => {
  const lineState = seed.line_state ?? lineFor(seed.card_state);
  const nozzleState = seed.nozzle_state ?? nozzleFor(seed.card_state);
  return {
    addr_id: seed.display_index - 1,
    display_index: seed.display_index,
    ams_id: '0',
    slot_id: String(seed.display_index - 1),
    bind_ams: Array.from({ length: seed.bind_ams_count ?? 0 }, (_, i) => ({
      ams_id: String(i),
      ams_type: 'ZMULTI_A',
      slot_count: 4,
    })),
    bind_ams_count: seed.bind_ams_count ?? 0,
    menu_actions: {
      can_edit: seed.card_state !== 'Empty',
      can_load: lineFor(seed.card_state) === 'no_filament' && seed.card_state !== 'Empty',
      can_unload: lineFor(seed.card_state) === 'loaded',
      can_read: seed.card_state !== 'Reading' && seed.card_state !== 'Empty',
      read_label: seed.card_state === 'Unknown' ? 'Read' : 'Reread',
      can_ams_operation: (seed.bind_ams_count ?? 0) > 0,
    },
    filament_card: {
      state: seed.card_state,
      color: seed.color ?? '#00AE42',
      color_type: seed.color_type ?? 'single',
      text: seed.filament_name ?? '',
      name: seed.filament_name ?? '',
      type: seed.filament_name?.split(' ')[0] ?? '',
      remain: seed.filament_remain ?? 100,
      status_text: seed.status_text ?? '',
      new_filament_hint: seed.new_filament_hint ?? false,
    },
    filament_line: {
      state: lineState,
      has_filament: lineState === 'loaded' || lineState === 'loading' || lineState === 'unloading',
      filament_color: lineState === 'no_filament' || lineState === 'no_hotend'
        ? '' : (seed.color ?? '#00AE42'),
    },
    filament_nozzle: {
      state: nozzleState,
      has_filament: nozzleState === 'Active',
      filament_color: nozzleState === 'Active' ? (seed.color ?? '#00AE42') : '',
      on_extruder: seed.is_on_extruder ?? false,
    },
    ams_operation: seed.ams_operation,
    enabled: true,
    is_top_tier: seed.display_index % 2 === 0,
    selected: seed.selected ?? false,
    is_external_spool: seed.is_external_spool ?? false,
  };
};

const hotendMenuActions = (hotend) => {
  const kind = hotend?.tool_kind ?? 'empty';
  const op = hotend?.op_state ?? 'Idle';
  const busy = op === 'Reading' || op === 'Taking' || op === 'Placing' || op === 'Removing';
  const canOperate = !busy;
  return {
    can_read: canOperate,
    can_detail: true,
    can_set_temperature: canOperate && kind === 'nozzle' && op !== 'Error',
    can_take_place: canOperate,
  };
};

const mockDetail = {
  filament_name: 'BambuLab PLA Basic',
  filament_color: '#675AFF',
  show_filament: true,
  tool_image_name: 'hotend_detail_nozzle',
  tool_type: '0.4mm Standard Hardened',
  nozzle_type: '0.4mm Standard Hardened',
  firmware_version: '10.10.10.10',
  serial_number: '1234567890',
  used_time_hours: '2057.0h',
  switch_count: 128,
};

const buildHotendDetail = (hotend) => {
  if (!hotend || hotend.tool_kind === 'empty') {
    return { used_time_hours: null, switch_count: null, tool_image_name: 'empty', show_filament: false };
  }
  if (hotend.tool_kind !== 'nozzle') {
    return {
      ...mockDetail,
      filament_name: '',
      filament_color: '',
      show_filament: false,
      tool_image_name: 'empty',
      tool_type: hotend.nozzle_label ?? '--',
      nozzle_type: hotend.nozzle_label ?? '--',
    };
  }
  return mockDetail;
};

const buildHotendSlot = (display_index, seed) => ({
  addr_id: display_index - 1,
  display_index,
  ams_id: '0',
  slot_id: String(display_index - 1),
  enabled: true,
  taken: seed.is_on_extruder ?? false,
  menu_actions: hotendMenuActions(seed.hotend),
  hotend: seed.hotend ?? { op_state: 'Idle', tool_kind: 'empty' },
  hotend_detail_dialog: buildHotendDetail(seed.hotend),
});

const mockHotend = (display_index) => {
  switch (display_index) {
    case 1: return { current_temp: 220, target_temp: 0, op_state: 'Idle', tool_kind: 'nozzle', nozzle_label: '0.4 mm', nozzle_subtext: 'Std', selected: true };
    case 2: return { current_temp: 64, target_temp: 0, op_state: 'Heating', tool_kind: 'nozzle', nozzle_label: '0.4 mm', nozzle_subtext: 'Std' };
    case 3: return { current_temp: 23, target_temp: 0, op_state: 'Idle', tool_kind: 'nozzle', nozzle_label: '0.4 mm', nozzle_subtext: 'Std' };
    case 4: return { op_state: 'Idle', tool_kind: 'cutter', nozzle_label: 'Cutter' };
    case 5: return { current_temp: 23, target_temp: 0, op_state: 'Error', tool_kind: 'pen', nozzle_label: 'Highlighter' };
    case 6: return { op_state: 'Idle', tool_kind: 'laser', nozzle_label: 'Laser' };
    case 7: return { op_state: 'Idle', tool_kind: 'fan', nozzle_label: 'Fan' };
    case 8: return { op_state: 'Placing', tool_kind: 'nozzle', nozzle_label: '0.4 mm', nozzle_subtext: 'Std' };
    case 9: return { op_state: 'Reading', tool_kind: 'nozzle' };
    default: return { op_state: 'Idle', tool_kind: 'empty' };
  }
};

const slotSeeds = [
  { display_index: 1, card_state: 'Loading', filament_name: 'PLA Basic', status_text: 'Loading/Unloading Filament', color: '#00AE42', selected: true, is_on_extruder: true, is_external_spool: true, hotend: mockHotend(1) },
  { display_index: 2, card_state: 'Reading', status_text: 'Reading', hotend: mockHotend(2) },
  { display_index: 3, card_state: 'ReadyToLoad', filament_name: 'PLA Basic', status_text: 'Ready to load', color: '#00AE42', new_filament_hint: true, hotend: mockHotend(3) },
  { display_index: 4, card_state: 'Empty', status_text: '--', hotend: mockHotend(4) },
  { display_index: 5, card_state: 'ReadyToUse', filament_name: 'PLA Basic', status_text: 'Loaded', color: '#00AE42', bind_ams_count: 2, filament_remain: 35, ams_operation: { available: true }, hotend: mockHotend(5) },
  { display_index: 6, card_state: 'ReadyToUse', filament_name: 'PLA Basic', status_text: 'Loaded', color: '#00AE42', filament_remain: 75, hotend: mockHotend(6) },
  { display_index: 7, card_state: 'ReadyToUse', filament_name: 'PLA Basic', status_text: 'Loaded', color: '#00AE42', hotend: mockHotend(7) },
  { display_index: 8, card_state: 'ReadyToUse', filament_name: 'PLA Basic', status_text: 'Loaded', color: '#00AE42', filament_remain: 10, hotend: mockHotend(8) },
  { display_index: 9, card_state: 'ReadyToUse', filament_name: 'PLA Basic', status_text: 'Loaded', color: '#00AE42', hotend: mockHotend(9) },
];

export const mockAmsFilamentHotendState = {
  filament_page: {
    mode: 'normal',
    actions: {
      can_load: false,
      can_load_external: false,
      can_show_load_auto: false,
      can_unload: false,
      can_dry: true,
      show_ams_settings: true,
      show_auto_refill: true,
      show_read_all: true,
      can_read_all_slots: true,
      tips: 'Slot 1 loading/unloading filament in progress',
    },
    // Deliberately inactive: the progress panel is compared separately, and an
    // active change would cover the stage that this fixture exists to check.
    filament_change_status: { active: false, mode: 'idle' },
    ams_dry_zones: [
      {
        ams_id: '0',
        // Native AMS slot_id values. With buildSlot's slot_id = display_index-1,
        // [5, 7] covers display_index 6 and 8 (the dryer slot columns).
        zone_slots: [5, 7],
        humidity_raw: 42,
        dry_time: 120,
        dry_status: 'Drying',
        dry_status_text: 'Drying',
      },
    ],
    hotend_slots: slotSeeds.map(buildSlot),
  },
  hotend_page: {
    actions: { can_read_all_hotends: true, read_all_disabled_reason: '' },
    hotend_rack_status: {
      active: false, mode: 'idle', display_index: 0, current: 0, total: 9,
      message: '', tips: '', job: 0, operation: 'idle', state: 'idle',
      target_display_index: 0, current_step: 0, steps: [],
      can_stop: false, can_retry: false,
    },
    hotend_slots: slotSeeds.map((seed) => buildHotendSlot(seed.display_index, seed)),
  },
};

export default mockAmsFilamentHotendState;
