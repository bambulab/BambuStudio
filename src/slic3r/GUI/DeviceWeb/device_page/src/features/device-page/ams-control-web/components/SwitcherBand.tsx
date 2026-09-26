import { SWITCHER, px } from '../dip';
import type { SwitcherArea } from '../types';
import { filaSwitchUrl } from '../assets';

// Icon only. C++ keeps m_switcher in m_sizer_switcher_option between the
// DownRoad and the extruder row; the setup banner is a separate tipPanel.
export function SwitcherBand({ switcher }: { switcher: SwitcherArea }) {
  if (!switcher.visible) return null;

  return (
    <div
      className="flex shrink-0 items-center justify-center"
      style={{ height: px(SWITCHER.height) }}
    >
      <img
        src={filaSwitchUrl}
        alt=""
        aria-hidden
        data-testid="ams-switcher"
        style={{ width: px(SWITCHER.width), height: px(SWITCHER.height) }}
      />
    </div>
  );
}

// AMSControl::show_switcher_status: full-width orange bar under the nozzles.
export function SwitcherSetupHint({ switcher }: { switcher: SwitcherArea }) {
  if (!switcher.show_setup_hint || !switcher.setup_hint) return null;

  return (
    <div
      data-testid="ams-switcher-setup-hint"
      className="mt-[5px] box-border flex w-full items-center"
      style={{ background: '#FF9900' }}
    >
      <span
        aria-hidden
        className="ml-2 flex h-4 w-4 shrink-0 items-center justify-center rounded-full bg-white text-[11px] font-bold leading-none"
        style={{ color: '#FF9900' }}
      >
        i
      </span>
      <span className="px-2 py-2 text-[13px] font-bold leading-4 text-white whitespace-nowrap">
        {switcher.setup_hint}
      </span>
    </div>
  );
}
