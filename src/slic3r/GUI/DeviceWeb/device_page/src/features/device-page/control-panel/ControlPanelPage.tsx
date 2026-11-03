import { useEffect, useId, useMemo, useRef, useState } from 'react';
import { useFitHostWidth } from '../../../hooks/useFitHostWidth';
import { useSendToCpp } from '../../../hooks/Bridge';
import { useDeviceWebTheme } from '../../../hooks/useDeviceWebTheme';
import { useControlPanelBridge } from './useControlPanelBridge';
import type {
  BedTempArea,
  ChamberTempArea,
  DualExtruderView,
  ExtruderId,
  ExtruderTempItem,
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
import lampOnIconUrl from '../../../../../../../../../resources/images/monitor_lamp_on.svg';
import lampOffIconUrl from '../../../../../../../../../resources/images/monitor_lamp_off.svg';
import axisHomeIconUrl from '../../../../../../../../../resources/images/monitor_axis_home.svg';
import bedUpIconUrl from '../../../../../../../../../resources/images/monitor_bed_up.svg';
import bedUpDisableIconUrl from '../../../../../../../../../resources/images/monitor_bed_up_disable.svg';
import bedDownIconUrl from '../../../../../../../../../resources/images/monitor_bed_down.svg';
import bedDownDisableIconUrl from '../../../../../../../../../resources/images/monitor_bed_down_disable.svg';
import monitorAmsExtruderIconUrl from '../../../../../../../../../resources/images/monitor_ams_extruder.svg';
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
import rackToolheadClampedUrl from '../../../../../../../../../resources/images/dev_rack_toolhead_clamped.png';
import rackToolheadClampedNoFilamentUrl from '../../../../../../../../../resources/images/dev_rack_toolhead_clamped_no_filament.png';
import rackToolheadEmptyUrl from '../../../../../../../../../resources/images/dev_rack_toolhead_unclamped.png';
import rackToolheadPickingUrl from '../../../../../../../../../resources/images/dev_rack_toolhead_picking.png';
import rackToolheadPlacingUrl from '../../../../../../../../../resources/images/dev_rack_toolhead_placing.png';
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
  lampOn: lampOnIconUrl,
  lampOff: lampOffIconUrl,
  axisHome: axisHomeIconUrl,
  bedUp: bedUpIconUrl,
  bedUpDisable: bedUpDisableIconUrl,
  bedDown: bedDownIconUrl,
  bedDownDisable: bedDownDisableIconUrl,
  monitorAmsExtruder: monitorAmsExtruderIconUrl,
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
  rackToolheadClamped: rackToolheadClampedUrl,
  rackToolheadClampedNoFilament: rackToolheadClampedNoFilamentUrl,
  rackToolheadEmpty: rackToolheadEmptyUrl,
  rackToolheadPicking: rackToolheadPickingUrl,
  rackToolheadPlacing: rackToolheadPlacingUrl,
};

const extruderIconImages: Record<string, string> = {
  monitor_ams_extruder: icons.monitorAmsExtruder,
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
};

