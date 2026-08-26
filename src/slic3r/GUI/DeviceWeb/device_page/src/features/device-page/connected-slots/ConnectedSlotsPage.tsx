import { useEffect, useMemo, useRef } from 'react';
import { useConnectedSlotsBridge } from './useConnectedSlotsBridge';
import { useDeviceWebTheme } from '../../../hooks/useDeviceWebTheme';
import useStore from '../../../store/AppStore';
import { AmsUnitBoard } from './components/AmsUnitBoard';
import { ConnectedSourceSchematic } from './components/ConnectedSourceSchematic';
import { CopyReportButton } from './components/CopyReportButton';
import { HotendFilamentPath } from './components/HotendFilamentPath';
import { HotendSlotSchematic } from './components/HotendSlotSchematic';
import type { ConnectedSlotsSlotViewModel, ConnectedSlotsTrayAction, ConnectedSlotsTrayInfo } from './types';
import './connected-slots.css';

function parseAddrIdFromHash(): number | undefined {
  const hash = window.location.hash;
  const query = hash.includes('?') ? hash.slice(hash.indexOf('?') + 1) : '';
  const raw = new URLSearchParams(query).get('addr_id');
  if (raw === null || raw === '') return undefined;
  const n = Number(raw);
  return Number.isFinite(n) ? n : undefined;
}

function SlotView({
  slot,
  onSelectSource,
  onTrayAction,
}: {
  slot: ConnectedSlotsSlotViewModel;
  onSelectSource: (extIndex: number) => void;
  onTrayAction: (action: ConnectedSlotsTrayAction, tray: ConnectedSlotsTrayInfo) => void;
}) {
  const stageRef = useRef<HTMLDivElement>(null);
  return (
    <>
      <div className="cs-schematic-bar">
        <HotendSlotSchematic schematic={slot.top_slots_area.left_slot.schematic} />
        <ConnectedSourceSchematic
          schematic={slot.top_slots_area.right_slots.schematic}
          onSelectSource={onSelectSource}
        />
      </div>
      <div className="cs-body" ref={stageRef}>
        <div className="cs-boards">
          <div className="cs-boards__left">
            <AmsUnitBoard column={slot.top_slots_area.left_slot} side="left" onTrayAction={onTrayAction} />
          </div>
          <div className="cs-boards__right">
            <AmsUnitBoard column={slot.top_slots_area.right_slots} side="right" onTrayAction={onTrayAction} />
          </div>
        </div>
        <HotendFilamentPath slot={slot} stageRef={stageRef} />
      </div>
    </>
  );
}

export function ConnectedSlotsPage() {
  useDeviceWebTheme();
  const setTheme = useStore((s) => s.filament.setTheme);
  const addrId = useMemo(() => parseAddrIdFromHash(), []);
  const { state, error, getReport, selectSource, runTrayAction } = useConnectedSlotsBridge(addrId);
  // Diagnostics affordance: Debug builds and the vite dev server only, never an
  // internal or public Release package.
  const showCopyReport = import.meta.env.DEV
    || Boolean((window as Window & { __debugBuild?: boolean }).__debugBuild);

  useEffect(() => {
    if (state.theme === 'dark' || state.theme === 'light') setTheme(state.theme);
  }, [setTheme, state.theme]);

  return (
    <div className="cs-page" data-testid="connected-slots-page" data-theme={state.theme}>
      {error && <div className="cs-page__error">{error}</div>}
      {state.slots.map((slot) => (
        <SlotView
          key={slot.addr_id}
          slot={slot}
          onSelectSource={(extIndex) => void selectSource(extIndex)}
          onTrayAction={(action, tray) => void runTrayAction(action, tray.ams_id, tray.slot_id)}
        />
      ))}
      {showCopyReport && <CopyReportButton getReport={getReport} />}
    </div>
  );
}
