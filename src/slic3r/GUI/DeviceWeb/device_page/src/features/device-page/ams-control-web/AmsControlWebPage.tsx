import { useState } from 'react';
import { useAmsControlWebBridge } from './useAmsControlWebBridge';
import type { PreviewItem, SlotView } from './types';
import { columnExtSlots, columnPreviewItems, columnUnits, panelColumns } from './layout';
import { AmsFooter } from './components/AmsFooter';
import { AmsPreviewBar } from './components/AmsPreviewBar';
import { AmsUnitPanel } from './components/AmsUnitPanel';
import type { SlotHandlers } from './components/SlotCard';
import { ExtPanel } from './components/ExtPanel';
import { SwitcherPanel } from './components/SwitcherPanel';
import { cn } from './components/style';

export function AmsControlWebPage() {
  const {
    state,
    error,
    selectSlot,
    switchAms,
    editSlot,
    readSlot,
    openSettings,
    openAutoRefill,
    loadFilament,
    unloadFilament,
    openBridgeDebugDialog,
  } = useAmsControlWebBridge();
  const { display } = state;
  const { ams_ext_area: amsExt, ams_preview_area: preview, filament_line_area: lineArea } = display;
  const [debugOpening, setDebugOpening] = useState(false);
  const showDebugButton = Boolean((window as Window & { __internalBuild?: boolean }).__internalBuild);

  // C++ resolves the columns and which unit each one views, so the page only
  // reports what the user clicked and re-renders from the state it gets back.
  const columns = panelColumns(amsExt);
  // left_right splits the width down the middle and centres each half on its own,
  // the way the classic panel gives every extruder an equal share. A half with
  // nothing in it still holds its ground, so the divide does not drift.
  const splitColumns = amsExt.layout.layout_style === 'left_right';

  const handlers: SlotHandlers = {
    onSelect: (slot: SlotView) => { void selectSlot(slot.ams_id, slot.slot_id); },
    onEdit: (slot: SlotView) => { void editSlot(slot.ams_id, slot.slot_id); },
    onRead: (slot: SlotView) => { void readSlot(slot.ams_id, slot.slot_id); },
  };

  // Picking a unit in the strip changes which unit its column shows and drops the
  // slot selection, so nothing stays picked behind a card that is no longer drawn.
  const onPreview = (item: PreviewItem) => {
    void switchAms(item.ams_id);
  };

  // The footer commands act on the slot C++ resolved as selected, which is the
  // slot the gate behind can_load / can_unload was computed for. With nothing
  // picked both are false, so neither button can fire on empty ids.
  const onLoad = () => { void loadFilament(state.selected_ams_id, state.selected_slot_id); };
  const onUnload = () => { void unloadFilament(state.selected_ams_id, state.selected_slot_id); };

  if (!display.visible) {
    return <div data-testid="ams-control-web-page" className="hidden" />;
  }

  return (
    <div
      data-testid="ams-control-web-page"
      className="flex w-full flex-col bg-white px-3 py-2 text-[#1F1F1F]"
    >
      {/* The debug button is out of flow so it cannot shift the halves off centre. */}
      <div className="relative flex min-h-6 items-center">
        {preview.visible ? (
          <div className={cn('flex w-full items-center', !splitColumns && 'gap-4')}>
            {columns.map((column) => (
              <div key={column.pos} className={cn('flex items-center', splitColumns && 'flex-1')}>
                <AmsPreviewBar
                  items={columnPreviewItems(column, preview.items)}
                  onSelect={onPreview}
                />
              </div>
            ))}
          </div>
        ) : null}
        {showDebugButton ? (
          <button
            type="button"
            data-testid="ams-control-web-debug"
            disabled={debugOpening}
            className="absolute right-0 top-0 h-6 shrink-0 rounded-full border border-[#C2C2C2] bg-white px-2.5 text-xs text-[#1F1F1F] hover:bg-[#F5F5F5] disabled:opacity-60"
            onClick={async () => {
              setDebugOpening(true);
              try {
                await openBridgeDebugDialog(state);
              } finally {
                setDebugOpening(false);
              }
            }}
          >
            {debugOpening ? 'Opening...' : 'Debug'}
          </button>
        ) : null}
      </div>

      {amsExt.visible ? (
        <div className={cn('mt-3 flex items-start', !splitColumns && 'justify-center')}>
          {columns.map((column) => (
            <div
              key={column.pos}
              className={cn('flex items-start gap-6', splitColumns && 'flex-1 justify-center')}
            >
              {columnUnits(column, amsExt.units).map((unit) => (
                <AmsUnitPanel key={unit.ams_id} unit={unit} lineArea={lineArea} handlers={handlers} />
              ))}
              {columnExtSlots(column, amsExt.ext_slots).map((slot) => (
                <ExtPanel
                  key={`${slot.ams_id}-${slot.slot_id}`}
                  slot={slot}
                  lineArea={lineArea}
                  handlers={handlers}
                />
              ))}
            </div>
          ))}
        </div>
      ) : null}

      <SwitcherPanel area={display.switcher_area} />
      <AmsFooter
        actions={state.actions}
        extruderArea={display.extruder_area}
        onSettings={() => { void openSettings(); }}
        onAutoRefill={() => { void openAutoRefill(); }}
        onLoad={onLoad}
        onUnload={onUnload}
      />
      {error ? <div className="mt-1 text-xs text-red-600">{error}</div> : null}
    </div>
  );
}
