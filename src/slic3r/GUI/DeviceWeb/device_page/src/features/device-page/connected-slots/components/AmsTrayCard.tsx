import type { MouseEvent as ReactMouseEvent } from 'react';
import { LoadingArcIcon } from '../../ams-filament-hotend/components/LoadingArcIcon';
import { SpoolDisc } from '../../ams-filament-hotend/components/SpoolDisc';
import { useCardContextMenu } from '../../ams-filament-hotend/useCardContextMenu';
import type { AmsFilamentHotendSlotViewModel } from '../../ams-filament-hotend/types';
import type { ConnectedSlotsTrayAction, ConnectedSlotsTrayInfo } from '../types';
import { TrayContextMenu, hasTrayMenuItems } from './TrayContextMenu';

function trayToSlot(tray: ConnectedSlotsTrayInfo, index: number): AmsFilamentHotendSlotViewModel {
  const state =
    tray.tray_state === 'empty' ? 'Empty' :
    tray.tray_state === 'reading' ? 'Reading' :
    tray.tray_state === 'unknown' ? 'Unknown' :
    tray.tray_state === 'error' ? 'Error' :
    tray.tray_state === 'loading' ? 'Loading' :
    tray.tray_state === 'unloading' ? 'Unloading' :
    'ReadyToLoad';

  return {
    addr_id: index,
    display_index: Number(tray.slot_id) + 1 || index + 1,
    ams_id: tray.ams_id,
    slot_id: tray.slot_id,
    is_top_tier: false,
    enabled: tray.exists,
    filament_card: {
      state,
      color: tray.color || '#d9d9d9',
      color_type: tray.color_type,
      colors: tray.colors,
      text: tray.filament_name,
      name: tray.filament_name,
      type: tray.filament_type,
      remain: typeof tray.remain === 'number' ? tray.remain : 100,
      status_text: '',
    },
    filament_line: { state: 'no_filament', has_filament: false, filament_color: '' },
    filament_nozzle: { state: 'Normal', has_filament: false, filament_color: '', on_extruder: false },
  };
}

export function TrayIndexBadge({ index }: { index: number }) {
  return <span className="cs-tray-index-badge">{index}</span>;
}

export function TrayFilamentLabel({ tray }: { tray: ConnectedSlotsTrayInfo }) {
  const text =
    tray.tray_state === 'unknown' ? '?' :
    tray.tray_state === 'empty' ? '' :
    (tray.filament_name || tray.filament_type || '--');
  return (
    <span className={`cs-tray-filament-label${tray.tray_state === 'unknown' ? ' cs-tray-filament-label--unknown' : ''}`}>
      {text}
    </span>
  );
}

export function AmsTrayCard({
  tray,
  index,
  showHead,
  isFeed = false,
  onAction,
}: {
  tray: ConnectedSlotsTrayInfo;
  index: number;
  showHead: boolean;
  isFeed?: boolean;
  onAction?: (action: ConnectedSlotsTrayAction, tray: ConnectedSlotsTrayInfo) => void;
}) {
  const slot = trayToSlot(tray, index);
  const hasMenu = hasTrayMenuItems(tray);
  const menu = useCardContextMenu({ disabled: !hasMenu });
  // The spool disc keeps its loaded shape while in transit; the spinning arc in
  // the card corner is what conveys the motion (mirrors FilamentCard).
  const isTransporting = tray.tray_state === 'loading' || tray.tray_state === 'unloading';

  // Left-click opens the same menu as right-click and long-press, mirroring
  // FilamentCard in normal mode. openAt is a no-op while the menu is disabled.
  const handleBodyClick = (event: ReactMouseEvent<HTMLDivElement>) => {
    menu.openAt(event.clientX, event.clientY);
  };

  return (
    <div
      className={`cs-ams-tray-card${hasMenu ? ' cs-ams-tray-card--has-menu' : ''}`}
      data-testid={`ams-tray-card-${tray.ams_id}-${tray.slot_id}`}
      data-cs-tray-anchor=""
      data-cs-feed={isFeed ? 'true' : undefined}
      onClick={handleBodyClick}
      {...menu.bind}
    >
      {showHead && <TrayIndexBadge index={index} />}
      {isTransporting && (
        <span className="cs-tray-loading-arc">
          <LoadingArcIcon />
        </span>
      )}
      <SpoolDisc slot={slot} />
      <TrayFilamentLabel tray={tray} />
      {menu.open && menu.anchor && (
        <TrayContextMenu
          tray={tray}
          anchor={menu.anchor}
          onClose={menu.close}
          onAction={(action) => onAction?.(action, tray)}
        />
      )}
    </div>
  );
}
