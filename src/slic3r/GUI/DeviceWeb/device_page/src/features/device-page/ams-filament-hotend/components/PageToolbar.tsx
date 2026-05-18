import type { AmsFilamentHotendActionState, AmsFilamentHotendTab } from '../types';

export function PageToolbar({
  selectedTab,
  onTabChange,
  actions,
  onAction,
}: {
  selectedTab: AmsFilamentHotendTab;
  onTabChange: (tab: AmsFilamentHotendTab) => void;
  actions: AmsFilamentHotendActionState;
  onAction: (action: string) => void;
}) {
  return (
    <header className="page-toolbar">
      <div className="tab-group">
        <button
          type="button"
          data-testid="ams-hotend-tab-filament"
          className={`tab${selectedTab === 'filament' ? ' active' : ''}`}
          onClick={() => onTabChange('filament')}
        >
          耗材
        </button>
        <button
          type="button"
          data-testid="ams-hotend-tab-hotend"
          className={`tab${selectedTab === 'hotend' ? ' active' : ''}`}
          onClick={() => onTabChange('hotend')}
        >
          热端
        </button>
      </div>

      {selectedTab === 'filament'
        ? <FilamentActions actions={actions} onAction={onAction} />
        : <HotendActions onAction={onAction} />}
    </header>
  );
}

function FilamentActions({
  actions,
  onAction,
}: {
  actions: AmsFilamentHotendActionState;
  onAction: (action: string) => void;
}) {
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
        烘干
      </button>
      <span className="action-divider" aria-hidden="true" />
      <button
        type="button"
        data-testid="ams-hotend-action-load-external"
        disabled={!canLoadExternal}
        onClick={() => onAction('load_external')}
      >
        外挂手动进料
      </button>
      <button
        type="button"
        data-testid="ams-hotend-action-load-auto"
        disabled={!canLoadAuto}
        onClick={() => onAction('load_auto')}
      >
        自动进料
      </button>
      <button
        type="button"
        data-testid="ams-hotend-action-unload"
        disabled={!actions.can_unload}
        onClick={() => onAction('unload')}
      >
        退料
      </button>
    </div>
  );
}

function HotendActions({ onAction }: { onAction: (action: string) => void }) {
  return (
    <div className="action-group hotend-actions">
      <button
        type="button"
        className="primary"
        data-testid="ams-hotend-action-read-all"
        onClick={() => onAction('read_all_hotends')}
      >
        读取全部
      </button>
    </div>
  );
}
