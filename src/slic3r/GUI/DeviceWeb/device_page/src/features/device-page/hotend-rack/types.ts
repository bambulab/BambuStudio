export type HotendRackSlotStatus = 'empty' | 'normal' | 'unknown' | 'error';
export type HotendRackToolheadStatus = HotendRackSlotStatus;
export type HotendRackRowId = 'A' | 'B';
export type HotendRackPosition = 'a_top' | 'b_top' | 'center' | 'unknown';

export interface HotendRackActionControl {
  enabled: boolean;
  disabled_reason: string;
}

export interface HotendRackSlot {
  id: number;
  display_id: string;
  status: HotendRackSlotStatus;
  diameter: string;
  flow_type: string;
  filament_color: string;
  has_error_detail: boolean;
}

export interface HotendRackRow {
  id: HotendRackRowId;
  raised: boolean;
  slots: HotendRackSlot[];
  move_control: HotendRackActionControl;
}

export interface HotendRackToolhead {
  status: HotendRackToolheadStatus;
  diameter: string;
  flow_type: string;
  filament_color: string;
}

export interface HotendRackReading {
  active: boolean;
  index: number;
  count: number;
  text: string;
}

export interface HotendRackArea {
  position: HotendRackPosition;
  busy: boolean;
  busy_text: string;
  rows: HotendRackRow[];
}

export interface HotendRackActions {
  home: HotendRackActionControl;
  read_all: HotendRackActionControl;
  hotends_info: HotendRackActionControl;
}

export interface HotendRackDisplay {
  visible: boolean;
  title: string;
  toolhead: HotendRackToolhead;
  rack: HotendRackArea;
  reading: HotendRackReading;
  actions: HotendRackActions;
}

export interface HotendRackState {
  display: HotendRackDisplay;
}

export interface BridgeResponseBody<T> {
  module: string;
  submod: string;
  action: string;
  error_code: number;
  message?: string;
  payload?: T;
}
