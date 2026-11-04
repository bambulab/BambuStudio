import { useCallback, useEffect, useRef, useState } from 'react';
import { useDeviceBridge } from '../../../hooks/Bridge';
import { emptyFilamentLoadState, normalizeState } from './normalize';
import { mockFilamentLoadState } from './mockData';
import type { BridgeResponseBody, FilamentLoadState } from './types';

const MODULE = 'device_page_filament_load';

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

function isFilamentLoadBody(body: unknown): body is BridgeResponseBody<FilamentLoadState> {
  return !!body
    && typeof body === 'object'
    && (body as { module?: unknown }).module === MODULE;
}

export function useFilamentLoadBridge() {
  const request = useDeviceBridge();
  const [state, setState] = useState<FilamentLoadState>(
    hasNativeHost() ? emptyFilamentLoadState : mockFilamentLoadState,
  );
  const [error, setError] = useState('');
  const stateKeyRef = useRef('');

  const applyState = useCallback((payload: Partial<FilamentLoadState> | undefined) => {
    const next = normalizeState(payload);
    const key = JSON.stringify(next);
    if (key === stateKeyRef.current) return;
    stateKeyRef.current = key;
    setState(next);
  }, []);

  const refresh = useCallback(async () => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<FilamentLoadState>>(
      makeBody('state', 'get'),
      5000,
    );
    if (!resp.ok) {
      setError(resp.error);
      return;
    }
    if (resp.value.error_code !== 0) {
      setError(resp.value.message ?? 'Filament load state failed');
      return;
    }
    applyState(resp.value.payload);
    setError('');
  }, [applyState, request]);

  const sendAction = useCallback(async (action: string, payload?: Record<string, unknown>) => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<FilamentLoadState>>(
      makeBody('action', action, payload),
      5000,
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
      if (!isFilamentLoadBody(body) || (e as CustomEvent).detail?.head?.type !== 'report') return;
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
    toggleExpand: () => sendAction('toggle_expand'),
    retry: () => sendAction('retry'),
    stop: () => sendAction('stop'),
    openBridgeDebugDialog: (webState: FilamentLoadState) => sendDebug('open_bridge_trace', { web_state: webState }),
  };
}
