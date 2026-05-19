import { useCallback, useEffect, useRef, useState } from 'react';
import { useDeviceBridge } from '../../../hooks/Bridge';
import { mockAmsFilamentHotendState } from './mockData';
import type {
  AmsFilamentHotendDryerZone,
  AmsFilamentHotendSlotActionPayload,
  AmsFilamentHotendSlotViewModel,
  AmsFilamentHotendViewModel,
  BridgeResponseBody,
  FilamentChangeStatus,
  HotendRackStatus,
} from './types';

const MODULE = 'device_page_ams_filament_hotend';

function makeBody(submod: string, action: string, payload?: Record<string, unknown>) {
  return { module: MODULE, submod, action, payload: payload ?? {} };
}

function isAmsFilamentHotendBody(body: unknown): body is BridgeResponseBody<AmsFilamentHotendViewModel> {
  return !!body
    && typeof body === 'object'
    && (body as { module?: unknown }).module === MODULE;
}

function normalizeDryStatus(status?: string) {
  return status || 'Idle';
}

function isDryingStatus(status?: string) {
  const normalized = normalizeDryStatus(status);
  return !!normalized && normalized !== 'Idle' && normalized !== 'Off';
}

function zoneContainsSlot(zone: AmsFilamentHotendDryerZone, slot: AmsFilamentHotendSlotViewModel) {
  const values = zone.zone_slots ?? [];
  return values.some((value) => {
    const n = Number(value);
    return n === slot.display_index
      || n === slot.display_index - 1
      || n === Number(slot.slot_id);
  });
}

function firstZoneWithDryerSignal(zones: AmsFilamentHotendDryerZone[]) {
  return zones.find((zone) => typeof zone.humidity_raw === 'number' && zone.humidity_raw >= 0)
    ?? zones.find((zone) => !!zone.dry_status)
    ?? zones[0];
}

function applyDryerZoneSignals(state: AmsFilamentHotendViewModel): AmsFilamentHotendSlotViewModel[] {
  const zones = state.ams?.zones ?? [];
  if (!zones.length) return state.hotend_slots;

  const dryerSlots = state.hotend_slots.filter((slot) => slot.in_dryer);
  return state.hotend_slots.map((slot) => {
    if (!slot.in_dryer) return slot;

    const zone = zones.find((candidate) => zoneContainsSlot(candidate, slot))
      ?? (dryerSlots.length === 1 ? firstZoneWithDryerSignal(zones) : zones[dryerSlots.findIndex((s) => s.display_index === slot.display_index)])
      ?? firstZoneWithDryerSignal(zones);
    if (!zone) return slot;

    const humidity = typeof zone.humidity_raw === 'number' && zone.humidity_raw >= 0
      ? zone.humidity_raw
      : slot.dryer_humidity_pct;
    const status = normalizeDryStatus(zone.dry_status || slot.dryer_status_text);

    return {
      ...slot,
      dryer_humidity_pct: humidity,
      dryer_status_text: status,
      is_drying: isDryingStatus(status) || (typeof zone.dry_time === 'number' && zone.dry_time > 0),
    };
  });
}

function normalizeState(payload: AmsFilamentHotendViewModel): AmsFilamentHotendViewModel {
  const defaultRackStatus: HotendRackStatus = mockAmsFilamentHotendState.hotend_rack_status ?? {
    active: false,
    mode: 'idle',
    display_index: 0,
    current: 0,
    total: 0,
    message: '',
    tips: '',
  };
  const defaultFilamentChangeStatus: FilamentChangeStatus = {
    active: false,
    mode: 'idle',
  };
  const next = {
    ...mockAmsFilamentHotendState,
    ...payload,
    actions: {
      ...mockAmsFilamentHotendState.actions,
      ...(payload.actions ?? {}),
    },
    hotend_rack_status: {
      ...defaultRackStatus,
      ...(payload.hotend_rack_status ?? {}),
    },
    filament_change_status: {
      ...defaultFilamentChangeStatus,
      ...(payload.filament_change_status ?? {}),
    },
    hotend_slots: Array.isArray(payload.hotend_slots) ? payload.hotend_slots : mockAmsFilamentHotendState.hotend_slots,
  };
  return {
    ...next,
    hotend_slots: applyDryerZoneSignals(next),
  };
}

export function useAmsFilamentHotendBridge() {
  const request = useDeviceBridge();
  const [state, setState] = useState<AmsFilamentHotendViewModel>(mockAmsFilamentHotendState);
  const [error, setError] = useState('');
  const stateKeyRef = useRef(JSON.stringify(mockAmsFilamentHotendState));

  const applyState = useCallback((payload: AmsFilamentHotendViewModel, force = false) => {
    const next = normalizeState(payload);
    const stateKey = JSON.stringify(next);
    if (!force && stateKey === stateKeyRef.current) return;
    stateKeyRef.current = stateKey;
    setState(next);
  }, []);

  const refresh = useCallback(async () => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<AmsFilamentHotendViewModel>>(
      makeBody('state', 'get'),
      5000,
    );
    if (!resp.ok) {
      setError(resp.error);
      return;
    }
    if (resp.value.error_code !== 0) {
      setError(resp.value.message ?? 'AMS filament hotend state failed');
      return;
    }
    if (resp.value.payload) {
      applyState(resp.value.payload, true);
      setError('');
    }
  }, [applyState, request]);

  const runSlotAction = useCallback(async (
    action: string,
    slot?: AmsFilamentHotendSlotViewModel,
    extraPayload: AmsFilamentHotendSlotActionPayload = {},
  ) => {
    const payload = slot
      ? {
        nozzle_pos: slot.nozzle_pos,
        display_index: slot.display_index,
        ams_id: slot.ams_id,
        slot_id: slot.slot_id,
        bind_ams_slot_count: slot.bind_ams_slot_count ?? 0,
        ...extraPayload,
      }
      : extraPayload;
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<AmsFilamentHotendViewModel>>(
      makeBody('action', action, payload),
      8000,
    );
    if (resp.ok && resp.value.payload) {
      applyState(resp.value.payload);
    }
  }, [applyState, request]);

  const openBridgeDebugDialog = useCallback(async (webState: AmsFilamentHotendViewModel) => {
    const rawWebTrace = (window as Window & { __deviceWebTrace?: unknown[] }).__deviceWebTrace ?? [];
    let webTrace: unknown[] = [];
    try {
      webTrace = JSON.parse(JSON.stringify(rawWebTrace)) as unknown[];
    } catch (e) {
      webTrace = [{ unserializable: true, error: String(e) }];
    }
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<unknown>>(
      makeBody('debug', 'open_bridge_trace', {
        web_state: webState,
        web_trace: webTrace,
      }),
      8000,
    );
    if (!resp.ok) {
      setError(`Debug request failed: ${resp.error}`);
      return false;
    }
    if (resp.value.error_code !== 0) {
      setError(`Debug request failed: ${resp.value.message ?? 'unknown error'}`);
      return false;
    }
    setError('');
    return true;
  }, [request]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  useEffect(() => {
    const handler = (e: Event) => {
      const detail = (e as CustomEvent).detail;
      const body = detail?.body;
      if (!isAmsFilamentHotendBody(body) || detail.head?.type !== 'report') return;
      if (body.payload) {
        applyState(body.payload);
        setError('');
      }
    };

    document.addEventListener('cpp:device', handler);
    return () => document.removeEventListener('cpp:device', handler);
  }, [applyState]);

  return { state, error, refresh, runSlotAction, openBridgeDebugDialog };
}
