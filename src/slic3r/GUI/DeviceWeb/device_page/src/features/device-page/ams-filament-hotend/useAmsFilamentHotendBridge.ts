import { useCallback, useEffect, useRef, useState } from 'react';
import { useDeviceBridge } from '../../../hooks/Bridge';
import useStore from '../../../store/AppStore';
import { mockAmsFilamentHotendState } from './mockData';
import type {
  AmsFilamentHotendSlotActionPayload,
  AmsFilamentHotendSlotViewModel,
  AmsFilamentHotendViewModel,
  BridgeResponseBody,
  FilamentChangeStatus,
  FilamentPageMode,
  HotendRackStatus,
  HotendSlotViewModel,
} from './types';

// Extract the theme field when present on state payloads.
function pickTheme(payload: unknown): 'dark' | 'light' | null {
  if (!payload || typeof payload !== 'object') return null;
  const value = (payload as { theme?: unknown }).theme;
  return value === 'dark' || value === 'light' ? value : null;
}

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
  const amsEnvUnits = Array.isArray(incomingFp.ams_env_units)
    ? incomingFp.ams_env_units
    : mockAmsFilamentHotendState.filament_page.ams_env_units ?? [];
  const showAmsEnvUnits = typeof incomingFp.show_ams_env_units === 'boolean'
    ? incomingFp.show_ams_env_units
    : mockAmsFilamentHotendState.filament_page.show_ams_env_units ?? true;

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
      ams_env_units: amsEnvUnits,
      show_ams_env_units: showAmsEnvUnits,
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
  const setTheme = useStore((s) => s.filament.setTheme);
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

  // `state.get` accepts an optional `mode` payload so the page can switch
  // between the normal view and the batch-* selection modes without a
  // separate endpoint. C++ remembers the last requested mode and re-uses it
  // for subsequent ReportState() pushes until Web changes mode again.
  const refresh = useCallback(async (mode: FilamentPageMode = 'normal') => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<AmsFilamentHotendViewModel>>(
      makeBody('state', 'get', { mode }),
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
      const theme = pickTheme(resp.value.payload);
      if (theme) setTheme(theme);
      applyState(resp.value.payload, true);
      setError('');
    }
  }, [applyState, request, setTheme]);

  const runSlotAction = useCallback(async (
    action: string,
    slot?: AmsFilamentHotendSlotViewModel | HotendSlotViewModel,
    extraPayload: AmsFilamentHotendSlotActionPayload = {},
  ) => {
    const payload = slot
      ? {
        addr_id: slot.addr_id,
        display_index: slot.display_index,
        ams_id: slot.ams_id,
        slot_id: slot.slot_id,
        // Present only on filament-tab slots.
        bind_ams_count: 'bind_ams_count' in slot ? (slot.bind_ams_count ?? 0) : 0,
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

  // Batch-mode commit action. Sends `{ mode, slots: [{ams_id, slot_id, display_index}, ...] }`
  // so C++ has everything it needs to dispatch the matching MQTT command.
  // C++ flips the page back to `normal` mode in its response; the next state
  // payload picked up by `applyState` reflects that.
  const runBatchAction = useCallback(async (
    mode: FilamentPageMode,
    slots: Array<Pick<AmsFilamentHotendSlotViewModel, 'ams_id' | 'slot_id' | 'display_index'>>,
    // `committed` = the caller already resolved every unknown slot in the edit
    // dialog. C++ then skips its own unknown re-check (which lags device RFID
    // round-trips) and runs the load directly, exiting batch mode to Idle.
    committed = false,
  ) => {
    const payload = {
      mode,
      committed,
      slots: slots.map((s) => ({
        ams_id: s.ams_id,
        slot_id: s.slot_id,
        display_index: s.display_index,
      })),
    };
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<AmsFilamentHotendViewModel>>(
      makeBody('action', 'batch_run', payload),
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

  return { state, error, refresh, runSlotAction, runBatchAction, openBridgeDebugDialog };
}
