import type { FilamentPageActionState } from '../types';

export function PageFooter({
  actions,
  showDebugButton,
  debugOpening,
  onDebug,
}: {
  actions: FilamentPageActionState;
  showDebugButton?: boolean;
  debugOpening?: boolean;
  onDebug?: () => void;
}) {
  return (
    <footer className="page-footer">
      {/* AMS Settings and Auto-refill moved to the top toolbar (icon button /
          More dropdown) in the new N1x design; the footer now only carries the
          dev-only Debug button and the inline tip. */}
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
    </footer>
  );
}
