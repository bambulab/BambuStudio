import { COLORS, px } from '../dip';
import {
  extruderBitmapOffset,
  extruderBitmapSize,
  extruderIconSize,
  nozzleHoleFill,
  roadFilamentColor,
} from '../geometry';
import type { ExtruderView } from '../types';
import { leftNozzleUrl, rightNozzleUrl, singleNozzleNUrl, singleNozzleXpUrl } from '../assets';

const ICONS: Record<string, string> = {
  left_nozzle: leftNozzleUrl,
  right_nozzle: rightNozzleUrl,
  single_nozzle_n: singleNozzleNUrl,
  single_nozzle_xp: singleNozzleXpUrl,
};

export function ExtruderIcon({ extruder, icon }: { extruder: ExtruderView; icon: string }) {
  const cell = extruderIconSize(icon);
  const bmp = extruderBitmapSize(icon);
  const origin = extruderBitmapOffset(icon);
  const hole = nozzleHoleFill(icon);
  // C++ OnAmsLoading(true, colour) paints the hole whenever filament is in the
  // extruder, not only during the load animation. Empty stays *wxWHITE.
  const filled = extruder.has_filament;
  return (
    <div
      className="relative shrink-0 overflow-hidden"
      style={{ width: px(cell.width), height: px(cell.height) }}
    >
      <span
        className="absolute block rounded-full"
        style={{
          // Paint only the evenodd cutout. A full-width rect shows through the
          // transparent sides of the dual-nozzle SVGs in dark mode.
          left: px(hole.left),
          top: px(hole.top),
          width: px(hole.width),
          height: px(hole.height),
          background: filled
            ? roadFilamentColor(extruder.filament_color) || COLORS.brand
            : COLORS.white,
        }}
      />
      <img
        src={ICONS[icon] ?? singleNozzleXpUrl}
        alt=""
        aria-hidden
        className="absolute block"
        style={{
          left: px(origin.x),
          top: px(origin.y),
          width: px(bmp.width),
          height: px(bmp.height),
          maxWidth: 'none',
        }}
      />
    </div>
  );
}
