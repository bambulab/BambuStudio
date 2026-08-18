import { useTranslation } from 'react-i18next';
import type { AmsControlActions, ExtruderArea } from '../types';
import { cn } from './style';
import amsSettingsIconUrl from '../assets/toolbar_ams_settings.svg';

function ExtruderIcon({ color, loading }: { color: string; loading: boolean }) {
  return (
    <svg viewBox="0 0 28 36" className={cn('h-9 w-7', loading && 'animate-pulse')} aria-hidden>
      <rect x="8" y="2" width="12" height="8" rx="1.5" fill="#9A9A9A" />
      <path d="M6 10h16l-3 18H9L6 10Z" fill="#B0B0B0" />
      <path d="M11 28h6l-1.4 6h-3.2L11 28Z" fill="#8E8E8E" />
      {color ? <rect x="12" y="12" width="4" height="16" fill={color} /> : null}
    </svg>
  );
}

export function AmsFooter({
  actions,
  extruderArea,
  onSettings,
  onAutoRefill,
  onLoad,
  onUnload,
}: {
  actions: AmsControlActions;
  extruderArea: ExtruderArea;
  onSettings: () => void;
  onAutoRefill: () => void;
  onLoad: () => void;
  onUnload: () => void;
}) {
  const { t } = useTranslation();
  // The id doubles as the side, so the deputy extruder (1) is drawn first.
  const extruders = [...extruderArea.extruders].sort((a, b) => b.id - a.id);

  return (
    <div className="mt-1 flex items-center">
      <div className="flex min-w-[8rem] items-center gap-2">
        {actions.show_auto_refill ? (
          <button
            type="button"
            onClick={onAutoRefill}
            className="h-8 rounded border border-[#C2C2C2] px-3 text-sm text-[#1F1F1F]"
          >
            {t('Auto-refill')}
          </button>
        ) : null}
        {actions.show_settings ? (
          <button
            type="button"
            onClick={onSettings}
            className="flex h-8 w-8 items-center justify-center"
            aria-label={t('AMS Settings')}
          >
            <img src={amsSettingsIconUrl} alt="" className="h-6 w-6" />
          </button>
        ) : null}
      </div>
      <div className="flex flex-1 justify-center gap-1">
        {extruderArea.visible
          ? extruders.map((extruder) => (
            <ExtruderIcon
              key={extruder.id}
              color={extruder.has_filament ? extruder.filament_color : ''}
              loading={extruder.state === 'loading'}
            />
          ))
          : null}
      </div>
      <div className="flex min-w-[8rem] items-center justify-end gap-3">
        <button
          type="button"
          disabled={!actions.can_unload}
          title={actions.unload_tips}
          onClick={onUnload}
          className="h-8 rounded border border-[#C2C2C2] px-4 text-sm text-[#1F1F1F] disabled:cursor-not-allowed disabled:text-[#B0B0B0]"
        >
          {t('Unload')}
        </button>
        <button
          type="button"
          disabled={!actions.can_load}
          title={actions.load_tips}
          onClick={onLoad}
          className="h-8 rounded bg-[#00AE42] px-4 text-sm text-white disabled:cursor-not-allowed disabled:bg-[#D8D8D8] disabled:text-[#9A9A9A]"
        >
          {t('Load')}
        </button>
      </div>
    </div>
  );
}
