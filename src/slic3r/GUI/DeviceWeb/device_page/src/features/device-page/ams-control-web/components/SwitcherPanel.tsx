import type { SwitcherArea } from '../types';

// Filament switch: two inputs multiplexed onto the extruders. Which slot reaches
// which input is drawn by the filament lines, from SlotLink.switcher_port, so this
// only marks where the switch body sits and carries its setup warning.
export function SwitcherPanel({ area }: { area: SwitcherArea }) {
  if (!area.visible && !area.show_setup_hint) return null;

  return (
    <div className="mt-2 flex flex-col items-center gap-1">
      {area.show_setup_hint ? (
        <div className="rounded border border-[#F5A623] bg-[#FFF7E6] px-2 py-1 text-[11px] text-[#8A5A00]">
          {area.setup_hint}
        </div>
      ) : null}
      {area.visible ? (
        <div className="h-4 w-10 rounded-sm border border-[#D0D0D0] bg-[#F8F8F8]" title="Filament switch" />
      ) : null}
    </div>
  );
}
