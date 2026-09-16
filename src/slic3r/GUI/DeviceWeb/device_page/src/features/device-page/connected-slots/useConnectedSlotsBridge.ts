import { useCallback, useEffect, useRef, useState } from 'react';
import { useDeviceBridge } from '../../../hooks/Bridge';
import { mockConnectedSlotsState } from './mockData';
import type { ConnectedSlotsTrayAction, ConnectedSlotsViewModel, BridgeResponseBody } from './types';

const MODULE = 'device_page_connected_slots';

function makeBody(submod: string, action: string, payload?: Record<string, unknown>) {
  return { module: MODULE, submod, action, payload: payload ?? {} };
}

function isConnectedSlotsBody(body: unknown): body is BridgeResponseBody<ConnectedSlotsViewModel> {
  return !!body
    && typeof body === 'object'
    && (body as { module?: unknown }).module === MODULE;
}

// The Figma mock is only the pre-push placeholder so the page stays inspectable
// without a C++ host. Once the host pushes, render exactly what it sent: an
// empty tree is a real device state, not a reason to fall back to the mock.
function normalizeState(payload: ConnectedSlotsViewModel): ConnectedSlotsViewModel {
  return {
    ...payload,
    slots: Array.isArray(payload.slots) ? payload.slots : [],
  };
}

export function useConnectedSlotsBridge(addrId?: number) {
  const request = useDeviceBridge();
  const [state, setState] = useState<ConnectedSlotsViewModel>(mockConnectedSlotsState);
  const [error, setError] = useState('');
  const stateKeyRef = useRef(JSON.stringify(mockConnectedSlotsState));
  // Raw payload as pushed by C++, before normalizeState() substitutes the mock
  // for an empty tree. Kept in a ref so capturing it never triggers a render.
  const lastPushRef = useRef<ConnectedSlotsViewModel>(mockConnectedSlotsState);

  const applyState = useCallback((payload: ConnectedSlotsViewModel) => {
    lastPushRef.current = payload;
    const next = normalizeState(payload);
    const stateKey = JSON.stringify(next);
    if (stateKey === stateKeyRef.current) return;
    stateKeyRef.current = stateKey;
    setState(next);
  }, []);

  const getReport = useCallback(() => JSON.stringify(lastPushRef.current, null, 2), []);

  const openDryControls = useCallback(async (amsId: string) => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<ConnectedSlotsViewModel>>(
      makeBody('action', 'dry', { ams_id: amsId }),
      8000,
    );
    if (!resp.ok) return;
    if (resp.value.error_code !== 0) {
      setError(resp.value.message ?? 'Connected Slots dry action failed');
      return;
    }
    setError('');
  }, [request]);

  const selectSource = useCallback(async (extIndex: number) => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<ConnectedSlotsViewModel>>(
      makeBody('source', 'select', { ext_index: extIndex }),
      5000,
    );
    if (!resp.ok) return;
    if (resp.value.error_code !== 0) {
      setError(resp.value.message ?? 'Connected Slots source select failed');
      return;
    }
    if (resp.value.payload) {
      applyState(resp.value.payload);
      setError('');
    }
  }, [applyState, request]);

  /** Runs one tray context-menu action; C++ answers with the refreshed state. */
  const runTrayAction = useCallback(async (action: ConnectedSlotsTrayAction, amsId: string, slotId: string) => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<ConnectedSlotsViewModel>>(
      makeBody('action', action, { ams_id: amsId, slot_id: slotId }),
      8000,
    );
    if (!resp.ok) return;
    if (resp.value.error_code !== 0) {
      setError(resp.value.message ?? `Connected Slots ${action} failed`);
      return;
    }
    if (resp.value.payload) {
      applyState(resp.value.payload);
      setError('');
    }
  }, [applyState, request]);

  const refresh = useCallback(async () => {
    const payload = typeof addrId === 'number' && addrId >= 0 ? { addr_id: addrId } : {};
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<ConnectedSlotsViewModel>>(
      makeBody('state', 'get', payload),
      5000,
    );
    if (!resp.ok) {
      // Keep the Figma mock so the page is inspectable without a C++ host.
      return;
    }
    if (resp.value.error_code !== 0) {
      setError(resp.value.message ?? 'Connected Slots state failed');
      return;
    }
    if (resp.value.payload) {
      applyState(resp.value.payload);
      setError('');
    }
  }, [addrId, applyState, request]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  useEffect(() => {
    const handler = (e: Event) => {
      const detail = (e as CustomEvent).detail;
      const body = detail?.body;
      if (!isConnectedSlotsBody(body) || detail.head?.type !== 'report') return;
      if (body.payload) {
        applyState(body.payload);
        setError('');
      }
    };

    document.addEventListener('cpp:device', handler);
    return () => document.removeEventListener('cpp:device', handler);
  }, [applyState]);

  return { state, error, refresh, getReport, selectSource, openDryControls, runTrayAction };
}
