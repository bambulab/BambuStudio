import { useTranslation } from 'react-i18next';
import { isRowVisible, normalizeQuery } from '../chipPaging';
import type { FilamentSelectManager } from '../types';
import { ChipBar } from './ChipBar';
import { SpoolRow } from './SpoolRow';

interface ManagerTabProps {
  manager: FilamentSelectManager;
  loaded: boolean;
  query: string;
  onQueryChange: (query: string) => void;
  activeChip: string;
  onActiveChipChange: (key: string) => void;
  chipOffset: number;
  onChipOffsetChange: (offset: number) => void;
  selectedRowKey: string;
  onSelectRow: (rowKey: string) => void;
}

export function ManagerTab({
  manager,
  loaded,
  query,
  onQueryChange,
  activeChip,
  onActiveChipChange,
  chipOffset,
  onChipOffsetChange,
  selectedRowKey,
  onSelectRow,
}: ManagerTabProps) {
  const { t } = useTranslation();
  const normalizedQuery = normalizeQuery(query);

  return (
    <div className="fs-manager" data-testid="filament-select-manager">
      <div className="fs-search">
        <input
          type="text"
          className="fs-search__input"
          data-testid="filament-select-search"
          placeholder={t('Search filament')}
          value={query}
          spellCheck={false}
          onChange={(e) => onQueryChange(e.target.value)}
        />
      </div>
      <ChipBar
        chips={manager.chips}
        rows={manager.rows}
        normalizedQuery={normalizedQuery}
        activeKey={activeChip}
        onSelect={onActiveChipChange}
        offset={chipOffset}
        onOffsetChange={onChipOffsetChange}
      />
      <div className="fs-list" role="listbox" data-testid="filament-select-list">
        {loaded && manager.rows.length === 0 && (
          <div className="fs-empty" data-testid="filament-select-empty">{t('No filaments')}</div>
        )}
        {manager.rows
          .filter((row) => isRowVisible(row, activeChip, normalizedQuery))
          .map((row) => (
            <SpoolRow
              key={row.row_key}
              row={row}
              selected={row.row_key === selectedRowKey}
              onSelect={onSelectRow}
            />
          ))}
      </div>
    </div>
  );
}
