import { useLayoutEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { COLORS, SLOT_LIB, SLOT_LIB_LITE, SLOT_REMAIN_LINE, TRAY_ICON, px } from '../dip';
import { LITE_COLOR_INSET, LITE_EXT_COLOR_INSET } from '../geometry';
import type { SlotView } from '../types';
import {
  amsEditableLightUrl,
  amsEditableUrl,
  amsFilamentHintUrl,
  amsReadonlyLightUrl,
  amsReadonlyUrl,
  trayLeftHoverSvg,
  trayLeftSelectedSvg,
  trayLeftSvg,
  trayMidHoverSvg,
  trayMidSelectedSvg,
  trayMidSvg,
  trayRightHoverSvg,
  trayRightSelectedSvg,
  trayRightSvg,
} from '../assets';
import { cn, clearCheckerStyle, colorBandRects, filamentAlphaKind, remainBarFill, remainLineRatio, slotContrast, slotFill, slotFillRatio, translucentCheckerStyle, type FilamentAlphaKind } from './style';

export type SlotCardVariant = 'generic' | 'lite' | 'lite-ext' | 'ext';

interface TrayArtwork {
  base: string;
  hover: string;
  selected: string;
  width: number;
}

function trayArtwork(slot: SlotView, variant: SlotCardVariant): TrayArtwork {
  if (variant === 'lite-ext') {
    return { base: trayMidSvg, hover: trayMidHoverSvg, selected: trayMidSelectedSvg, width: TRAY_ICON.midWidth };
  }
  const left = slot.slot_id === '0' || slot.slot_id === '1';
  return left
    ? { base: trayLeftSvg, hover: trayLeftHoverSvg, selected: trayLeftSelectedSvg, width: TRAY_ICON.sideWidth }
    : { base: trayRightSvg, hover: trayRightHoverSvg, selected: trayRightSelectedSvg, width: TRAY_ICON.sideWidth };
}

// Mirrors AMSLib::render_generic_text: a name holding a space or a hyphen is laid
// out as two smaller lines instead of one, so a long name cannot spill over the K
// row below it. The separator stays with the second line and, as in C++, the last
// separator of this list wins when the name carries both.
const NAME_SPLIT_CHARS = [' ', '-'];

function splitFilamentName(name: string): [string, string] | null {
  let at = -1;
  for (const separator of NAME_SPLIT_CHARS) {
    const index = name.indexOf(separator);
    if (index >= 0) at = index;
  }
  if (at < 0) return null;
  return [name.slice(0, at), name.slice(at)];
}

const CURSOR_TIP_OFFSET = { x: 16, y: 8 } as const;

function CursorTip({ text, origin }: { text: string; origin: { x: number; y: number } }) {
  const ref = useRef<HTMLSpanElement>(null);

  useLayoutEffect(() => {
    const el = ref.current;
    if (!el) return;
    const rect = el.getBoundingClientRect();
    let left = origin.x + CURSOR_TIP_OFFSET.x;
    let top = origin.y - rect.height - CURSOR_TIP_OFFSET.y;
    const maxLeft = window.innerWidth - rect.width - 4;
    if (left > maxLeft) left = Math.max(4, maxLeft);
    if (left < 4) left = 4;
    if (top < 4) top = origin.y + CURSOR_TIP_OFFSET.y;
    el.style.left = `${left}px`;
    el.style.top = `${top}px`;
    el.style.visibility = 'visible';
  }, [origin.x, origin.y, text]);

  return (
    <span
      ref={ref}
      className="ams-slot-cursor-tip"
      style={{ left: origin.x + CURSOR_TIP_OFFSET.x, top: origin.y, visibility: 'hidden' }}
    >
      {text}
    </span>
  );
}

function TrayMark({
  svg,
  className,
  width,
  height,
  cardWidth,
  cardHeight,
}: {
  svg: string;
  className?: string;
  width: number;
  height: number;
  cardWidth: number;
  cardHeight: number;
}) {
  return (
    <span
      className={cn('ams-lite-tray pointer-events-none absolute', className)}
      style={{
        left: px(Math.trunc((cardWidth - width) / 2)),
        top: px(Math.trunc((cardHeight - height) / 2)),
        width: px(width),
        height: px(height),
      }}
      aria-hidden
      dangerouslySetInnerHTML={{ __html: svg }}
    />
  );
}

function SlotSwatchFill({
  slot,
  kind,
  solid,
  bandWidth,
}: {
  slot: SlotView;
  kind: FilamentAlphaKind;
  solid: boolean;
  bandWidth: number;
}) {
  const colors = slot.colors.length > 0 ? slot.colors : [slot.color];
  if (solid && slot.color_type === 1 && colors.length >= 2) {
    return (
      <span className="absolute inset-0">
        {colorBandRects(colors.length, bandWidth).map((rect, i) => (
          <span
            key={i}
            className="absolute top-0 bottom-0"
            style={{ left: px(rect.x), width: px(rect.width), background: colors[i] }}
          />
        ))}
      </span>
    );
  }
  if (solid) {
    return <span className="absolute inset-0" style={{ background: slotFill(slot) }} />;
  }
  if (kind === 'clear') {
    // Tile to the lib size. The SVG asset is 48x68 and WebView2 will not
    // stretch an <img> to the 52x80 card, which left a white band under Ext.
    return <span className="absolute inset-0" style={clearCheckerStyle()} />;
  }
  return <span className="absolute inset-0" style={translucentCheckerStyle(slot.color)} />;
}

function SlotSwatch({
  slot,
  lite,
  liteExt,
  fillRatio,
}: {
  slot: SlotView;
  lite: boolean;
  liteExt: boolean;
  fillRatio: number;
}) {
  const multi = slot.color_type !== 2 && slot.colors.length > 1;
  const kind = filamentAlphaKind(slot.color);
  const solid = multi || kind === 'opaque';
  const inset = liteExt ? LITE_EXT_COLOR_INSET : LITE_COLOR_INSET;
  const bandWidth = lite
    ? SLOT_LIB_LITE.width - inset.left - inset.right
    : SLOT_LIB.width;
  const fill = <SlotSwatchFill slot={slot} kind={kind} solid={solid} bandWidth={bandWidth} />;

  // Lite colour sits inside the tray flange. Generic / Ext match the opaque
  // remain fill: full-bleed to the card edge, border painted by ::after.
  if (lite) {
    return (
      <span
        className="absolute overflow-hidden"
        style={{
          left: px(inset.left),
          top: px(inset.top),
          right: px(inset.right),
          bottom: px(inset.bottom),
        }}
      >
        {fill}
      </span>
    );
  }

  // C++ AMSLib draws the clear / translucent checker over the whole lib
  // (DrawBitmap at DIP 2,2). Remain height only clips opaque / multi fills.
  if (!solid) {
    return <span className="absolute inset-0 overflow-hidden">{fill}</span>;
  }

  return (
    <span
      className="absolute inset-x-0 bottom-0 overflow-hidden"
      style={{ height: `${fillRatio * 100}%` }}
    >
      {fill}
    </span>
  );
}

// Capsule above the card: light-grey outline, filament fill stays inside the
// border and never overlaps it.
function SlotRemainLineBar({
  slot,
  left,
  top,
  width,
}: {
  slot: SlotView;
  left: number;
  top: number;
  width: number;
}) {
  const { height, border, borderColor, trackColor } = SLOT_REMAIN_LINE;
  const innerWidth = width - 2 * border;
  const innerHeight = height - 2 * border;
  const fillWidth = Math.round(innerWidth * remainLineRatio(slot.slot_remain_line.remain_percent));

  return (
    <span
      data-testid={`ams-slot-remain-line-${slot.ams_id}-${slot.slot_id}`}
      className="pointer-events-none absolute z-[1] box-border block"
      style={{
        left: px(left),
        top: px(top),
        width: px(width),
        height: px(height),
        borderRadius: px(height / 2),
        border: `${border}px solid ${borderColor}`,
        background: trackColor,
      }}
    >
      {fillWidth > 0 ? (
        <span
          className="absolute block"
          style={{
            left: 0,
            top: 0,
            width: px(fillWidth),
            height: px(innerHeight),
            borderRadius: px(innerHeight / 2),
            background: remainBarFill(slot),
          }}
        />
      ) : null}
    </span>
  );
}

export function SlotCard({
  slot,
  variant,
  onSelect,
  onEdit,
  onView,
  onFilamentHint,
}: {
  slot: SlotView;
  variant: SlotCardVariant;
  onSelect: (slot: SlotView) => void;
  onEdit: (slot: SlotView) => void;
  onView: (slot: SlotView) => void;
  onFilamentHint: (slot: SlotView) => void;
}) {
  const { t } = useTranslation();
  const liteExt = variant === 'lite-ext';
  const lite = variant === 'lite' || liteExt;
  const liteInset = liteExt ? LITE_EXT_COLOR_INSET : LITE_COLOR_INSET;
  const size = lite ? SLOT_LIB_LITE : SLOT_LIB;
  const glyphBottom = lite ? 20 : 15;
  const empty = slot.slot_state === 'empty' || slot.slot_state === 'none';
  const showEmptySlash = lite && slot.slot_state === 'empty';
  const showEmptyLabel = !lite && slot.slot_state === 'empty';
  const pale = empty || slot.show_unknown;
  const contrast = slotContrast({
    color: slot.color,
    remain: slot.remain,
    showRemain: slot.show_remain_height,
    pale,
  });
  const fillRatio = slotFillRatio(slot.remain, slot.show_remain_height);
  // Sits above the card, so it hangs off the wrapper on a negative offset
  // instead of being clipped by the card's own overflow. Lite has no headroom
  // there - its refresh arrows flank the card - so it skips the bar entirely.
  const remainLineBox = {
    left: SLOT_REMAIN_LINE.marginX,
    top: -(SLOT_REMAIN_LINE.height + SLOT_REMAIN_LINE.gapToCard),
    width: size.width - 2 * SLOT_REMAIN_LINE.marginX,
  };
  const showEdit = slot.menu_actions.show_edit;
  const showRead = slot.menu_actions.show_read;
  const glyph = showEdit
    ? contrast.light
      ? amsEditableLightUrl
      : amsEditableUrl
    : contrast.light
      ? amsReadonlyLightUrl
      : amsReadonlyUrl;
  const tray = lite ? trayArtwork(slot, variant) : null;
  const showK = !lite && (!!slot.k_text || slot.k_loading);
  const nameLines =
    !lite && !showEmptyLabel && !slot.show_unknown ? splitFilamentName(slot.fila_type) : null;
  const kTip = slot.k_loading
    ? (slot.k_loading_text ? `K ${slot.k_loading_text}` : 'K')
    : slot.k_text;
  const tip = !empty && !slot.show_unknown && slot.fila_type
    ? (showK && kTip ? `${slot.fila_type}\n${kTip}` : slot.fila_type)
    : undefined;
  const [tipOrigin, setTipOrigin] = useState<{ x: number; y: number } | null>(null);

  return (
    <div
      className="relative shrink-0"
      style={{ width: px(size.width), height: px(size.height) }}
      onMouseEnter={(event) => {
        if (tip) setTipOrigin({ x: event.clientX, y: event.clientY });
      }}
      onMouseLeave={() => setTipOrigin(null)}
    >
      {tip && tipOrigin ? <CursorTip text={tip} origin={tipOrigin} /> : null}
      {slot.slot_remain_line.show_line && !lite ? (
        <SlotRemainLineBar
          slot={slot}
          left={remainLineBox.left}
          top={remainLineBox.top}
          width={remainLineBox.width}
        />
      ) : null}
    <div
      role="button"
      tabIndex={0}
      data-testid={`ams-slot-${slot.ams_id}-${slot.slot_id}`}
      aria-label={tip ? `${slot.label} ${tip}` : slot.label}
      onClick={() => onSelect(slot)}
      onKeyDown={(e) => {
        if (e.key === 'Enter' || e.key === ' ') onSelect(slot);
      }}
      className={cn(
        'ams-icon-swap relative size-full cursor-default',
        lite
          ? 'overflow-visible rounded-[6px]'
          : 'ams-slot-card box-border overflow-hidden rounded-[4px]',
        !lite && slot.selected && 'is-selected',
      )}
      style={{
        background: lite ? COLORS.liteLibBg : COLORS.libBg,
      }}
    >
      {!pale ? (
        <SlotSwatch slot={slot} lite={lite} liteExt={liteExt} fillRatio={fillRatio} />
      ) : showEmptySlash ? (
        <span
          className="absolute block"
          style={{
            left: px(liteInset.left),
            top: px(liteInset.top),
            right: px(liteInset.right),
            bottom: px(liteInset.bottom),
            background: COLORS.white,
          }}
        />
      ) : null}

      {showEdit || showRead ? (
        <button
          type="button"
          aria-label={showEdit ? 'Edit filament' : 'View filament'}
          className="absolute left-1/2 z-[1] -translate-x-1/2 cursor-default"
          style={{ bottom: px(glyphBottom), marginLeft: lite ? 3 : 0 }}
          onClick={(e) => {
            e.stopPropagation();
            if (!slot.selected) onSelect(slot);
            if (showEdit) onEdit(slot);
            else onView(slot);
          }}
        >
          <img src={glyph} alt="" aria-hidden style={{ height: px(14) }} />
        </button>
      ) : null}

      {tray ? (
        slot.selected ? (
          <TrayMark svg={tray.selected} width={tray.width} height={TRAY_ICON.height} cardWidth={size.width} cardHeight={size.height} />
        ) : (
          <>
            <TrayMark className="ams-icon-base" svg={tray.base} width={tray.width} height={TRAY_ICON.height} cardWidth={size.width} cardHeight={size.height} />
            <TrayMark className="ams-icon-hover" svg={tray.hover} width={tray.width} height={TRAY_ICON.height} cardWidth={size.width} cardHeight={size.height} />
          </>
        )
      ) : null}

      {showEmptySlash ? (
        <span
          className="pointer-events-none absolute inset-0 z-[1] flex items-center justify-center text-[13px] leading-[17px]"
          style={{ color: '#323A3D' }}
        >
          /
        </span>
      ) : showEmptyLabel ? (
        <span
          className="pointer-events-none absolute inset-0 z-[1] flex items-center justify-center text-[13px] leading-[17px]"
          style={{ color: contrast.textColor }}
        >
          {t('Empty')}
        </span>
      ) : nameLines ? (
        <>
          <span
            className={cn(
              'pointer-events-none absolute inset-x-0 z-[1] block truncate px-[2px] text-center text-[12px] leading-[14px]',
              showK ? 'top-[2px]' : 'top-[22px]',
            )}
            style={{ color: contrast.textColor }}
          >
            {nameLines[0]}
          </span>
          <span
            className={cn(
              'pointer-events-none absolute inset-x-0 z-[1] block truncate px-[2px] text-center text-[12px] leading-[14px]',
              showK ? 'top-[16px]' : 'top-[36px]',
            )}
            style={{ color: contrast.textColor }}
          >
            {nameLines[1]}
          </span>
        </>
      ) : (
        <span
          className={
            lite
              ? 'pointer-events-none absolute inset-x-0 top-[20px] z-[1] block truncate px-[2px] text-center text-[10px] leading-[12px]'
              : showK
                ? 'pointer-events-none absolute inset-x-0 top-[6px] z-[1] block truncate px-[2px] text-center text-[13px] leading-[17px]'
                : 'absolute inset-x-0 top-[28px] z-[1] block truncate px-[2px] text-center text-[13px] leading-[17px]'
          }
          style={{ color: contrast.textColor, transform: lite ? 'translateX(3px)' : undefined }}
        >
          {slot.show_unknown ? '?' : slot.fila_type}
        </span>
      )}

      {showK ? (
        <span
          className={cn(
            'pointer-events-none absolute inset-x-0 z-[1] block px-[2px] text-center text-[11px] leading-[14px]',
            nameLines ? 'top-[30px]' : 'top-[24px]',
          )}
          style={{ color: contrast.textColor }}
        >
          {slot.k_loading ? (
            <>
              <span className="block">K</span>
              <span className="block">{slot.k_loading_text || 'loading'}</span>
            </>
          ) : (
            slot.k_text
          )}
        </span>
      ) : null}

      {slot.menu_actions.show_filament_mgr_hint ? (
        <button
          type="button"
          aria-label="New filament"
          className="absolute right-0 top-0 z-[1] cursor-default"
          onClick={(e) => {
            e.stopPropagation();
            onFilamentHint(slot);
          }}
        >
          <img src={amsFilamentHintUrl} alt="" aria-hidden style={{ width: px(14), height: px(14) }} />
        </button>
      ) : null}
    </div>
    </div>
  );
}
