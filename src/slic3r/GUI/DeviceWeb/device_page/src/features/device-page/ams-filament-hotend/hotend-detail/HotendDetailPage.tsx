import { useCallback, useEffect, useMemo, useState } from 'react';
import type { ReactNode } from 'react';
import { useTranslation } from 'react-i18next';
import { useDeviceBridge } from '../../../../hooks/Bridge';
import { useDeviceWebTheme } from '../../../../hooks/useDeviceWebTheme';
import useStore from '../../../../store/AppStore';
import { mockAmsFilamentHotendState } from '../mockData';
import type { AmsFilamentHotendViewModel, BridgeResponseBody, HotendDetailDialogInfo, HotendSlotInfo } from '../types';
import './hotend-detail.css';

const MODULE = 'device_page_ams_filament_hotend';
const HOTEND_DETAIL_NOZZLE_URL = './assets/hotend_detail_nozzle.png';
const TOOL_IMAGE_MAP: Record<string, string> = {
  hotend_detail_nozzle: HOTEND_DETAIL_NOZZLE_URL,
};

function makeBody(submod: string, action: string, payload?: Record<string, unknown>) {
  return { module: MODULE, submod, action, payload: payload ?? {} };
}

function isAmsFilamentHotendBody(body: unknown): body is BridgeResponseBody<AmsFilamentHotendViewModel> {
  return !!body
    && typeof body === 'object'
    && (body as { module?: unknown }).module === MODULE;
}

function getQueryDisplayIndex() {
  const hash = window.location.hash;
  const query = hash.includes('?') ? hash.slice(hash.indexOf('?') + 1) : '';
  const params = new URLSearchParams(query);
  return Number(params.get('display_index') ?? 0);
}

function textOrDash(value?: string | null) {
  return value && value.trim() ? value : '--';
}

function formatUsedTime(usedTimeText?: string | null) {
  return textOrDash(usedTimeText);
}

function formatSwitchCount(switchCount?: number | null) {
  return switchCount == null ? '--' : String(switchCount);
}

function toolImageSrc(imageName?: string | null) {
  if (!imageName || imageName === 'empty') return null;
  return TOOL_IMAGE_MAP[imageName] ?? null;
}

export function HotendDetailPage() {
  const { t } = useTranslation();
  const request = useDeviceBridge();
  const setTheme = useStore((s) => s.filament.setTheme);
  const [state, setState] = useState<AmsFilamentHotendViewModel>(mockAmsFilamentHotendState);
  const [selectedDisplayIndex, setSelectedDisplayIndex] = useState(() => getQueryDisplayIndex() || 1);

  useDeviceWebTheme();

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
    const handler = (event: Event) => {
      const detail = (event as CustomEvent).detail;
      const body = detail?.body;
      if (!isAmsFilamentHotendBody(body) || detail.head?.type !== 'report') return;
      applyState(body.payload);
    };
    document.addEventListener('cpp:device', handler);
    return () => document.removeEventListener('cpp:device', handler);
  }, [applyState]);

  const slots = useMemo(
    () => [...(state.hotend_page?.hotend_slots ?? [])].sort((a, b) => a.display_index - b.display_index),
    [state.hotend_page?.hotend_slots],
  );
  const selectedSlot = slots.find((slot) => slot.display_index === selectedDisplayIndex) ?? slots[0];
  const hotend: HotendSlotInfo = selectedSlot?.hotend ?? {};
  const detail: HotendDetailDialogInfo = selectedSlot?.hotend_detail_dialog ?? {};
  const toolImage = toolImageSrc(detail.tool_image_name);
  const showFilament = detail.show_filament ?? (hotend.tool_kind === 'nozzle');
  const toolTypeText = detail.tool_type ?? detail.nozzle_type;

  return (
    <div className="hotend-detail-page" data-testid="hotend-detail-page">
      <div className="hotend-detail-tabs" role="tablist" aria-label={t('Hotend list')}>
        {slots.map((slot) => (
          <button
            key={slot.display_index}
            type="button"
            className={slot.display_index === selectedSlot?.display_index ? 'active' : ''}
            onClick={() => setSelectedDisplayIndex(slot.display_index)}
          >
            {slot.display_index}
          </button>
        ))}
      </div>

      <div className="hotend-detail-content">
        <aside className="hotend-detail-card">
          <div className="hotend-detail-image">
            {toolImage ? <img src={toolImage} alt="" /> : <span className="hotend-detail-empty">--</span>}
          </div>
          <InfoLine label="SN" value={textOrDash(detail.serial_number)} />
          <InfoLine label="Usedtime" value={formatUsedTime(detail.used_time_hours)} />
          <InfoLine label="Switch Count" value={formatSwitchCount(detail.switch_count)} />
        </aside>

        <section className="hotend-detail-info">
          {showFilament && (
            <InfoBlock label="Nozzle Filament">
              <div className="hotend-detail-filament">
                <span className="hotend-detail-color" style={{ backgroundColor: detail.filament_color || '#D9D9D9' }} />
                <span>{textOrDash(detail.filament_name)}</span>
              </div>
            </InfoBlock>
          )}
          <InfoBlock label="Type">
            <span>{textOrDash(toolTypeText)}</span>
          </InfoBlock>
          <InfoBlock label="Version">
            <span>{textOrDash(detail.firmware_version)}</span>
          </InfoBlock>
        </section>
      </div>
    </div>
  );
}

function InfoLine({ label, value }: { label: string; value: string }) {
  return (
    <div className="hotend-detail-line">
      <span>{label}</span>
      <strong>{value}</strong>
    </div>
  );
}

function InfoBlock({ label, children }: { label: string; children: ReactNode }) {
  return (
    <div className="hotend-detail-block">
      <h2>{label}</h2>
      <div className="hotend-detail-value">{children}</div>
    </div>
  );
}
