import type { FilamentPageActionState } from '../types';

export function PageFooter({
  actions,
  onAction,
  showDebugButton,
  debugOpening,
  onDebug,
}: {
  actions: FilamentPageActionState;
  onAction: (action: string) => void;
  showDebugButton?: boolean;
  debugOpening?: boolean;
  onDebug?: () => void;
}) {
  return (
    <footer className="page-footer">
      {/* AMS Settings and Auto-refill moved to the top toolbar (icon button /
          More dropdown) in the new N1x design; the footer now only carries the
          dev-only Debug button, the inline tip, and the help affordance. */}
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
