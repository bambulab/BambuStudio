import { useEffect, useId, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { documentZoom, useFitHostWidth } from '../../../hooks/useFitHostWidth';
import { useSendToCpp } from '../../../hooks/Bridge';
import { useDeviceWebTheme } from '../../../hooks/useDeviceWebTheme';
import { useControlPanelBridge } from './useControlPanelBridge';
import type {
  BedTempArea,
  ChamberTempArea,
  DualExtruderView,
  ExtruderId,
  ExtruderTempItem,
  RackControl,
  RackToolheadVisualState,
  SingleExtruderView,
  SingleRackExtruderView,
  TempInputControl,
} from './types';
import nozzleIconUrl from '../../../../../../../../../resources/images/monitor_nozzle_temp.svg';
import nozzleActiveIconUrl from '../../../../../../../../../resources/images/monitor_nozzle_temp_active.svg';
import bedIconUrl from '../../../../../../../../../resources/images/monitor_bed_temp.svg';
import bedActiveIconUrl from '../../../../../../../../../resources/images/monitor_bed_temp_active.svg';
import chamberIconUrl from '../../../../../../../../../resources/images/monitor_frame_temp.svg';
import chamberActiveIconUrl from '../../../../../../../../../resources/images/monitor_frame_temp_active.svg';
import fanOffIconUrl from '../../../../../../../../../resources/images/monitor_fan_off.svg';
import speedIconUrl from '../../../../../../../../../resources/images/monitor_speed.svg';
import speedActiveIconUrl from '../../../../../../../../../resources/images/monitor_speed_active.svg';
import lampOnIconUrl from '../../../../../../../../../resources/images/monitor_lamp_on.svg';
import lampOffIconUrl from '../../../../../../../../../resources/images/monitor_lamp_off.svg';
import degreeIconUrl from '../../../../../../../../../resources/images/degree.svg';
import axisHomeIconUrl from '../../../../../../../../../resources/images/monitor_axis_home.svg';
import bedUpIconUrl from '../../../../../../../../../resources/images/monitor_bed_up.svg';
import bedUpDisableIconUrl from '../../../../../../../../../resources/images/monitor_bed_up_disable.svg';
import bedDownIconUrl from '../../../../../../../../../resources/images/monitor_bed_down.svg';
import bedDownDisableIconUrl from '../../../../../../../../../resources/images/monitor_bed_down_disable.svg';
import monitorAmsExtruderIconUrl from '../../../../../../../../../resources/images/monitor_ams_extruder.svg';
import singleExtruderFilledLoadUrl from '../../../../../../../../../resources/images/monitor_extruder_filled_load.png';
import singleExtruderFilledUnloadUrl from '../../../../../../../../../resources/images/monitor_extruder_filled_unload.png';
import singleExtruderEmptyLoadUrl from '../../../../../../../../../resources/images/monitor_extruder_empty_load.png';
import singleExtruderEmptyUnloadUrl from '../../../../../../../../../resources/images/monitor_extruder_empty_unload.png';
import pipeFilledLoadUrl from '../../../../../../../../../resources/images/pipe_of_loading_selected.svg';
import leftExtruderActiveFilledUrl from '../../../../../../../../../resources/images/left_extruder_active_filled.svg';
import leftExtruderActiveEmptyUrl from '../../../../../../../../../resources/images/left_extruder_active_empty.svg';
import leftExtruderUnactiveFilledUrl from '../../../../../../../../../resources/images/left_extruder_unactive_filled.svg';
import leftExtruderUnactiveEmptyUrl from '../../../../../../../../../resources/images/left_extruder_unactive_empty.svg';
import rightExtruderActiveFilledUrl from '../../../../../../../../../resources/images/right_extruder_active_filled.svg';
import rightExtruderActiveEmptyUrl from '../../../../../../../../../resources/images/right_extruder_active_empty.svg';
import rightExtruderUnactiveFilledUrl from '../../../../../../../../../resources/images/right_extruder_unactive_filled.svg';
import rightExtruderUnactiveEmptyUrl from '../../../../../../../../../resources/images/right_extruder_unactive_empty.svg';
import rackLiftIconUrl from '../../../../../../../../../resources/images/monitor_hotend_rack_lift.svg';
import rackCenterIconUrl from '../../../../../../../../../resources/images/monitor_hotend_rack_center.svg';
import rackLowerIconUrl from '../../../../../../../../../resources/images/monitor_hotend_rack_lower.svg';
import extruderUpIconUrl from '../../../../../../../../../resources/images/dev_rack_extruder_up.svg';
import extruderUpDisableIconUrl from '../../../../../../../../../resources/images/dev_rack_extruder_up_disable.svg';
import extruderDownIconUrl from '../../../../../../../../../resources/images/dev_rack_extruder_down.svg';
import extruderDownDisableIconUrl from '../../../../../../../../../resources/images/dev_rack_extruder_down_disable.svg';
import legacyExtruderUpIconUrl from '../../../../../../../../../resources/images/monitor_extruder_up.svg';
import legacyExtruderUpDisableIconUrl from '../../../../../../../../../resources/images/monitor_extruder_up_disable.svg';
import legacyExtruderDownIconUrl from '../../../../../../../../../resources/images/monitor_extrduer_down.svg';
import legacyExtruderDownDisableIconUrl from '../../../../../../../../../resources/images/monitor_extrduer_down_disable.svg';
import rackToolheadClampedUrl from '../../../../../../../../../resources/images/dev_rack_toolhead_clamped.png';
import rackToolheadClampedNoFilamentUrl from '../../../../../../../../../resources/images/dev_rack_toolhead_clamped_no_filament.png';
import rackToolheadEmptyUrl from '../../../../../../../../../resources/images/dev_rack_toolhead_unclamped.png';
import rackToolheadPickingUrl from '../../../../../../../../../resources/images/dev_rack_toolhead_picking.png';
import rackToolheadPlacingUrl from '../../../../../../../../../resources/images/dev_rack_toolhead_placing.png';
import rackToolheadCutterUrl from '../../../../../../../../../resources/images/dev_rack_toolhead_cutter.png';
import rackToolheadPenUrl from '../../../../../../../../../resources/images/dev_rack_toolhead_pen.png';
import rackToolheadDefaultUrl from '../../../../../../../../../resources/images/dev_rack_toolhead.png';
import './control-panel.css';

const HOST_WIDTH_DIP = 586;
const HOST_LAYOUT_MODULE = 'device_host';

const icons = {
  nozzle: nozzleIconUrl,
  nozzleActive: nozzleActiveIconUrl,
  bed: bedIconUrl,
  bedActive: bedActiveIconUrl,
  chamber: chamberIconUrl,
  chamberActive: chamberActiveIconUrl,
  fanOff: fanOffIconUrl,
  speed: speedIconUrl,
  speedActive: speedActiveIconUrl,
  lampOn: lampOnIconUrl,
  lampOff: lampOffIconUrl,
  degree: degreeIconUrl,
  axisHome: axisHomeIconUrl,
  bedUp: bedUpIconUrl,
  bedUpDisable: bedUpDisableIconUrl,
  bedDown: bedDownIconUrl,
  bedDownDisable: bedDownDisableIconUrl,
  monitorAmsExtruder: monitorAmsExtruderIconUrl,
  singleExtruderFilledLoad: singleExtruderFilledLoadUrl,
  singleExtruderFilledUnload: singleExtruderFilledUnloadUrl,
  singleExtruderEmptyLoad: singleExtruderEmptyLoadUrl,
  singleExtruderEmptyUnload: singleExtruderEmptyUnloadUrl,
  pipeFilledLoad: pipeFilledLoadUrl,
  leftExtruderActiveFilled: leftExtruderActiveFilledUrl,
  leftExtruderActiveEmpty: leftExtruderActiveEmptyUrl,
  leftExtruderUnactiveFilled: leftExtruderUnactiveFilledUrl,
  leftExtruderUnactiveEmpty: leftExtruderUnactiveEmptyUrl,
  rightExtruderActiveFilled: rightExtruderActiveFilledUrl,
  rightExtruderActiveEmpty: rightExtruderActiveEmptyUrl,
  rightExtruderUnactiveFilled: rightExtruderUnactiveFilledUrl,
  rightExtruderUnactiveEmpty: rightExtruderUnactiveEmptyUrl,
  rackLift: rackLiftIconUrl,
  rackCenter: rackCenterIconUrl,
  rackLower: rackLowerIconUrl,
  extruderUp: extruderUpIconUrl,
  extruderUpDisable: extruderUpDisableIconUrl,
  extruderDown: extruderDownIconUrl,
  extruderDownDisable: extruderDownDisableIconUrl,
  legacyExtruderUp: legacyExtruderUpIconUrl,
  legacyExtruderUpDisable: legacyExtruderUpDisableIconUrl,
  legacyExtruderDown: legacyExtruderDownIconUrl,
  legacyExtruderDownDisable: legacyExtruderDownDisableIconUrl,
  rackToolheadClamped: rackToolheadClampedUrl,
  rackToolheadClampedNoFilament: rackToolheadClampedNoFilamentUrl,
  rackToolheadEmpty: rackToolheadEmptyUrl,
  rackToolheadPicking: rackToolheadPickingUrl,
  rackToolheadPlacing: rackToolheadPlacingUrl,
  rackToolheadCutter: rackToolheadCutterUrl,
  rackToolheadPen: rackToolheadPenUrl,
  rackToolheadDefault: rackToolheadDefaultUrl,
};

const extruderIconImages: Record<string, string> = {
  monitor_ams_extruder: icons.singleExtruderFilledLoad,
  monitor_extruder_filled_load: icons.singleExtruderFilledLoad,
  monitor_extruder_filled_unload: icons.singleExtruderFilledUnload,
  monitor_extruder_empty_load: icons.singleExtruderEmptyLoad,
  monitor_extruder_empty_unload: icons.singleExtruderEmptyUnload,
  left_extruder_active_filled: icons.leftExtruderActiveFilled,
  left_extruder_active_empty: icons.leftExtruderActiveEmpty,
  left_extruder_unactive_filled: icons.leftExtruderUnactiveFilled,
  left_extruder_unactive_empty: icons.leftExtruderUnactiveEmpty,
  right_extruder_active_filled: icons.rightExtruderActiveFilled,
  right_extruder_active_empty: icons.rightExtruderActiveEmpty,
  right_extruder_unactive_filled: icons.rightExtruderUnactiveFilled,
  right_extruder_unactive_empty: icons.rightExtruderUnactiveEmpty,
};

const rackToolheadVisualImages: Record<RackToolheadVisualState, string> = {
  empty: icons.rackToolheadEmpty,
  clamped: icons.rackToolheadClamped,
  clamped_retracting: icons.rackToolheadClamped,
  clamped_extruding: icons.rackToolheadClamped,
  picking: icons.rackToolheadPicking,
  placing: icons.rackToolheadPlacing,
  clamped_no_filament: icons.rackToolheadClampedNoFilament,
  cutter: icons.rackToolheadCutter,
  pen: icons.rackToolheadPen,
  default: icons.rackToolheadDefault,
};

function useReportHostSize(ref: React.RefObject<HTMLDivElement | null>) {
  const send = useSendToCpp();
  const lastRef = useRef('');

  useEffect(() => {
    const html = document.documentElement;
    const body = document.body;
    const prevHtmlOverflow = html.style.overflow;
    const prevBodyOverflow = body.style.overflow;
    html.style.overflow = 'hidden';
    body.style.overflow = 'hidden';
    return () => {
      html.style.overflow = prevHtmlOverflow;
      body.style.overflow = prevBodyOverflow;
    };
  }, []);

  useEffect(() => {
    const node = ref.current;
    if (!node) return;
    const report = () => {
      const height = Math.ceil(node.offsetHeight || node.getBoundingClientRect().height);
      if (height <= 0) return;
      const key = `${HOST_WIDTH_DIP}x${height}`;
      if (key === lastRef.current) return;
      lastRef.current = key;
      send(
        { version: '1.0', type: 'request', seq: 1_100_000, ts: Date.now() },
        {
          module: HOST_LAYOUT_MODULE,
          submod: 'layout',
          action: 'content_size',
          payload: { width: HOST_WIDTH_DIP, height },
        },
      );
    };
    report();
    if (typeof ResizeObserver !== 'function') return;
    const observer = new ResizeObserver(report);
    observer.observe(node);
    return () => observer.disconnect();
  }, [ref, send]);
}

function formatTemp(value: number | null) {
  return value === null ? '_' : String(value);
}

// Keep all C++-originated English source keys registered for `pnpm run i18n`
// so `i18next-parser` retains them in `locales/*.json` and `t(dynamicValue)`
// translates every status, label, and tooltip regardless of whether C++ `.mo`
// already contains the string.
function useControlPanelI18nCatalog() {
  const { t } = useTranslation();
  void [
    t('Running'),
    t('Running...'),
    t('Reading'),
    t('Raised'),
    t('Lowered'),
    t('Centered'),
    t('Hotend Rack'),
    t('Lift Hotend Rack'),
    t('Recenter Hotend Rack'),
    t('Lower Hotend Rack'),
    t('Unclamped'),
    t('Extruder'),
    t('Bed'),
    t('Fan'),
    t('Air Management'),
    t('Lamp'),
    t('Home'),
    t('Left'),
    t('Right'),
    t('Main'),
    t('Auxiliary'),
    t('Switching...'),
    t('Switching failed'),
    t('Quit', { context: 'Quit_Switching' }),
    t('Retry'),
    t('Silent'),
    t('Standard'),
    t('Sport'),
    t('Ludicrous'),
    t('This only takes effect during printing'),
    t('Please select a printer'),
    t('This control is currently unavailable.'),
    t('Chamber temperature is not editable.'),
    t('High Flow'),
    t('TPU High Flow'),
    t('TPU285'),
    t('E3D High Flow'),
    t('Unknown'),
    t('Error'),
    t('Pen'),
    t('Cutter'),
    t('Laser'),
    t('Debug'),
    t('Opening...'),
  ];
}

function TempInput({
  item,
  action,
  onCommit,
}: {
  item: ExtruderTempItem | ChamberTempArea | BedTempArea;
  action: TempInputControl;
  onCommit: (value: number) => void;
}) {
  const { t } = useTranslation();
  const externalText = item.target === null ? '_' : String(item.target);
  const [text, setText] = useState(externalText);
  const [focused, setFocused] = useState(false);
  const [warningText, setWarningText] = useState('');
  const skipNextBlurCommitRef = useRef(false);

  useEffect(() => {
    if (!focused) {
      setText(externalText);
    }
  }, [externalText, focused]);

  const disabled = !item.editable || !action.enabled;
  const disabledTip = disabled
    ? t(action.disabled_reason || item.read_only_reason)
    : '';

  const commitOnBlur = (raw: string) => {
    const trimmed = raw.trim();
    if (!trimmed || trimmed === '_') {
      setText(externalText);
      return;
    }
    if (!/^\d+$/.test(trimmed)) {
      setText(externalText);
      return;
    }
    let num = Number.parseInt(trimmed, 10);
    if (num > item.max) {
      num = item.max;
    }
    setText(String(num));
    onCommit(num);
  };

  const commitOnEnter = (raw: string, inputEl: HTMLInputElement) => {
    const trimmed = raw.trim();
    if (!trimmed || !/^\d+$/.test(trimmed)) {
      return;
    }
    const num = Number.parseInt(trimmed, 10);
    if (num !== 0) {
      if (num > item.max) {
        setWarningText(
          action.max_warning
            ? t(action.max_warning)
            : `${t('The maximum temperature cannot exceed ')}${item.max}`,
        );
        return;
      }
      if (num < item.min) {
        setWarningText(
          action.min_warning
            ? t(action.min_warning)
            : `${t('The minmum temperature should not be less than ')}${item.min}`,
        );
        return;
      }
    }
    setWarningText('');
    onCommit(num);
    skipNextBlurCommitRef.current = true;
    inputEl.blur();
  };

  return (
    <div
      className={`cp-temp-input ${disabled ? 'disabled' : ''} ${warningText ? 'warning' : ''}`}
      title={disabledTip}
    >
      <div className="cp-temp-values">
        <span className="cp-temp-current">{formatTemp(item.current)}</span>
        <span className="cp-temp-sep">/</span>
        {!item.editable ? (
          <span className="cp-temp-target-readonly">{externalText}</span>
        ) : (
          <input
            aria-label={t('Set Temperature')}
            value={text}
            maxLength={3}
            inputMode="numeric"
            disabled={disabled}
            onFocus={() => {
              setFocused(true);
              setWarningText('');
              if (text === '_') {
                setText('');
              }
            }}
            onChange={(e) => {
              const digitsOnly = e.currentTarget.value.replace(/\D+/g, '').slice(0, 3);
              setText(digitsOnly);
              if (warningText) {
                setWarningText('');
              }
            }}
            onBlur={(e) => {
              setFocused(false);
              setWarningText('');
              if (skipNextBlurCommitRef.current) {
                skipNextBlurCommitRef.current = false;
                return;
              }
              if (!disabled) {
                commitOnBlur(e.currentTarget.value);
              }
            }}
            onKeyDown={(e) => {
              if (e.key === 'Enter' && !disabled) {
                e.preventDefault();
                commitOnEnter(e.currentTarget.value, e.currentTarget);
              }
            }}
          />
        )}
        <img className="cp-temp-degree" src={icons.degree} alt="°C" />
      </div>
      {warningText ? (
        <div className="cp-temp-warning-popup" role="alert">
          {warningText}
        </div>
      ) : null}
    </div>
  );
}

function ExtruderTempRow({
  item,
  action,
  onCommit,
}: {
  item: ExtruderTempItem;
  action: TempInputControl;
  onCommit: (id: ExtruderId, value: number) => void;
}) {
  const { t } = useTranslation();
  if (!item.visible) return null;
  const icon = item.temp_status === 'heating' ? icons.nozzleActive : icons.nozzle;
  return (
    <div className="cp-row cp-temp-row" data-testid={`control-panel-extruder-temp-${item.id}`}>
      <div className="cp-row-label">
        {item.badge ? <span className="cp-nozzle-badge">{item.badge_nozzle_index}</span> : null}
        <img className={`cp-temp-icon ${item.temp_status === 'heating' ? 'active' : ''}`} src={icon} alt="" />
        {item.side_badge ? <span className="cp-side-badge">{item.side_badge}</span> : null}
      </div>
      {item.show_nozzle ? (
        <TempInput item={item} action={action} onCommit={(value) => onCommit(item.id, value)} />
      ) : (
        <div className="cp-unclamped">{t(item.read_only_reason || 'Unclamped')}</div>
      )}
    </div>
  );
}

function TemperatureSection(bridge: ReturnType<typeof useControlPanelBridge>) {
  const {
    state,
    setExtruderTemp,
    setChamberTemp,
    setBedTemp,
  } = bridge;
  const bedHeating = state.display.bed_temp.temp_status === 'heating';
  const chamberHeating = state.display.chamber_temp.temp_status === 'heating';
  const bedIcon = bedHeating ? icons.bedActive : icons.bed;
  const chamberIcon = chamberHeating ? icons.chamberActive : icons.chamber;

  return (
    <section className="cp-section cp-temperature">
      <div className="cp-temp-list">
        {state.display.extruder_temp.extruders.map((item) => (
          <ExtruderTempRow
            key={item.id}
            item={item}
            action={item.temp_input}
            onCommit={setExtruderTemp}
          />
        ))}
        {state.display.bed_temp.visible ? (
          <div className="cp-row" data-testid="control-panel-bed-temp">
            <div className="cp-row-label">
              <img className={`cp-temp-icon ${bedHeating ? 'active' : ''}`} src={bedIcon} alt="" />
            </div>
            <TempInput item={state.display.bed_temp} action={state.display.bed_temp.temp_input} onCommit={setBedTemp} />
          </div>
        ) : null}
        {state.display.chamber_temp.visible ? (
          <div className="cp-row" data-testid="control-panel-chamber-temp">
            <div className="cp-row-label">
              <img className={`cp-temp-icon ${chamberHeating ? 'active' : ''}`} src={chamberIcon} alt="" />
            </div>
            <TempInput item={state.display.chamber_temp} action={state.display.chamber_temp.temp_input} onCommit={setChamberTemp} />
          </div>
        ) : null}
      </div>
      <MiscSection {...bridge} />
    </section>
  );
}

function resolveElementAnchorDip(el: HTMLElement) {
  const rect = el.getBoundingClientRect();
  const zoom = documentZoom();
  return {
    x: Math.round(rect.left / zoom),
    y: Math.round(rect.top / zoom),
    height: Math.round(rect.height / zoom),
  };
}

function MiscSection({ state, openSpeed, toggleLamp, openFan }: ReturnType<typeof useControlPanelBridge>) {
  const { t } = useTranslation();
  const speedCtrl = state.display.speed.speed_control;
  const fanLabel = t(state.display.fan.fan_label || 'Fan');
  const lampLabel = t(state.display.lamp.label || 'Lamp');

  return (
    <div className="cp-misc">
      {state.display.fan.fan_visible ? (
        <button
          type="button"
          data-testid="control-panel-fan"
          className="cp-fan-button"
          disabled={!state.display.fan.fan_control.openable}
          title={state.display.fan.fan_control.disabled_reason ? t(state.display.fan.fan_control.disabled_reason) : fanLabel}
          onClick={(e) => {
            void openFan(resolveElementAnchorDip(e.currentTarget));
          }}
        >
          <img src={icons.fanOff} alt="" />
          <span className={fanLabel.length > 6 ? 'cp-fan-label-compact' : ''}>
            {fanLabel}
          </span>
        </button>
      ) : null}
      <div className="cp-misc-bottom">
        {state.display.speed.visible ? (
          <button
            type="button"
            data-testid="control-panel-speed"
            className="cp-speed-button"
            disabled={!speedCtrl.selectable}
            title={speedCtrl.disabled_reason ? t(speedCtrl.disabled_reason) : state.display.speed.speed_text}
            onClick={(e) => {
              void openSpeed(resolveElementAnchorDip(e.currentTarget));
            }}
          >
            <img
              className={speedCtrl.selectable ? 'cp-speed-icon-active' : 'cp-speed-icon-idle'}
              src={speedCtrl.selectable ? icons.speedActive : icons.speed}
              alt=""
            />
            <strong>{state.display.speed.speed_text}</strong>
          </button>
        ) : null}
        {state.display.lamp.visible ? (
          <button
            type="button"
            data-testid="control-panel-lamp"
            className={`cp-lamp-button ${state.display.lamp.lamp_on ? 'active' : ''}`}
            disabled={!state.display.lamp.lamp_control.switchable}
            title={state.display.lamp.lamp_control.disabled_reason ? t(state.display.lamp.lamp_control.disabled_reason) : lampLabel}
            onClick={() => {
              void toggleLamp(!state.display.lamp.lamp_on);
            }}
          >
            <img src={state.display.lamp.lamp_on ? icons.lampOn : icons.lampOff} alt="" />
            <span>{lampLabel}</span>
          </button>
        ) : null}
      </div>
    </div>
  );
}

interface SectorDef {
  op: string;
  rIn: number;
  rOut: number;
  startDeg: number;
  endDeg: number;
  isInner: boolean;
}

const DIAL_HALF_GAP = 2 * Math.SQRT2;

const DIAL_SECTORS: SectorDef[] = [
  { op: 'y_pos_10', rIn: 58, rOut: 105, startDeg: -135, endDeg: -45, isInner: false },
  { op: 'x_pos_10', rIn: 58, rOut: 105, startDeg: -45, endDeg: 45, isInner: false },
  { op: 'y_neg_10', rIn: 58, rOut: 105, startDeg: 45, endDeg: 135, isInner: false },
  { op: 'x_neg_10', rIn: 58, rOut: 105, startDeg: 135, endDeg: 225, isInner: false },
  { op: 'y_pos_1', rIn: 25, rOut: 58, startDeg: -135, endDeg: -45, isInner: true },
  { op: 'x_pos_1', rIn: 25, rOut: 58, startDeg: -45, endDeg: 45, isInner: true },
  { op: 'y_neg_1', rIn: 25, rOut: 58, startDeg: 45, endDeg: 135, isInner: true },
  { op: 'x_neg_1', rIn: 25, rOut: 58, startDeg: 135, endDeg: 225, isInner: true },
];

function polarToCartesian(r: number, deg: number) {
  const rad = (deg * Math.PI) / 180;
  return { x: r * Math.cos(rad), y: r * Math.sin(rad) };
}

function buildAnnularSectorPath(
  rIn: number,
  rOut: number,
  startDeg: number,
  endDeg: number,
  halfGap = DIAL_HALF_GAP,
) {
  const outDeltaDeg = (Math.asin(Math.min(0.99, halfGap / rOut)) * 180) / Math.PI;
  const inDeltaDeg = (Math.asin(Math.min(0.99, halfGap / rIn)) * 180) / Math.PI;
  const p1 = polarToCartesian(rOut, startDeg + outDeltaDeg);
  const p2 = polarToCartesian(rOut, endDeg - outDeltaDeg);
  const p3 = polarToCartesian(rIn, endDeg - inDeltaDeg);
  const p4 = polarToCartesian(rIn, startDeg + inDeltaDeg);
  return [
    `M ${p1.x.toFixed(2)} ${p1.y.toFixed(2)}`,
    `A ${rOut} ${rOut} 0 0 1 ${p2.x.toFixed(2)} ${p2.y.toFixed(2)}`,
    `L ${p3.x.toFixed(2)} ${p3.y.toFixed(2)}`,
    `A ${rIn} ${rIn} 0 0 0 ${p4.x.toFixed(2)} ${p4.y.toFixed(2)}`,
    'Z',
  ].join(' ');
}

function AxisDial({
  axis,
  onMove,
}: {
  axis: ReturnType<typeof useControlPanelBridge>['state']['display']['motion']['axis_moves'];
  onMove: (op: string) => void;
}) {
  const { t } = useTranslation();
  const [hoveredOp, setHoveredOp] = useState<string | null>(null);
  const homeEnabled = Boolean(axis.xy_home?.movable);
  const anyEnabled = homeEnabled || DIAL_SECTORS.some((sec) => Boolean(axis[sec.op]?.movable));
  const hoveredSector = DIAL_SECTORS.find(
    (sec) => sec.op === hoveredOp && Boolean(axis[sec.op]?.movable),
  );
  const hoveredSectorPath = hoveredSector
    ? buildAnnularSectorPath(
        hoveredSector.rIn,
        hoveredSector.rOut,
        hoveredSector.startDeg,
        hoveredSector.endDeg,
      )
    : null;

  return (
    <div className="cp-axis-dial" data-testid="control-panel-xy">
      <svg viewBox="-108 -108 216 216" className="cp-axis-svg">
        {DIAL_SECTORS.map((sec) => {
          const enabled = Boolean(axis[sec.op]?.movable);
          const d = buildAnnularSectorPath(sec.rIn, sec.rOut, sec.startDeg, sec.endDeg);
          const tip = axis[sec.op]?.disabled_reason ? t(axis[sec.op]?.disabled_reason ?? '') : '';
          return (
            <path
              key={sec.op}
              d={d}
              className={`cp-dial-sector ${sec.isInner ? 'inner' : 'outer'} ${!enabled ? 'disabled' : ''}`}
              onMouseEnter={() => setHoveredOp(sec.op)}
              onMouseLeave={() => setHoveredOp((prev) => (prev === sec.op ? null : prev))}
              onClick={() => {
                if (enabled) onMove(sec.op);
              }}
            >
              {tip ? <title>{tip}</title> : null}
            </path>
          );
        })}

        {hoveredSectorPath ? (
          <path
            d={hoveredSectorPath}
            className="cp-dial-sector-hover-outline"
            pointerEvents="none"
          />
        ) : null}

        {/* Outer ring axis labels */}
        <g className={`cp-dial-axis-labels ${!anyEnabled ? 'disabled' : ''}`} pointerEvents="none">
          <text x="0" y="-81.5" textAnchor="middle" dominantBaseline="central">Y</text>
          <text x="-81.5" y="0" textAnchor="middle" dominantBaseline="central">-X</text>
          <text x="0" y="81.5" textAnchor="middle" dominantBaseline="central">-Y</text>
          <text x="81.5" y="0" textAnchor="middle" dominantBaseline="central">X</text>
        </g>

        {/* Diagonal step labels (+10, +1, -1, -10) rotated 45 deg matching AxisCtrlButton */}
        <g className="cp-dial-step-labels" transform="rotate(45)" pointerEvents="none">
          <text x="6" y="-81.5" textAnchor="start" dominantBaseline="central">+10</text>
          <text x="6" y="-41.5" textAnchor="start" dominantBaseline="central">+1</text>
          <text x="6" y="41.5" textAnchor="start" dominantBaseline="central">-1</text>
          <text x="6" y="81.5" textAnchor="start" dominantBaseline="central">-10</text>
        </g>

        {/* Center Home circle */}
        <circle
          cx="0"
          cy="0"
          r="22.5"
          className={`cp-dial-home-circle ${hoveredOp === 'xy_home' && homeEnabled ? 'hovered' : ''}`}
          pointerEvents="none"
        />
      </svg>

      <button
        type="button"
        aria-label="Home"
        className="cp-dial-home-btn"
        disabled={!homeEnabled}
        title={axis.xy_home?.disabled_reason ? t(axis.xy_home.disabled_reason) : t('Home')}
        onMouseEnter={() => setHoveredOp('xy_home')}
        onMouseLeave={() => setHoveredOp((prev) => (prev === 'xy_home' ? null : prev))}
        onClick={() => {
          if (homeEnabled) onMove('xy_home');
        }}
      >
        <img src={icons.axisHome} alt="Home" />
      </button>
    </div>
  );
}

function MotionSection({ state, axisMove, openBridgeDebugDialog }: ReturnType<typeof useControlPanelBridge>) {
  const { t } = useTranslation();
  const axis = state.display.motion.axis_moves;
  const rack = state.display.rack;
  const [debugOpening, setDebugOpening] = useState(false);
  const showDebugButton = Boolean(
    (window as Window & { __debugBuild?: boolean }).__debugBuild,
  );
  const zEnabled = Boolean(
    axis.z_up_10?.movable || axis.z_up_1?.movable || axis.z_down_1?.movable || axis.z_down_10?.movable,
  );
  const rackAnyEnabled = Boolean(rack.lift.movable || rack.center.movable || rack.lower.movable);

  const zButton = (op: string, label: string, icon: string, disabledIcon: string, tone: 'light' | 'dark') => {
    const enabled = Boolean(axis[op]?.movable);
    return (
      <button
        type="button"
        className={`cp-step-btn ${tone}`}
        disabled={!enabled}
        title={axis[op]?.disabled_reason ? t(axis[op]?.disabled_reason ?? '') : ''}
        onClick={() => {
          void axisMove(op);
        }}
      >
        <img className={enabled ? 'cp-step-icon-enabled' : ''} src={enabled ? icon : disabledIcon} alt="" />
        <span>{label}</span>
      </button>
    );
  };

  const rackButton = (op: string, ctrl: RackControl, icon: string, tone: 'light' | 'dark') => {
    const enabled = Boolean(ctrl.movable);
    const hasLabel = Boolean(ctrl.label);
    const tipKey = ctrl.tooltip || ctrl.disabled_reason || '';
    return (
      <button
        type="button"
        className={`cp-rack-btn ${tone} ${hasLabel ? 'has-label' : ''}`}
        disabled={!enabled}
        title={tipKey ? t(tipKey) : ''}
        onClick={() => {
          if (enabled) {
            void axisMove(op);
          }
        }}
      >
        {hasLabel ? <span>{t(ctrl.label)}</span> : <img src={icon} alt="" />}
      </button>
    );
  };

  return (
    <section className="cp-section cp-motion">
      {state.display.motion.xy_visible ? (
        <AxisDial axis={axis} onMove={(op) => { void axisMove(op); }} />
      ) : null}
      {showDebugButton ? (
        <button
          type="button"
          data-testid="control-panel-debug"
          disabled={debugOpening}
          className="cp-debug-btn"
          onClick={async () => {
            setDebugOpening(true);
            try {
              await openBridgeDebugDialog(state);
            } finally {
              setDebugOpening(false);
            }
          }}
        >
          {debugOpening ? t('Opening...') : t('Debug')}
        </button>
      ) : null}
      {state.display.motion.z_visible ? (
        <div className="cp-z-stack" data-testid="control-panel-z">
          <div className="cp-z-row">
            {zButton('z_up_10', '10', icons.bedUp, icons.bedUpDisable, 'light')}
            {zButton('z_up_1', '1', icons.bedUp, icons.bedUpDisable, 'dark')}
            <span className={`cp-z-label ${!zEnabled ? 'disabled' : ''}`}>
              {t(state.display.motion.z_label || 'Bed')}
            </span>
            {zButton('z_down_1', '1', icons.bedDown, icons.bedDownDisable, 'dark')}
            {zButton('z_down_10', '10', icons.bedDown, icons.bedDownDisable, 'light')}
          </div>
          {rack.visible ? (
            <div className="cp-rack-row" data-testid="control-panel-rack">
              <span className={`cp-rack-title ${!rackAnyEnabled ? 'disabled' : ''}`}>
                {t(rack.title || 'Hotend Rack')}
              </span>
              <div className="cp-rack-buttons">
                {rackButton('rack_lift', rack.lift, icons.rackLift, 'light')}
                {rackButton('rack_center', rack.center, icons.rackCenter, 'dark')}
                {rackButton('rack_lower', rack.lower, icons.rackLower, 'light')}
              </div>
            </div>
          ) : null}
        </div>
      ) : null}
    </section>
  );
}

function ToolheadVisual({
  showFilamentPath,
  upperFilamentColor,
  lowerFilamentColor,
  extruderStatus,
  visualState,
}: {
  showFilamentPath: boolean;
  upperFilamentColor: string;
  lowerFilamentColor: string;
  extruderStatus?: string;
  visualState: RackToolheadVisualState;
}) {
  const gradId = useId();
  const clipId = useId();
  const isDown = extruderStatus === 'loading';
  const isUp = extruderStatus === 'unloading';
  const showArrows = isDown || isUp;

  const arrowBands: number[] = [];
  for (let band = 2; band + 8 <= 101; band += 12.5) {
    arrowBands.push(band);
  }

  return (
    <div className="cp-toolhead-art">
      <img
        className="cp-rack-toolhead"
        src={rackToolheadVisualImages[visualState]}
        alt=""
      />
      {showFilamentPath ? (
        <svg className="cp-toolhead-overlay" viewBox="0 0 72 110" aria-hidden="true">
          <defs>
            <clipPath id={clipId}>
              <rect x="33" y="2" width="8" height="99" />
            </clipPath>
            <linearGradient id={gradId} x1="0" y1={isDown ? '0' : '1'} x2="0" y2={isDown ? '1' : '0'}>
              <stop offset="0%" stopColor="#FFFFFF" stopOpacity="0" />
              <stop offset="100%" stopColor="#FFFFFF" stopOpacity="1" />
            </linearGradient>
          </defs>
          {upperFilamentColor ? (
            <rect x="35" y="2" width="4" height="57" fill={upperFilamentColor} />
          ) : null}
          {lowerFilamentColor ? (
            <rect x="35" y="59" width="4" height="42" fill={lowerFilamentColor} />
          ) : null}
          {showArrows ? (
            <g clipPath={`url(#${clipId})`}>
              {arrowBands.map((band) => {
                const base = isDown ? band : band + 8;
                const tip = isDown ? band + 8 : band;
                return (
                  <polygon
                    key={band}
                    points={`33,${base} 41,${base} 37,${tip}`}
                    fill={`url(#${gradId})`}
                  />
                );
              })}
            </g>
          ) : null}
        </svg>
      ) : null}
    </div>
  );
}

function SingleRackExtruderBody({
  view,
  axis,
  axisMove,
}: {
  view: SingleRackExtruderView;
  axis: ReturnType<typeof useControlPanelBridge>['state']['display']['motion']['axis_moves'];
  axisMove: ReturnType<typeof useControlPanelBridge>['axisMove'];
}) {
  const { t } = useTranslation();
  const eUpEnabled = Boolean(axis.e_up_10?.movable);
  const eDownEnabled = Boolean(axis.e_down_10?.movable);
  const flowOnly = view.current_nozzle_id === null && !view.nozzle_diameter;

  return (
    <div className="cp-extruder-body cp-rack-extruder-body">
      <button
        type="button"
        className="cp-extruder-arrow"
        disabled={!eUpEnabled}
        title={axis.e_up_10?.disabled_reason ? t(axis.e_up_10.disabled_reason) : ''}
        onClick={() => {
          void axisMove('e_up_10');
        }}
      >
        <img src={eUpEnabled ? icons.extruderUp : icons.extruderUpDisable} alt="E+" />
      </button>

      <div className="cp-nozzle-info">
        {view.current_nozzle_id !== null ? (
          <b className="cp-nozzle-index-circle">{view.current_nozzle_id}</b>
        ) : null}
        {view.nozzle_diameter ? <span>{view.nozzle_diameter}</span> : null}
        {view.nozzle_flow_type ? (
          <span className={flowOnly ? 'cp-nozzle-flow-standalone' : ''}>{t(view.nozzle_flow_type)}</span>
        ) : null}
      </div>

      <ToolheadVisual
        showFilamentPath={view.show_filament_path}
        upperFilamentColor={view.upper_filament_clr}
        lowerFilamentColor={view.lower_filament_clr}
        extruderStatus={view.extruder_status}
        visualState={view.rack_toolhead_visual_state}
      />

      <button
        type="button"
        className="cp-extruder-arrow"
        disabled={!eDownEnabled}
        title={axis.e_down_10?.disabled_reason ? t(axis.e_down_10.disabled_reason) : ''}
        onClick={() => {
          void axisMove('e_down_10');
        }}
      >
        <img src={eDownEnabled ? icons.extruderDown : icons.extruderDownDisable} alt="E-" />
      </button>
    </div>
  );
}

function BasicExtruderBody({
  view,
  label,
  axis,
  axisMove,
}: {
  view: SingleExtruderView;
  label: string;
  axis: ReturnType<typeof useControlPanelBridge>['state']['display']['motion']['axis_moves'];
  axisMove: ReturnType<typeof useControlPanelBridge>['axisMove'];
}) {
  const { t } = useTranslation();
  const eUpEnabled = Boolean(axis.e_up_10?.movable);
  const eDownEnabled = Boolean(axis.e_down_10?.movable);
  const extruderIcon = extruderIconImages[view.extruder_icon]
    ?? (view.has_filament ? icons.singleExtruderFilledLoad : icons.singleExtruderEmptyLoad);

  return (
    <div className="cp-legacy-extruder">
      <div className="cp-extruder-body">
        <button
          type="button"
          className="cp-legacy-extruder-btn"
          disabled={!eUpEnabled}
          title={axis.e_up_10?.disabled_reason ? t(axis.e_up_10.disabled_reason) : ''}
          onClick={() => {
            void axisMove('e_up_10');
          }}
        >
          <img
            className={eUpEnabled ? 'cp-legacy-extruder-icon-enabled' : ''}
            src={eUpEnabled ? icons.legacyExtruderUp : icons.legacyExtruderUpDisable}
            alt="E+"
          />
        </button>

        <div className="cp-single-extruder-book">
          <img className="cp-single-extruder-img" src={extruderIcon} alt="" />
        </div>

        <button
          type="button"
          className="cp-legacy-extruder-btn"
          disabled={!eDownEnabled}
          title={axis.e_down_10?.disabled_reason ? t(axis.e_down_10.disabled_reason) : ''}
          onClick={() => {
            void axisMove('e_down_10');
          }}
        >
          <img
            className={eDownEnabled ? 'cp-legacy-extruder-icon-enabled' : ''}
            src={eDownEnabled ? icons.legacyExtruderDown : icons.legacyExtruderDownDisable}
            alt="E-"
          />
        </button>
      </div>
      <div className={`cp-extruder-footer-label ${!eUpEnabled && !eDownEnabled ? 'disabled' : ''}`}>
        {t(label || 'Extruder')}
      </div>
    </div>
  );
}

function DualExtruderSwitch({
  view,
  disabledOverride,
  onSwitch,
}: {
  view: DualExtruderView;
  disabledOverride: boolean;
  onSwitch: (extruderId: number) => void;
}) {
  const { t } = useTranslation();
  const disabled = !view.switch_control.switchable || disabledOverride;
  const isLeftActive = view.current_extruder_id === 1;
  const isRightActive = view.current_extruder_id === 0;

  return (
    <div
      className={`cp-extruder-switch ${disabled ? 'disabled' : ''}`}
      title={disabled && view.switch_control.disabled_reason ? t(view.switch_control.disabled_reason) : ''}
    >
      <button
        type="button"
        className={isLeftActive ? 'active' : ''}
        disabled={disabled}
        onClick={() => {
          if (!disabled) onSwitch(1);
        }}
      >
        {t(view.labels.left || 'Left')}
      </button>
      <button
        type="button"
        className={isRightActive ? 'active' : ''}
        disabled={disabled}
        onClick={() => {
          if (!disabled) onSwitch(0);
        }}
      >
        {t(view.labels.right || 'Right')}
      </button>
    </div>
  );
}

function DualExtruderBody({
  view,
  label,
  axis,
  axisMove,
  switchExtruder,
  quitSwitching,
  retrySwitching,
}: {
  view: DualExtruderView;
  label: string;
  axis: ReturnType<typeof useControlPanelBridge>['state']['display']['motion']['axis_moves'];
  axisMove: ReturnType<typeof useControlPanelBridge>['axisMove'];
  switchExtruder: ReturnType<typeof useControlPanelBridge>['switchExtruder'];
  quitSwitching: ReturnType<typeof useControlPanelBridge>['quitSwitching'];
  retrySwitching: ReturnType<typeof useControlPanelBridge>['retrySwitching'];
}) {
  const { t } = useTranslation();
  const [switchingPending, setSwitchingPending] = useState(false);

  useEffect(() => {
    setSwitchingPending(false);
  }, [view.current_extruder_id, view.switching_status_text, view.switch_control.switchable]);

  const eUpEnabled = Boolean(axis.e_up_10?.movable);
  const eDownEnabled = Boolean(axis.e_down_10?.movable);
  const leftExt = view.extruders.find((item) => item.id === 1);
  const rightExt = view.extruders.find((item) => item.id === 0);

  const leftNozzleImg = leftExt
    ? (extruderIconImages[leftExt.extruder_icon] ?? icons.leftExtruderUnactiveEmpty)
    : icons.leftExtruderUnactiveEmpty;
  const rightNozzleImg = rightExt
    ? (extruderIconImages[rightExt.extruder_icon] ?? icons.rightExtruderActiveEmpty)
    : icons.rightExtruderActiveEmpty;

  const hasSwitchingContent = Boolean(
    view.switching_status_text || view.can_quit_switching || view.can_retry_switching,
  );

  return (
    <div className="cp-legacy-extruder cp-dual-extruder">
      <DualExtruderSwitch
        view={view}
        disabledOverride={switchingPending}
        onSwitch={(extruderId) => {
          setSwitchingPending(true);
          void switchExtruder(extruderId);
        }}
      />

      <div className="cp-extruder-body">
        <button
          type="button"
          className="cp-legacy-extruder-btn"
          disabled={!eUpEnabled}
          title={axis.e_up_10?.disabled_reason ? t(axis.e_up_10.disabled_reason) : ''}
          onClick={() => {
            void axisMove('e_up_10');
          }}
        >
          <img
            className={eUpEnabled ? 'cp-legacy-extruder-icon-enabled' : ''}
            src={eUpEnabled ? icons.legacyExtruderUp : icons.legacyExtruderUpDisable}
            alt="E+"
          />
        </button>

        <div className="cp-dual-extruder-book">
          <div className="cp-dual-extruder-col">
            <img className="cp-dual-pipe" src={icons.pipeFilledLoad} alt="" />
            <img className="cp-dual-nozzle" src={leftNozzleImg} alt="" />
          </div>
          <div className="cp-dual-extruder-col">
            <img className="cp-dual-pipe" src={icons.pipeFilledLoad} alt="" />
            <img className="cp-dual-nozzle" src={rightNozzleImg} alt="" />
          </div>
        </div>

        <button
          type="button"
          className="cp-legacy-extruder-btn"
          disabled={!eDownEnabled}
          title={axis.e_down_10?.disabled_reason ? t(axis.e_down_10.disabled_reason) : ''}
          onClick={() => {
            void axisMove('e_down_10');
          }}
        >
          <img
            className={eDownEnabled ? 'cp-legacy-extruder-icon-enabled' : ''}
            src={eDownEnabled ? icons.legacyExtruderDown : icons.legacyExtruderDownDisable}
            alt="E-"
          />
        </button>
      </div>

      {hasSwitchingContent ? (
        <div className="cp-switching-status" data-testid="control-panel-switching-status">
          {view.switching_status_text ? (
            <div className={`cp-switching-text ${view.switching_failed ? 'failed' : ''}`}>
              {t(view.switching_status_text)}
            </div>
          ) : null}
          {view.switching_failed && (view.can_quit_switching || view.can_retry_switching) ? (
            <div className="cp-switching-actions">
              {view.can_quit_switching ? (
                <button
                  type="button"
                  className="cp-switching-btn"
                  onClick={() => {
                    void quitSwitching();
                  }}
                >
                  {view.quit_label && view.quit_label !== 'Quit'
                    ? t(view.quit_label)
                    : t('Quit', { context: 'Quit_Switching' })}
                </button>
              ) : null}
              {view.can_retry_switching ? (
                <button
                  type="button"
                  className="cp-switching-btn"
                  onClick={() => {
                    void retrySwitching();
                  }}
                >
                  {t(view.retry_label || 'Retry')}
                </button>
              ) : null}
            </div>
          ) : null}
        </div>
      ) : (
        <div className={`cp-extruder-footer-label ${!eUpEnabled && !eDownEnabled ? 'disabled' : ''}`}>
          {t(label || 'Extruder')}
        </div>
      )}
    </div>
  );
}

function ExtruderSection({
  state,
  axisMove,
  switchExtruder,
  quitSwitching,
  retrySwitching,
}: ReturnType<typeof useControlPanelBridge>) {
  const area = state.display.extruder;
  const axis = state.display.motion.axis_moves;

  return (
    <section className="cp-section cp-extruder" data-testid="control-panel-extruder">
      {area.type === 'dual' ? (
        <DualExtruderBody
          view={area.dual_extruder}
          label={area.label}
          axis={axis}
          axisMove={axisMove}
          switchExtruder={switchExtruder}
          quitSwitching={quitSwitching}
          retrySwitching={retrySwitching}
        />
      ) : null}
      {area.type === 'single' ? (
        <BasicExtruderBody
          view={area.single_extruder}
          label={area.label}
          axis={axis}
          axisMove={axisMove}
        />
      ) : null}
      {area.type === 'single_rack' ? (
        <SingleRackExtruderBody view={area.single_rack_extruder} axis={axis} axisMove={axisMove} />
      ) : null}
    </section>
  );
}

export function ControlPanelPage() {
  useDeviceWebTheme();
  useControlPanelI18nCatalog();
  useFitHostWidth(HOST_WIDTH_DIP);
  const bridge = useControlPanelBridge();
  const rootRef = useRef<HTMLDivElement>(null);
  useReportHostSize(rootRef);

  const { state } = bridge;
  if (!state.display.visible) {
    return <div ref={rootRef} data-testid="control-panel-page" className="control-panel-page hidden" />;
  }

  return (
    <div ref={rootRef} data-testid="control-panel-page" className="control-panel-page">
      <TemperatureSection {...bridge} />
      <MotionSection {...bridge} />
      <ExtruderSection {...bridge} />
    </div>
  );
}
