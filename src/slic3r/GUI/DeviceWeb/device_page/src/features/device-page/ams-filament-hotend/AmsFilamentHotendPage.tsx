import { useMemo, useState } from 'react';
import { useAmsFilamentHotendBridge } from './useAmsFilamentHotendBridge';
import type { AmsFilamentHotendSlotViewModel, AmsFilamentHotendTab } from './types';
import { PageToolbar } from './components/PageToolbar';
import { Stage } from './components/Stage';
import { HotendPanel } from './components/HotendPanel';
import { PageFooter } from './components/PageFooter';
import './ams-filament-hotend.css';

export function AmsFilamentHotendPage() {
  const { state, error, runSlotAction } = useAmsFilamentHotendBridge();
  const [tab, setTab] = useState<AmsFilamentHotendTab>('filament');

  const hotendSlots = useMemo(
    () => [...state.hotend_slots].sort((a, b) => a.display_index - b.display_index),
    [state.hotend_slots],
  );
  const topSlots = hotendSlots.filter((s) => s.is_top_tier);
  const bottomSlots = hotendSlots.filter((s) => !s.is_top_tier);

  const handleToolbarAction = (action: string) => runSlotAction(action);

  const handleSlotAction = (action: string, slot?: AmsFilamentHotendSlotViewModel) =>
    runSlotAction(action, slot);

  return (
    <div className="ams-filament-hotend-page" data-testid="ams-filament-hotend-page">
      <PageToolbar
        selectedTab={tab}
        onTabChange={setTab}
        actions={state.actions}
        onAction={handleToolbarAction}
      />

      {error && <div className="page-error">{error}</div>}

      {tab === 'filament' ? (
        <Stage
          topSlots={topSlots}
          bottomSlots={bottomSlots}
          hotendMode={false}
          onAction={handleSlotAction}
        />
      ) : (
        <HotendPanel
          slots={hotendSlots}
          onAction={handleSlotAction}
        />
      )}

      <PageFooter actions={state.actions} onAction={handleToolbarAction} />
    </div>
  );
}