function useReportHostSize(ref: React.RefObject<HTMLDivElement | null>) {
  const send = useSendToCpp();
  const lastRef = useRef('');

  useEffect(() => {
    const node = ref.current;
    if (!node) return;
    const report = () => {
      const height = Math.ceil(node.getBoundingClientRect().height);
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
  return value === null ? '--' : String(value);
}

function clampTemp(value: string, min: number, max: number) {
  const n = Number(value);
  if (!Number.isFinite(n)) return min;
  return Math.max(min, Math.min(max, Math.round(n)));
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
  const initialTarget = item.target ?? item.current ?? item.min;
  const [value, setValue] = useState(initialTarget);

  useEffect(() => {
    setValue(initialTarget);
  }, [initialTarget]);

  const disabled = !item.editable || !action.enabled;

  return (
    <div
      className={`cp-temp-input ${disabled ? 'disabled' : ''}`}
      title={disabled ? (action.disabled_reason || item.read_only_reason) : ''}
    >
      <div className="cp-temp-values">
        <span className="cp-temp-current">{formatTemp(item.current)}</span>
        <span className="cp-temp-sep">/</span>
        <input
          aria-label="target temperature"
          value={value}
          inputMode="numeric"
          disabled={disabled}
          onChange={(e) => setValue(clampTemp(e.currentTarget.value, item.min, item.max))}
          onBlur={() => {
            if (!disabled) onCommit(value);
          }}
          onKeyDown={(e) => {
            if (e.key === 'Enter' && !disabled) {
              onCommit(value);
              (e.currentTarget as HTMLInputElement).blur();
            }
          }}
        />
        <span className="cp-temp-unit">℃</span>
      </div>
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
  if (!item.visible) return null;
  const icon = item.temp_status === 'heating' ? icons.nozzleActive : icons.nozzle;
  return (
    <div className="cp-row cp-temp-row" data-testid={`control-panel-extruder-temp-${item.id}`}>
      <div className="cp-row-label">
        {item.badge ? <span className="cp-nozzle-badge">{item.badge_nozzle_index}</span> : null}
        <img src={icon} alt="" />
      </div>
      {item.show_nozzle ? (
        <TempInput item={item} action={action} onCommit={(value) => onCommit(item.id, value)} />
      ) : (
        <div className="cp-unclamped">{item.read_only_reason || 'Unclamped'}</div>
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
  const bedIcon = state.display.bed_temp.temp_status === 'heating' ? icons.bedActive : icons.bed;
  const chamberIcon = state.display.chamber_temp.temp_status === 'heating' ? icons.chamberActive : icons.chamber;

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
              <img src={bedIcon} alt="" />
            </div>
            <TempInput item={state.display.bed_temp} action={state.display.bed_temp.temp_input} onCommit={setBedTemp} />
          </div>
        ) : null}
        {state.display.chamber_temp.visible ? (
          <div className="cp-row" data-testid="control-panel-chamber-temp">
            <div className="cp-row-label">
              <img src={chamberIcon} alt="" />
            </div>
            <TempInput item={state.display.chamber_temp} action={state.display.chamber_temp.temp_input} onCommit={setChamberTemp} />
          </div>
        ) : null}
      </div>
      <MiscSection {...bridge} />
    </section>
  );
}

function MiscSection({ state, setSpeed, toggleLamp, openFan }: ReturnType<typeof useControlPanelBridge>) {
  const speedLevels = useMemo(() => [1, 2, 3, 4] as const, []);
  return (
    <div className="cp-misc">
      {state.display.fan.fan_visible ? (
        <button
          type="button"
          data-testid="control-panel-fan"
          className="cp-fan-button"
          disabled={!state.display.fan.fan_control.openable}
          title={state.display.fan.fan_control.disabled_reason}
          onClick={() => {
            void openFan();
          }}
        >
          <img src={icons.fanOff} alt="" />
          <span>{state.display.fan.fan_label || '风扇'}</span>
        </button>
      ) : null}
      <div className="cp-misc-bottom">
        {state.display.speed.visible ? (
          <button
            type="button"
            data-testid="control-panel-speed"
            className="cp-speed-button"
            disabled={!state.display.speed.speed_control.selectable}
            title={state.display.speed.speed_control.disabled_reason}
            onClick={() => {
              const index = speedLevels.indexOf(state.display.speed.speed_level);
              void setSpeed(speedLevels[(index + 1) % speedLevels.length]);
            }}
          >
            <img src={icons.speed} alt="" />
            <strong>{state.display.speed.speed_text}</strong>
          </button>
        ) : null}
        {state.display.lamp.visible ? (
          <button
            type="button"
            data-testid="control-panel-lamp"
            className={`cp-lamp-button ${state.display.lamp.lamp_on ? 'active' : ''}`}
          disabled={!state.display.lamp.lamp_control.switchable}
          title={state.display.lamp.lamp_control.disabled_reason}
            onClick={() => {
              void toggleLamp(!state.display.lamp.lamp_on);
            }}
          >
            <img src={state.display.lamp.lamp_on ? icons.lampOn : icons.lampOff} alt="" />
            <span>{state.display.lamp.label || 'LED灯'}</span>
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

const DIAL_SECTORS: SectorDef[] = [
  { op: 'y_pos_10', rIn: 58, rOut: 105, startDeg: -135, endDeg: -45, isInner: false },
  { op: 'x_pos_10', rIn: 58, rOut: 105, startDeg: -45, endDeg: 45, isInner: false },
  { op: 'y_neg_10', rIn: 58, rOut: 105, startDeg: 45, endDeg: 135, isInner: false },
  { op: 'x_neg_10', rIn: 58, rOut: 105, startDeg: 135, endDeg: 225, isInner: false },
  { op: 'y_pos_1', rIn: 24, rOut: 58, startDeg: -135, endDeg: -45, isInner: true },
  { op: 'x_pos_1', rIn: 24, rOut: 58, startDeg: -45, endDeg: 45, isInner: true },
  { op: 'y_neg_1', rIn: 24, rOut: 58, startDeg: 45, endDeg: 135, isInner: true },
  { op: 'x_neg_1', rIn: 24, rOut: 58, startDeg: 135, endDeg: 225, isInner: true },
];

function polarToCartesian(r: number, deg: number) {
  const rad = (deg * Math.PI) / 180;
  return { x: r * Math.cos(rad), y: r * Math.sin(rad) };
}

function buildAnnularSectorPath(rIn: number, rOut: number, startDeg: number, endDeg: number) {
  const p1 = polarToCartesian(rOut, startDeg);
  const p2 = polarToCartesian(rOut, endDeg);
  const p3 = polarToCartesian(rIn, endDeg);
  const p4 = polarToCartesian(rIn, startDeg);
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
  const [hoveredOp, setHoveredOp] = useState<string | null>(null);
  const homeEnabled = Boolean(axis.xy_home?.movable);

  return (
    <div className="cp-axis-dial" data-testid="control-panel-xy">
      <svg viewBox="-108 -108 216 216" className="cp-axis-svg">
        {DIAL_SECTORS.map((sec) => {
          const enabled = Boolean(axis[sec.op]?.movable);
          const isHovered = hoveredOp === sec.op && enabled;
          const d = buildAnnularSectorPath(sec.rIn, sec.rOut, sec.startDeg, sec.endDeg);
          return (
            <path
              key={sec.op}
              d={d}
              className={`cp-dial-sector ${sec.isInner ? 'inner' : 'outer'} ${isHovered ? 'hovered' : ''} ${!enabled ? 'disabled' : ''}`}
              onMouseEnter={() => setHoveredOp(sec.op)}
              onMouseLeave={() => setHoveredOp((prev) => (prev === sec.op ? null : prev))}
              onClick={() => {
                if (enabled) onMove(sec.op);
              }}
            >
              {axis[sec.op]?.disabled_reason ? <title>{axis[sec.op]?.disabled_reason}</title> : null}
            </path>
          );
        })}

        {/* Diagonal 4px white separator bars matching AxisCtrlButton::render */}
        <g className="cp-dial-gaps" pointerEvents="none">
          <rect x="-120" y="-2.83" width="240" height="5.66" transform="rotate(-45)" />
          <rect x="-120" y="-2.83" width="240" height="5.66" transform="rotate(45)" />
        </g>

        {/* Outer ring axis labels */}
        <g className="cp-dial-axis-labels" pointerEvents="none">
          <text x="0" y="-81.5" textAnchor="middle" dominantBaseline="central">Y</text>
          <text x="-81.5" y="0" textAnchor="middle" dominantBaseline="central">-X</text>
          <text x="0" y="81.5" textAnchor="middle" dominantBaseline="central">-Y</text>
          <text x="81.5" y="0" textAnchor="middle" dominantBaseline="central">X</text>
        </g>

        {/* Diagonal step labels (+10, +1, -1, -10) rotated 45 deg matching AxisCtrlButton */}
        <g className="cp-dial-step-labels" transform="rotate(45)" pointerEvents="none">
          <text x="6" y="-81.5" textAnchor="start" dominantBaseline="central">+10</text>
          <text x="6" y="-41" textAnchor="start" dominantBaseline="central">+1</text>
          <text x="6" y="41" textAnchor="start" dominantBaseline="central">-1</text>
          <text x="6" y="81.5" textAnchor="start" dominantBaseline="central">-10</text>
        </g>

        {/* Center white ring & Home circle */}
        <circle cx="0" cy="0" r="25" className="cp-dial-center-gap" pointerEvents="none" />
        <circle
          cx="0"
          cy="0"
          r="23"
          className={`cp-dial-home-circle ${hoveredOp === 'xy_home' && homeEnabled ? 'hovered' : ''}`}
          pointerEvents="none"
        />
      </svg>

      <button
        type="button"
        aria-label="Home"
        className="cp-dial-home-btn"
        disabled={!homeEnabled}
        title={axis.xy_home?.disabled_reason ?? ''}
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
  const axis = state.display.motion.axis_moves;
  const [debugOpening, setDebugOpening] = useState(false);
  const showDebugButton = Boolean(
    (window as Window & { __debugBuild?: boolean }).__debugBuild,
  );
  const zButton = (op: string, label: string, icon: string, disabledIcon: string, tone: 'light' | 'dark') => {
    const enabled = Boolean(axis[op]?.movable);
    return (
      <button
        type="button"
        className={`cp-step-btn ${tone}`}
        disabled={!enabled}
        title={axis[op]?.disabled_reason ?? ''}
        onClick={() => {
          void axisMove(op);
        }}
      >
        <img src={enabled ? icon : disabledIcon} alt="" />
        <span>{label}</span>
      </button>
    );
  };

  const rackButton = (op: string, icon: string, tone: 'light' | 'dark') => {
    const enabled = Boolean(axis[op]?.movable);
    return (
      <button
        type="button"
        className={`cp-rack-btn ${tone}`}
        disabled={!enabled}
        title={axis[op]?.disabled_reason ?? ''}
        onClick={() => {
          void axisMove(op);
        }}
      >
        <img src={icon} alt="" />
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
          {debugOpening ? 'Opening...' : 'Debug'}
        </button>
      ) : null}
      {state.display.motion.z_visible ? (
        <div className="cp-z-stack" data-testid="control-panel-z">
          <div className="cp-z-row">
            {zButton('z_up_10', '10', icons.bedUp, icons.bedUpDisable, 'light')}
            {zButton('z_up_1', '1', icons.bedUp, icons.bedUpDisable, 'dark')}
            <span className="cp-z-label">{state.display.motion.z_label || '热床'}</span>
            {zButton('z_down_1', '1', icons.bedDown, icons.bedDownDisable, 'dark')}
            {zButton('z_down_10', '10', icons.bedDown, icons.bedDownDisable, 'light')}
          </div>
          {state.display.rack.visible ? (
            <div className="cp-rack-row" data-testid="control-panel-rack">
              <span className="cp-rack-title">热端挂架</span>
              <div className="cp-rack-buttons">
                {rackButton('rack_lift', icons.rackLift, 'light')}
                {rackButton('rack_center', icons.rackCenter, 'dark')}
                {rackButton('rack_lower', icons.rackLower, 'light')}
              </div>
            </div>
          ) : null}
        </div>
      ) : null}
    </section>
  );
}

function ToolheadVisual({
  clamped,
  filamentColor,
  extruderStatus,
  visualState,
}: {
  clamped: boolean;
  filamentColor: string;
  extruderStatus?: string;
  visualState: RackToolheadVisualState;
}) {
  const gradId = useId();
  const clipId = useId();
  const isDown = extruderStatus === 'loading';
  const isUp = extruderStatus === 'unloading';
  const showArrows = isDown || isUp;
  const showFilamentPath = clamped && (
    visualState === 'clamped' ||
    visualState === 'clamped_retracting' ||
    visualState === 'clamped_extruding' ||
    visualState === 'clamped_no_filament'
  );
  const strandColor = filamentColor || '#7CC7E8';

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
          {filamentColor || showArrows ? (
            <rect x="35" y="2" width="4" height="99" fill={strandColor} />
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
  const clamped = view.current_nozzle_id !== null && view.nozzle_flow_type !== 'Unclamped';
  const eUpEnabled = Boolean(axis.e_up_10?.movable);
  const eDownEnabled = Boolean(axis.e_down_10?.movable);

  return (
    <div className="cp-extruder-body">
      <button
        type="button"
        className="cp-extruder-arrow"
        disabled={!eUpEnabled}
        title={axis.e_up_10?.disabled_reason ?? ''}
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
        {view.nozzle_flow_type ? <span>{view.nozzle_flow_type}</span> : null}
      </div>

      <ToolheadVisual
        clamped={clamped}
        filamentColor={view.filament_clr}
        extruderStatus={view.extruder_status}
        visualState={view.rack_toolhead_visual_state}
      />

      <button
        type="button"
        className="cp-extruder-arrow"
        disabled={!eDownEnabled}
        title={axis.e_down_10?.disabled_reason ?? ''}
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
  axis,
  axisMove,
}: {
  view: SingleExtruderView;
  axis: ReturnType<typeof useControlPanelBridge>['state']['display']['motion']['axis_moves'];
  axisMove: ReturnType<typeof useControlPanelBridge>['axisMove'];
}) {
  const eUpEnabled = Boolean(axis.e_up_10?.movable);
  const eDownEnabled = Boolean(axis.e_down_10?.movable);
  const extruderIcon = extruderIconImages[view.extruder_icon] ?? icons.monitorAmsExtruder;

  return (
    <div className="cp-extruder-body">
      <button
        type="button"
        className="cp-extruder-arrow"
        disabled={!eUpEnabled}
        title={axis.e_up_10?.disabled_reason ?? ''}
        onClick={() => {
          void axisMove('e_up_10');
        }}
      >
        <img src={eUpEnabled ? icons.extruderUp : icons.extruderUpDisable} alt="E+" />
      </button>

      <div className="cp-nozzle-info">
        <img className="cp-extruder-icon" src={extruderIcon} alt="" />
        {view.filament_clr ? <span style={{ color: view.filament_clr }}>●</span> : null}
      </div>

      <button
        type="button"
        className="cp-extruder-arrow"
        disabled={!eDownEnabled}
        title={axis.e_down_10?.disabled_reason ?? ''}
        onClick={() => {
          void axisMove('e_down_10');
        }}
      >
        <img src={eDownEnabled ? icons.extruderDown : icons.extruderDownDisable} alt="E-" />
      </button>
    </div>
  );
}

function DualExtruderSwitch({
  view,
  switchExtruder,
}: {
  view: DualExtruderView;
  switchExtruder: ReturnType<typeof useControlPanelBridge>['switchExtruder'];
}) {
  return (
    <div className="cp-extruder-switch">
      <button
        type="button"
        className={view.current_extruder_id === 1 ? 'active' : ''}
        disabled={!view.switch_control.switchable}
        onClick={() => {
          void switchExtruder(1);
        }}
      >
        {view.labels.left}
      </button>
      <button
        type="button"
        className={view.current_extruder_id === 0 ? 'active' : ''}
        disabled={!view.switch_control.switchable}
        onClick={() => {
          void switchExtruder(0);
        }}
      >
        {view.labels.right}
      </button>
    </div>
  );
}

function ExtruderSection({ state, axisMove, switchExtruder }: ReturnType<typeof useControlPanelBridge>) {
  const area = state.display.extruder;
  const axis = state.display.motion.axis_moves;
  const dual = area.dual_extruder;
  const activeDualExtruder = dual.extruders.find((item) => item.id === dual.current_extruder_id);
  const dualBodyView: SingleExtruderView = {
    has_filament: activeDualExtruder?.has_filament ?? false,
    filament_clr: activeDualExtruder?.filament_clr ?? '',
    extruder_status: activeDualExtruder?.extruder_status ?? 'idle',
    extruder_icon: activeDualExtruder?.extruder_icon ?? 'monitor_ams_extruder',
  };

  return (
    <section className="cp-section cp-extruder" data-testid="control-panel-extruder">
      {area.type === 'dual' ? (
        <>
          <DualExtruderSwitch view={dual} switchExtruder={switchExtruder} />
          <BasicExtruderBody view={dualBodyView} axis={axis} axisMove={axisMove} />
        </>
      ) : null}
      {area.type === 'single' ? (
        <BasicExtruderBody view={area.single_extruder} axis={axis} axisMove={axisMove} />
      ) : null}
      {area.type === 'single_rack' ? (
        <SingleRackExtruderBody view={area.single_rack_extruder} axis={axis} axisMove={axisMove} />
      ) : null}
    </section>
  );
}

export function ControlPanelPage() {
  useDeviceWebTheme();
  useFitHostWidth(HOST_WIDTH_DIP);
  const bridge = useControlPanelBridge();
  const rootRef = useRef<HTMLDivElement>(null);
  useReportHostSize(rootRef);

  const { state, error } = bridge;
  if (!state.display.visible) {
    return <div ref={rootRef} data-testid="control-panel-page" className="control-panel-page hidden" />;
  }

  return (
    <div ref={rootRef} data-testid="control-panel-page" className="control-panel-page">
      <TemperatureSection {...bridge} />
      <MotionSection {...bridge} />
      <ExtruderSection {...bridge} />
      {error ? <div className="cp-error">{error}</div> : null}
    </div>
  );
}
