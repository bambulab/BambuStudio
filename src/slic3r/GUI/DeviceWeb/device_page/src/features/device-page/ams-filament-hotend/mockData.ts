import type {
  AmsFilamentHotendCardState,
  AmsFilamentHotendLineState,
  AmsFilamentHotendNozzleState,
  AmsFilamentHotendSlotViewModel,
  AmsFilamentHotendViewModel,
  HotendSlotInfo,
} from './types';

interface SlotSeed {
  display_index: number;
  card_state: AmsFilamentHotendCardState;
  filament_name?: string;
  status_text?: string;
  color?: string;
  color_type?: AmsFilamentHotendSlotViewModel['color_type'];
  selected?: boolean;
  in_dryer?: boolean;
  line_state?: AmsFilamentHotendLineState;
  nozzle_state?: AmsFilamentHotendNozzleState;
  bind_ams_slot_count?: number;
  hotend?: HotendSlotInfo;
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
    case 'Unknown':
      return 'Unknown';
    case 'Error':
      return 'Error';
    default:
      return 'Normal';
  }
};

const buildSlot = (seed: SlotSeed): AmsFilamentHotendSlotViewModel => ({
  nozzle_pos: seed.display_index - 1,
  display_index: seed.display_index,
  ams_id: '0',
  slot_id: String(seed.display_index - 1),
  bind_ams: seed.bind_ams_slot_count ? [{ ams_id: '0', ams_type: 'ZMULTI_A', slot_count: seed.bind_ams_slot_count }] : [],
  bind_ams_slot_count: seed.bind_ams_slot_count ?? 0,
  card_state: seed.card_state,
  line_state: seed.line_state ?? lineFor(seed.card_state),
  nozzle_state: seed.nozzle_state ?? nozzleFor(seed.card_state),
  filament_name: seed.filament_name ?? '',
  filament_type: seed.filament_name?.split(' ')[0] ?? '',
  color: seed.color ?? '#00AE42',
  color_type: seed.color_type ?? 'single',
  status_text: seed.status_text ?? '',
  enabled: true,
  is_top_tier: seed.display_index % 2 === 0,
  selected: seed.selected ?? false,
  in_dryer: seed.in_dryer ?? false,
  hotend: seed.hotend,
});

const mockHotend = (display_index: number): HotendSlotInfo => {
  switch (display_index) {
    case 1: return { current_temp: 220, target_temp: 0,   op_state: 'Idle',     tool_kind: 'nozzle',  nozzle_label: '0.4 mm',     nozzle_subtext: 'Std',  selected: true };
    case 2: return { current_temp: 64,  target_temp: 0,   op_state: 'Heating',  tool_kind: 'nozzle',  nozzle_label: '0.4 mm',     nozzle_subtext: 'Std' };
    case 3: return { current_temp: 23,  target_temp: 0,   op_state: 'Idle',     tool_kind: 'nozzle',  nozzle_label: '0.4 mm',     nozzle_subtext: 'Std' };
    case 4: return {                                                        op_state: 'Idle',     tool_kind: 'cutter',  nozzle_label: '刀名?' };
    case 5: return { current_temp: 23,  target_temp: 0,   op_state: 'Error',    tool_kind: 'pen',     nozzle_label: '荧光笔' };
    case 6: return {                                                        op_state: 'Idle',     tool_kind: 'empty',   nozzle_label: '--' };
    case 7: return { current_temp: 233, target_temp: 0,   op_state: 'Idle',     tool_kind: 'nozzle',  nozzle_label: '0.4 mm',     nozzle_subtext: 'Std' };
    case 8: return {                                                        op_state: 'Placing',  tool_kind: 'nozzle',  nozzle_label: '0.4 mm',     nozzle_subtext: 'Std' };
    case 9: return {                                                        op_state: 'Reading',  tool_kind: 'nozzle' };
    default: return { op_state: 'Idle', tool_kind: 'empty' };
  }
};

export const mockAmsFilamentHotendState: AmsFilamentHotendViewModel = {
  selected_dev_id: 'mock-device',
  actions: {
    can_load: true,
    can_unload: true,
    can_dry: true,
    show_ams_settings: true,
    show_auto_refill: true,
    tips: '',
  },
  hotend_slots: [
    buildSlot({ display_index: 1, card_state: 'Loading',     filament_name: 'PLA Basic', status_text: '进料中', color: '#00AE42', selected: true, hotend: mockHotend(1) }),
    buildSlot({ display_index: 2, card_state: 'Reading',                                  status_text: '读取中', hotend: mockHotend(2) }),
    buildSlot({ display_index: 3, card_state: 'ReadyToLoad', filament_name: 'PLA Basic', status_text: '待进料', color: '#00AE42', hotend: mockHotend(3) }),
    buildSlot({ display_index: 4, card_state: 'Empty',                                    status_text: '--', hotend: mockHotend(4) }),
    buildSlot({ display_index: 5, card_state: 'ReadyToUse',  filament_name: 'PLA Basic', status_text: '待使用', color: '#00AE42', bind_ams_slot_count: 4, hotend: mockHotend(5) }),
    buildSlot({ display_index: 6, card_state: 'ReadyToUse',  filament_name: 'PLA Basic', status_text: '待使用', color: '#00AE42', in_dryer: true, hotend: mockHotend(6) }),
    buildSlot({ display_index: 7, card_state: 'ReadyToUse',  filament_name: 'PLA Basic', status_text: '待使用', color: '#00AE42', hotend: mockHotend(7) }),
    buildSlot({ display_index: 8, card_state: 'ReadyToUse',  filament_name: 'PLA Basic', status_text: '待使用', color: '#00AE42', in_dryer: true, hotend: mockHotend(8) }),
    buildSlot({ display_index: 9, card_state: 'ReadyToUse',  filament_name: 'PLA Basic', status_text: '待使用', color: '#00AE42', hotend: mockHotend(9) }),
  ],
};
