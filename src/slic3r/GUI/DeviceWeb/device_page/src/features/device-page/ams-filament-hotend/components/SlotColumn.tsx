import type {
  AmsFilamentHotendNozzleState,
  AmsFilamentHotendSlotViewModel,
  FilamentNozzleInfo,
  FilamentPageMode,
} from '../types';
import { FilamentCard } from './FilamentCard';
import { FeedLine } from './FeedLine';
import { Nozzle } from './Nozzle';

export function SlotColumn({
  slot,
  lineHeight,
  mode,
  batchChecked,
  onToggleSelect,
  onAction,
}: {
  slot: AmsFilamentHotendSlotViewModel;
  lineHeight: number;
  mode: FilamentPageMode;
  batchChecked: boolean;
  onToggleSelect: (slot: AmsFilamentHotendSlotViewModel) => void;
  onAction: (action: string, slot: AmsFilamentHotendSlotViewModel) => void;
}) {
  const nozzleHint = nozzleHintKind(slot.filament_nozzle.state);
  return (
    <div
      className="slot-column"
      data-tier={slot.is_top_tier ? 'top' : 'bottom'}
    >
      <FilamentCard
        slot={slot}
        mode={mode}
        batchChecked={batchChecked}
        onToggleSelect={onToggleSelect}
        onAction={onAction}
      />
      <FeedLine
        height={lineHeight}
        state={slot.filament_line.state}
        color={slot.filament_line.filament_color || slot.filament_card.color}
        hasFilamentInNozzle={slot.filament_line.has_filament}
        useFeedLineDisplay={mode === 'normal'}
      />
      <div
        className="slot-nozzle"
        data-hint={nozzleHint}
        title={slot.filament_nozzle.tooltip || undefined}
        onClick={nozzleHint === 'dialog' ? () => onAction('nozzle_hint', slot) : undefined}
      >
        <Nozzle
          nozzleState={slot.filament_nozzle.state}
          displayIndex={slot.display_index}
          color={slot.filament_nozzle.filament_color || slot.filament_card.color}
          hasFilamentInNozzle={slot.filament_nozzle.has_filament}
          lineColor={slot.filament_line.filament_color || slot.filament_card.color}
          hasFilamentInLine={slot.filament_line.has_filament}
          onExtruder={slot.filament_nozzle.on_extruder}
          toolKind={slot.filament_nozzle.tool_kind}
        />
      </div>
      <NozzleCaption nozzle={slot.filament_nozzle} />
    </div>
  );
}

// Diameter + flow caption under the nozzle (Filament tab payload has no op_state).
function NozzleCaption({ nozzle }: { nozzle: FilamentNozzleInfo }) {
  const kind = nozzle.tool_kind ?? 'empty';
  const isMuted =
    kind === 'empty' ||
    kind === 'unknown' ||
    nozzle.state === 'None' ||
    nozzle.state === 'Unknown' ||
    nozzle.state === 'Lost';
  return (
    <div className={`slot-nozzle-caption${isMuted ? ' muted' : ''}`}>
      <div className="slot-nozzle-caption-name">{nozzle.nozzle_label || '--'}</div>
      <div className="slot-nozzle-caption-sub">{nozzle.nozzle_subtext || '\u00A0'}</div>
    </div>
  );
}

// Lost only explains itself on hover; the other alert states open a native hint dialog on click.
function nozzleHintKind(state: AmsFilamentHotendNozzleState): 'blocked' | 'dialog' | undefined {
  if (state === 'Lost') return 'blocked';
  if (state === 'Error' || state === 'FeedSuspect' || state === 'Unknown') return 'dialog';
  return undefined;
}
