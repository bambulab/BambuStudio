import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useDeviceBridge } from '../../../../hooks/Bridge';
import { useDeviceWebTheme } from '../../../../hooks/useDeviceWebTheme';
import useStore from '../../../../store/AppStore';
import { Nozzle } from '../components/Nozzle';
import { SpoolDisc } from '../components/SpoolDisc';
import type { AmsFilamentHotendSlotViewModel, AmsFilamentHotendViewModel, AmsTrayInfo, BridgeResponseBody } from '../types';
import './ams-ext-operation.css';

type BindAms = {
  ams_id: string;
  ams_type: string;
  slot_count: number;
};

type SlotQuery = {
  addr_id: string;
  display_index: string;
  ams_id: string;
  slot_id: string;
  count: string;
  bind_ams: BindAms[];
};

function parseBindAms(raw: string): BindAms[] {
  if (!raw) return [];
  return raw.split(',').map((part) => {
    const [ams_id, ams_type, slotCount] = part.split('|');
    return {
      ams_id: ams_id ?? '',
      ams_type: ams_type ?? '',
      slot_count: Number(slotCount || 0),
    };
  }).filter((item) => item.ams_id);
}

function getQuery(): SlotQuery {
  const hash = window.location.hash;
  const query = hash.includes('?') ? hash.slice(hash.indexOf('?') + 1) : '';
  const params = new URLSearchParams(query);
  return {
    addr_id: params.get('addr_id') ?? '',
    display_index: params.get('display_index') ?? '',
    ams_id: params.get('ams_id') ?? '',
    slot_id: params.get('slot_id') ?? '',
    count: params.get('count') ?? '0',
    bind_ams: parseBindAms(params.get('bind_ams') ?? ''),
  };
}

const MODULE = 'device_page_ams_filament_hotend';

const fallbackColors = [
  ['#ff8181', '#b7f6ca', '#675aff', '#d9d9d9'],
  ['#f09a17', '#d7bafd', '#b6f34f', '#5c5c5c'],
  ['#f09a17', '#d7bafd', '#b6f34f', '#5c5c5c'],
  ['#f09a17', '#d7bafd', '#b6f34f', '#5c5c5c'],
];

function makeBody(submod: string, action: string, payload?: Record<string, unknown>) {
  return { module: MODULE, submod, action, payload: payload ?? {} };
}

function isAmsFilamentHotendBody(body: unknown): body is BridgeResponseBody<AmsFilamentHotendViewModel> {
  return !!body
    && typeof body === 'object'
    && (body as { module?: unknown }).module === MODULE;
}

