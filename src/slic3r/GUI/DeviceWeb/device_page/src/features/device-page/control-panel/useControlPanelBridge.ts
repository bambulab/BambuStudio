import { useCallback, useEffect, useRef, useState } from 'react';
import { useDeviceBridge } from '../../../hooks/Bridge';
import { mockControlPanelState } from './mockData';
import { emptyControlPanelState, normalizeState } from './normalize';
import type { BridgeResponseBody, ControlPanelState, ExtruderId } from './types';

const MODULE = 'device_page_control_panel';

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

function isControlPanelBody(body: unknown): body is BridgeResponseBody<ControlPanelState> {
  return !!body
    && typeof body === 'object'
    && (body as { module?: unknown }).module === MODULE;
}

export function useControlPanelBridge() {
  const request = useDeviceBridge();
  const [state, setState] = useState<ControlPanelState>(
    hasNativeHost() ? emptyControlPanelState : mockControlPanelState,
  );
  const [error, setError] = useState('');
  const stateKeyRef = useRef('');

  const applyState = useCallback((payload: Partial<ControlPanelState> | undefined) => {
    const next = normalizeState(payload);
    const key = JSON.stringify(next);
    if (key === stateKeyRef.current) return;
    stateKeyRef.current = key;
    setState(next);
  }, []);

  const refresh = useCallback(async () => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<ControlPanelState>>(
      makeBody('state', 'get'),
      5000,
    );
    if (!resp.ok) {
      setError(resp.error);
      return;
    }
    if (resp.value.error_code !== 0) {
      setError(resp.value.message ?? 'Control panel state failed');
      return;
    }
    applyState(resp.value.payload);
    setError('');
  }, [applyState, request]);

  const sendAction = useCallback(async (action: string, payload?: Record<string, unknown>) => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<ControlPanelState>>(
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

  useEffect(() => {
    void refresh();
  }, [refresh]);

  useEffect(() => {
    const handler = (e: Event) => {
      const body = (e as CustomEvent).detail?.body;
      if (!isControlPanelBody(body) || (e as CustomEvent).detail?.head?.type !== 'report') return;
      if (body.submod === 'init') return;
      applyState(body.payload);
      setError('');
    };
    document.addEventListener('cpp:device', handler);
    return () => document.removeEventListener('cpp:device', handler);
  }, [applyState]);

  const openBridgeDebugDialog = useCallback(async (webState: ControlPanelState) => {
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
    setExtruderTemp: (extruderId: ExtruderId, value: number) => sendAction('set_extruder_temp', { extruder_id: extruderId, value }),
    setChamberTemp: (value: number) => sendAction('set_chamber_temp', { value }),
    setBedTemp: (value: number) => sendAction('set_bed_temp', { value }),
    setSpeed: (level: 1 | 2 | 3 | 4) => sendAction('set_speed', { level }),
    toggleLamp: (on: boolean) => sendAction('toggle_lamp', { on }),
    openFan: () => sendAction('open_fan'),
    axisMove: (op: string) => sendAction('axis_move', { op }),
    switchExtruder: (extruderId: number) => sendAction('switch_extruder', { extruder_id: extruderId }),
    openBridgeDebugDialog,
  };
}
