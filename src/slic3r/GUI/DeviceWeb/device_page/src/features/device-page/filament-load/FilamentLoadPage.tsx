import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useSendToCpp } from '../../../hooks/Bridge';
import { useDeviceWebTheme } from '../../../hooks/useDeviceWebTheme';
import { useFitHostWidth } from '../../../hooks/useFitHostWidth';
import { useFilamentLoadBridge } from './useFilamentLoadBridge';
import type { FilamentLoadActionControl, FilamentLoadState, FilamentLoadStepItem } from './types';
import foldIconUrl from '../../../../../../../../../resources/images/filament_load_fold.svg';
import expandIconUrl from '../../../../../../../../../resources/images/filament_load_expand.svg';
import stepIsOkIconUrl from '../../../../../../../../../resources/images/step_is_ok.svg';
import filamentLoadASeriesUrl from '../../../../../../../../../resources/images/filament_load_a_series.png';
import filamentLoadN6Ext0Url from '../../../../../../../../../resources/images/filament_load_n6_series_ext0.png';
import filamentLoadN6Ext1Url from '../../../../../../../../../resources/images/filament_load_n6_series_ext1.png';
import filamentLoadOExt0Url from '../../../../../../../../../resources/images/filament_load_o_series_ext0.png';
import filamentLoadOExt1Url from '../../../../../../../../../resources/images/filament_load_o_series_ext1.png';
import filamentLoadO1cExt0Url from '../../../../../../../../../resources/images/filament_load_o1c_series_ext0.png';
import filamentLoadO1cExt1Url from '../../../../../../../../../resources/images/filament_load_o1c_series_ext1.png';
import filamentLoadO1sExt0Url from '../../../../../../../../../resources/images/filament_load_o1s_series_ext0.png';
import filamentLoadPSeriesUrl from '../../../../../../../../../resources/images/filament_load_p_series_ext0.png';
import filamentLoadXSeriesUrl from '../../../../../../../../../resources/images/filament_load_x_series_ext0.png';
import './filament-load.css';

const HOST_WIDTH_DIP = 586;
const HOST_LAYOUT_MODULE = 'device_host';

const imageMap: Record<string, string> = {
  'filament_load_a_series.png': filamentLoadASeriesUrl,
  'filament_load_n6_series_ext0.png': filamentLoadN6Ext0Url,
  'filament_load_n6_series_ext1.png': filamentLoadN6Ext1Url,
  'filament_load_o_series_ext0.png': filamentLoadOExt0Url,
  'filament_load_o_series_ext1.png': filamentLoadOExt1Url,
  'filament_load_o1c_series_ext0.png': filamentLoadO1cExt0Url,
  'filament_load_o1c_series_ext1.png': filamentLoadO1cExt1Url,
  'filament_load_o1s_series_ext0.png': filamentLoadO1sExt0Url,
  'filament_load_p_series_ext0.png': filamentLoadPSeriesUrl,
  'filament_load_x_series_ext0.png': filamentLoadXSeriesUrl,
};

function useReportHostSize(ref: React.RefObject<HTMLDivElement | null>) {
  const send = useSendToCpp();
  const lastRef = useRef('');

  useEffect(() => {
    const node = ref.current;
    if (!node) return;
    const report = () => {
      const height = Math.ceil(node.getBoundingClientRect().height);
      const key = `${HOST_WIDTH_DIP}x${height}`;
      if (key === lastRef.current) return;
      lastRef.current = key;
      send(
        { version: '1.0', type: 'request', seq: 1_300_000, ts: Date.now() },
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

function StepRow({ step, index, last }: { step: FilamentLoadStepItem; index: number; last: boolean }) {
  return (
    <li className={`fl-step fl-step-${step.status}`}>
      <span className="fl-step-rail">
        <span className="fl-step-dot">
          {step.status === 'done' ? <img className="fl-step-ok-icon" src={stepIsOkIconUrl} alt="" /> : index + 1}
        </span>
        {!last ? <span className="fl-step-line" /> : null}
      </span>
      <span className="fl-step-text">{step.text}</span>
    </li>
  );
}

function ActionButton({
  action,
  fallbackText,
  onClick,
}: {
  action: FilamentLoadActionControl;
  fallbackText: string;
  onClick: () => void;
}) {
  if (!action.visible) return null;
  const label = action.text || fallbackText;
  return (
    <button
      type="button"
      className="fl-action-btn"
      disabled={!action.enabled}
      title={action.disabled_reason}
      onClick={onClick}
    >
      {label}
    </button>
  );
}

function DebugButton({
  opening,
  state,
  openBridgeDebugDialog,
  setOpening,
}: {
  opening: boolean;
  state: FilamentLoadState;
  openBridgeDebugDialog: (webState: FilamentLoadState) => Promise<boolean>;
  setOpening: (value: boolean) => void;
}) {
  const showDebugButton = Boolean(
    (window as Window & { __debugBuild?: boolean; __internalBuild?: boolean }).__debugBuild
    || (window as Window & { __debugBuild?: boolean; __internalBuild?: boolean }).__internalBuild,
  );
  if (!showDebugButton) return null;
  return (
    <button
      type="button"
      className="fl-action-btn fl-debug-btn"
      disabled={opening}
      onClick={() => {
        setOpening(true);
        void openBridgeDebugDialog(state).finally(() => setOpening(false));
      }}
    >
      Debug Log
    </button>
  );
}

export function FilamentLoadPage() {
  const rootRef = useRef<HTMLDivElement>(null);
  const { t } = useTranslation();
  const { state, error, toggleExpand, retry, stop, openBridgeDebugDialog } = useFilamentLoadBridge();
  const [debugOpening, setDebugOpening] = useState(false);
  useDeviceWebTheme();
  useFitHostWidth(HOST_WIDTH_DIP);
  useReportHostSize(rootRef);

  const display = state.display;
  const expanded = display.visible && display.expanded;
  const imageUrl = imageMap[display.image_name] || imageMap[`${display.image_name}.png`] || '';

  return (
    <div
      ref={rootRef}
      data-testid="filament-load-page"
      className={`filament-load-page ${display.visible ? '' : 'hidden'}`}
    >
      <button
        type="button"
        className="fl-header"
        onClick={() => { void toggleExpand(); }}
      >
        <span className="fl-title">{display.title || t('Filament loading...')}</span>
        <img className="fl-fold-icon" src={expanded ? expandIconUrl : foldIconUrl} alt="" />
      </button>
      {expanded ? (
        <section className="fl-body">
          <div className="fl-steps-panel">
            <div className="fl-step-heading">{t(display.steps.kind === 'unload' ? 'Unloading' : 'Loading')}</div>
            <ol className="fl-step-list">
              {display.steps.items.map((step, index) => (
                <StepRow
                  key={`${step.text}-${index}`}
                  step={step}
                  index={index}
                  last={index === display.steps.items.length - 1}
                />
              ))}
            </ol>
            <div className="fl-actions">
              <ActionButton action={display.actions.retry} fallbackText={t('Retry')} onClick={() => { void retry(); }} />
              <ActionButton action={display.actions.stop} fallbackText={t('Stop')} onClick={() => { void stop(); }} />
              <DebugButton
                opening={debugOpening}
                state={state}
                openBridgeDebugDialog={openBridgeDebugDialog}
                setOpening={setDebugOpening}
              />
            </div>
          </div>
          <div className="fl-image-panel">
            {imageUrl ? <img className="fl-load-image" src={imageUrl} alt="" /> : null}
          </div>
        </section>
      ) : null}
      {error ? <div className="fl-error" role="status">{error}</div> : null}
    </div>
  );
}
