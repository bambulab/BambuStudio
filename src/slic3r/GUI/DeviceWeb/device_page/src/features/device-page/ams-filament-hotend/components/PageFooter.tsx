import type { AmsFilamentHotendActionState } from '../types';

export function PageFooter({
  actions,
  onAction,
  showDebugButton,
  debugOpening,
  onDebug,
}: {
  actions: AmsFilamentHotendActionState;
  onAction: (action: string) => void;
  showDebugButton?: boolean;
  debugOpening?: boolean;
  onDebug?: () => void;
}) {
  return (
    <footer className="page-footer">
      {actions.show_ams_settings && (
        <button
          type="button"
          data-testid="ams-hotend-action-settings"
          onClick={() => onAction('settings')}
        >
          AMS Settings
        </button>
      )}
      {actions.show_auto_refill && (
        <button
          type="button"
          data-testid="ams-hotend-action-auto-refill"
          onClick={() => onAction('auto_refill')}
        >
          Auto-refill
        </button>
      )}
      {showDebugButton && (
        <button
          type="button"
          className="bridge-debug-button"
          disabled={debugOpening}
          onClick={onDebug}
        >
          {debugOpening ? 'Opening...' : 'Debug'}
        </button>
      )}
      {actions.tips && <span className="footer-tip">{actions.tips}</span>}
      <button
        type="button"
        className="dev-tips"
        data-testid="ams-hotend-action-tips"
        title="Development hints (S2 placeholder)"
        onClick={() => onAction('tips')}
      >
        ?
      </button>
    </footer>
  );
}
