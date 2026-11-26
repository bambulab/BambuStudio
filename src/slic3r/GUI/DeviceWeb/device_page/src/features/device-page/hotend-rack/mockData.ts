import type { HotendRackState } from './types';

const enabled = { enabled: true, disabled_reason: '' };
const disabled = { enabled: false, disabled_reason: '' };

const slot = (id: number, status: 'empty' | 'normal' | 'unknown' | 'error' = 'normal') => ({
  id,
  display_id: String(id + 1),
  status,
  diameter: status === 'normal' ? '0.4mm' : status === 'empty' ? 'Empty' : status === 'error' ? 'Error' : 'Unknown',
  flow_type: status === 'normal' ? 'Standard' : '',
  filament_color: '#d8d744',
  has_error_detail: status === 'error',
});

export const mockHotendRackState: HotendRackState = {
  display: {
    visible: true,
    title: 'Induction Hotend Rack',
    toolhead: {
      status: 'normal',
      diameter: '0.4mm',
      flow_type: 'Standard',
      filament_color: '#d8d744',
    },
    rack: {
      position: 'a_top',
      busy: false,
      rows: [
        { id: 'A', raised: true, slots: [slot(0), slot(2), slot(4)], move_control: disabled },
        { id: 'B', raised: false, slots: [slot(1), slot(3), slot(5)], move_control: enabled },
      ],
    },
    reading: {
      active: false,
      index: 0,
      count: 0,
    },
    actions: {
      home: enabled,
      read_all: enabled,
      hotends_info: enabled,
    },
  },
};
