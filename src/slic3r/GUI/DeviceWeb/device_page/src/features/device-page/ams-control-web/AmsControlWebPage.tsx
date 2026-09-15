import { useEffect, useRef, useState, type ReactNode } from 'react';
import { useDeviceWebTheme } from '../../../hooks/useDeviceWebTheme';
import { useSendToCpp } from '../../../hooks/Bridge';
import { useAmsControlWebBridge } from './useAmsControlWebBridge';
import './ams-control-web.css';
import { CONTENT_WIDTH, DOWN_ROAD, MIXED_EXT_WIDTH, MIXED_LITE_WIDTH, PREVIEW_BODY_GAP, PREVIEW_STRIP, UNIT_BODY, px } from './dip';
import { extruderIconName, extrudersLeftToRight, unitSlotLayout } from './displayAdapters';
import { SINGLE_EXT_ELBOW, amsControlWebContentHeight, extPanelWidth, isFilamentRoadActive, nozzleIconLefts, nozzleInletX, overlayStageHeight, roadExtruderId, snapRoadX, unitWidth } from './geometry';
import type { PanelView, PreviewItem, SlotLink, SlotView, UnitView } from './types';
import { columnExtSlots, columnPreviewItems, columnShowsPreviewStrip, columnUnits, isLiteMixedBodyRow, isLiteMixedMachine, panelColumns, splitMixedPreview } from './layout';
import { buildRoadOverlay, type RoadBlock, type RoadBlockDraft } from './roadPaths';
import { AmsFooter } from './components/AmsFooter';
import { AmsPreviewBar } from './components/AmsPreviewBar';
import { AmsUnitPanel, type SlotHandlers } from './components/AmsUnitPanel';
import { ExtPanel } from './components/ExtPanel';
import { RoadOverlay } from './components/RoadOverlay';
import { SwitcherBand, SwitcherSetupHint } from './components/SwitcherBand';

const COLUMN_GAP = 10;
const HOST_LAYOUT_MODULE = 'device_host';
const HOST_WIDTH_DIP = 586;

function useReportAmsControlWebHostSize(height: number) {
  const send = useSendToCpp();
  const lastRef = useRef(0);

  useEffect(() => {
    if (height <= 0 || height === lastRef.current) return;
    lastRef.current = height;
    send(
      { version: '1.0', type: 'request', seq: 1_000_000, ts: Date.now() },
      {
        module: HOST_LAYOUT_MODULE,
        submod: 'layout',
        action: 'content_size',
        payload: { width: HOST_WIDTH_DIP, height },
      },
    );
  }, [height, send]);
}

interface BodyBlock {
  key: string;
  width: number;
  road: RoadBlockDraft;
  render: () => ReactNode;
}

interface ColumnLayout {
  column: PanelView;
  blocks: BodyBlock[];
  previewItems: PreviewItem[];
  innerWidth: number;
  pad: number;
  origin: number;
}

function linkOf(links: SlotLink[], amsId: string, slotId: string) {
  return links.find((link) => link.ams_id === amsId && link.slot_id === slotId);
}

function carryingLink(links: SlotLink[]): SlotLink | undefined {
  return links.find((link) => link.state !== 'idle') ?? links[0];
}

