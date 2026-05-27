import { useTranslation } from 'react-i18next';
import type { AmsFilamentHotendSlotViewModel, FilamentPageMode } from '../types';

// Mode-specific commit-action labels. The Web side does not know the
// underlying machine command — it just labels the button per mode and
// relays the click + selected-slot list to C++ which dispatches the
// matching MQTT command.
const COMMIT_LABEL: Record<FilamentPageMode, string> = {
  normal: '',
  batch_ams_load: 'Auto Load',
  batch_ext_load: 'Load External Spool',
  batch_unload: 'Unload',
};

export function BatchModeFooter({
  mode,
  selectableSlots,
  selectedKeys,
  onSelectAll,
  onClearSelection,
  onCommit,
  onEdit,
}: {
  mode: FilamentPageMode;
  selectableSlots: AmsFilamentHotendSlotViewModel[];
  selectedKeys: ReadonlySet<string>;
  // Toggles the master "select all" checkbox: ticks every selectable slot
  // when not all-selected, clears selection when fully-selected.
  onSelectAll: () => void;
  onClearSelection: () => void;
  onCommit: () => void;
  onEdit?: () => void;
}) {
  const { t } = useTranslation();
  if (mode === 'normal') return null;

  const selectedCount = selectedKeys.size;
  const totalSelectable = selectableSlots.length;
  // "checked" only when every selectable slot is in the set; "indeterminate"
  // when some are selected. Visually we model both check states via class.
  const allSelected = totalSelectable > 0 && selectedCount === totalSelectable;
  const someSelected = selectedCount > 0 && selectedCount < totalSelectable;
  const canCommit = selectedCount > 0;
  const showEdit = mode === 'batch_ext_load';

  const commitLabel = t(COMMIT_LABEL[mode] || 'Confirm');

  return (
    <footer
      className="batch-mode-footer"
      data-testid="ams-hotend-batch-footer"
      data-mode={mode}
    >
      <button
        type="button"
        role="checkbox"
        aria-checked={allSelected ? 'true' : (someSelected ? 'mixed' : 'false')}
        className={`batch-select-all${allSelected ? ' checked' : ''}${someSelected ? ' indeterminate' : ''}`}
        data-testid="ams-hotend-batch-select-all"
        // No selectable slots → toggling does nothing; keep the control
        // visible but disabled so the layout stays stable.
        disabled={totalSelectable === 0}
        onClick={() => {
          if (totalSelectable === 0) return;
          if (allSelected) {
            onClearSelection();
          } else {
            onSelectAll();
          }
        }}
      >
        <span className="batch-select-all-box" aria-hidden="true">
          {allSelected && (
            <svg width={12} height={12} viewBox="0 0 12 12">
              <path d="M2.5 6.2 5 8.7 9.5 3.6" fill="none" stroke="currentColor" strokeWidth={1.6} strokeLinecap="round" strokeLinejoin="round" />
            </svg>
          )}
          {someSelected && <span className="batch-select-all-dash" />}
        </span>
        <span className="batch-select-all-label">{t('Select All')}</span>
      </button>

      <span className="batch-select-count" data-testid="ams-hotend-batch-count">
        {t('{{n}} selected', { n: selectedCount })}
      </span>

      <button
        type="button"
        className={`batch-commit${canCommit ? ' active' : ''}`}
        data-testid="ams-hotend-batch-commit"
        disabled={!canCommit}
        onClick={() => {
          if (!canCommit) return;
          onCommit();
        }}
      >
        {commitLabel}
      </button>
      {showEdit && (
        <button
          type="button"
          className={`batch-edit${selectedCount > 0 ? ' active' : ''}`}
          data-testid="ams-hotend-batch-edit"
          disabled={selectedCount === 0}
          onClick={() => {
            if (selectedCount === 0) return;
            onEdit?.();
          }}
        >
          {t('Edit Filament')}
        </button>
      )}
    </footer>
  );
}
