import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useSendToCpp } from '../../../hooks/Bridge';
import { useDeviceWebTheme } from '../../../hooks/useDeviceWebTheme';
import { useFitHostWidth } from '../../../hooks/useFitHostWidth';
import { useHotendRackBridge } from './useHotendRackBridge';
import type { HotendRackActionControl, HotendRackRow, HotendRackSlot, HotendRackToolhead } from './types';
import toolheadEmptyUrl from '../../../../../../../../../resources/images/dev_rack_toolhead_empty.svg';
import toolheadNormalUrl from '../../../../../../../../../resources/images/dev_rack_toolhead_normal.svg';
import toolheadNormalSvg from '../../../../../../../../../resources/images/dev_rack_toolhead_normal.svg?raw';
import nozzleEmptyUrl from '../../../../../../../../../resources/images/dev_rack_nozzle_empty.svg';
import nozzleNormalUrl from '../../../../../../../../../resources/images/dev_rack_nozzle_normal.svg';
import nozzleNormalSvg from '../../../../../../../../../resources/images/dev_rack_nozzle_normal.svg?raw';
import nozzleUnknownUrl from '../../../../../../../../../resources/images/dev_rack_nozzle_unknown.svg';
import nozzleErrorUrl from '../../../../../../../../../resources/images/dev_rack_nozzle_error.svg';
import homeUrl from '../../../../../../../../../resources/images/dev_rack_home.svg';
import rowUpUrl from '../../../../../../../../../resources/images/dev_rack_row_up.svg';
import './hotend-rack.css';

const HOST_WIDTH_DIP = 563;
const HOST_LAYOUT_MODULE = 'device_host';

const slotIcons: Record<HotendRackSlot['status'], string> = {
  empty: nozzleEmptyUrl,
  normal: nozzleNormalUrl,
  unknown: nozzleUnknownUrl,
  error: nozzleErrorUrl,
};

