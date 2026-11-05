import type {
  BedTempArea,
  ChamberTempArea,
  ControlPanelDisplay,
  ControlPanelState,
  ExtruderArea,
  ExtruderAreaType,
  ExtruderId,
  ExtruderStatus,
  ExtruderTempArea,
  ExtruderTempItem,
  FanArea,
  FanLabel,
  HotendRackArea,
  LampArea,
  MotionArea,
  RackToolheadVisualState,
  SingleExtruderView,
  SingleRackExtruderView,
  SpeedArea,
  TempStatus,
} from './types';

function asString(value: unknown, fallback = '') {
  return typeof value === 'string' ? value : fallback;
}

function asBool(value: unknown) {
  return value === true;
}

function asNumber(value: unknown, fallback: number) {
  const n = Number(value);
  return Number.isFinite(n) ? n : fallback;
}

function asNullableNumber(value: unknown): number | null {
  return value === null || value === undefined ? null : asNumber(value, 0);
}

function asTempStatus(value: unknown): TempStatus {
  return value === 'heating' || value === 'cooling' ? value : 'idle';
}

function asExtruderStatus(value: unknown): ExtruderStatus {
  return value === 'active' || value === 'loading' || value === 'unloading' || value === 'switching' ? value : 'idle';
}

function asExtruderAreaType(value: unknown): ExtruderAreaType {
  return value === 'dual' || value === 'single_rack' ? value : 'single';
}

function asRackToolheadVisualState(value: unknown): RackToolheadVisualState {
  return value === 'clamped' ||
    value === 'clamped_retracting' ||
    value === 'clamped_extruding' ||
    value === 'picking' ||
    value === 'placing' ||
    value === 'clamped_no_filament' ||
    value === 'cutter' ||
    value === 'pen' ||
    value === 'default'
    ? value
    : 'empty';
}

function asFanLabel(value: unknown): FanLabel {
  if (value === 'AirCondition') return 'Air Management';
  if (value === 'Fan') return 'Fan';
  return asString(value, 'Fan');
}

function normalizeTempInput(value: unknown, minTemp = 20, maxTemp = 300) {
  const raw = (value ?? {}) as {
    enabled?: unknown;
    disabled_reason?: unknown;
    max_warning?: unknown;
    min_warning?: unknown;
  };
  return {
    enabled: asBool(raw.enabled),
    disabled_reason: asString(raw.disabled_reason),
    max_warning: asString(raw.max_warning, `The maximum temperature cannot exceed ${maxTemp}`),
    min_warning: asString(raw.min_warning, `The minmum temperature should not be less than ${minTemp}`),
  };
}

function normalizeSpeedControl(value: unknown) {
  const raw = (value ?? {}) as {
    selectable?: unknown;
    editable?: unknown;
    hint?: unknown;
    disabled_reason?: unknown;
    levels?: unknown;
  };
  const levels = Array.isArray(raw.levels)
    ? raw.levels.map((item) => {
      const row = (item ?? {}) as { level?: unknown; label?: unknown };
      const level = asNumber(row.level, 2);
      return { level: (level === 1 || level === 3 || level === 4 ? level : 2) as 1 | 2 | 3 | 4, label: asString(row.label) };
    })
    : [];
  const selectable = asBool(raw.selectable);
  return {
    selectable,
    editable: raw.editable === undefined ? selectable : asBool(raw.editable),
    hint: asString(raw.hint, 'This only takes effect during printing'),
    disabled_reason: asString(raw.disabled_reason),
    levels,
  };
}

function normalizeLampControl(value: unknown, lampOn: boolean) {
  const raw = (value ?? {}) as { switchable?: unknown; disabled_reason?: unknown; next_lamp_on?: unknown };
  return { switchable: asBool(raw.switchable), disabled_reason: asString(raw.disabled_reason), next_lamp_on: raw.next_lamp_on === undefined ? !lampOn : asBool(raw.next_lamp_on) };
}

function normalizeFanControl(value: unknown) {
  const raw = (value ?? {}) as { openable?: unknown; disabled_reason?: unknown; opens_dialog?: unknown };
  return { openable: asBool(raw.openable), disabled_reason: asString(raw.disabled_reason), opens_dialog: true as const };
}

function normalizeAxisMove(value: unknown) {
  const raw = (value ?? {}) as { movable?: unknown; disabled_reason?: unknown; step?: unknown };
  return { movable: asBool(raw.movable), disabled_reason: asString(raw.disabled_reason), step: asString(raw.step) };
}

function normalizeRackControl(value: unknown, targetPos: 'lift' | 'center' | 'lower') {
  const raw = (value ?? {}) as {
    movable?: unknown;
    disabled_reason?: unknown;
    target_pos?: unknown;
    label?: unknown;
    tooltip?: unknown;
  };
  const target = raw.target_pos === 'lift' || raw.target_pos === 'center' || raw.target_pos === 'lower' ? raw.target_pos : targetPos;
  return {
    movable: asBool(raw.movable),
    disabled_reason: asString(raw.disabled_reason),
    target_pos: target,
    label: asString(raw.label),
    tooltip: asString(raw.tooltip),
  };
}