export function AmsExtOperationPage() {
  const { t } = useTranslation();
  const query = getQuery();
  const request = useDeviceBridge();
  const setTheme = useStore((s) => s.filament.setTheme);
  const [state, setState] = useState<AmsFilamentHotendViewModel | null>(null);
  const [selectedAmsId, setSelectedAmsId] = useState(query.bind_ams[0]?.ams_id ?? query.ams_id);

  // Sync host theme into page CSS variables.
  useDeviceWebTheme();

  const applyState = useCallback((payload?: AmsFilamentHotendViewModel) => {
    if (payload) setState(payload);
  }, []);

  const refresh = useCallback(async () => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<AmsFilamentHotendViewModel>>(
      makeBody('state', 'get'),
      5000,
    );
    if (resp.ok && resp.value.error_code === 0) {
      // Seed theme before first paint.
      const themeRaw = (resp.value.payload as unknown as { theme?: unknown })?.theme;
      if (themeRaw === 'dark' || themeRaw === 'light') setTheme(themeRaw);
      applyState(resp.value.payload);
    }
  }, [applyState, request, setTheme]);

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

  const selectedSlot = useMemo(() => {
    const addrId = Number(query.addr_id);
    return state?.filament_page?.hotend_slots?.find((slot) => slot.addr_id === addrId);
  }, [query.addr_id, state]);

  const operationSlot = useMemo(() => {
    const addrId = Number(query.addr_id);
    return state?.ams_ext_operation_page?.slots?.find((slot) => slot.addr_id === addrId);
  }, [query.addr_id, state]);

  const bindAms = operationSlot?.bind_ams && operationSlot.bind_ams.length > 0
    ? operationSlot.bind_ams
    : selectedSlot?.bind_ams && selectedSlot.bind_ams.length > 0
    ? selectedSlot.bind_ams
    : query.bind_ams.length > 0
    ? query.bind_ams
    : [{ ams_id: query.ams_id || '0', ams_type: 'ZMULTI_A', slot_count: Number(query.count || 4) }];

  // Dialog AMS units come from the dedicated C++ operation-page payload.
  // Fall back to legacy slot/query reconstruction for compatibility with
  // older payloads or mock data that predates ams_ext_operation_page.
  const operationUnits = operationSlot?.units ?? [];
  const amsUnits = operationUnits.length > 0
    ? operationUnits
    : bindAms.map((ams) => {
      return { ...ams, humidity_raw: -1, dry_status: '', trays: [] };
    });

  useEffect(() => {
    if (!amsUnits.length) return;
    if (!selectedAmsId || !amsUnits.some((ams) => ams.ams_id === selectedAmsId)) {
      setSelectedAmsId(amsUnits[0].ams_id);
    }
  }, [amsUnits, selectedAmsId]);

  // Send a per-tray menu action through the bridge.
  const runTrayAction = useCallback(async (action: 'load' | 'unload' | 'read', tray: ExtAmsTray) => {
    const body = {
      module: MODULE,
      submod: 'action',
      action,
      payload: {
        addr_id: Number(query.addr_id) || selectedSlot?.addr_id || 0,
        display_index: Number(query.display_index) || 0,
        ams_id: tray.ams_id,
        slot_id: tray.slot_id,
      },
    };
    await request(body, 8000);
  }, [query.display_index, query.addr_id, request, selectedSlot]);

  const selectedAms = amsUnits.find((ams) => ams.ams_id === selectedAmsId) ?? amsUnits[0];
  const selectedTrays = selectedAms?.trays ?? [];
  const displayTrays = selectedTrays.length > 0 ? selectedTrays : buildFallbackTrays();
  const activeTrayIndex = Math.max(0, displayTrays.findIndex((tray) => selectedAms?.ams_id === query.ams_id && tray.slot_id === query.slot_id));

  return (
    <div className="ext-ams-page">
      <main className="ext-ams-body">
        <h1>{t('Ext AMS Details')}</h1>
        <div className="ext-ams-tabs">
          {amsUnits.map((ams, index) => (
            <button
              className={ams.ams_id === selectedAmsId ? 'active' : ''}
              type="button"
              key={ams.ams_id || index}
              onClick={() => setSelectedAmsId(ams.ams_id)}
              title={`AMS ${ams.ams_id}, slots ${ams.trays?.length ?? ams.slot_count ?? 0}`}
            >
              <MiniAms colors={colorsForAms(index, ams.trays)} />
            </button>
          ))}
        </div>

        <section className="ext-ams-card">
          <div className="ext-ams-card-head">
            <span>{formatAmsName(selectedAms?.ams_id)}</span>
            <span className="humidity-pill">💧 {formatHumidity(selectedAms?.humidity_raw)} <b /> ☼ {selectedAms?.dry_status || 'Idle'}</span>
          </div>

          <div className="ext-ams-flow">
            <div className="ext-ams-slots">
              {displayTrays.map((tray, index) => (
                <AmsSlot
                  key={`${tray.ams_id}-${tray.slot_id}-${index}`}
                  index={Number(tray.slot_id) + 1 || index + 1}
                  slot={trayToSlot(tray, index)}
                  actions={tray.actions}
                  onAction={(action) => void runTrayAction(action, tray)}
                />
              ))}
            </div>
            <AmsRoute
              count={displayTrays.length}
              activeIndex={activeTrayIndex}
              displayIndex={Number(query.display_index) || 0}
              nozzleState={selectedSlot?.filament_nozzle.state ?? 'Normal'}
              nozzleColor={selectedSlot?.filament_nozzle.filament_color || selectedSlot?.filament_card.color}
              hasFilamentInNozzle={selectedSlot?.filament_nozzle.has_filament}
            />
          </div>

        </section>
      </main>
    </div>
  );
}

type ExtAmsTray = AmsTrayInfo;

function colorsForAms(index: number, trays?: ExtAmsTray[]) {
  const colors = trays?.map((tray) => tray.color).filter(Boolean).slice(0, 4);
  return colors && colors.length > 0 ? colors : fallbackColors[index % fallbackColors.length];
}

