import { useCallback, useEffect, useRef, useState } from 'react';
import { useDeviceBridge } from '../../../hooks/Bridge';
import { mockAutoRefillRefillInfoState } from './mockData';
import type { AutoRefillRefillInfoViewModel, BridgeResponseBody } from './types';

const MODULE = 'device_page_auto_refill_refill_info';

function makeBody(submod: string, action: string, payload?: Record<string, unknown>) {
  return { module: MODULE, submod, action, payload: payload ?? {} };
}

function isRefillInfoBody(body: unknown): body is BridgeResponseBody<AutoRefillRefillInfoViewModel> {
  return !!body
    && typeof body === 'object'
    && (body as { module?: unknown }).module === MODULE;
}

function normalizeState(payload: AutoRefillRefillInfoViewModel): AutoRefillRefillInfoViewModel {
  return {
    ...mockAutoRefillRefillInfoState,
    ...payload,
    extruders: Array.isArray(payload.extruders) ? payload.extruders : [],
  };
}

// Phase 1 is read-only: the hook only pulls state and mirrors C++ reports.
// reorder / add / remove commands are reserved for a later phase once the
// device write-back protocol lands.
export function useAutoRefillRefillInfoBridge() {
  const request = useDeviceBridge();
  const [state, setState] = useState<AutoRefillRefillInfoViewModel>(mockAutoRefillRefillInfoState);
  const [error, setError] = useState('');
  const stateKeyRef = useRef(JSON.stringify(mockAutoRefillRefillInfoState));

  const applyState = useCallback((payload: AutoRefillRefillInfoViewModel) => {
    const next = normalizeState(payload);
    const stateKey = JSON.stringify(next);
    if (stateKey === stateKeyRef.current) return;
    stateKeyRef.current = stateKey;
    setState(next);
  }, []);

  const refresh = useCallback(async () => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<AutoRefillRefillInfoViewModel>>(
      makeBody('state', 'get'),
      5000,
    );
    if (!resp.ok) {
      setError(resp.error);
      return;
    }
    if (resp.value.error_code !== 0) {
      setError(resp.value.message ?? 'Auto-refill state failed');
      return;
    }
    if (resp.value.payload) {
      applyState(resp.value.payload);
      setError('');
    }
  }, [applyState, request]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  // Acknowledge to C++ once a real (session_seq > 0) state has been committed
  // to the DOM, so the dialog can defer its first show until the freshly
  // pushed machine data is on screen (no stale-then-fresh flicker on open).
  const ackedSeqRef = useRef(-1);
  useEffect(() => {
    const seq = state.session_seq;
    if (seq > 0 && seq !== ackedSeqRef.current) {
      ackedSeqRef.current = seq;
      void request<ReturnType<typeof makeBody>, BridgeResponseBody<AutoRefillRefillInfoViewModel>>(
        makeBody('action', 'rendered', { session_seq: seq }),
        2000,
      );
    }
  }, [state.session_seq, request]);

  useEffect(() => {
    const handler = (e: Event) => {
      const detail = (e as CustomEvent).detail;
      const body = detail?.body;
      if (!isRefillInfoBody(body) || detail.head?.type !== 'report') return;
      if (body.payload) {
        applyState(body.payload);
        setError('');
      }
    };

    document.addEventListener('cpp:device', handler);
    return () => document.removeEventListener('cpp:device', handler);
  }, [applyState]);

  return { state, error, refresh };
}