function normalizeSwitchControl(value: unknown) {
  const raw = (value ?? {}) as { switchable?: unknown; disabled_reason?: unknown; available_extruder_ids?: unknown };
  return {
    switchable: asBool(raw.switchable),
    disabled_reason: asString(raw.disabled_reason),
    available_extruder_ids: Array.isArray(raw.available_extruder_ids)
      ? raw.available_extruder_ids.map((id) => asNumber(id, -1)).filter((id) => id >= 0)
      : [],
  };
}

function normalizeExtruderView(value: unknown) {
  const raw = (value ?? {}) as { id?: unknown; has_filament?: unknown; filament_clr?: unknown; extruder_status?: unknown; extruder_icon?: unknown };
  return {
    id: asNumber(raw.id, 0),
    has_filament: asBool(raw.has_filament),
    filament_clr: asString(raw.filament_clr),
    extruder_status: asExtruderStatus(raw.extruder_status),
    extruder_icon: asString(raw.extruder_icon),
  };
}

function normalizeExtruderTempItem(value: unknown): ExtruderTempItem {
  const raw = (value ?? {}) as Partial<ExtruderTempItem>;
  const id: ExtruderId = raw.id === 'deputy' ? 'deputy' : 'main';
  const min = asNumber(raw.min, 20);
  const max = asNumber(raw.max, 300);
  return {
    id,
    label: asString(raw.label, id),
    current: asNullableNumber(raw.current),
    target: asNullableNumber(raw.target),
    min,
    max,
    editable: asBool(raw.editable),
    temp_status: asTempStatus(raw.temp_status),
    visible: asBool(raw.visible),
    read_only_reason: asString(raw.read_only_reason),
    show_nozzle: raw.show_nozzle === undefined ? true : asBool(raw.show_nozzle),
    badge: asBool(raw.badge),
    badge_nozzle_index: asNumber(raw.badge_nozzle_index, 0),
    side_badge: asString(raw.side_badge),
    temp_input: normalizeTempInput(raw.temp_input, min, max),
  };
}

function normalizeExtruderTempArea(value: unknown): ExtruderTempArea {
  const raw = (value ?? {}) as Partial<ExtruderTempArea>;
  return {
    visible: asBool(raw.visible),
    extruder_count: asNumber(raw.extruder_count, 0),
    extruders: Array.isArray(raw.extruders) ? raw.extruders.map(normalizeExtruderTempItem) : [],
  };
}

function normalizeChamberTemp(value: unknown): ChamberTempArea {
  const raw = (value ?? {}) as Partial<ChamberTempArea>;
  const min = asNumber(raw.min, 20);
  const max = asNumber(raw.max, 60);
  return {
    visible: raw.visible === undefined ? true : asBool(raw.visible),
    current: asNullableNumber(raw.current),
    target: asNullableNumber(raw.target),
    min,
    max,
    editable: asBool(raw.editable),
    temp_status: asTempStatus(raw.temp_status),
    read_only_reason: asString(raw.read_only_reason),
    temp_input: normalizeTempInput(raw.temp_input, min, max),
  };
}

function normalizeBedTemp(value: unknown): BedTempArea {
  const raw = (value ?? {}) as Partial<BedTempArea>;
  const min = asNumber(raw.min, 20);
  const max = asNumber(raw.max, 120);
  return {
    visible: raw.visible === undefined ? true : asBool(raw.visible),
    current: asNullableNumber(raw.current),
    target: asNullableNumber(raw.target),
    min,
    max,
    editable: asBool(raw.editable),
    temp_status: asTempStatus(raw.temp_status),
    read_only_reason: asString(raw.read_only_reason),
    temp_input: normalizeTempInput(raw.temp_input, min, max),
  };
}

function normalizeSpeed(value: unknown): SpeedArea {
  const raw = (value ?? {}) as Partial<SpeedArea>;
  const speed = asNumber(raw.speed_level, 2);
  return {
    visible: asBool(raw.visible),
    speed_text: asString(raw.speed_text, '100%'),
    speed_level: speed === 1 || speed === 3 || speed === 4 ? speed : 2,
    speed_control: normalizeSpeedControl(raw.speed_control),
  };
}

function normalizeLamp(value: unknown): LampArea {
  const raw = (value ?? {}) as Partial<LampArea>;
  const lampOn = asBool(raw.lamp_on);
  return { visible: asBool(raw.visible), lamp_on: lampOn, label: asString(raw.label, 'Lamp'), lamp_control: normalizeLampControl(raw.lamp_control, lampOn) };
}

function normalizeFan(value: unknown): FanArea {
  const raw = (value ?? {}) as Partial<FanArea>;
  return { fan_visible: asBool(raw.fan_visible), fan_label: asFanLabel(raw.fan_label), fan_control: normalizeFanControl(raw.fan_control) };
}