function formatAmsName(amsId?: string) {
  if (!amsId) return 'AMS';
  const n = Number(amsId);
  if (Number.isFinite(n) && n >= 0 && n < 26) return `AMS-${String.fromCharCode(65 + n)}`;
  return `AMS-${amsId}`;
}

function MiniAms({ colors }: { colors: string[] }) {
  return (
    <span className="mini-ams">
      {colors.map((color, index) => <i key={`${color}-${index}`} style={{ background: color }} />)}
    </span>
  );
}

function formatHumidity(value?: number) {
  return typeof value === 'number' && value >= 0 ? `${value}%` : '--';
}

function trayLabel(tray: ExtAmsTray) {
  if (tray.tray_state === 'unknown') return '?';
  if (tray.tray_state === 'empty') return 'Empty';
  return tray.filament_type || tray.filament_name || '--';
}

function trayToSlot(tray: ExtAmsTray, index: number): AmsFilamentHotendSlotViewModel {
  const state =
    tray.tray_state === 'empty' ? 'Empty' :
    tray.tray_state === 'reading' ? 'Reading' :
    tray.tray_state === 'unknown' ? 'Unknown' :
    tray.tray_state === 'error' ? 'Error' :
    'ReadyToLoad';

  return {
    addr_id: index,
    display_index: Number(tray.slot_id) + 1 || index + 1,
    ams_id: tray.ams_id,
    slot_id: tray.slot_id,
    is_top_tier: false,
    enabled: tray.exists,
    filament_card: {
      state,
      color: tray.color || '#d9d9d9',
      color_type: tray.color_type,
      colors: tray.colors,
      text: tray.filament_name,
      name: tray.filament_name,
      type: tray.filament_type,
      remain: typeof tray.remain === 'number' ? tray.remain : 100,
      status_text: '',
    },
    filament_line: {
      state: 'no_filament',
      has_filament: false,
      filament_color: '',
    },
    filament_nozzle: {
      state: 'Normal',
      has_filament: false,
      filament_color: '',
      on_extruder: false,
    },
  };
}

function buildFallbackTrays(): ExtAmsTray[] {
  return [
    { ams_id: '0', slot_id: '0', exists: true, tray_state: 'unknown', filament_type: '', filament_name: '', color: '#d9d9d9' },
    { ams_id: '0', slot_id: '1', exists: true, tray_state: 'ready', filament_type: 'PLA', filament_name: 'PLA Basic', color: '#00e64d' },
    { ams_id: '0', slot_id: '2', exists: true, tray_state: 'ready', filament_type: 'PLA', filament_name: 'PLA Basic', color: '#7a75ff' },
    { ams_id: '0', slot_id: '3', exists: true, tray_state: 'ready', filament_type: 'PLA', filament_name: 'PLA Basic', color: '#00e64d' },
  ];
}

function AmsSlot({
  index,
  slot,
  actions,
  onAction,
}: {
  index: number;
  slot: AmsFilamentHotendSlotViewModel;
  actions?: ExtAmsTray['actions'];
  onAction?: (action: 'load' | 'unload' | 'read') => void;
}) {
  const canLoad = actions?.can_load ?? false;
  const canUnload = actions?.can_unload ?? false;
  // Keep read visible for legacy payloads.
  const canRead = actions?.can_read ?? !actions;
  const readLabel = actions?.read_label ?? 'Read';
  const hasMenu = canLoad || canUnload || canRead;

  return (
    <div className="ext-ams-slot">
      <span className="slot-index">{index}</span>
      {hasMenu && (
        <ExtTrayMenu
          canLoad={canLoad}
          canUnload={canUnload}
          canRead={canRead}
          readLabel={readLabel}
          onAction={(action) => onAction?.(action)}
        />
      )}
      <SpoolDisc slot={slot} />
      <span className="slot-label">{trayLabel({
        ams_id: slot.ams_id,
        slot_id: slot.slot_id,
        exists: slot.enabled,
        tray_state: slot.filament_card.state === 'Unknown' ? 'unknown' : slot.filament_card.state === 'Empty' ? 'empty' : 'ready',
        filament_type: slot.filament_card.type,
        filament_name: slot.filament_card.name,
        color: slot.filament_card.color,
      })}</span>
    </div>
  );
}

