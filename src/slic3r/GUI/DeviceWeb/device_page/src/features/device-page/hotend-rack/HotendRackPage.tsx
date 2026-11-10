import { useEffect, useMemo, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useSendToCpp } from '../../../hooks/Bridge';
import { useDeviceWebTheme } from '../../../hooks/useDeviceWebTheme';
import { useFitHostWidth } from '../../../hooks/useFitHostWidth';
import useStore from '../../../store/AppStore';
import { nativeSvgUrl } from './nativeSvg';
import { useHotendRackBridge } from './useHotendRackBridge';
import type { HotendRackActionControl, HotendRackRow, HotendRackSlot, HotendRackToolhead } from './types';
import toolheadEmptySvg from '../../../../../../../../../resources/images/dev_rack_toolhead_empty.svg?raw';
import toolheadNormalSvg from '../../../../../../../../../resources/images/dev_rack_toolhead_normal.svg?raw';
import nozzleEmptySvg from '../../../../../../../../../resources/images/dev_rack_nozzle_empty.svg?raw';
import nozzleNormalSvg from '../../../../../../../../../resources/images/dev_rack_nozzle_normal.svg?raw';
import nozzleUnknownSvg from '../../../../../../../../../resources/images/dev_rack_nozzle_unknown.svg?raw';
import nozzleErrorSvg from '../../../../../../../../../resources/images/dev_rack_nozzle_error.svg?raw';
import nozzleErrorIconSvg from '../../../../../../../../../resources/images/dev_rack_nozzle_error_icon.svg?raw';
import homeSvg from '../../../../../../../../../resources/images/dev_rack_home.svg?raw';
import rowUpSvg from '../../../../../../../../../resources/images/dev_rack_row_up.svg?raw';
import rfidFrame1Svg from '../../../../../../../../../resources/images/ams_rfid_1.svg?raw';
import rfidFrame2Svg from '../../../../../../../../../resources/images/ams_rfid_2.svg?raw';
import rfidFrame3Svg from '../../../../../../../../../resources/images/ams_rfid_3.svg?raw';
import rfidFrame4Svg from '../../../../../../../../../resources/images/ams_rfid_4.svg?raw';
import './hotend-rack.css';

// Same width as the native wgtDeviceNozzleRack panel; content is centered inside it.
const HOST_WIDTH_DIP = 586;
const HOST_LAYOUT_MODULE = 'device_host';
const READING_FRAMES = [rfidFrame1Svg, rfidFrame2Svg, rfidFrame3Svg, rfidFrame4Svg];
const READING_FRAME_MS = 100;

function useRackIcons(dark: boolean) {
  return useMemo(() => ({
    toolheadEmpty: nativeSvgUrl(toolheadEmptySvg, dark),
    nozzleEmpty: nativeSvgUrl(nozzleEmptySvg, dark),
    nozzleUnknown: nativeSvgUrl(nozzleUnknownSvg, dark),
    nozzleError: nativeSvgUrl(nozzleErrorSvg, dark),
    nozzleErrorIcon: nativeSvgUrl(nozzleErrorIconSvg, dark),
    home: nativeSvgUrl(homeSvg, dark),
    rowUp: nativeSvgUrl(rowUpSvg, dark),
    readingFrames: READING_FRAMES.map((svg) => nativeSvgUrl(svg, dark)),
    toolheadNormal: (color: string) => nativeSvgUrl(toolheadNormalSvg, dark, color),
    nozzleNormal: (color: string) => nativeSvgUrl(nozzleNormalSvg, dark, color),
  }), [dark]);
}

type RackIcons = ReturnType<typeof useRackIcons>;

function slotIconUrl(slot: HotendRackSlot, icons: RackIcons) {
  switch (slot.status) {
    case 'normal': return icons.nozzleNormal(slot.filament_color);
    case 'unknown': return icons.nozzleUnknown;
    case 'error': return icons.nozzleError;
    default: return icons.nozzleEmpty;
  }
}

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
        { version: '1.0', type: 'request', seq: 1_200_000, ts: Date.now() },
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

function useAnimationFrame(frames: string[], intervalMs: number, active: boolean) {
  const [index, setIndex] = useState(0);
  useEffect(() => {
    if (!active) return;
    const timer = window.setInterval(() => setIndex((i) => (i + 1) % frames.length), intervalMs);
    return () => window.clearInterval(timer);
  }, [active, frames.length, intervalMs]);
  return frames[index];
}