function normalizeMotion(value: unknown): MotionArea {
  const raw = (value ?? {}) as Partial<MotionArea>;
  return {
    z_label: asString(raw.z_label, 'Bed'),
    xy_visible: raw.xy_visible === undefined ? true : asBool(raw.xy_visible),
    z_visible: raw.z_visible === undefined ? true : asBool(raw.z_visible),
    e_visible: raw.e_visible === undefined ? true : asBool(raw.e_visible),
    axis_moves: Object.fromEntries(Object.entries(raw.axis_moves ?? {}).map(([key, value]) => [key, normalizeAxisMove(value)])),
  };
}

function normalizeSingleExtruder(value: unknown): SingleExtruderView {
  const raw = (value ?? {}) as Partial<SingleExtruderView>;
  const hasFilament = asBool(raw.has_filament);
  return {
    has_filament: hasFilament,
    filament_clr: asString(raw.filament_clr),
    extruder_status: asExtruderStatus(raw.extruder_status),
    extruder_icon: asString(raw.extruder_icon, hasFilament ? 'monitor_extruder_filled_load' : 'monitor_extruder_empty_load'),
  };
}

function normalizeDualExtruder(value: unknown) {
  const raw = (value ?? {}) as Partial<ExtruderArea['dual_extruder']>;
  return {
    current_extruder_id: asNumber(raw.current_extruder_id, 0),
    labels: {
      left: asString(raw.labels?.left, 'Left'),
      right: asString(raw.labels?.right, 'Right'),
    },
    extruders: Array.isArray(raw.extruders) ? raw.extruders.map(normalizeExtruderView) : [],
    switching_status_text: asString(raw.switching_status_text),
    switching_failed: asBool(raw.switching_failed),
    can_quit_switching: asBool(raw.can_quit_switching),
    can_retry_switching: asBool(raw.can_retry_switching),
    quit_label: asString(raw.quit_label, 'Quit'),
    retry_label: asString(raw.retry_label, 'Retry'),
    switch_control: normalizeSwitchControl(raw.switch_control),
  };
}

function normalizeSingleRackExtruder(value: unknown): SingleRackExtruderView {
  const raw = (value ?? {}) as Partial<SingleRackExtruderView>;
  const filamentClr = asString(raw.filament_clr);
  const currentNozzleId = asNullableNumber(raw.current_nozzle_id);
  const nozzleFlowType = asString(raw.nozzle_flow_type);
  const visualState = asRackToolheadVisualState(raw.rack_toolhead_visual_state);
  const defaultShowPath = currentNozzleId !== null && nozzleFlowType !== 'Unclamped' && (
    visualState === 'clamped' ||
    visualState === 'clamped_retracting' ||
    visualState === 'clamped_extruding' ||
    visualState === 'clamped_no_filament'
  );
  return {
    current_nozzle_id: currentNozzleId,
    has_filament: asBool(raw.has_filament),
    filament_clr: filamentClr,
    upper_filament_clr: asString(raw.upper_filament_clr, filamentClr),
    lower_filament_clr: asString(raw.lower_filament_clr, filamentClr),
    show_filament_path: raw.show_filament_path === undefined ? defaultShowPath : asBool(raw.show_filament_path),
    nozzle_diameter: asString(raw.nozzle_diameter),
    nozzle_flow_type: nozzleFlowType,
    extruder_status: asExtruderStatus(raw.extruder_status),
    rack_toolhead_visual_state: visualState,
  };
}

function normalizeExtruder(value: unknown): ExtruderArea {
  const raw = (value ?? {}) as Partial<ExtruderArea>;
  return {
    type: asExtruderAreaType(raw.type),
    label: asString(raw.label, 'Extruder'),
    single_extruder: normalizeSingleExtruder(raw.single_extruder),
    dual_extruder: normalizeDualExtruder(raw.dual_extruder),
    single_rack_extruder: normalizeSingleRackExtruder(raw.single_rack_extruder),
  };
}

function normalizeRack(value: unknown): HotendRackArea {
  const raw = (value ?? {}) as Partial<HotendRackArea>;
  return {
    visible: asBool(raw.visible),
    title: asString(raw.title, 'Hotend Rack'),
    lift: normalizeRackControl(raw.lift, 'lift'),
    center: normalizeRackControl(raw.center, 'center'),
    lower: normalizeRackControl(raw.lower, 'lower'),
  };
}

function normalizeDisplay(value: unknown): ControlPanelDisplay {
  const raw = (value ?? {}) as Partial<ControlPanelDisplay>;
  return {
    visible: raw.visible === undefined ? true : asBool(raw.visible),
    extruder_temp: normalizeExtruderTempArea(raw.extruder_temp),
    chamber_temp: normalizeChamberTemp(raw.chamber_temp),
    bed_temp: normalizeBedTemp(raw.bed_temp),
    speed: normalizeSpeed(raw.speed),
    lamp: normalizeLamp(raw.lamp),
    fan: normalizeFan(raw.fan),
    motion: normalizeMotion(raw.motion),
    extruder: normalizeExtruder(raw.extruder),
    rack: normalizeRack(raw.rack),
  };
}

export const emptyControlPanelState: ControlPanelState = {
  display: normalizeDisplay({}),
};

export function normalizeState(value: Partial<ControlPanelState> | undefined): ControlPanelState {
  if (!value) return emptyControlPanelState;
  return {
    display: normalizeDisplay(value.display),
  };
}
