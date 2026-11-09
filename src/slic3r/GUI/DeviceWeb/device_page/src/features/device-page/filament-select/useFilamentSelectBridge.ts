import { useCallback, useEffect, useRef, useState } from 'react';
import { useDeviceBridge } from '../../../hooks/Bridge';
import { mockFilamentSelectState } from './mockData';
import { normalizeFilamentSelectState } from './normalize';
import {
  FILAMENT_SELECT_MODULE,
  type BridgeResponseBody,
  type FilamentSelectConfirmPayload,
  type FilamentSelectState,
} from './types';

function makeBody(submod: string, action: string, payload?: Record<string, unknown>) {
  return { module: FILAMENT_SELECT_MODULE, submod, action, payload: payload ?? {} };
}

function isFilamentSelectBody(body: unknown): body is BridgeResponseBody<unknown> {
  return !!body
    && typeof body === 'object'
    && (body as { module?: unknown }).module === FILAMENT_SELECT_MODULE;
}

type HostWindow = Window & {
  chrome?: { webview?: { postMessage?: unknown } };
  webkit?: { messageHandlers?: { wx?: { postMessage?: unknown } } };
};

/** True inside Studio (WebView2 / WKWebView); false on the vite dev server. */
function hasNativeHost(): boolean {
  const w = window as HostWindow;
  return typeof w.chrome?.webview?.postMessage === 'function'
    || typeof w.webkit?.messageHandlers?.wx?.postMessage === 'function';
}

const EMPTY_STATE: FilamentSelectState = normalizeFilamentSelectState({});

function dataKey(state: FilamentSelectState): string {
  return JSON.stringify([state.manager, state.presets]);
}

export function useFilamentSelectBridge() {
  const request = useDeviceBridge();
  // Inside Studio start empty so a placeholder list never flashes; without a
  // host (dev server) show the mock so the page stays inspectable.
  const [state, setState] = useState<FilamentSelectState>(() =>
    hasNativeHost() ? EMPTY_STATE : mockFilamentSelectState);
  const [loaded, setLoaded] = useState(() => !hasNativeHost());
  const lastPushRef = useRef<unknown>(null);
  const dataKeyRef = useRef(dataKey(hasNativeHost() ? EMPTY_STATE : mockFilamentSelectState));

  // A theme-only report keeps the manager / presets objects, so the page does
  // not reset the user's tab, selection and search.
  const applyState = useCallback((payload: unknown) => {
    lastPushRef.current = payload;
    const next = normalizeFilamentSelectState(payload);
    const key = dataKey(next);
    const sameData = key === dataKeyRef.current;
    dataKeyRef.current = key;
    setState((prev) => {
      if (!sameData) return next;
      return prev.theme === next.theme ? prev : { ...prev, theme: next.theme };
    });
    setLoaded(true);
  }, []);

  const refresh = useCallback(async () => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<unknown>>(
      makeBody('state', 'get'),
      5000,
    );
    if (!resp.ok || resp.value.error_code !== 0 || !resp.value.payload) return;
    applyState(resp.value.payload);
  }, [applyState, request]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  // Host reports (theme changes). The list itself is a snapshot.
  useEffect(() => {
    const handler = (e: Event) => {
      const detail = (e as CustomEvent).detail;
      const body = detail?.body;
      if (!isFilamentSelectBody(body) || detail.head?.type !== 'report') return;
      if (body.payload) applyState(body.payload);
    };
    document.addEventListener('cpp:device', handler);
    return () => document.removeEventListener('cpp:device', handler);
  }, [applyState]);

  /** C++ validates and closes the dialog; a rejected confirm changes nothing. */
  const confirm = useCallback(async (payload: FilamentSelectConfirmPayload) => {
    await request(makeBody('action', 'confirm', payload), 5000);
  }, [request]);

  const close = useCallback(async () => {
    await request(makeBody('action', 'close'), 5000);
  }, [request]);

  /** Debug builds only: C++ state + bridge trace, for the debug overlay. */
  const debugSnapshot = useCallback(async (webState: Record<string, unknown>) => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<unknown>>(
      makeBody('debug', 'snapshot', { web_state: { ...webState, last_push: lastPushRef.current } }),
      5000,
    );
    if (!resp.ok) return { error: resp.error };
    return resp.value.payload ?? { error_code: resp.value.error_code };
  }, [request]);

  return { state, loaded, confirm, close, debugSnapshot };
}
