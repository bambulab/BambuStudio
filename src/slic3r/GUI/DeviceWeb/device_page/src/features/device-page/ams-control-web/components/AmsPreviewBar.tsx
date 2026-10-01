import { useEffect, useRef } from 'react';
import {
  COLORS,
  PREVIEW_CHIP_FOUR,
  PREVIEW_CHIP_GAP,
  PREVIEW_CHIP_SINGLE,
  PREVIEW_STRIP,
  px,
} from '../dip';
import type { PreviewCube, PreviewItem } from '../types';
import {
  fourSlotFrameDarkUrl,
  fourSlotFrameUrl,
  singleSlotFrameDarkUrl,
  singleSlotFrameUrl,
  tsCubeDarkUrl,
  tsCubeUrl,
} from '../assets';
import { cn, colorBandRects, filamentAlphaKind, isPaleColor, isTransparentColor, previewCubeFill, translucentCheckerStyle } from './style';
import { ThemeImage } from './ThemeImage';

const EMPTY_CUBE_FILL = '#FFFFFF';

// Slot empty glyph is "/". CSS 1px gradients alias on the 9x14 chip; SVG stroke
// is anti-aliased. Inset 1px matches C++ AMSPreview empty-cube padding.
function EmptyCubeSlash() {
  return (
    <svg
      aria-hidden
      className="pointer-events-none absolute inset-0"
      viewBox="0 0 9 14"
      preserveAspectRatio="none"
    >
      <line
        x1="1"
        y1="1"
        x2="8"
        y2="13"
        stroke="#000000"
        strokeWidth="1"
        strokeLinecap="round"
        vectorEffect="non-scaling-stroke"
      />
    </svg>
  );
}

const CUBE = {
  four: { width: 9, height: 14, radius: 2 },
  single: { width: 9, height: 14, radius: 0 },
  ext: { width: 6, height: 12, radius: 3 },
} as const;

// four_slot_ams_item.svg: hole diameter 6, cube 9, (9-6)/2 per side.
const FOUR_SLOT_HOLE_INSET = 1.5;

// AMSPreview::doRender: four-slot path unless EXT_SPOOL or N3S.
function isFourSlotPreview(item: PreviewItem) {
  return item.ams_type_name !== 'EXT_SPOOL' && item.ams_type_name !== 'N3S';
}

function CubeColorBands({ colors, width }: { colors: string[]; width: number }) {
  return colorBandRects(colors.length, width).map((rect, i) => (
    <span
      key={i}
      className="absolute top-0 bottom-0"
      style={{ left: px(rect.x), width: px(rect.width), background: colors[i] }}
    />
  ));
}

function CubeView({ cube, index, four, isExt }: { cube: PreviewCube; index: number; four: boolean; isExt: boolean }) {
  const kind = four ? CUBE.four : isExt ? CUBE.ext : CUBE.single;
  const chip = four ? PREVIEW_CHIP_FOUR : PREVIEW_CHIP_SINGLE;
  const left = four ? 8 + index * CUBE.four.width : (chip.width - kind.width) / 2;
  const top = (chip.height - kind.height) / 2;
  const colors = cube.colors.length > 0 ? cube.colors : [cube.color];
  // AMSPreview multi: DrawRectangle, no rounded clip. Radius 2 on a 9px cube
  // eats most of the last 3px band.
  const multi = cube.color_type === 1 && colors.length >= 2;
  const gradient = cube.color_type === 0 && colors.length >= 2;

  // C++ only draws ts_bitmap_cube (12px) in the four-slot Alpha()==0 branch.
  // N3S / Ext fall through to the 9x14 / 6x12 rounded rect.
  if (four && cube.is_exists && isTransparentColor(cube.color)) {
    return (
      <ThemeImage
        light={tsCubeUrl}
        dark={tsCubeDarkUrl}
        width={12}
        height={12}
        className="absolute"
        style={{ left: px(left - 1), top: px((chip.height - 12) / 2) }}
      />
    );
  }

  const empty = !cube.is_exists;
  const alphaKind = empty ? 'opaque' : filamentAlphaKind(cube.color);
  const cubeRadius = multi ? 0 : kind.radius;
  // Frame holes are 6px (r=3) on a 9px cube. Split colours in the hole so
  // 三色 is 2+2+2 through the aperture, not 1.5+3+1.5.
  const pocket = four && !empty && multi;
  const drawLeft = pocket ? left + FOUR_SLOT_HOLE_INSET : left;
  const drawWidth = pocket ? kind.width - FOUR_SLOT_HOLE_INSET * 2 : kind.width;
  const cubeBackground = empty
    ? EMPTY_CUBE_FILL
    : multi || alphaKind === 'translucent'
      ? undefined
      : gradient
        ? `linear-gradient(90deg, ${colors.map((color, i) => `${color} ${(i / (colors.length - 1)) * 100}%`).join(', ')})`
        : previewCubeFill(cube);
  return (
    <span
      key={index}
      className="absolute block"
      style={{
        left: px(drawLeft),
        top: px(top),
        width: px(drawWidth),
        height: px(kind.height),
        borderRadius: px(cubeRadius),
        overflow: 'hidden',
        background: cubeBackground,
        ...(alphaKind === 'translucent' && !multi ? translucentCheckerStyle(cube.color) : {}),
        boxShadow:
          isExt && (empty || isPaleColor(cube.color))
            ? `inset 0 0 0 1px ${COLORS.gray500}`
            : undefined,
      }}
    >
      {!empty && multi ? <CubeColorBands colors={colors} width={drawWidth} /> : null}
      {empty && four ? <EmptyCubeSlash /> : null}
    </span>
  );
}

