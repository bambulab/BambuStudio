import { useCallback, useEffect, useLayoutEffect, useMemo, useRef, type RefObject } from 'react';
import { useConnectedSlotsBridge } from './useConnectedSlotsBridge';
import { useDeviceWebTheme } from '../../../hooks/useDeviceWebTheme';
import { documentZoom, useFitHostWidth } from '../../../hooks/useFitHostWidth';
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

// Boards are sized by their content, so their centres move. Shift each schematic
// horizontally onto its board's centre; transform keeps the stage geometry (and
// the routes measured against it) untouched.
function useAlignSchematicsToBoards(
  barRef: RefObject<HTMLDivElement | null>,
  stageRef: RefObject<HTMLDivElement | null>,
  revision: unknown,
  alignRightSchematic: boolean,
) {
  const align = useCallback(() => {
    const bar = barRef.current;
    const stage = stageRef.current;
    if (!bar || !stage) return;
    const barLeft = bar.getBoundingClientRect().left;
    const zoom = documentZoom();
    const sides = ['left', 'right'] as const;
    sides.forEach((side, i) => {
      const schematic = bar.children[i] as HTMLElement | undefined;
      if (!schematic) return;
      if (side === 'right' && !alignRightSchematic) {
        // Keep the right source strip left-anchored in the fixed four-slot mode.
        schematic.style.transform = '';
        return;
      }
      const board = stage.querySelector<HTMLElement>(`[data-cs-board-anchor="${side}"]`);
      if (!board) return;
      const boardRect = board.getBoundingClientRect();
      const boardCenter = (boardRect.left + boardRect.width / 2 - barLeft) / zoom;
      const schematicCenter = schematic.offsetLeft + schematic.offsetWidth / 2;
      schematic.style.transform = `translateX(${boardCenter - schematicCenter}px)`;
    });
  }, [alignRightSchematic, barRef, stageRef]);

  useLayoutEffect(() => {
    align();
  }, [align, revision]);

  useEffect(() => {
    const stage = stageRef.current;
    if (!stage || typeof ResizeObserver !== 'function') return;
    const observer = new ResizeObserver(() => align());
    observer.observe(stage);
    return () => observer.disconnect();
  }, [align, stageRef]);
}

function SlotView({
  slot,
  onSelectSource,
  onDry,
  onTrayAction,
}: {
  slot: ConnectedSlotsSlotViewModel;
  onSelectSource: (extIndex: number) => void;
  onDry: (amsId: string) => void;
  onTrayAction: (action: ConnectedSlotsTrayAction, tray: ConnectedSlotsTrayInfo) => void;
}) {
  const stageRef = useRef<HTMLDivElement>(null);
  const barRef = useRef<HTMLDivElement>(null);
  const rightSchematicItems = slot.top_slots_area.right_slots.schematic.items ?? [];
  const rightHasFourSlotAms = rightSchematicItems.some(
    (item) => !!item.ams_id && item.slot_count === 4,
  );
  const rightUnit = slot.top_slots_area.right_slots.unit;
  // Fallback for payloads that do not expose right schematic items.
  const useFixedFourSlotLayout = rightHasFourSlotAms
    || (!!rightUnit && rightUnit.trays.length === 4);
  useAlignSchematicsToBoards(barRef, stageRef, slot, !useFixedFourSlotLayout);

  return (
    <div className={useFixedFourSlotLayout ? 'cs-slot-view cs-slot-view--fixed-four-slot' : 'cs-slot-view'}>
      <div className="cs-schematic-bar" ref={barRef}>
        <HotendSlotSchematic schematic={slot.top_slots_area.left_slot.schematic} />
        <ConnectedSourceSchematic
          schematic={slot.top_slots_area.right_slots.schematic}
          onSelectSource={onSelectSource}
        />
      </div>
      <div className="cs-body" ref={stageRef}>
        <div className="cs-boards">
          <div className="cs-boards__left">
            <AmsUnitBoard column={slot.top_slots_area.left_slot} side="left" onDry={onDry} onTrayAction={onTrayAction} />
          </div>
          <div className="cs-boards__right">
            <AmsUnitBoard column={slot.top_slots_area.right_slots} side="right" onDry={onDry} onTrayAction={onTrayAction} />
          </div>
        </div>
        <HotendFilamentPath slot={slot} stageRef={stageRef} />
      </div>
    </div>
  );
}

// ConnectedSlotsDialog is a fixed FromDIP(560) wide host.
const HOST_WIDTH_DIP = 560;

export function ConnectedSlotsPage() {
  useDeviceWebTheme();
  useFitHostWidth(HOST_WIDTH_DIP);
  const setTheme = useStore((s) => s.filament.setTheme);
  const addrId = useMemo(() => parseAddrIdFromHash(), []);
  const { state, error, getReport, selectSource, openDryControls, runTrayAction } = useConnectedSlotsBridge(addrId);
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
      <div className="cs-page__content">
        {state.slots.map((slot) => (
          <SlotView
            key={slot.addr_id}
            slot={slot}
            onSelectSource={(extIndex) => void selectSource(extIndex)}
            onDry={(amsId) => void openDryControls(amsId)}
            onTrayAction={(action, tray) => void runTrayAction(action, tray.ams_id, tray.slot_id)}
          />
        ))}
        {showCopyReport && (
          <div className="cs-page__actions">
            <CopyReportButton getReport={getReport} />
          </div>
        )}
      </div>
    </div>
  );
}