function ToolheadPanel({ toolhead, title, icons }: { toolhead: HotendRackToolhead; title: string; icons: RackIcons }) {
  const toolheadIcon = toolhead.status === 'empty'
    ? icons.toolheadEmpty
    : icons.toolheadNormal(toolhead.filament_color);
  const isNormal = toolhead.status === 'normal';
  return (
    <section className="hr-toolhead" data-testid="hotend-rack-toolhead">
      <div className="hr-title">{title}</div>
      <div className="hr-toolhead-body">
        <img className="hr-toolhead-img" src={toolheadIcon} alt="" />
        {isNormal ? (
          <>
            <span className="hr-toolhead-diameter">{toolhead.diameter}</span>
            <span className="hr-toolhead-line">{toolhead.flow_type}</span>
          </>
        ) : (
          <span className="hr-toolhead-line hr-toolhead-status">{toolhead.diameter}</span>
        )}
      </div>
    </section>
  );
}

function HotendSlotCard({
  slot,
  icons,
  errorTitle,
  onShowErrorDetail,
}: {
  slot: HotendRackSlot;
  icons: RackIcons;
  errorTitle: string;
  onShowErrorDetail: (slotId: number) => void;
}) {
  const isError = slot.status === 'error';
  return (
    <div className={`hr-slot ${slot.status}`} data-testid={`hotend-rack-slot-${slot.display_id}`}>
      <div className="hr-slot-top">
        <span className="hr-slot-id">{slot.display_id}</span>
        <span className="hr-slot-icon-box">
          <img className="hr-slot-icon" src={slotIconUrl(slot, icons)} alt="" />
        </span>
        <span className="hr-slot-select-placeholder" />
      </div>
      <div className="hr-slot-labels">
        <span className="hr-slot-label-row">
          <span className={`hr-slot-line ${isError ? 'error' : ''}`}>{slot.diameter}</span>
          {isError ? (
            <img
              className="hr-slot-error-icon"
              data-testid={`hotend-rack-slot-error-${slot.display_id}`}
              src={icons.nozzleErrorIcon}
              alt=""
              title={errorTitle}
              onClick={() => {
                if (slot.has_error_detail) void onShowErrorDetail(slot.id);
              }}
            />
          ) : null}
        </span>
        <span className="hr-slot-line">{slot.flow_type}</span>
      </div>
    </div>
  );
}

function RowPositionPanel({
  row,
  label,
  rowUpIcon,
  running,
  runningText,
  raisedText,
  onMoveRow,
}: {
  row: HotendRackRow;
  label: string;
  rowUpIcon: string;
  running: boolean;
  runningText: string;
  raisedText: string;
  onMoveRow: (row: HotendRackRow['id']) => void;
}) {
  const canMove = row.move_control.enabled && !row.raised;
  return (
    <div className={`hr-row-pos ${row.raised && !running ? 'raised' : ''}`} data-testid={`hotend-rack-row-pos-${row.id}`}>
      {!running && !row.raised ? (
        <button
          type="button"
          className="hr-row-up-btn"
          disabled={!canMove}
          title={canMove ? '' : row.move_control.disabled_reason}
          onClick={() => {
            if (canMove) void onMoveRow(row.id);
          }}
        >
          <img src={rowUpIcon} alt="" />
        </button>
      ) : null}
      {running ? <span className="hr-row-pos-status">{runningText}</span> : null}
      {!running && row.raised ? <span className="hr-row-pos-status">{raisedText}</span> : null}
      <span className="hr-row-pos-label">{label}</span>
    </div>
  );
}

function RackSlotRow({
  row,
  index,
  icons,
  errorTitle,
  onShowErrorDetail,
}: {
  row: HotendRackRow;
  index: number;
  icons: RackIcons;
  errorTitle: string;
  onShowErrorDetail: (slotId: number) => void;
}) {
  return (
    <div className="hr-slot-row" data-testid={`hotend-rack-row-${index}`}>
      {row.slots.map((slot) => (
        <HotendSlotCard key={slot.id} slot={slot} icons={icons} errorTitle={errorTitle} onShowErrorDetail={onShowErrorDetail} />
      ))}
    </div>
  );
}

