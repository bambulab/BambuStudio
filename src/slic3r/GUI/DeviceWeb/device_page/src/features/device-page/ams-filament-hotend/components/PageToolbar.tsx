import { useTranslation } from 'react-i18next';
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
  const { t } = useTranslation();
  return (
    <header className="page-toolbar">
      <div className="tab-group">
        <button
          type="button"
          data-testid="ams-hotend-tab-filament"
          className={`tab${selectedTab === 'filament' ? ' active' : ''}`}
          onClick={() => onTabChange('filament')}
        >
          {t('Filament')}
        </button>
        <button
          type="button"
          data-testid="ams-hotend-tab-hotend"
          className={`tab${selectedTab === 'hotend' ? ' active' : ''}`}
          onClick={() => onTabChange('hotend')}
        >
          {t('Hotend')}
        </button>
      </div>

      {selectedTab === 'filament'
        ? <FilamentActions actions={actions} onAction={onAction} />
        : <HotendActions actions={actions} onAction={onAction} />}
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

function HotendActions({
  actions,
  onAction,
}: {
  actions: AmsFilamentHotendActionState;
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