function ExtTrayMenu({
  canLoad,
  canUnload,
  canRead,
  readLabel,
  onAction,
}: {
  canLoad: boolean;
  canUnload: boolean;
  canRead: boolean;
  readLabel: 'Read' | 'Reread';
  onAction: (action: 'load' | 'unload' | 'read') => void;
}) {
  const { t } = useTranslation();
  const [open, setOpen] = useState(false);
  const rootRef = useRef<HTMLSpanElement>(null);

  useEffect(() => {
    if (!open) return;
    const onPointerDown = (event: PointerEvent) => {
      if (!rootRef.current?.contains(event.target as Node)) setOpen(false);
    };
    document.addEventListener('pointerdown', onPointerDown);
    return () => document.removeEventListener('pointerdown', onPointerDown);
  }, [open]);

  const run = (action: 'load' | 'unload' | 'read') => {
    setOpen(false);
    onAction(action);
  };

  return (
    <span className="ext-tray-menu-wrap" ref={rootRef}>
      <button
        type="button"
        className={`ext-tray-menu${open ? ' open' : ''}`}
        aria-label="tray menu"
        aria-haspopup="menu"
        aria-expanded={open}
        onClick={(event) => {
          event.stopPropagation();
          setOpen((value) => !value);
        }}
      >
        <span /><span /><span />
      </button>
      {open && (
        <div className="ext-tray-menu-popover" role="menu">
          {canLoad && <button type="button" role="menuitem" onClick={() => run('load')}>{t('Load')}</button>}
          {canUnload && <button type="button" role="menuitem" onClick={() => run('unload')}>{t('Unload')}</button>}
          {canRead && <button type="button" role="menuitem" onClick={() => run('read')}>{t(readLabel)}</button>}
        </div>
      )}
    </span>
  );
}

function AmsRoute({
  count,
  activeIndex,
  displayIndex,
  nozzleState,
  nozzleColor,
  hasFilamentInNozzle,
}: {
  count: number;
  activeIndex: number;
  displayIndex: number;
  nozzleState: AmsFilamentHotendSlotViewModel['filament_nozzle']['state'];
  nozzleColor?: string;
  hasFilamentInNozzle?: boolean;
}) {
  const safeCount = Math.max(1, count);
  const slotWidth = 80;
  const gap = 4;
  const totalWidth = safeCount * slotWidth + (safeCount - 1) * gap;
  const centers = Array.from({ length: safeCount }, (_, index) => index * (slotWidth + gap) + slotWidth / 2);
  const activeX = centers[Math.min(Math.max(activeIndex, 0), centers.length - 1)] ?? totalWidth / 2;
  const centerX = totalWidth / 2;
  const busY = 24;
  const nozzleTopY = 62;
  const height = 108;

  // Only loaded filament uses the active route color.
  const activeColor = nozzleState === 'Active'
    ? 'var(--ams-route-active)'
    : 'var(--ams-route-inactive)';

  return (
    <div className="ext-ams-route" style={{ width: totalWidth, ['--center-x' as string]: `${centerX}px` }}>
      <svg className="route-svg" width={totalWidth} height={height} viewBox={`0 0 ${totalWidth} ${height}`} aria-hidden="true">
        {centers.map((x) => (
          <line key={x} x1={x} y1={0} x2={x} y2={busY} stroke="var(--ams-route-inactive)" strokeWidth={3} />
        ))}
        {safeCount > 1 && (
          <line x1={centers[0]} y1={busY} x2={centers[centers.length - 1]} y2={busY} stroke="var(--ams-route-inactive)" strokeWidth={3} />
        )}
        {activeX !== centerX && (
          <line x1={activeX} y1={busY} x2={centerX} y2={busY} stroke={activeColor} strokeWidth={3} />
        )}
        <line x1={centerX} y1={busY} x2={centerX} y2={nozzleTopY} stroke={activeColor} strokeWidth={3} />
      </svg>
      <div className="route-nozzle">
        <Nozzle
          nozzleState={nozzleState}
          displayIndex={displayIndex}
          color={nozzleColor}
          hasFilamentInNozzle={hasFilamentInNozzle}
        />
      </div>
    </div>
  );
}
