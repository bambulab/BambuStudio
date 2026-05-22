import { useCallback, useEffect, useRef, useState } from 'react';
import { useDeviceBridge } from '../../../hooks/Bridge';
import { mockAmsFilamentHotendState } from './mockData';
import type {
  AmsFilamentHotendSlotActionPayload,
  AmsFilamentHotendSlotViewModel,
  AmsFilamentHotendViewModel,
  BridgeResponseBody,
  FilamentChangeStatus,
  HotendRackStatus,
  HotendSlotViewModel,
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

function normalizeState(payload: AmsFilamentHotendViewModel): AmsFilamentHotendViewModel {
  const defaultRackStatus: HotendRackStatus = mockAmsFilamentHotendState.hotend_page.hotend_rack_status ?? {
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

  const incomingFp = payload.filament_page ?? ({} as AmsFilamentHotendViewModel['filament_page']);
  const incomingHp = payload.hotend_page ?? ({} as AmsFilamentHotendViewModel['hotend_page']);

  const filamentSlots = Array.isArray(incomingFp.hotend_slots)
    ? incomingFp.hotend_slots
    : mockAmsFilamentHotendState.filament_page.hotend_slots;
  const amsDryZones = Array.isArray(incomingFp.ams_dry_zones)
    ? incomingFp.ams_dry_zones
    : mockAmsFilamentHotendState.filament_page.ams_dry_zones ?? [];

  return {
    filament_page: {
      ...mockAmsFilamentHotendState.filament_page,
      ...incomingFp,
      actions: {
        ...mockAmsFilamentHotendState.filament_page.actions,
        ...(incomingFp.actions ?? {}),
      },
      filament_change_status: {
        ...defaultFilamentChangeStatus,
        ...(incomingFp.filament_change_status ?? {}),
      },
      ams_dry_zones: amsDryZones,
      hotend_slots: filamentSlots,
    },
    hotend_page: {
      ...mockAmsFilamentHotendState.hotend_page,
      ...incomingHp,
      actions: {
        ...mockAmsFilamentHotendState.hotend_page.actions,
        ...(incomingHp.actions ?? {}),
      },
      hotend_rack_status: {
        ...defaultRackStatus,
        ...(incomingHp.hotend_rack_status ?? {}),
      },
      hotend_slots: Array.isArray(incomingHp.hotend_slots)
        ? incomingHp.hotend_slots
        : mockAmsFilamentHotendState.hotend_page.hotend_slots,
    },
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
    slot?: AmsFilamentHotendSlotViewModel | HotendSlotViewModel,
    extraPayload: AmsFilamentHotendSlotActionPayload = {},
  ) => {
    const payload = slot
      ? {
        nozzle_pos: slot.nozzle_pos,
        display_index: slot.display_index,
        ams_id: slot.ams_id,
        slot_id: slot.slot_id,
        // Only filament-tab slots ship bind_ams_slot_count; hotend-tab slots
        // do not need it on action payloads.
        bind_ams_slot_count: 'bind_ams_slot_count' in slot ? (slot.bind_ams_slot_count ?? 0) : 0,
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
