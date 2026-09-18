import { useTranslation } from 'react-i18next';
import type { FilamentPageActionState, FilamentPageMode, HotendPageActionState, AmsFilamentHotendTab } from '../types';
import { ToolbarMoreMenu } from './ToolbarMoreMenu';
import readAllIconUrl from '../assets/toolbar_read_all.svg';
import amsSettingsIconUrl from '../assets/toolbar_ams_settings.svg';

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
}: {
  selectedTab: AmsFilamentHotendTab;
  onTabChange: (tab: AmsFilamentHotendTab) => void;
  actions: ToolbarActions;
  mode: FilamentPageMode;
  onAction: (action: string) => void;
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
        >
          {t('Filament')}
        </button>
        <button
          type="button"
          data-testid="ams-hotend-tab-hotend"
          className={`tab${selectedTab === 'hotend' ? ' active' : ''}`}
          // Tabs stay clickable in batch mode; the page drops the pending
          // selection and returns to normal mode when the user leaves the
          // Filament tab, so the Hotend tab needs no manual Exit first.
          onClick={() => onTabChange('hotend')}
        >
          {t('Hotend')}
        </button>
      </div>

      {selectedTab === 'filament'
        ? (
          // In batch (load/unload) selection mode the top toolbar shows no
          // actions — Dry/commit/edit/exit all live on the BatchModeFooter.
          inBatchMode
            ? null
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
  // Rack-B "read all filament slots": visibility from show_read_all, enabled
  // from can_read_all_slots. Dispatches the dedicated slots-RFID action, not
  // the hotend-rack read-all.
  const canReadAllSlots = actions.can_read_all_slots ?? false;
  return (
    <div className="action-group action-group-icons">
      {actions.show_read_all && (
        <button
          type="button"
          className="toolbar-icon-btn"
          data-testid="ams-hotend-action-read-all"
          disabled={!canReadAllSlots}
          title={t('Read All')}
          onClick={() => onAction('read_all_slots_rfid')}
        >
          <img src={readAllIconUrl} alt="" aria-hidden="true" />
        </button>
      )}
      {actions.show_ams_settings && (
        <button
          type="button"
          className="toolbar-icon-btn"
          data-testid="ams-hotend-action-settings"
          title={t('AMS Settings')}
          onClick={() => onAction('settings')}
        >
          <img src={amsSettingsIconUrl} alt="" aria-hidden="true" />
        </button>
      )}
      <ToolbarMoreMenu actions={actions} onAction={onAction} />
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