function FooterButton({
  testId,
  control,
  children,
  onClick,
}: {
  testId: string;
  control: HotendRackActionControl;
  children: string;
  onClick: () => void;
}) {
  return (
    <button
      type="button"
      data-testid={testId}
      className="hr-footer-btn"
      disabled={!control.enabled}
      title={control.enabled ? '' : control.disabled_reason}
      onClick={() => {
        void onClick();
      }}
    >
      {children}
    </button>
  );
}

export function HotendRackPage() {
  const rootRef = useRef<HTMLDivElement>(null);
  const { state, moveRow, home, readAll, openHotendsInfo, showErrorDetail, openBridgeDebugDialog } = useHotendRackBridge();
  const [debugOpening, setDebugOpening] = useState(false);
  const { t } = useTranslation();
  useDeviceWebTheme();
  useFitHostWidth(HOST_WIDTH_DIP);
  useReportHostSize(rootRef);
  const icons = useRackIcons(useStore((s) => s.filament.theme) === 'dark');

  const display = state.display;
  const rows = display.rack.rows;
  const reading = display.reading;
  const readingFrame = useAnimationFrame(icons.readingFrames, READING_FRAME_MS, reading.active);
  const rowLabels: Record<HotendRackRow['id'], string> = { A: t('Row A'), B: t('Row B') };
  const errorTitle = t('Abnormal Hotend');
  const showDebugButton = Boolean(
    (window as Window & { __debugBuild?: boolean }).__debugBuild,
  );

  return (
    <div
      ref={rootRef}
      data-testid="hotend-rack-page"
      className={`hotend-rack-page ${display.visible ? '' : 'hidden'}`}
    >
      <ToolheadPanel toolhead={display.toolhead} title={t('Toolhead')} icons={icons} />
      <div className="hr-separator" />
      <section className="hr-rack">
        <div className="hr-title">{display.title}</div>
        {reading.active ? (
          <div className="hr-reading" data-testid="hotend-rack-reading">
            <img className="hr-reading-icon" data-testid="hotend-rack-reading-icon" src={readingFrame} alt="" />
            <span className="hr-reading-progress">
              <span>{`${t('Reading')} `}</span>
              <span className="hr-reading-count">{`(${reading.index}/${reading.count})`}</span>
              <span>{' ...'}</span>
            </span>
            <span className="hr-reading-tip">{t('Please wait')}</span>
          </div>
        ) : (
          <div className="hr-rack-content">
            <div className="hr-rows">
              <div className="hr-slot-rows">
                {rows.map((row, index) => (
                  <RackSlotRow
                    key={`${row.id}-${index}`}
                    row={row}
                    index={index}
                    icons={icons}
                    errorTitle={errorTitle}
                    onShowErrorDetail={showErrorDetail}
                  />
                ))}
              </div>
              {rows.length >= 2 ? (
                <div className="hr-position-column">
                  <RowPositionPanel
                    row={rows[0]}
                    label={rowLabels[rows[0].id]}
                    rowUpIcon={icons.rowUp}
                    running={display.rack.busy}
                    runningText={t('Running...')}
                    raisedText={t('Raised')}
                    onMoveRow={moveRow}
                  />
                  <button
                    type="button"
                    className="hr-home-btn"
                    disabled={!display.actions.home.enabled}
                    title={display.actions.home.enabled ? '' : display.actions.home.disabled_reason}
                    onClick={() => { void home(); }}
                  >
                    <img src={icons.home} alt="" />
                  </button>
                  <RowPositionPanel
                    row={rows[1]}
                    label={rowLabels[rows[1].id]}
                    rowUpIcon={icons.rowUp}
                    running={display.rack.busy}
                    runningText={t('Running...')}
                    raisedText={t('Raised')}
                    onMoveRow={moveRow}
                  />
                </div>
              ) : null}
            </div>
            <div className="hr-footer">
              <FooterButton testId="hotend-rack-hotends-info" control={display.actions.hotends_info} onClick={openHotendsInfo}>
                {t('Hotends Info')}
              </FooterButton>
              <FooterButton testId="hotend-rack-read-all" control={display.actions.read_all} onClick={readAll}>
                {t('Read All')}
              </FooterButton>
            </div>
            {showDebugButton ? (
              <button
                type="button"
                data-testid="hotend-rack-debug"
                disabled={debugOpening}
                className="hr-debug-btn"
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
          </div>
        )}
      </section>
    </div>
  );
}