function PreviewChip({
  item,
  isExt,
  onSelect,
}: {
  item: PreviewItem;
  isExt: boolean;
  onSelect: (item: PreviewItem) => void;
}) {
  const four = isFourSlotPreview(item);
  const chip = four ? PREVIEW_CHIP_FOUR : PREVIEW_CHIP_SINGLE;
  const ref = useRef<HTMLButtonElement>(null);

  useEffect(() => {
    if (item.active) ref.current?.scrollIntoView({ block: 'nearest', inline: 'nearest' });
  }, [item.active]);

  return (
    <button
      ref={ref}
      type="button"
      data-testid={`ams-preview-${item.ams_id}`}
      aria-label={`AMS ${item.ams_id}`}
      onClick={() => onSelect(item)}
      className={cn('ams-preview-chip relative shrink-0 overflow-hidden rounded-[3px] border-0', item.active && 'active')}
      style={{
        width: px(chip.width),
        height: px(chip.height),
        background: COLORS.chipBg,
      }}
    >
      {item.cubes.map((item_cube, index) => (
        <CubeView key={index} cube={item_cube} index={index} four={four} isExt={isExt} />
      ))}
      {four ? (
        <ThemeImage
          light={fourSlotFrameUrl}
          dark={fourSlotFrameDarkUrl}
          width={chip.width}
          height={chip.height}
          className="pointer-events-none absolute inset-0"
          style={{ maxWidth: 'none', maxHeight: 'none' }}
        />
      ) : null}
      {!four && !isExt ? (
        <ThemeImage
          light={singleSlotFrameUrl}
          dark={singleSlotFrameDarkUrl}
          width={chip.width}
          height={chip.height}
          className="pointer-events-none absolute inset-0"
          style={{ maxWidth: 'none', maxHeight: 'none' }}
        />
      ) : null}
    </button>
  );
}

export function AmsPreviewBar({
  items,
  extAmsIds,
  onSelect,
  width = PREVIEW_STRIP.width,
}: {
  items: PreviewItem[];
  extAmsIds: Set<string>;
  onSelect: (item: PreviewItem) => void;
  width?: number;
}) {
  return (
    <div
      className="flex items-center overflow-x-auto"
      style={{
        width: px(width),
        height: px(PREVIEW_STRIP.height),
        gap: px(PREVIEW_CHIP_GAP),
        background: COLORS.blockBg,
        paddingLeft: px(PREVIEW_CHIP_GAP),
        paddingRight: px(PREVIEW_CHIP_GAP),
      }}
    >
      {items.map((item) => (
        <PreviewChip key={item.ams_id} item={item} isExt={extAmsIds.has(item.ams_id)} onSelect={onSelect} />
      ))}
    </div>
  );
}
