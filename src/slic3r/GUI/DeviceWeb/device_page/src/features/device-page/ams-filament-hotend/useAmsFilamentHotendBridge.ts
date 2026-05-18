import { useCallback, useEffect, useRef, useState } from 'react';
import { useDeviceBridge } from '../../../hooks/Bridge';
import { mockAmsFilamentHotendState } from './mockData';
import type { AmsFilamentHotendSlotViewModel, AmsFilamentHotendViewModel, BridgeResponseBody } from './types';

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
  return {
    ...mockAmsFilamentHotendState,
    ...payload,
    actions: {
      ...mockAmsFilamentHotendState.actions,
      ...(payload.actions ?? {}),
    },
    hotend_slots: Array.isArray(payload.hotend_slots) ? payload.hotend_slots : mockAmsFilamentHotendState.hotend_slots,
  };
}

export function useAmsFilamentHotendBridge() {
  const request = useDeviceBridge();
  const [state, setState] = useState<AmsFilamentHotendViewModel>(mockAmsFilamentHotendState);
  const [error, setError] = useState('');
  const stateKeyRef = useRef(JSON.stringify(mockAmsFilamentHotendState));

  const applyState = useCallback((payload: AmsFilamentHotendViewModel) => {
    const next = normalizeState(payload);
    const stateKey = JSON.stringify(next);
    if (stateKey === stateKeyRef.current) return;
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
      applyState(resp.value.payload);
      setError('');
    }
  }, [applyState, request]);

  const runSlotAction = useCallback(async (action: string, slot?: AmsFilamentHotendSlotViewModel) => {
    const payload = slot
      ? {
        nozzle_pos: slot.nozzle_pos,
        display_index: slot.display_index,
        ams_id: slot.ams_id,
        slot_id: slot.slot_id,
        bind_ams_slot_count: slot.bind_ams_slot_count ?? 0,
      }
      : {};
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<AmsFilamentHotendViewModel>>(
      makeBody('action', action, payload),
      8000,
    );
    if (resp.ok && resp.value.payload) {
      applyState(resp.value.payload);
    }
  }, [applyState, request]);

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

  return { state, error, refresh, runSlotAction };
}
