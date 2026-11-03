import { useCallback, useEffect, useRef, useState } from 'react';
import { useDeviceBridge } from '../../../hooks/Bridge';
import { mockAmsMappingHotendState } from './mockData';
import type {
  AmsMappingHotendSlotViewModel,
  AmsMappingHotendSubSlotViewModel,
  AmsMappingHotendViewModel,
  BridgeResponseBody,
} from './types';

const MODULE = 'device_page_ams_mapping_hotend';

function makeBody(submod: string, action: string, payload?: Record<string, unknown>) {
  return { module: MODULE, submod, action, payload: payload ?? {} };
}

function isAmsMappingHotendBody(body: unknown): body is BridgeResponseBody<AmsMappingHotendViewModel> {
  return !!body
    && typeof body === 'object'
    && (body as { module?: unknown }).module === MODULE;
}

function normalizeState(payload: AmsMappingHotendViewModel): AmsMappingHotendViewModel {
  return {
    ...mockAmsMappingHotendState,
    ...payload,
    hotend_slots: Array.isArray(payload.hotend_slots)
      ? payload.hotend_slots
      : mockAmsMappingHotendState.hotend_slots,
  };
}

// Decompose "#RRGGBB" / "#RRGGBBAA" into 0-255 RGBA components for the
// EVT_SET_FINISH_MAPPING wire format.
function colorToRgba(color: string): { r: number; g: number; b: number; a: number } {
  if (!color || color[0] !== '#' || color.length < 7) {
    return { r: 0, g: 0, b: 0, a: 255 };
  }
  const r = parseInt(color.substr(1, 2), 16);
  const g = parseInt(color.substr(3, 2), 16);
  const b = parseInt(color.substr(5, 2), 16);
  const a = color.length >= 9 ? parseInt(color.substr(7, 2), 16) : 255;
  return { r, g, b, a };
}

function buildSelectPayload(
  slot: AmsMappingHotendSlotViewModel | AmsMappingHotendSubSlotViewModel,
  current_filament_id: number,
): Record<string, unknown> {
  const { r, g, b, a } = colorToRgba(slot.color);
  return {
    r, g, b, a,
    tray_index: slot.index,
    filament_id: current_filament_id,
    tray_id: slot.tray_id,
    ams_id: slot.ams_id,
    slot_id: slot.slot_id,
    ctype: slot.ctype,
    multi_colors: slot.multi_colors,
  };
}

export function useAmsMappingHotendBridge() {
  const request = useDeviceBridge();
  const [state, setState] = useState<AmsMappingHotendViewModel>(mockAmsMappingHotendState);
  const [error, setError] = useState('');
  const stateKeyRef = useRef(JSON.stringify(mockAmsMappingHotendState));

  const applyState = useCallback((payload: AmsMappingHotendViewModel) => {
    const next = normalizeState(payload);
    const stateKey = JSON.stringify(next);
    if (stateKey === stateKeyRef.current) return;
    stateKeyRef.current = stateKey;
    setState(next);
  }, []);

  const refresh = useCallback(async () => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<AmsMappingHotendViewModel>>(
      makeBody('state', 'get'),
      5000,
    );
    if (!resp.ok) {
      setError(resp.error);
      return;
    }
    if (resp.value.error_code !== 0) {
      setError(resp.value.message ?? 'AMS mapping hotend state failed');
      return;
    }
    if (resp.value.payload) {
      applyState(resp.value.payload);
      setError('');
    }
  }, [applyState, request]);

  const selectSlot = useCallback(async (
    slot: AmsMappingHotendSlotViewModel | AmsMappingHotendSubSlotViewModel,
  ) => {
    const payload = buildSelectPayload(slot, state.current_filament_id);
    await request<ReturnType<typeof makeBody>, BridgeResponseBody<AmsMappingHotendViewModel>>(
      makeBody('action', 'select', payload),
      8000,
    );
  }, [request, state.current_filament_id]);

  const editSlot = useCallback(async (
    slot: AmsMappingHotendSlotViewModel | AmsMappingHotendSubSlotViewModel,
  ) => {
    await request<ReturnType<typeof makeBody>, BridgeResponseBody<AmsMappingHotendViewModel>>(
      makeBody('action', 'edit', { ams_id: slot.ams_id, slot_id: slot.slot_id }),
      8000,
    );
    // C++ re-publishes state on its own after the modal closes (immediate +
    // delayed). These web-side refreshes are a defensive fallback in case a
    // report is missed while the device echo is still in flight.
    void refresh();
    window.setTimeout(() => void refresh(), 500);
    window.setTimeout(() => void refresh(), 1500);
  }, [request, refresh]);

  const resetMapping = useCallback(async () => {
    await request<ReturnType<typeof makeBody>, BridgeResponseBody<AmsMappingHotendViewModel>>(
      makeBody('action', 'reset', {}),
      4000,
    );
  }, [request]);

  const dismiss = useCallback(async () => {
    await request<ReturnType<typeof makeBody>, BridgeResponseBody<AmsMappingHotendViewModel>>(
      makeBody('action', 'dismiss', {}),
      4000,
    );
  }, [request]);

  const showNozzleError = useCallback(async () => {
    await request<ReturnType<typeof makeBody>, BridgeResponseBody<AmsMappingHotendViewModel>>(
      makeBody('action', 'show_nozzle_error', {}),
      4000,
    );
  }, [request]);

  // Ask C++ to open the filament-matching-rules Wiki in the system browser.
  // The WebView cannot navigate away itself, so the URL lives on the C++ side.
  const openWiki = useCallback(async () => {
    await request<ReturnType<typeof makeBody>, BridgeResponseBody<AmsMappingHotendViewModel>>(
      makeBody('action', 'open_wiki', {}),
      4000,
    );
  }, [request]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  useEffect(() => {
    if (state.theme === 'dark' || state.theme === 'light') {
      document.documentElement.dataset.theme = state.theme;
    }
  }, [state.theme]);

  // Acknowledge to C++ once a real (session_seq > 0) state has been committed
  // to the DOM. The popup defers its show until this ack so the window's first
  // frame is the freshly pushed machine data (no stale-then-fresh flicker on
  // machine switch). Runs after render commit, so the DOM is already updated.
  const ackedSeqRef = useRef(-1);
  useEffect(() => {
    const seq = state.session_seq;
    if (seq > 0 && seq !== ackedSeqRef.current) {
      ackedSeqRef.current = seq;
      void request<ReturnType<typeof makeBody>, BridgeResponseBody<AmsMappingHotendViewModel>>(
        makeBody('action', 'rendered', { session_seq: seq }),
        2000,
      );
      // A committed DOM is not a painted one: macOS keeps the popup masked until this ack.
      requestAnimationFrame(() => requestAnimationFrame(() => {
        void request<ReturnType<typeof makeBody>, BridgeResponseBody<AmsMappingHotendViewModel>>(
          makeBody('action', 'presented', { session_seq: seq }),
          2000,
        );
      }));
    }
  }, [state.session_seq, request]);

  useEffect(() => {
    const handler = (e: Event) => {
      const detail = (e as CustomEvent).detail;
      const body = detail?.body;
      if (!isAmsMappingHotendBody(body) || detail.head?.type !== 'report') return;
      if (body.payload) {
        applyState(body.payload);
        setError('');
      }
    };

    document.addEventListener('cpp:device', handler);
    return () => document.removeEventListener('cpp:device', handler);
  }, [applyState]);

  return { state, error, refresh, selectSlot, editSlot, resetMapping, dismiss, openWiki, showNozzleError };
}
