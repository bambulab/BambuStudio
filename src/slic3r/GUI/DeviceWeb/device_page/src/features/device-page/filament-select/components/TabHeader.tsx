import { useTranslation } from 'react-i18next';
import type { FilamentSelectTab } from '../types';

interface TabHeaderProps {
  active: FilamentSelectTab;
  onChange: (tab: FilamentSelectTab) => void;
}

export function TabHeader({ active, onChange }: TabHeaderProps) {
  const { t } = useTranslation();
  const tabs: Array<{ key: FilamentSelectTab; label: string }> = [
    { key: 'manager', label: t('Filament Manager') },
    { key: 'presets', label: t('System presets') },
  ];

  return (
    <>
      <div className="fs-tabs" role="tablist">
        {tabs.map((tab) => {
          const isActive = tab.key === active;
          return (
            <div key={tab.key} className="fs-tab-unit">
              <button
                type="button"
                role="tab"
                aria-selected={isActive}
                data-testid={`filament-select-tab-${tab.key}`}
                className={isActive ? 'fs-tab fs-tab--active' : 'fs-tab'}
                onClick={() => onChange(tab.key)}
              >
                {tab.label}
              </button>
              <span className={isActive ? 'fs-tab-underline fs-tab-underline--active' : 'fs-tab-underline'} />
            </div>
          );
        })}
      </div>
      <div className="fs-separator" />
    </>
  );
}