export function AmsControlWebPage() {
  useDeviceWebTheme();
  const {
    state,
    error,
    selectSlot,
    switchAms,
    editSlot,
    readSlot,
    viewSlot,
    openFilamentHint,
    openHumidity,
    openSettings,
    openAutoRefill,
    loadFilament,
    unloadFilament,
    openBridgeDebugDialog,
  } = useAmsControlWebBridge();
  const [debugOpening, setDebugOpening] = useState(false);
  const showDebugButton = Boolean((window as Window & { __internalBuild?: boolean }).__internalBuild);

  const { display } = state;
  const { ams_ext_area: amsExt, ams_preview_area: preview, filament_line_area: lineArea } = display;
  const columns = panelColumns(amsExt);
  const liteStyle = amsExt.lite_style;
  const mixedMachine = isLiteMixedMachine(amsExt.units);
  const bareSingleExt = !liteStyle && state.data.ams_units.length === 0 && amsExt.ext_slots.length === 1;
  const extAmsIds = new Set(amsExt.ext_slots.map((slot) => slot.ams_id));
  const links = lineArea.links;
  const extruders = extrudersLeftToRight(display.extruder_area.extruders);
  const handlers: SlotHandlers = {
    onSelect: (slot: SlotView) => { void selectSlot(slot.ams_id, slot.slot_id); },
    onEdit: (slot: SlotView) => { void editSlot(slot.ams_id, slot.slot_id); },
    onRead: (slot: SlotView) => { void readSlot(slot.ams_id, slot.slot_id); },
    onView: (slot: SlotView) => { void viewSlot(slot.ams_id, slot.slot_id); },
    onFilamentHint: (slot: SlotView) => { void openFilamentHint(slot.ams_id, slot.slot_id); },
  };

  const onPreview = (item: PreviewItem) => {
    void switchAms(item.ams_id);
  };

  const onHumidityClick = (unit: UnitView) => {
    void openHumidity(unit.ams_id);
  };

  const dualNozzle = extruders.length >= 2;
  const hasPreview = preview.visible && (
    mixedMachine
    || columns.some((column) =>
      columnShowsPreviewStrip(column, columnPreviewItems(column, preview.items), dualNozzle),
    )
  );
  useReportAmsControlWebHostSize(
    display.visible
      ? amsControlWebContentHeight({
          hasPreview,
          slotAreaVisible: amsExt.visible,
          hasSwitcher: display.switcher_area.visible,
          hasSetupHint: Boolean(display.switcher_area.show_setup_hint && display.switcher_area.setup_hint),
        })
      : 0,
  );

  if (!display.visible) {
    return <div data-testid="ams-control-web-page" className="hidden" />;
  }

  const columnBlocks = columns.map((column) => {
    const blocks: BodyBlock[] = [];
    const unitsInColumn = columnUnits(column, amsExt.units);
    const mixedBody = isLiteMixedBodyRow(unitsInColumn);

    for (const unit of unitsInColumn) {
      const slotLayout = unitSlotLayout(unit);
      const mixedCompact = mixedBody && slotLayout === 'lite_cross';
      const blockWidth = unitWidth(slotLayout, mixedCompact);
      const unitLinks = unit.slots
        .map((slot) => linkOf(links, slot.ams_id, slot.slot_id))
        .filter((link): link is SlotLink => !!link);
      const carrying = carryingLink(unitLinks);
      const destId = carrying ? roadExtruderId(carrying) : undefined;
      const drawn = destId !== undefined;
      const passing = !!carrying && destId !== undefined && isFilamentRoadActive(carrying);
      blocks.push({
        key: `unit-${column.pos}-${unit.ams_id}`,
        width: blockWidth,
        road: {
          key: `unit-${column.pos}-${unit.ams_id}`,
          kind: slotLayout,
          slots: unit.slots,
          hub: unit.hub,
          links: unitLinks,
          bodyWidth: slotLayout === 'lite_cross' ? blockWidth : undefined,
          exit: drawn
            ? {
                extruderId: destId,
                active: passing,
                color: passing ? carrying.color : '',
              }
            : undefined,
        },
        render: () => (
          <AmsUnitPanel
            unit={unit}
            slotLayout={slotLayout}
            bodyWidth={blockWidth}
            onHumidityClick={onHumidityClick}
            {...handlers}
          />
        ),
      });
    }

    for (const slot of columnExtSlots(column, amsExt.ext_slots)) {
      const link = linkOf(links, slot.ams_id, slot.slot_id);
      const destId = link ? roadExtruderId(link) : undefined;
      const drawn = destId !== undefined;
      const framed = bareSingleExt;
      const extShift = framed ? SINGLE_EXT_ELBOW : 0;
      const panelWidth = extPanelWidth(mixedBody && !framed);
      const passing = !!link && destId !== undefined && isFilamentRoadActive(link);
      blocks.push({
        key: `ext-${slot.ams_id}-${slot.slot_id}`,
        width: framed ? UNIT_BODY.width : panelWidth,
        road: {
          key: `ext-${slot.ams_id}-${slot.slot_id}`,
          kind: 'ext',
          liteStyle,
          framed,
          slotOffset: extShift,
          panelWidth,
          link,
          exit: drawn
            ? {
                extruderId: destId,
                active: passing,
                color: passing ? link.color : '',
              }
            : undefined,
        },
        render: () => (
          <ExtPanel
            slot={slot}
            liteStyle={liteStyle}
            framed={framed}
            slotOffset={extShift}
            panelWidth={panelWidth}
            {...handlers}
          />
        ),
      });
    }

    return { column, blocks, previewItems: columnPreviewItems(column, preview.items) };
  });

  const splitColumns = columnBlocks.length > 1;
  const columnWidth = splitColumns ? CONTENT_WIDTH / columnBlocks.length : CONTENT_WIDTH;

  const laidOut: ColumnLayout[] = columnBlocks.map((entry, index) => {
    const innerWidth =
      entry.blocks.reduce((sum, block) => sum + block.width, 0)
      + Math.max(entry.blocks.length - 1, 0) * COLUMN_GAP;
    // wxALIGN_CENTER integer-divides leftover space. A 264px AMS in a 289px
    // half-row is 12.5px; floor it so overlay x stays on whole pixels.
    const pad = Math.floor((columnWidth - innerWidth) / 2);
    const origin = snapRoadX(columnWidth * index + pad);
    return { ...entry, innerWidth, pad, origin };
  });

  const nozzleXOf = (extruderId: number) => {
    const names = extruders.map((extruder) => extruderIconName(display.extruder_area.extruders, extruder));
    const lefts = nozzleIconLefts(names);
    const index = extruders.findIndex((extruder) => extruder.id === extruderId);
    if (index < 0) return snapRoadX(CONTENT_WIDTH / 2);
    return nozzleInletX(names[index], lefts[index]);
  };

  const overlayBlocks: RoadBlock[] = [];
  laidOut.forEach((entry) => {
    let cursor = entry.origin;
    for (const block of entry.blocks) {
      overlayBlocks.push({ ...block.road, origin: cursor } as RoadBlock);
      cursor += block.width + COLUMN_GAP;
    }
  });
  const overlay = buildRoadOverlay({
    blocks: overlayBlocks,
    slotAreaVisible: amsExt.visible,
    nozzleXOf,
  });

  const previewEntries = laidOut.filter(
    (entry) =>
      preview.visible &&
      columnShowsPreviewStrip(entry.column, entry.previewItems, extruders.length >= 2),
  );
  const mixedPreview = mixedMachine ? splitMixedPreview(preview.items, extAmsIds) : null;

  return (
    <div
      data-testid="ams-control-web-page"
      className="ams-control-web-page relative flex w-full flex-col items-center py-[4px]"
    >
      <div className="relative flex flex-col" style={{ width: px(CONTENT_WIDTH) }}>
        {preview.visible && mixedPreview ? (
          <>
            <div
              className="flex justify-center"
              style={{ width: px(CONTENT_WIDTH), gap: px(COLUMN_GAP) }}
            >
              <AmsPreviewBar items={mixedPreview.ams} extAmsIds={extAmsIds} onSelect={onPreview} width={PREVIEW_STRIP.width} />
              <AmsPreviewBar items={mixedPreview.lite} extAmsIds={extAmsIds} onSelect={onPreview} width={MIXED_LITE_WIDTH} />
              <AmsPreviewBar items={mixedPreview.ext} extAmsIds={extAmsIds} onSelect={onPreview} width={MIXED_EXT_WIDTH} />
            </div>
            <div style={{ height: px(PREVIEW_BODY_GAP) }} />
          </>
        ) : previewEntries.length > 0 ? (
          <>
            <div
              className="flex"
              style={
                splitColumns
                  ? { width: px(CONTENT_WIDTH) }
                  : { justifyContent: 'center', gap: px(COLUMN_GAP), width: px(CONTENT_WIDTH) }
              }
            >
              {(splitColumns ? laidOut : previewEntries).map((entry) => {
                const showStrip =
                  preview.visible &&
                  columnShowsPreviewStrip(entry.column, entry.previewItems, extruders.length >= 2);
                const bar = showStrip ? (
                  <AmsPreviewBar
                    items={entry.previewItems}
                    extAmsIds={extAmsIds}
                    onSelect={onPreview}
                  />
                ) : null;
                if (!splitColumns) {
                  return bar ? <div key={entry.column.pos}>{bar}</div> : null;
                }
                return (
                  <div
                    key={entry.column.pos}
                    className="flex justify-center"
                    style={{ width: px(columnWidth) }}
                  >
                    {bar}
                  </div>
                );
              })}
            </div>
            <div style={{ height: px(PREVIEW_BODY_GAP) }} />
          </>
        ) : null}

        <div className="relative overflow-visible" style={{ width: px(CONTENT_WIDTH), minHeight: px(overlayStageHeight(amsExt.visible)) }}>
          <div
            className="flex"
            style={splitColumns ? { gap: 0 } : { marginLeft: px(laidOut[0]?.origin ?? 0), gap: px(COLUMN_GAP) }}
          >
            {laidOut.map((entry) => (
              <div
                key={entry.column.pos}
                className="flex flex-col"
                style={splitColumns ? { width: px(columnWidth), paddingLeft: px(entry.pad) } : undefined}
              >
                {amsExt.visible ? (
                  <div className="flex" style={{ gap: px(COLUMN_GAP) }}>
                    {entry.blocks.map((block) => (
                      <div key={block.key} className="shrink-0" style={{ width: px(block.width), minWidth: px(block.width), maxWidth: px(block.width) }}>{block.render()}</div>
                    ))}
                  </div>
                ) : null}
              </div>
            ))}
          </div>
          <div style={{ height: px(DOWN_ROAD.height) }} />
          <RoadOverlay
            model={overlay}
            showSlot={amsExt.visible}
            showLine={lineArea.visible}
          />
        </div>

        <SwitcherBand switcher={display.switcher_area} />

        <AmsFooter
          actions={state.actions}
          extruderArea={display.extruder_area}
          onSettings={() => { void openSettings(); }}
          onAutoRefill={() => { void openAutoRefill(); }}
          onLoad={() => { void loadFilament(state.selected_ams_id, state.selected_slot_id); }}
          onUnload={() => { void unloadFilament(state.selected_ams_id, state.selected_slot_id); }}
          showDebug={showDebugButton}
          debugOpening={debugOpening}
          onDebug={async () => {
            setDebugOpening(true);
            try {
              await openBridgeDebugDialog(state);
            } finally {
              setDebugOpening(false);
            }
          }}
        />

        <SwitcherSetupHint switcher={display.switcher_area} />

      </div>

      {error ? <div className="mt-1 text-xs text-red-600">{error}</div> : null}
    </div>
  );
}
