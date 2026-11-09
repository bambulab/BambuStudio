import { useCallback, useEffect, useState } from 'react';
import { useDeviceWebTheme } from '../../../hooks/useDeviceWebTheme';
import useStore from '../../../store/AppStore';
import { DebugPanel } from './components/DebugPanel';
import { Footer } from './components/Footer';
import { ManagerTab } from './components/ManagerTab';
import { PresetsTab } from './components/PresetsTab';
import { TabHeader } from './components/TabHeader';
import type { FilamentSelectTab } from './types';
import { useFilamentSelectBridge } from './useFilamentSelectBridge';
import './filament-select.css';

// Web version of FilamentSelectDialog. C++ owns the list contents, order and
// which rows are selectable; the page only keeps view state (tab, search, chip,
// selection) and submits the selection on Confirm, where C++ re-validates it.
export function FilamentSelectPage() {
  useDeviceWebTheme();
  const setTheme = useStore((s) => s.filament.setTheme);
  const { state, loaded, confirm, close, debugSnapshot } = useFilamentSelectBridge();
  const { manager, presets } = state;

  const [tab, setTab] = useState<FilamentSelectTab>('manager');
  const [query, setQuery] = useState('');
  const [activeChip, setActiveChip] = useState('all');
  const [chipOffset, setChipOffset] = useState(0);
  const [selectedRowKey, setSelectedRowKey] = useState('');
  const [selectedBrand, setSelectedBrand] = useState(presets.initial_brand);
  const [checkedAlias, setCheckedAlias] = useState(presets.checked_alias);

  const showDebug = import.meta.env.DEV
    || Boolean((window as Window & { __debugBuild?: boolean }).__debugBuild);

  useEffect(() => {
    setTheme(state.theme);
  }, [setTheme, state.theme]);

  // New snapshot -> same reset as the native fill_manager_tab() / fill_default_tab().
  useEffect(() => {
    setActiveChip('all');
    setChipOffset(0);
    setSelectedRowKey('');
  }, [manager]);

  useEffect(() => {
    setSelectedBrand(presets.initial_brand);
    setCheckedAlias(presets.checked_alias);
  }, [presets]);

  const onQueryChange = useCallback((next: string) => {
    setQuery(next);
    setChipOffset(0); // native apply_filters(reset_chip_offset = true)
  }, []);

  const onConfirm = useCallback(() => {
    void confirm(tab === 'manager'
      ? { tab: 'manager', row_key: selectedRowKey }
      : { tab: 'presets', alias: checkedAlias });
  }, [checkedAlias, confirm, selectedRowKey, tab]);

  const onClose = useCallback(() => {
    void close();
  }, [close]);

  // Focus sits in the WebView, so Esc never reaches the wx dialog; mirror the
  // native "Esc cancels" here.
  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onClose();
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [onClose]);

  const loadDebug = useCallback(() => debugSnapshot({
    tab,
    query,
    active_chip: activeChip,
    chip_offset: chipOffset,
    selected_row_key: selectedRowKey,
    selected_brand: selectedBrand,
    checked_alias: checkedAlias,
  }), [activeChip, checkedAlias, chipOffset, debugSnapshot, query, selectedBrand, selectedRowKey, tab]);

  return (
    <div className="fs-page" data-testid="filament-select-page" data-theme={state.theme}>
      <TabHeader active={tab} onChange={setTab} />
      <div className="fs-body">
        {tab === 'manager' ? (
          <ManagerTab
            manager={manager}
            loaded={loaded}
            query={query}
            onQueryChange={onQueryChange}
            activeChip={activeChip}
            onActiveChipChange={setActiveChip}
            chipOffset={chipOffset}
            onChipOffsetChange={setChipOffset}
            selectedRowKey={selectedRowKey}
            onSelectRow={setSelectedRowKey}
          />
        ) : (
          <PresetsTab
            presets={presets}
            selectedBrand={selectedBrand}
            onSelectBrand={setSelectedBrand}
            checkedAlias={checkedAlias}
            onCheckAlias={setCheckedAlias}
          />
        )}
      </div>
      <Footer onConfirm={onConfirm} onClose={onClose} />
      {showDebug && <DebugPanel load={loadDebug} />}
    </div>
  );
}
