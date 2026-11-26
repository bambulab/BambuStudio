import { useCallback, useEffect, useRef, useState } from 'react';
import { useDeviceBridge } from '../../../hooks/Bridge';
import { emptyHotendRackState, normalizeState } from './normalize';
import { mockHotendRackState } from './mockData';
import type { BridgeResponseBody, HotendRackRowId, HotendRackState } from './types';

const MODULE = 'device_page_hotend_rack';

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

function isHotendRackBody(body: unknown): body is BridgeResponseBody<HotendRackState> {
  return !!body
    && typeof body === 'object'
    && (body as { module?: unknown }).module === MODULE;
}

export function useHotendRackBridge() {
  const request = useDeviceBridge();
  const [state, setState] = useState<HotendRackState>(
    hasNativeHost() ? emptyHotendRackState : mockHotendRackState,
  );
  const [error, setError] = useState('');
  const stateKeyRef = useRef('');

  const applyState = useCallback((payload: Partial<HotendRackState> | undefined) => {
    const next = normalizeState(payload);
    const key = JSON.stringify(next);
    if (key === stateKeyRef.current) return;
    stateKeyRef.current = key;
    setState(next);
  }, []);

  const refresh = useCallback(async () => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<HotendRackState>>(
      makeBody('state', 'get'),
      5000,
    );
    if (!resp.ok) {
      setError(resp.error);
      return;
    }
    if (resp.value.error_code !== 0) {
      setError(resp.value.message ?? 'Hotend rack state failed');
      return;
    }
    applyState(resp.value.payload);
    setError('');
  }, [applyState, request]);

  const sendAction = useCallback(async (action: string, payload?: Record<string, unknown>) => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<HotendRackState>>(
      makeBody('action', action, payload),
      8000,
    );
    if (!resp.ok) {
      setError(resp.error);
      return;
    }
    if (resp.value.payload) applyState(resp.value.payload);
    setError(resp.value.error_code === 0 ? '' : (resp.value.message ?? `${action} failed`));
  }, [applyState, request]);

  const sendDebug = useCallback(async (action: string, payload?: Record<string, unknown>) => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<unknown>>(
      makeBody('debug', action, payload),
      8000,
    );
    if (!resp.ok) {
      setError(`Debug request failed: ${resp.error}`);
      return false;
    }
    setError(resp.value.error_code === 0 ? '' : (resp.value.message ?? `${action} failed`));
    return resp.value.error_code === 0;
  }, [request]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  useEffect(() => {
    const handler = (e: Event) => {
      const body = (e as CustomEvent).detail?.body;
      if (!isHotendRackBody(body) || (e as CustomEvent).detail?.head?.type !== 'report') return;
      if (body.submod === 'init') return;
      applyState(body.payload);
      setError('');
    };
    document.addEventListener('cpp:device', handler);
    return () => document.removeEventListener('cpp:device', handler);
  }, [applyState]);

  return {
    state,
    error,
    refresh,
    moveRow: (row: HotendRackRowId) => sendAction('move_row', { row }),
    home: () => sendAction('home'),
    readAll: () => sendAction('read_all'),
    openHotendsInfo: () => sendAction('open_hotends_info'),
    showErrorDetail: (slotId: number) => sendAction('show_error_detail', { slot_id: slotId }),
    openBridgeDebugDialog: (webState: HotendRackState) => sendDebug('open_bridge_trace', { web_state: webState }),
  };
}
