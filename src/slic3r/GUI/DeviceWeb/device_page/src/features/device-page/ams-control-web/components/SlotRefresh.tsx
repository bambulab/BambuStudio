import { useEffect, useState } from 'react';
import { COLORS, REFRESH_ICON, SLOT_REFRESH, px } from '../dip';
import type { SlotView } from '../types';
import { amsRefreshNormalUrl, amsRefreshSelectedUrl } from '../assets';

// C++ AMSrefresh: 28x28 hitbox, ScalableBitmap(..., 32) drawn centered.
// Tailwind preflight sets img { max-width: 100% }, which would squash the
// 32px glyph into the 28px button and make it look smaller than C++.
const OPTIMISTIC_READING_MS = 8000;

const iconStyle = {
  width: px(REFRESH_ICON),
  height: px(REFRESH_ICON),
  maxWidth: 'none',
  maxHeight: 'none',
  left: '50%',
  top: '50%',
  objectFit: 'contain',
} as const;

export function SlotRefresh({
  slot,
  onRefresh,
}: {
  slot: SlotView;
  onRefresh: (slot: SlotView) => void;
}) {
  const [optimisticReading, setOptimisticReading] = useState(false);
  const playing = slot.reading || optimisticReading;

  useEffect(() => {
    if (slot.reading) setOptimisticReading(false);
  }, [slot.reading]);

  useEffect(() => {
    if (!optimisticReading) return;
    const timer = window.setTimeout(() => setOptimisticReading(false), OPTIMISTIC_READING_MS);
    return () => window.clearTimeout(timer);
  }, [optimisticReading]);

  if (!slot.show_rfid) return null;

  return (
    <button
      type="button"
      aria-label={slot.label || 'Read RFID'}
      data-testid={`ams-rfid-${slot.ams_id}-${slot.slot_id}`}
      className="ams-icon-swap relative flex shrink-0 items-center justify-center self-center overflow-visible border-0 bg-transparent p-0"
      style={{
        width: px(SLOT_REFRESH.size),
        height: px(SLOT_REFRESH.size),
        minWidth: px(SLOT_REFRESH.size),
        maxWidth: px(SLOT_REFRESH.size),
        minHeight: px(SLOT_REFRESH.size),
        maxHeight: px(SLOT_REFRESH.size),
      }}
      onClick={(e) => {
        e.stopPropagation();
        if (playing) return;
        setOptimisticReading(true);
        onRefresh(slot);
      }}
    >
      {playing ? (
        <img
          src={amsRefreshSelectedUrl}
          alt=""
          aria-hidden
          className="ams-rfid-spinning pointer-events-none absolute"
          style={iconStyle}
        />
      ) : (
        <>
          <img
            src={amsRefreshNormalUrl}
            alt=""
            aria-hidden
            className="ams-icon-base pointer-events-none absolute"
            style={{ ...iconStyle, transform: 'translate(-50%, -50%)' }}
          />
          <img
            src={amsRefreshSelectedUrl}
            alt=""
            aria-hidden
            className="ams-icon-hover pointer-events-none absolute"
            style={{ ...iconStyle, transform: 'translate(-50%, -50%)' }}
          />
        </>
      )}
      {slot.label ? (
        <span
          aria-hidden
          className="pointer-events-none relative z-[1] select-none text-center"
          style={{
            fontSize: px(10),
            lineHeight: 1,
            color: COLORS.gray700,
          }}
        >
          {slot.label}
        </span>
      ) : null}
    </button>
  );
}
