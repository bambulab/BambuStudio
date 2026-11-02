import type {
  AmsFilamentHotendCardState,
  AmsFilamentHotendLineState,
  AmsFilamentHotendNozzleState,
  AmsFilamentHotendSlotViewModel,
  AmsFilamentHotendViewModel,
  HotendSlotInfo,
  HotendSlotViewModel,
} from './types';

interface SlotSeed {
  display_index: number;
  card_state: AmsFilamentHotendCardState;
  filament_name?: string;
  status_text?: string;
  color?: string;
  color_type?: AmsFilamentHotendSlotViewModel['filament_card']['color_type'];
  selected?: boolean;
  is_on_extruder?: boolean;
  is_external_spool?: boolean;
  line_state?: AmsFilamentHotendLineState;
  nozzle_state?: AmsFilamentHotendNozzleState;
  bind_ams_count?: number;
  filament_remain?: number;
  ams_operation?: AmsFilamentHotendSlotViewModel['ams_operation'];
  hotend?: HotendSlotInfo;
  new_filament_hint?: boolean;
}

const lineFor = (state: AmsFilamentHotendCardState): AmsFilamentHotendLineState => {
  switch (state) {
    case 'Loading':
      return 'loading';
    case 'Unloading':
      return 'unloading';
    case 'ReadyToUse':
      return 'loaded';
    case 'Empty':
    case 'Reading':
    case 'Unknown':
    case 'Error':
      return 'no_filament';
    default:
      return 'no_filament';
  }
};

const nozzleFor = (state: AmsFilamentHotendCardState): AmsFilamentHotendNozzleState => {
  switch (state) {
    case 'Loading':
    case 'Unloading':
    case 'ReadyToUse':
      return 'Active';
    case 'Error':
      return 'Error';
    default:
      return 'Normal';
  }
};

const buildSlot = (seed: SlotSeed): AmsFilamentHotendSlotViewModel => {
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
      filament_color: lineState === 'no_filament' || lineState === 'no_hotend' ? '' : (seed.color ?? '#00AE42'),
    },
    filament_nozzle: {
      state: nozzleState,
      has_filament: nozzleState === 'Active',
      filament_color: nozzleState === 'Active' ? (seed.color ?? '#00AE42') : '',
      on_extruder: seed.is_on_extruder ?? false,
      tool_kind: seed.hotend?.tool_kind,
      // The Filament tab caption drops the unit the Hotend tab labels carry.
      nozzle_label: seed.hotend?.nozzle_label?.replace(/\s*mm$/, ''),
      nozzle_subtext: seed.hotend?.nozzle_subtext,
    },
    ams_operation: seed.ams_operation,
    enabled: true,
    is_top_tier: seed.display_index % 2 === 0,
    selected: seed.selected ?? false,
    is_external_spool: seed.is_external_spool ?? false,
  };
};

const hotendMenuActions = (hotend?: HotendSlotInfo) => {
  const kind = hotend?.tool_kind ?? 'empty';
  const op = hotend?.op_state ?? 'None';
  const busy = op === 'Reading' || op === 'Taking' || op === 'Placing' || op === 'Removing';
  const canOperate = !busy;
  return {
    can_read: canOperate,
    can_detail: true,
    can_set_temperature: canOperate && kind === 'nozzle' && op !== 'Error',
    can_take_place: canOperate,
  };
};

const buildHotendSlot = (display_index: number, seed: SlotSeed): HotendSlotViewModel => ({
  addr_id: display_index - 1,
  display_index,
  ams_id: '0',
  slot_id: String(display_index - 1),
  enabled: true,
  taken: seed.is_on_extruder ?? false,
  menu_actions: hotendMenuActions(seed.hotend),
  filament_nozzle: buildSlot(seed).filament_nozzle,
  hotend: seed.hotend ?? { op_state: 'None', tool_kind: 'empty' },
  hotend_detail_dialog: buildHotendDetail(seed.hotend),
});

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

