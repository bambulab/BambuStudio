import { useCallback, useEffect, useMemo, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useDeviceBridge } from '../../../../hooks/Bridge';
import { Nozzle } from '../components/Nozzle';
import { SpoolDisc } from '../components/SpoolDisc';
import type { AmsFilamentHotendSlotViewModel, AmsFilamentHotendViewModel, BridgeResponseBody } from '../types';
import './ams-ext-operation.css';

type BindAms = {
  ams_id: string;
  ams_type: string;
  slot_count: number;
};

type SlotQuery = {
  nozzle_pos: string;
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
    nozzle_pos: params.get('nozzle_pos') ?? '',
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
  const [state, setState] = useState<AmsFilamentHotendViewModel | null>(null);
  const [selectedAmsId, setSelectedAmsId] = useState(query.bind_ams[0]?.ams_id ?? query.ams_id);

  const applyState = useCallback((payload?: AmsFilamentHotendViewModel) => {
    if (payload) setState(payload);
  }, []);

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

  const selectedSlot = useMemo(() => {
    const nozzlePos = Number(query.nozzle_pos);
    return state?.hotend_slots.find((slot) => slot.nozzle_pos === nozzlePos);
  }, [query.nozzle_pos, state]);

  const bindAms = selectedSlot?.bind_ams && selectedSlot.bind_ams.length > 0
    ? selectedSlot.bind_ams
    : query.bind_ams.length > 0
    ? query.bind_ams
    : [{ ams_id: query.ams_id || '0', ams_type: 'ZMULTI_A', slot_count: Number(query.count || 4) }];

  const amsUnits = bindAms.map((ams) => {
    const unit = state?.ams?.units.find((item) => item.ams_id === ams.ams_id);
    return unit ?? { ...ams, trays: [] };
  });

  useEffect(() => {
    if (!amsUnits.length) return;
    if (!selectedAmsId || !amsUnits.some((ams) => ams.ams_id === selectedAmsId)) {
      setSelectedAmsId(amsUnits[0].ams_id);
    }
  }, [amsUnits, selectedAmsId]);

  const selectedAms = amsUnits.find((ams) => ams.ams_id === selectedAmsId) ?? amsUnits[0];
  const selectedTrays = selectedAms?.trays ?? [];
  const displayTrays = selectedTrays.length > 0 ? selectedTrays : buildFallbackTrays();
  const activeTrayIndex = Math.max(0, displayTrays.findIndex((tray) => selectedAms?.ams_id === query.ams_id && tray.slot_id === query.slot_id));

  return (
    <div className="ext-ams-page">
      <header className="ext-ams-titlebar">
        <span className="window-dots" aria-hidden="true"><i /><i /><i /></span>
        <strong>{t('Ext AMS Operation')}</strong>
        <span className="window-spacer" aria-hidden="true" />
      </header>

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
                />
              ))}
            </div>
            <AmsRoute
              count={displayTrays.length}
              activeIndex={activeTrayIndex}
              displayIndex={Number(query.display_index) || 0}
              nozzleState={selectedSlot?.nozzle_state ?? 'Normal'}
            />
          </div>

          <p className="ext-ams-source">
            {t('Current hotend {{hotend}}, source AMS {{ams}} / slot {{slot}}, current AMS {{currentAms}}, total bound slots {{count}}', {
              hotend: query.display_index || '--',
              ams: query.ams_id || '--',
              slot: query.slot_id || '--',
              currentAms: selectedAms?.ams_id ?? '--',
              count: selectedTrays.length || Number(query.count) || 0,
            })}
          </p>
        </section>
      </main>
    </div>
  );
}

type ExtAmsTray = NonNullable<NonNullable<AmsFilamentHotendViewModel['ams']>['units'][number]['trays']>[number];

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
    nozzle_pos: index,
    display_index: Number(tray.slot_id) + 1 || index + 1,
    ams_id: tray.ams_id,
    slot_id: tray.slot_id,
    is_top_tier: false,
    enabled: tray.exists,
    card_state: state,
    line_state: 'no_filament',
    nozzle_state: 'Normal',
    filament_name: tray.filament_name,
    filament_type: tray.filament_type,
    color: tray.color || '#d9d9d9',
    color_type: tray.color_type,
    colors: tray.colors,
    status_text: '',
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
}: {
  index: number;
  slot: AmsFilamentHotendSlotViewModel;
}) {
  return (
    <div className="ext-ams-slot">
      <span className="slot-index">{index}</span>
      <SpoolDisc slot={slot} />
      <span className="slot-label">{trayLabel({
        ams_id: slot.ams_id,
        slot_id: slot.slot_id,
        exists: slot.enabled,
        tray_state: slot.card_state === 'Unknown' ? 'unknown' : slot.card_state === 'Empty' ? 'empty' : 'ready',
        filament_type: slot.filament_type,
        filament_name: slot.filament_name,
        color: slot.color,
      })}</span>
    </div>
  );
}

function AmsRoute({
  count,
  activeIndex,
  displayIndex,
  nozzleState,
}: {
  count: number;
  activeIndex: number;
  displayIndex: number;
  nozzleState: AmsFilamentHotendSlotViewModel['nozzle_state'];
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

  return (
    <div className="ext-ams-route" style={{ width: totalWidth, ['--center-x' as string]: `${centerX}px` }}>
      <svg className="route-svg" width={totalWidth} height={height} viewBox={`0 0 ${totalWidth} ${height}`} aria-hidden="true">
        {centers.map((x) => (
          <line key={x} x1={x} y1={0} x2={x} y2={busY} stroke="#c2c2c2" strokeWidth={3} />
        ))}
        {safeCount > 1 && (
          <line x1={centers[0]} y1={busY} x2={centers[centers.length - 1]} y2={busY} stroke="#c2c2c2" strokeWidth={3} />
        )}
        {activeX !== centerX && (
          <line x1={activeX} y1={busY} x2={centerX} y2={busY} stroke="#7a75ff" strokeWidth={3} />
        )}
        <line x1={centerX} y1={busY} x2={centerX} y2={nozzleTopY} stroke="#7a75ff" strokeWidth={3} />
      </svg>
      <div className="route-nozzle">
        <Nozzle nozzleState={nozzleState} displayIndex={displayIndex} />
      </div>
    </div>
  );
}