function coloredSvgUrl(svg: string, color: string) {
  const normalized = /^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$/.test(color) ? color : '#FFF735';
  const colored = svg.replace(/#FFF735/g, normalized);
  return `data:image/svg+xml;charset=utf-8,${encodeURIComponent(colored)}`;
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

function ToolheadPanel({ toolhead, title }: { toolhead: HotendRackToolhead; title: string }) {
  const toolheadIcon = toolhead.status === 'normal' && toolhead.filament_color
    ? coloredSvgUrl(toolheadNormalSvg, toolhead.filament_color)
    : toolhead.status === 'empty' ? toolheadEmptyUrl : toolheadNormalUrl;
  return (
    <section className="hr-toolhead" data-testid="hotend-rack-toolhead">
      <div className="hr-title">{title}</div>
      <div className="hr-toolhead-body">
        <span className="hr-toolhead-art">
          <img className="hr-toolhead-img" src={toolheadIcon} alt="" />
        </span>
        {toolhead.status !== 'empty' ? (
          <div className="hr-toolhead-info">
            {toolhead.diameter ? <span>{toolhead.diameter}</span> : null}
            {toolhead.flow_type ? <span>{toolhead.flow_type}</span> : null}
          </div>
        ) : null}
      </div>
    </section>
  );
}

function HotendSlotCard({
  slot,
  onShowErrorDetail,
}: {
  slot: HotendRackSlot;
  onShowErrorDetail: (slotId: number) => void;
}) {
  const disabled = slot.status === 'empty';
  const slotIcon = slot.status === 'normal' && slot.filament_color
    ? coloredSvgUrl(nozzleNormalSvg, slot.filament_color)
    : slotIcons[slot.status];
  return (
    <button
      type="button"
      className={`hr-slot ${slot.status}`}
      data-testid={`hotend-rack-slot-${slot.display_id}`}
      disabled={!slot.has_error_detail}
      title={slot.status === 'error' ? 'Abnormal Hotend' : ''}
      onClick={() => {
        if (slot.has_error_detail) void onShowErrorDetail(slot.id);
      }}
    >
      <span className="hr-slot-id">{slot.display_id}</span>
      <img className="hr-slot-icon" src={slotIcon} alt="" />
      <span className="hr-slot-labels">
        <span className={`hr-slot-line ${disabled ? 'muted' : ''}`}>{slot.diameter || '--'}</span>
        {slot.flow_type ? <span className="hr-slot-line">{slot.flow_type}</span> : null}
      </span>
    </button>
  );
}

function RowPositionButton({
  row,
  running,
  runningText,
  label,
  raisedText,
  onMoveRow,
}: {
  row: HotendRackRow;
  running: boolean;
  runningText: string;
  label: string;
  raisedText: string;
  onMoveRow: (row: HotendRackRow['id']) => void;
}) {
  const canMove = row.move_control.enabled && !row.raised;
  return (
    <button
      type="button"
      className={`hr-row-status ${row.raised ? 'raised' : ''}`}
      disabled={!canMove || running}
      title={canMove ? '' : row.move_control.disabled_reason}
      onClick={() => {
        if (canMove && !running) {
          void onMoveRow(row.id);
        }
      }}
    >
      {running ? (
        <span className="hr-row-running">{runningText}</span>
      ) : row.raised ? (
        <span className="hr-row-raised">{raisedText}</span>
      ) : (
        <img className="hr-row-home" src={rowUpUrl} alt="" />
      )}
      <strong>{label}</strong>
    </button>
  );
}

function RackSlotRow({
  row,
  index,
  onShowErrorDetail,
}: {
  row: HotendRackRow;
  index: number;
  onShowErrorDetail: (slotId: number) => void;
}) {
  return (
    <div className={`hr-slot-row ${row.raised ? 'raised' : ''}`} data-testid={`hotend-rack-row-${index}`}>
      {row.slots.map((slot) => (
        <HotendSlotCard key={slot.id} slot={slot} onShowErrorDetail={onShowErrorDetail} />
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
      title={control.disabled_reason}
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
  const { state, error, moveRow, home, readAll, openHotendsInfo, showErrorDetail, openBridgeDebugDialog } = useHotendRackBridge();
  const [debugOpening, setDebugOpening] = useState(false);
  const { t } = useTranslation();
  useDeviceWebTheme();
  useFitHostWidth(HOST_WIDTH_DIP);
  useReportHostSize(rootRef);

  const display = state.display;
  const rows = display.rack.rows;
  const runningText = t('Running...');
  const showDebugButton = Boolean(
    (window as Window & { __debugBuild?: boolean }).__debugBuild,
  );

  return (
    <div
      ref={rootRef}
      data-testid="hotend-rack-page"
      className={`hotend-rack-page ${display.visible ? '' : 'hidden'}`}
    >
      <ToolheadPanel toolhead={display.toolhead} title={t('Toolhead')} />
      <section className="hr-rack">
        <div className="hr-title">{display.title}</div>
        {display.reading.active ? (
          <div className="hr-reading" data-testid="hotend-rack-reading">
            <img src={rowUpUrl} alt="" />
            <span>{`${t('Reading')} (${display.reading.index}/${display.reading.count}) ...`}</span>
            <small>{t('Please wait')}</small>
          </div>
        ) : (
          <>
            <div className="hr-rows">
              <div className="hr-slot-rows">
                {rows.map((row, index) => (
                  <RackSlotRow
                    key={`${row.id}-${index}`}
                    row={row}
                    index={index}
                    onShowErrorDetail={showErrorDetail}
                  />
                ))}
              </div>
              {rows.length >= 2 ? (
                <div className="hr-position-column">
                  <RowPositionButton
                    row={rows[0]}
                    running={display.rack.busy}
                    runningText={runningText}
                    label={t(`Row ${rows[0].id}`)}
                    raisedText={t('Raised')}
                    onMoveRow={moveRow}
                  />
                  <button
                    type="button"
                    className="hr-home-btn"
                    disabled={!display.actions.home.enabled}
                    title={display.actions.home.disabled_reason}
                    onClick={() => { void home(); }}
                  >
                    <img src={homeUrl} alt="" />
                  </button>
                  <RowPositionButton
                    row={rows[1]}
                    running={display.rack.busy}
                    runningText={runningText}
                    label={t(`Row ${rows[1].id}`)}
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
              {error ? <span className="hr-error">{error}</span> : null}
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
          </>
        )}
      </section>
    </div>
  );
}