const buildHotendDetail = (hotend?: HotendSlotInfo) => {
  if (!hotend || hotend.tool_kind === 'empty') return { used_time_hours: null, switch_count: null, tool_image_name: 'empty', show_filament: false };
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

const mockHotend = (display_index: number): HotendSlotInfo => {
  switch (display_index) {
    case 1: return { current_temp: 220, target_temp: 0,   op_state: 'Active',   tool_kind: 'nozzle',  nozzle_label: '0.4 mm',     nozzle_subtext: 'Std',  selected: true };
    case 2: return { current_temp: 64,  target_temp: 260, op_state: 'Normal',   temp_trend: 'Heating', tool_kind: 'nozzle', nozzle_label: '0.4 mm', nozzle_subtext: 'Std' };
    case 3: return { current_temp: 23,  target_temp: 0,   op_state: 'Normal',   tool_kind: 'nozzle',  nozzle_label: '0.4 mm',     nozzle_subtext: 'Std' };
    case 4: return {                                                        op_state: 'Normal',   tool_kind: 'cutter',  nozzle_label: 'Cutter' };
    case 5: return { current_temp: 23,  target_temp: 0,   op_state: 'Error',    tool_kind: 'pen',     nozzle_label: 'Highlighter' };
    case 6: return {                                                        op_state: 'Normal',   tool_kind: 'laser',   nozzle_label: 'Laser' };
    case 7: return {                                                        op_state: 'Normal',   tool_kind: 'fan',     nozzle_label: 'Fan' };
    case 8: return {                                                        op_state: 'Placing',  tool_kind: 'nozzle',  nozzle_label: '0.4 mm',     nozzle_subtext: 'Std' };
    case 9: return {                                                        op_state: 'Reading',  tool_kind: 'nozzle' };
    default: return { op_state: 'None', tool_kind: 'empty' };
  }
};

const slotSeeds: SlotSeed[] = [
  { display_index: 1, card_state: 'Loading',     filament_name: 'PLA Basic', status_text: 'Loading/Unloading Filament', color: '#00AE42', selected: true, is_on_extruder: true, is_external_spool: true, hotend: mockHotend(1) },
  { display_index: 2, card_state: 'Reading',                                  status_text: 'Reading', hotend: mockHotend(2) },
  { display_index: 3, card_state: 'ReadyToLoad', filament_name: 'PLA Basic', status_text: 'Ready to load', color: '#00AE42', new_filament_hint: true, hotend: mockHotend(3) },
  { display_index: 4, card_state: 'Empty',                                    status_text: '--', hotend: mockHotend(4) },
  {
    display_index: 5,
    card_state: 'ReadyToUse',
    filament_name: 'PLA Basic',
    status_text: 'Loaded',
    color: '#00AE42',
    bind_ams_count: 2,
    filament_remain: 35,
    ams_operation: {
      available: true,
    },
    hotend: mockHotend(5),
  },
  { display_index: 6, card_state: 'ReadyToUse',  filament_name: 'PLA Basic', status_text: 'Loaded', color: '#00AE42', filament_remain: 75, hotend: mockHotend(6) },
  { display_index: 7, card_state: 'ReadyToUse',  filament_name: 'PLA Basic', status_text: 'Loaded', color: '#00AE42', hotend: mockHotend(7) },
  { display_index: 8, card_state: 'ReadyToUse',  filament_name: 'PLA Basic', status_text: 'Loaded', color: '#00AE42', filament_remain: 10, hotend: mockHotend(8) },
  { display_index: 9, card_state: 'ReadyToUse',  filament_name: 'PLA Basic', status_text: 'Loaded', color: '#00AE42', hotend: mockHotend(9) },
];

export const mockAmsFilamentHotendState: AmsFilamentHotendViewModel = {
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
    filament_change_status: {
      active: true,
      mode: 'single',
      operation: 'change',
      running_slot: {
        ams_id: '0',
        slot_id: '0',
        display_index: 1,
      },
      current_step: 6,
      current_step_name: 'Grab new filament',
      steps: [
        { id: 2, label: 'Heat the nozzle', step_style: '', step_style_text: '', step_text: '' },
        { id: 5, label: 'Push new filament into extruder', step_style: '', step_style_text: '', step_text: '' },
        { id: 6, label: 'Grab new filament', step_style: '', step_style_text: '', step_text: '' },
        { id: 7, label: 'Purge old filament', step_style: '', step_style_text: '', step_text: '' },
      ],
      step_labels: ['Heat the nozzle', 'Push new filament into extruder', 'Grab new filament', 'Purge old filament'],
      current_step_index: 3,
      current_step_total: 4,
      message: 'Slot 1 loading/unloading filament in progress',
      tips: 'Other filaments are info-only',
      can_stop: true,
      can_retry: true,
    },
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
    ams_env_units: [
      {
        ams_id: '0',
        zone_addrs: [0, 1, 2, 3, 4, 6, 8],
        humidity_raw: 24,
        temp: 24,
      },
    ],
    show_ams_env_units: true,
    hotend_slots: slotSeeds.map((seed) => buildSlot(seed)),
  },
  hotend_page: {
    actions: {
      can_read_all_hotends: true,
      read_all_disabled_reason: '',
    },
    hotend_rack_status: {
      active: false,
      mode: 'idle',
      display_index: 0,
      current: 0,
      total: 9,
      message: '',
      tips: '',
      job: 0,
      operation: 'idle',
      state: 'idle',
      target_display_index: 0,
      current_step: 0,
      steps: [],
      can_stop: false,
      can_retry: false,
    },
    hotend_slots: slotSeeds.map((seed) => buildHotendSlot(seed.display_index, seed)),
  },
  ams_ext_operation_page: {
    slots: [
      {
        addr_id: 4,
        display_index: 5,
        ams_id: '0',
        slot_id: '0',
        bind_ams_count: 2,
        bind_ams: [
          { ams_id: '32', ams_type: '1', slot_count: 4 },
          { ams_id: '33', ams_type: '1', slot_count: 4 },
        ],
        units: [
          {
            ams_id: '32',
            ams_type: '1',
            slot_count: 4,
            humidity_raw: 55,
            dry_status: 'Idle',
            trays: [
              { ams_id: '32', slot_id: '0', exists: false, tray_state: 'empty',   filament_type: '',     filament_name: '',     color: '#D9D9D9', actions: { can_read: true, read_label: 'Reread' } },
              { ams_id: '32', slot_id: '1', exists: true,  tray_state: 'unknown', filament_type: '',     filament_name: '?',    color: '#BFBFBF', actions: { can_read: true, can_load: true, read_label: 'Read' } },
              { ams_id: '32', slot_id: '2', exists: true,  tray_state: 'unknown', filament_type: '',     filament_name: '?',    color: '#BFBFBF', actions: { can_read: true, can_load: true, read_label: 'Read' } },
              { ams_id: '32', slot_id: '3', exists: true,  tray_state: 'ready',   filament_type: 'PETG', filament_name: 'PETG', color: '#C29CFF', actions: { can_read: true, can_unload: true, read_label: 'Reread' } },
            ],
          },
          {
            ams_id: '33',
            ams_type: '1',
            slot_count: 4,
            humidity_raw: 41,
            dry_status: 'Idle',
            trays: [
              { ams_id: '33', slot_id: '0', exists: true, tray_state: 'ready',   filament_type: 'PLA', filament_name: 'PLA Basic', color: '#00AE42', actions: { can_read: true, can_load: true, read_label: 'Reread' } },
              { ams_id: '33', slot_id: '1', exists: true, tray_state: 'ready',   filament_type: 'ABS', filament_name: 'ABS',       color: '#999999', actions: { can_read: true, can_load: true, read_label: 'Reread' } },
              { ams_id: '33', slot_id: '2', exists: true, tray_state: 'unknown', filament_type: '',    filament_name: '?',         color: '#BFBFBF', actions: { can_read: true, can_load: true, read_label: 'Read' } },
              { ams_id: '33', slot_id: '3', exists: false, tray_state: 'empty',  filament_type: '',    filament_name: '',          color: '#D9D9D9', actions: { can_read: true, read_label: 'Reread' } },
            ],
          },
        ],
      },
    ],
  },
};
