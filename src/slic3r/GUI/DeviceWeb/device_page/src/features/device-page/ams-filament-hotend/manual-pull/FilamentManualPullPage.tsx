import { useCallback, useEffect, useRef, useState } from 'react';
import { useDeviceBridge } from '../../../../hooks/Bridge';
import { useDeviceWebTheme } from '../../../../hooks/useDeviceWebTheme';
import { useFitHostWidth } from '../../../../hooks/useFitHostWidth';
import useStore from '../../../../store/AppStore';
import { mockAmsFilamentHotendState } from '../mockData';
import type { AmsFilamentHotendViewModel, BridgeResponseBody } from '../types';
import { FilamentManualPullDialog } from '../components/FilamentManualPullDialog';

const MODULE = 'device_page_ams_filament_hotend';

function makeBody(submod: string, action: string, payload?: Record<string, unknown>) {
  return { module: MODULE, submod, action, payload: payload ?? {} };
}

function isAmsFilamentHotendBody(body: unknown): body is BridgeResponseBody<AmsFilamentHotendViewModel> {
  return !!body
    && typeof body === 'object'
    && (body as { module?: unknown }).module === MODULE;
}

// FilamentManualPullDialog is a fixed FromDIP(636) wide host.
const HOST_WIDTH_DIP = 636;

export function FilamentManualPullPage() {
  const request = useDeviceBridge();
  const setTheme = useStore((s) => s.filament.setTheme);
  const [state, setState] = useState<AmsFilamentHotendViewModel>(mockAmsFilamentHotendState);
  const hasSeenManualPullRef = useRef(false);
  const closeRequestedRef = useRef(false);

  useDeviceWebTheme();
  useFitHostWidth(HOST_WIDTH_DIP);

  const applyState = useCallback((payload?: AmsFilamentHotendViewModel) => {
    if (!payload) return;
    const themeRaw = (payload as unknown as { theme?: unknown })?.theme;
    if (themeRaw === 'dark' || themeRaw === 'light') setTheme(themeRaw);
    setState(payload);
  }, [setTheme]);

  const refresh = useCallback(async () => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<AmsFilamentHotendViewModel>>(
      makeBody('state', 'get'),
      5000,
    );
    if (resp.ok && resp.value.error_code === 0) {
      applyState(resp.value.payload);
    }
  }, [applyState, request]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  useEffect(() => {
    const timer = window.setInterval(() => {
      void refresh();
    }, 1000);
    return () => window.clearInterval(timer);
  }, [refresh]);

  useEffect(() => {
    const mode = state.filament_page?.filament_change_status?.mode;
    if (mode === 'batch_manual_pull') {
      hasSeenManualPullRef.current = true;
      return;
    }
    if (!hasSeenManualPullRef.current || closeRequestedRef.current) return;
    closeRequestedRef.current = true;
    void request(makeBody('action', 'manual_pull_close'), 1000);
  }, [request, state.filament_page?.filament_change_status?.mode]);

  useEffect(() => {
    const handler = (event: Event) => {
      const detail = (event as CustomEvent).detail;
      const body = detail?.body;
      if (!isAmsFilamentHotendBody(body) || detail.head?.type !== 'report') return;
      applyState(body.payload);
    };
    document.addEventListener('cpp:device', handler);
    return () => document.removeEventListener('cpp:device', handler);
  }, [applyState]);

  const status = state.filament_page?.filament_change_status?.manual_pull;

  return (
    <main className="filament-manual-pull-page" data-testid="ams-filament-manual-pull-page">
      <FilamentManualPullDialog
        open
        embedded
        status={status}
        onClose={() => void request(makeBody('action', 'manual_pull_close'), 1000)}
        onExit={() => void request(makeBody('action', 'manual_pull_exit'), 1000)}
      />
    </main>
  );
}
