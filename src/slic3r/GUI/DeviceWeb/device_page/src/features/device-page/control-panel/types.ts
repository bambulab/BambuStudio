export type TempStatus = 'idle' | 'heating' | 'cooling';
export type ExtruderId = 'main' | 'deputy';
export type FanLabel = string;
export type ExtruderStatus = 'idle' | 'active' | 'loading' | 'unloading' | 'switching';
export type ExtruderAreaType = 'single' | 'dual' | 'single_rack';
export type RackToolheadVisualState =
  | 'empty'
  | 'clamped'
  | 'clamped_retracting'
  | 'clamped_extruding'
  | 'picking'
  | 'placing'
  | 'clamped_no_filament'
  | 'cutter'
  | 'pen'
  | 'default';

export interface TempInputControl {
  enabled: boolean;
  disabled_reason: string;
  max_warning: string;
  min_warning: string;
}

export interface SpeedControl {
  selectable: boolean;
  editable: boolean;
  hint: string;
  disabled_reason: string;
  levels: Array<{ level: 1 | 2 | 3 | 4; label: string }>;
}

export interface LampControl {
  switchable: boolean;
  disabled_reason: string;
  next_lamp_on: boolean;
}

export interface FanControl {
  openable: boolean;
  disabled_reason: string;
  opens_dialog: true;
}

export interface AxisMoveControl {
  movable: boolean;
  disabled_reason: string;
  step: string;
}

export interface RackControl {
  movable: boolean;
  disabled_reason: string;
  target_pos: 'lift' | 'center' | 'lower';
  label: string;
  tooltip: string;
}

export interface ExtruderSwitchControl {
  switchable: boolean;
  disabled_reason: string;
  available_extruder_ids: number[];
}

export interface ExtruderTempItem {
  id: ExtruderId;
  label: string;
  current: number | null;
  target: number | null;
  min: number;
  max: number;
  editable: boolean;
  temp_status: TempStatus;
  visible: boolean;
  read_only_reason: string;
  show_nozzle: boolean;
  badge: boolean;
  badge_nozzle_index: number;
  side_badge: string;
  temp_input: TempInputControl;
}

export interface ExtruderTempArea {
  visible: boolean;
  extruder_count: number;
  extruders: ExtruderTempItem[];
}

export interface ChamberTempArea {
  visible: boolean;
  current: number | null;
  target: number | null;
  min: number;
  max: number;
  editable: boolean;
  temp_status: TempStatus;
  read_only_reason: string;
  temp_input: TempInputControl;
}

export interface BedTempArea {
  visible: boolean;
  current: number | null;
  target: number | null;
  min: number;
  max: number;
  editable: boolean;
  temp_status: TempStatus;
  read_only_reason: string;
  temp_input: TempInputControl;
}

export interface SpeedArea {
  visible: boolean;
  speed_text: string;
  speed_level: 1 | 2 | 3 | 4;
  speed_control: SpeedControl;
}

export interface LampArea {
  visible: boolean;
  lamp_on: boolean;
  label: string;
  lamp_control: LampControl;
}

export interface FanArea {
  fan_visible: boolean;
  fan_label: FanLabel;
  fan_control: FanControl;
}

export interface MotionArea {
  z_label: string;
  xy_visible: boolean;
  z_visible: boolean;
  e_visible: boolean;
  axis_moves: Record<string, AxisMoveControl>;
}

export interface ExtruderView {
  id: number;
  has_filament: boolean;
  filament_clr: string;
  extruder_status: ExtruderStatus;
  extruder_icon: string;
}

export interface SingleExtruderView {
  has_filament: boolean;
  filament_clr: string;
  extruder_status: ExtruderStatus;
  extruder_icon: string;
}

export interface DualExtruderView {
  current_extruder_id: number;
  labels: { left: string; right: string };
  extruders: ExtruderView[];
  switching_status_text: string;
  switching_failed: boolean;
  can_quit_switching: boolean;
  can_retry_switching: boolean;
  quit_label: string;
  retry_label: string;
  switch_control: ExtruderSwitchControl;
}

export interface SingleRackExtruderView {
  current_nozzle_id: number | null;
  has_filament: boolean;
  filament_clr: string;
  upper_filament_clr: string;
  lower_filament_clr: string;
  show_filament_path: boolean;
  nozzle_diameter: string;
  nozzle_flow_type: string;
  extruder_status: ExtruderStatus;
  rack_toolhead_visual_state: RackToolheadVisualState;
  rack_toolhead_image: string;
}

export interface ExtruderArea {
  type: ExtruderAreaType;
  label: string;
  single_extruder: SingleExtruderView;
  dual_extruder: DualExtruderView;
  single_rack_extruder: SingleRackExtruderView;
}

export interface HotendRackArea {
  visible: boolean;
  title: string;
  lift: RackControl;
  center: RackControl;
  lower: RackControl;
}

export interface ControlPanelDisplay {
  visible: boolean;
  extruder_temp: ExtruderTempArea;
  chamber_temp: ChamberTempArea;
  bed_temp: BedTempArea;
  speed: SpeedArea;
  lamp: LampArea;
  fan: FanArea;
  motion: MotionArea;
  extruder: ExtruderArea;
  rack: HotendRackArea;
}

export interface ControlPanelState {
  display: ControlPanelDisplay;
}

export interface BridgeResponseBody<T> {
  module: string;
  submod: string;
  action: string;
  error_code: number;
  message?: string;
  payload?: T;
}
