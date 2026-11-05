import type { ControlPanelState } from './types';

const tempOk = {
  enabled: true,
  disabled_reason: '',
  max_warning: 'The maximum temperature cannot exceed 300',
  min_warning: 'The minmum temperature should not be less than 20',
};
const speedOk = {
  selectable: true,
  editable: true,
  hint: 'This only takes effect during printing',
  disabled_reason: '',
  levels: [
    { level: 1 as const, label: 'Silent' },
    { level: 2 as const, label: 'Standard' },
    { level: 3 as const, label: 'Sport' },
    { level: 4 as const, label: 'Ludicrous' },
  ],
};
const lampOk = { switchable: true, disabled_reason: '', next_lamp_on: false };
const fanOk = { openable: true, disabled_reason: '', opens_dialog: true as const };
const move = (step: string) => ({ movable: true, disabled_reason: '', step });
const rack = (target_pos: 'lift' | 'center' | 'lower', tooltip: string) => ({
  movable: true,
  disabled_reason: '',
  target_pos,
  label: '',
  tooltip,
});

export const mockControlPanelState: ControlPanelState = {
  display: {
    visible: true,
    extruder_temp: {
      visible: true,
      extruder_count: 1,
      extruders: [
        {
          id: 'main',
          label: 'Nozzle',
          current: 220,
          target: 220,
          min: 20,
          max: 300,
          editable: true,
          temp_status: 'idle',
          visible: true,
          read_only_reason: '',
          show_nozzle: true,
          badge: true,
          badge_nozzle_index: 7,
          side_badge: '',
          temp_input: tempOk,
        },
      ],
    },
    chamber_temp: {
      visible: true,
      current: 36,
      target: null,
      min: 20,
      max: 60,
      editable: false,
      temp_status: 'idle',
      read_only_reason: '',
      temp_input: {
        enabled: false,
        disabled_reason: 'Chamber temperature is not editable.',
        max_warning: 'The maximum temperature cannot exceed 60',
        min_warning: 'The minmum temperature should not be less than 20',
      },
    },
    bed_temp: {
      visible: true,
      current: 55,
      target: 55,
      min: 20,
      max: 120,
      editable: true,
      temp_status: 'idle',
      read_only_reason: '',
      temp_input: {
        ...tempOk,
        max_warning: 'The maximum temperature cannot exceed 120',
      },
    },
    speed: { visible: true, speed_text: '100%', speed_level: 2, speed_control: speedOk },
    lamp: { visible: true, lamp_on: true, label: 'LED灯', lamp_control: lampOk },
    fan: { fan_visible: true, fan_label: '风扇', fan_control: fanOk },
    motion: {
      z_label: '热床',
      xy_visible: true,
      z_visible: true,
      e_visible: true,
      axis_moves: {
        xy_home: move('home'),
        x_pos_10: move('10'),
        x_neg_10: move('10'),
        x_pos_1: move('1'),
        x_neg_1: move('1'),
        y_pos_10: move('10'),
        y_neg_10: move('10'),
        y_pos_1: move('1'),
        y_neg_1: move('1'),
        z_up_10: move('10'),
        z_up_1: move('1'),
        z_down_1: move('1'),
        z_down_10: move('10'),
        e_up_10: move('10'),
        e_down_10: move('10'),
      },
    },
    extruder: {
      type: 'single_rack',
      label: 'Extruder',
      single_extruder: {
        has_filament: true,
        filament_clr: '#8B939A',
        extruder_status: 'idle',
        extruder_icon: 'monitor_extruder_filled_load',
      },
      dual_extruder: {
        current_extruder_id: 0,
        labels: { left: 'Left', right: 'Right' },
        extruders: [
          { id: 0, has_filament: true, filament_clr: '#8B939A', extruder_status: 'loading', extruder_icon: 'right_extruder_active_filled' },
          { id: 1, has_filament: false, filament_clr: '', extruder_status: 'idle', extruder_icon: 'left_extruder_unactive_empty' },
        ],
        switching_status_text: '',
        switching_failed: false,
        can_quit_switching: false,
        can_retry_switching: false,
        quit_label: 'Quit',
        retry_label: 'Retry',
        switch_control: { switchable: false, disabled_reason: '', available_extruder_ids: [0, 1] },
      },
      single_rack_extruder: {
        current_nozzle_id: 7,
        has_filament: true,
        filament_clr: '#8B939A',
        upper_filament_clr: '#8B939A',
        lower_filament_clr: '#8B939A',
        show_filament_path: true,
        nozzle_diameter: '0.4 mm',
        nozzle_flow_type: 'Standard',
        extruder_status: 'loading',
        rack_toolhead_visual_state: 'picking',
      },
    },
    rack: {
      visible: true,
      title: 'Hotend Rack',
      lift: rack('lift', 'Lift Hotend Rack'),
      center: rack('center', 'Recenter Hotend Rack'),
      lower: rack('lower', 'Lower Hotend Rack'),
    },
  },
};
