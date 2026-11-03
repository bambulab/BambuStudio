import type {
  HotendRackActionControl,
  HotendRackDisplay,
  HotendRackActions,
  HotendRackArea,
  HotendRackPosition,
  HotendRackReading,
  HotendRackRow,
  HotendRackRowId,
  HotendRackSlot,
  HotendRackSlotStatus,
  HotendRackState,
  HotendRackToolhead,
  HotendRackToolheadStatus,
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

function asSlotStatus(value: unknown): HotendRackSlotStatus {
  return value === 'normal' || value === 'unknown' || value === 'error' ? value : 'empty';
}

function asToolheadStatus(value: unknown): HotendRackToolheadStatus {
  return asSlotStatus(value);
}

function asRowId(value: unknown): HotendRackRowId {
  return value === 'B' ? 'B' : 'A';
}

function asPosition(value: unknown): HotendRackPosition {
  return value === 'a_top' || value === 'b_top' || value === 'center' ? value : 'unknown';
}

function normalizeControl(value: unknown): HotendRackActionControl {
  const raw = (value ?? {}) as Partial<HotendRackActionControl>;
  return { enabled: asBool(raw.enabled), disabled_reason: asString(raw.disabled_reason) };
}

function normalizeSlot(value: unknown): HotendRackSlot {
  const raw = (value ?? {}) as Partial<HotendRackSlot>;
  const id = asNumber(raw.id, 0);
  return {
    id,
    display_id: asString(raw.display_id, String(id + 1)),
    status: asSlotStatus(raw.status),
    diameter: asString(raw.diameter),
    flow_type: asString(raw.flow_type),
    filament_color: asString(raw.filament_color),
    has_error_detail: asBool(raw.has_error_detail),
  };
}

function normalizeRow(value: unknown): HotendRackRow {
  const raw = (value ?? {}) as Partial<HotendRackRow>;
  return {
    id: asRowId(raw.id),
    raised: asBool(raw.raised),
    slots: Array.isArray(raw.slots) ? raw.slots.map(normalizeSlot) : [],
    move_control: normalizeControl(raw.move_control),
  };
}

function normalizeDisplay(value: unknown): HotendRackDisplay {
  const raw = (value ?? {}) as Partial<HotendRackDisplay>;
  const toolhead = (raw.toolhead ?? {}) as Partial<HotendRackToolhead>;
  const rack = (raw.rack ?? {}) as Partial<HotendRackArea>;
  const reading = (raw.reading ?? {}) as Partial<HotendRackReading>;
  const actions = (raw.actions ?? {}) as Partial<HotendRackActions>;
  return {
    visible: asBool(raw.visible),
    title: asString(raw.title, 'Induction Hotend Rack'),
    toolhead: {
      status: asToolheadStatus(toolhead.status),
      diameter: asString(toolhead.diameter),
      flow_type: asString(toolhead.flow_type),
      filament_color: asString(toolhead.filament_color),
    },
    rack: {
      position: asPosition(rack.position),
      busy: asBool(rack.busy),
      busy_text: asString(rack.busy_text),
      rows: Array.isArray(rack.rows) ? rack.rows.map(normalizeRow) : [],
    },
    reading: {
      active: asBool(reading.active),
      index: asNumber(reading.index, 0),
      count: asNumber(reading.count, 0),
      text: asString(reading.text),
    },
    actions: {
      home: normalizeControl(actions.home),
      read_all: normalizeControl(actions.read_all),
      hotends_info: normalizeControl(actions.hotends_info),
    },
  };
}

export const emptyHotendRackState: HotendRackState = {
  display: normalizeDisplay({}),
};

export function normalizeState(value: Partial<HotendRackState> | undefined): HotendRackState {
  if (!value) return emptyHotendRackState;
  return { display: normalizeDisplay(value.display) };
}
