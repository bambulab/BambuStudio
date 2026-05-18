import type { AmsFilamentHotendActionState } from '../types';

export function PageFooter({
  actions,
  onAction,
}: {
  actions: AmsFilamentHotendActionState;
  onAction: (action: string) => void;
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
      <button
        type="button"
        className="dev-tips"
        data-testid="ams-hotend-action-tips"
        title="Development hints (S2 placeholder)"
        onClick={() => onAction('tips')}
      >
        ?
      </button>
      {actions.tips && <span className="footer-tip">{actions.tips}</span>}
    </footer>
  );
}
