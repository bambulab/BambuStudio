import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { FilamentPageActionState } from '../types';
import moreIconUrl from '../assets/toolbar_more.svg';

// The "More" (···) icon button plus its dropdown. Collects the actions that
// used to sit inline on the toolbar / footer (Load / External Load / Unload /
// Dry / Auto-refill) behind a single icon, per the new N1x design.
export function ToolbarMoreMenu({
  actions,
  onAction,
}: {
  actions: FilamentPageActionState;
  onAction: (action: string) => void;
}) {
  const { t } = useTranslation();
  const [open, setOpen] = useState(false);
  const rootRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!open) return;
    const onDocPointer = (e: MouseEvent) => {
      if (rootRef.current && !rootRef.current.contains(e.target as Node)) setOpen(false);
    };
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') setOpen(false);
    };
    document.addEventListener('mousedown', onDocPointer);
    document.addEventListener('keydown', onKey);
    return () => {
      document.removeEventListener('mousedown', onDocPointer);
      document.removeEventListener('keydown', onKey);
    };
  }, [open]);

  // Mirror the previous per-action gating: `Load` (auto load) follows the
  // show/hide flag; External / Unload disable when unavailable;
  // Auto-refill only appears when the AMS supports it.
  const showLoad = actions.can_show_load_auto ?? true;
  const canLoadExternal = actions.can_load_external ?? actions.can_load ?? false;
  const showDry = actions.can_dry ?? false;

  const run = (action: string) => {
    setOpen(false);
    onAction(action);
  };

  return (
    <div className="toolbar-more" ref={rootRef}>
      <button
        type="button"
        className="toolbar-icon-btn"
        data-testid="ams-hotend-action-more"
        aria-haspopup="menu"
        aria-expanded={open}
        title={t('More')}
        onClick={() => setOpen((v) => !v)}
      >
        <img src={moreIconUrl} alt="" aria-hidden="true" />
      </button>

      {open && (
        <div className="toolbar-more-menu" role="menu" data-testid="ams-hotend-more-menu">
          {showLoad && (
            <button
              type="button"
              role="menuitem"
              data-testid="ams-hotend-action-load-auto"
              onClick={() => run('load_auto')}
            >
              {t('Load', { context: 'AmsHotendWebRackA' })}
            </button>
          )}
          <button
            type="button"
            role="menuitem"
            data-testid="ams-hotend-action-load-external"
            disabled={!canLoadExternal}
            title={t('Supports batch loading from the direct feed port or external feed port.')}
            onClick={() => run('load_external')}
          >
            {t('Manual Loading')}
          </button>
          <button
            type="button"
            role="menuitem"
            data-testid="ams-hotend-action-unload"
            disabled={!actions.can_unload}
            onClick={() => run('unload')}
          >
            {t('Unload')}
          </button>
          {showDry && (
            <button
              type="button"
              role="menuitem"
              data-testid="ams-hotend-action-dry"
              onClick={() => run('dry')}
            >
              {t('Dry')}
            </button>
          )}
          {actions.show_auto_refill && (
            <button
              type="button"
              role="menuitem"
              data-testid="ams-hotend-action-auto-refill"
              onClick={() => run('auto_refill')}
            >
              {t('Auto-refill')}
            </button>
          )}
        </div>
      )}
    </div>
  );
}
