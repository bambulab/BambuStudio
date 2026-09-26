import { useCallback, useEffect, useRef, useState } from 'react';
import { useDeviceBridge } from '../../../hooks/Bridge';
import { emptyAmsControlWebState, normalizeState } from './normalize';
import { mockAmsControlWebState } from './mockData';
import type { AmsControlWebViewModel, BridgeResponseBody } from './types';

const MODULE = 'device_page_ams_control_web';
// Shield extra edit/view clicks. Duration matches wxSYS_DCLICK_MSEC (typically 500ms).
const FILAMENT_DLG_SHIELD_MS = 500;

function hasNativeHost() {
  const w = window as Window & {
    chrome?: { webview?: { postMessage?: unknown } };
    webkit?: { messageHandlers?: { wx?: { postMessage?: unknown } } };
  };
  return typeof w.chrome?.webview?.postMessage === 'function'
    || typeof w.webkit?.messageHandlers?.wx?.postMessage === 'function';
}

function makeBody(submod: string, action: string, payload?: Record<string, unknown>) {
  return { module: MODULE, submod, action, payload: payload ?? {} };
}

function isAmsControlWebBody(body: unknown): body is BridgeResponseBody<AmsControlWebViewModel> {
  return !!body
    && typeof body === 'object'
    && (body as { module?: unknown }).module === MODULE;
}

export function useAmsControlWebBridge() {
  const request = useDeviceBridge();
  const [state, setState] = useState<AmsControlWebViewModel>(
    hasNativeHost() ? emptyAmsControlWebState : mockAmsControlWebState,
  );
  const [error, setError] = useState('');
  const stateKeyRef = useRef('');

  const applyState = useCallback((payload: AmsControlWebViewModel) => {
    const next = normalizeState(payload);
    const stateKey = JSON.stringify(next);
    if (stateKey === stateKeyRef.current) return;
    stateKeyRef.current = stateKey;
    setState(next);
  }, []);

  const refresh = useCallback(async () => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<AmsControlWebViewModel>>(
      makeBody('state', 'get'),
      5000,
    );
    if (!resp.ok) {
      setError(resp.error);
      return;
    }
    if (resp.value.error_code !== 0) {
      setError(resp.value.message ?? 'AmsControlWeb state failed');
      return;
    }
    if (resp.value.payload) {
      applyState(resp.value.payload);
      setError('');
    }
  }, [applyState, request]);

  const sendAction = useCallback(async (action: string, payload?: Record<string, unknown>) => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<AmsControlWebViewModel>>(
      makeBody('action', action, payload),
      5000,
    );
    if (!resp.ok) {
      setError(resp.error);
      return;
    }
    if (resp.value.payload) {
      applyState(resp.value.payload);
    }
    setError(resp.value.error_code === 0 ? '' : (resp.value.message ?? `${action} failed`));
  }, [applyState, request]);

  const selectSlot = useCallback(
    (amsId: string, slotId: string) => sendAction('select_slot', { ams_id: amsId, slot_id: slotId }),
    [sendAction],
  );

  const switchAms = useCallback((amsId: string) => sendAction('switch_ams', { ams_id: amsId }), [sendAction]);

  const filamentDlgBusyRef = useRef(false);
  const filamentDlgClosedAtRef = useRef(0);

  const openFilamentDialog = useCallback(
    (action: 'edit_slot' | 'view_slot', amsId: string, slotId: string) => {
      const now = Date.now();
      if (filamentDlgBusyRef.current) return;
      if (now - filamentDlgClosedAtRef.current < FILAMENT_DLG_SHIELD_MS) return;
      filamentDlgBusyRef.current = true;
      void sendAction(action, { ams_id: amsId, slot_id: slotId }).finally(() => {
        window.setTimeout(() => {
          filamentDlgBusyRef.current = false;
          filamentDlgClosedAtRef.current = Date.now();
        }, FILAMENT_DLG_SHIELD_MS);
      });
    },
    [sendAction],
  );

  const editSlot = useCallback(
    (amsId: string, slotId: string) => openFilamentDialog('edit_slot', amsId, slotId),
    [openFilamentDialog],
  );

  const readSlot = useCallback(
    (amsId: string, slotId: string) => sendAction('read_slot', { ams_id: amsId, slot_id: slotId }),
    [sendAction],
  );

  const viewSlot = useCallback(
    (amsId: string, slotId: string) => openFilamentDialog('view_slot', amsId, slotId),
    [openFilamentDialog],
  );

  const openFilamentHint = useCallback(
    (amsId: string, slotId: string) => sendAction('open_filament_mgr_hint', { ams_id: amsId, slot_id: slotId }),
    [sendAction],
  );

  const openHumidity = useCallback(
    (amsId: string) => sendAction('open_humidity', { ams_id: amsId }),
    [sendAction],
  );

  const openSettings = useCallback(() => sendAction('settings'), [sendAction]);
  const openAutoRefill = useCallback(() => sendAction('auto_refill'), [sendAction]);

  const loadFilament = useCallback(
    (amsId: string, slotId: string) => sendAction('load', { ams_id: amsId, slot_id: slotId }),
    [sendAction],
  );

  const unloadFilament = useCallback(
    (amsId: string, slotId: string) => sendAction('unload', { ams_id: amsId, slot_id: slotId }),
    [sendAction],
  );

  useEffect(() => {
    void refresh();
  }, [refresh]);

  useEffect(() => {
    const handler = (e: Event) => {
      const detail = (e as CustomEvent).detail;
      const body = detail?.body;
      if (!isAmsControlWebBody(body) || detail.head?.type !== 'report') return;
      // Theme reports only carry a theme field; do not overwrite the view model.
      if (body.submod === 'init') return;
      if (body.payload) {
        applyState(body.payload);
        setError('');
      }
    };

    document.addEventListener('cpp:device', handler);
    return () => document.removeEventListener('cpp:device', handler);
  }, [applyState]);

  const openBridgeDebugDialog = useCallback(async (webState: AmsControlWebViewModel) => {
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

  return {
    state,
    error,
    refresh,
    selectSlot,
    switchAms,
    editSlot,
    readSlot,
    viewSlot,
    openFilamentHint,
    openHumidity,
    openSettings,
    openAutoRefill,
    loadFilament,
    unloadFilament,
    openBridgeDebugDialog,
  };
}
