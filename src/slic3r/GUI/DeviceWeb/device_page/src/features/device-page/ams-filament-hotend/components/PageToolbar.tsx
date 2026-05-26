import { useTranslation } from 'react-i18next';
import type { FilamentPageActionState, FilamentPageMode, HotendPageActionState, AmsFilamentHotendTab } from '../types';

// Union of fields PageToolbar needs from either page's actions section. The
// parent merges filament_page.actions and hotend_page.actions before passing
// in, so this is a flat read-only shape.
type ToolbarActions = FilamentPageActionState & HotendPageActionState;

export function PageToolbar({
  selectedTab,
  onTabChange,
  actions,
  mode,
  onAction,
  onExitBatchMode,
}: {
  selectedTab: AmsFilamentHotendTab;
  onTabChange: (tab: AmsFilamentHotendTab) => void;
  actions: ToolbarActions;
  mode: FilamentPageMode;
  onAction: (action: string) => void;
  onExitBatchMode: () => void;
}) {
  const { t } = useTranslation();
  const inBatchMode = mode !== 'normal';
  return (
    <header className="page-toolbar">
      <div className="tab-group">
        <button
          type="button"
          data-testid="ams-hotend-tab-filament"
          className={`tab${selectedTab === 'filament' ? ' active' : ''}`}
          onClick={() => onTabChange('filament')}
          // Tab switching is hidden behind batch mode so the user cannot leave
          // a partially-selected batch by toggling tabs by accident.
          disabled={inBatchMode}
        >
          {t('Filament')}
        </button>
        <button
          type="button"
          data-testid="ams-hotend-tab-hotend"
          className={`tab${selectedTab === 'hotend' ? ' active' : ''}`}
          onClick={() => onTabChange('hotend')}
          disabled={inBatchMode}
        >
          {t('Hotend')}
        </button>
      </div>

      {selectedTab === 'filament'
        ? (
          inBatchMode
            ? <BatchModeActions actions={actions} onAction={onAction} onExit={onExitBatchMode} />
            : <FilamentActions actions={actions} onAction={onAction} />
        )
        : <HotendActions actions={actions} onAction={onAction} />}
    </header>
  );
}

function FilamentActions({
  actions,
  onAction,
}: {
  actions: ToolbarActions;
  onAction: (action: string) => void;
}) {
  const { t } = useTranslation();
  const canLoadExternal = actions.can_load_external ?? actions.can_load ?? false;
  const canLoadAuto     = actions.can_load_auto     ?? actions.can_load ?? false;
  return (
    <div className="action-group">
      <button
        type="button"
        data-testid="ams-hotend-action-dry"
        disabled={!actions.can_dry}
        onClick={() => onAction('dry')}
      >
        {t('Dry')}
      </button>
      <span className="action-divider" aria-hidden="true" />
      <button
        type="button"
        data-testid="ams-hotend-action-load-external"
        disabled={!canLoadExternal}
        onClick={() => onAction('load_external')}
      >
        {t('Load External Spool')}
      </button>
      <button
        type="button"
        data-testid="ams-hotend-action-load-auto"
        disabled={!canLoadAuto}
        onClick={() => onAction('load_auto')}
      >
        {t('Auto Load')}
      </button>
      <button
        type="button"
        data-testid="ams-hotend-action-unload"
        disabled={!actions.can_unload}
        onClick={() => onAction('unload')}
      >
        {t('Unload')}
      </button>
    </div>
  );
}

// Batch-mode toolbar: collapses to `Dry | Exit`. The mode-specific commit
// action (Auto Load / External Load / Unload) lives on the BatchModeFooter
// at the bottom of the page, not here.
function BatchModeActions({
  actions,
  onAction,
  onExit,
}: {
  actions: ToolbarActions;
  onAction: (action: string) => void;
  onExit: () => void;
}) {
  const { t } = useTranslation();
  return (
    <div className="action-group" data-testid="ams-hotend-batch-toolbar">
      <button
        type="button"
        data-testid="ams-hotend-action-dry"
        disabled={!actions.can_dry}
        onClick={() => onAction('dry')}
      >
        {t('Dry')}
      </button>
      <span className="action-divider" aria-hidden="true" />
      <button
        type="button"
        className="primary"
        data-testid="ams-hotend-batch-exit"
        onClick={onExit}
      >
        {t('Exit')}
      </button>
    </div>
  );
}

function HotendActions({
  actions,
  onAction,
}: {
  actions: ToolbarActions;
  onAction: (action: string) => void;
}) {
  const { t } = useTranslation();
  const canReadAll = actions.can_read_all_hotends ?? true;
  return (
    <div className="action-group hotend-actions">
      <button
        type="button"
        className="primary"
        data-testid="ams-hotend-action-read-all"
        disabled={!canReadAll}
        title={canReadAll ? undefined : actions.read_all_disabled_reason}
        onClick={() => onAction('read_all_hotends')}
      >
        {t('Read All')}
      </button>
    </div>
  );
}
