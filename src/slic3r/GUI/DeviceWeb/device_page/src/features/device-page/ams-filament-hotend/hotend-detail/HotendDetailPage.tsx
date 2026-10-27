import { useCallback, useEffect, useMemo, useState } from 'react';
import type { ReactNode } from 'react';
import { useTranslation } from 'react-i18next';
import { useDeviceBridge } from '../../../../hooks/Bridge';
import { useDeviceWebTheme } from '../../../../hooks/useDeviceWebTheme';
import { useFitHostWidth } from '../../../../hooks/useFitHostWidth';
import useStore from '../../../../store/AppStore';
import { mockAmsFilamentHotendState } from '../mockData';
import type {
  AmsFilamentHotendViewModel,
  BridgeResponseBody,
  HotendDetailDialogInfo,
  HotendOpState,
  HotendSlotInfo,
  HotendToolKind,
} from '../types';
import './hotend-detail.css';

const MODULE = 'device_page_ams_filament_hotend';
const HOTEND_DETAIL_NOZZLE_URL = './assets/hotend_detail_nozzle.png';
const HOTEND_DETAIL_NOZZLE_ERR_URL = './assets/hotend_detail_nozzle_err.png';
const HOTEND_DETAIL_NOZZLE_UNKNOWN_URL = './assets/hotend_detail_nozzle_unkown.png';
const HOTEND_DETAIL_NOZZLE_2D_URL = './assets/hotend_detail_nozzle_unkownedited.png';
const UNOWNEDITED_TOOL_KINDS: HotendToolKind[] = ['laser', 'cutter', 'pen', 'empty', 'unknown'];

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

function filamentSwatchColor(color?: string | null) {
  const raw = color?.trim();
  if (!raw) return '#D9D9D9';
  return raw.startsWith('#') ? raw : `#${raw}`;
}

function toolImageSrc(toolKind?: HotendToolKind, opState?: HotendOpState) {
  if (opState === 'Error') return HOTEND_DETAIL_NOZZLE_ERR_URL;
  if (opState === 'Unknown') return HOTEND_DETAIL_NOZZLE_UNKNOWN_URL;
  if (!toolKind || UNOWNEDITED_TOOL_KINDS.includes(toolKind)) return HOTEND_DETAIL_NOZZLE_2D_URL;
  return HOTEND_DETAIL_NOZZLE_URL;
}

// HotendDetailDialog is a fixed FromDIP(520) wide host.
const HOST_WIDTH_DIP = 520;

export function HotendDetailPage() {
  const { t } = useTranslation();
  const request = useDeviceBridge();
  const setTheme = useStore((s) => s.filament.setTheme);
  const [state, setState] = useState<AmsFilamentHotendViewModel>(mockAmsFilamentHotendState);
  const [selectedDisplayIndex, setSelectedDisplayIndex] = useState(() => getQueryDisplayIndex() || 1);

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
  const toolImage = toolImageSrc(hotend.tool_kind, hotend.op_state);
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
          <InfoLine label={t('Usedtime')} value={formatUsedTime(detail.used_time_hours)} />
          <InfoLine label={t('Switch Count')} value={formatSwitchCount(detail.switch_count)} />
        </aside>

        <section className="hotend-detail-info">
          {showFilament && (
            <InfoBlock label={t('Nozzle Filament')}>
              <div className="hotend-detail-filament">
                <span className="hotend-detail-color" style={{ backgroundColor: filamentSwatchColor(detail.filament_color) }} />
                <span>{textOrDash(detail.filament_name)}</span>
              </div>
            </InfoBlock>
          )}
          <InfoBlock label={t('Type')}>
            <span>{textOrDash(toolTypeText)}</span>
          </InfoBlock>
          <InfoBlock label={t('Version')}>
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
