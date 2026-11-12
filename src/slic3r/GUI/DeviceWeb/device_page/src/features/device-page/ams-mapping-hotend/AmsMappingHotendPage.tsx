import { useEffect, useRef, useState } from 'react';
import type { RefObject } from 'react';
import { useTranslation } from 'react-i18next';
import { useAmsMappingHotendBridge } from './useAmsMappingHotendBridge';
import { useSendToCpp } from '../../../hooks/Bridge';
import { documentZoom, useFitHostWidth } from '../../../hooks/useFitHostWidth';
import { SlotCard } from './components/SlotCard';
import { SubAmsPage } from './components/SubAmsPage';
import { Tips } from './components/Tips';
import { BottomWikiTip } from './components/BottomWikiTip';
import { resolveHostContentSize } from './hostContentSize';
import type {
  AmsMappingHotendSlotViewModel,
  AmsMappingHotendSubSlotViewModel,
} from './types';
import './ams-mapping-hotend.css';

function findCheckedSubSlot(
  slot: AmsMappingHotendSlotViewModel,
): AmsMappingHotendSubSlotViewModel | undefined {
  if (!slot.sub_ams) return undefined;
  for (const group of slot.sub_ams) {
    const found = group.slots.find((sub) => sub.checked);
    if (found) return found;
  }
  return undefined;
}

// When a sub-AMS slot under this hotend is checked for the current filament,
// surface that sub-slot's filament and global AMS code ("A1", "HT-A", ...) on
// the level-1 card; otherwise show the hotend's own rack (AMS 3H) / ext slot.
function resolveLevelOneSlot(
  slot: AmsMappingHotendSlotViewModel,
  checkedSub: AmsMappingHotendSubSlotViewModel | undefined,
): AmsMappingHotendSlotViewModel {
  if (!checkedSub) return slot;
  return {
    ...slot,
    index: checkedSub.display_index || checkedSub.index,
    ext: checkedSub.ext,
    state: checkedSub.state,
    filament_type: checkedSub.filament_type,
    filament_name: checkedSub.filament_name,
    color: checkedSub.color,
    ctype: checkedSub.ctype,
    color_type: checkedSub.color_type,
    remain: checkedSub.remain,
    has_filament: checkedSub.has_filament,
    is_pickable: checkedSub.is_pickable,
    can_edit: checkedSub.can_edit,
    checked: checkedSub.checked,
    multi_colors: checkedSub.multi_colors,
  };
}

// True when the currently selected (checked) mapping source is a third-party /
// unknown-type filament. Mirrors SlotCard's `isThird` test (filament_name ==
// '?', set by BuildSlotJson for TrayType::THIRD). Covers both top-level slots
// and sub-AMS sub-slots since either can hold the active selection.
function hasUnknownTypeSelected(slots: AmsMappingHotendSlotViewModel[]): boolean {
  return slots.some((slot) => {
    if (slot.checked && slot.filament_name === '?') return true;
    return !!slot.sub_ams?.some((group) =>
      group.slots.some((sub) => sub.checked && sub.filament_name === '?'),
    );
  });
}

const HOST_LAYOUT_MODULE = 'device_host';

// Logical-pixel gap kept below the last element (the Wiki tip). Reported as a
// DIP baseline; the wx host multiplies by the display scale via FromDIP(), so
// this stays correct across resolutions / DPI scaling.
const CONTENT_BOTTOM_GAP = 6;

// DeviceWebHost latches this popup to kAmsMappingHotendDesignWidthDip.
const HOST_WIDTH_DIP = 660;

// Observe the real rendered size of the mapping page and report it to the wx
// host via device_host/layout/content_size so the popup can Fit itself instead
// of relying on the initial SetMinSize.
function useReportHostContentSize(rootRef: RefObject<HTMLElement | null>) {
  const send = useSendToCpp();
  const seqRef = useRef(1_000_000);
  const scheduleRef = useRef<() => void>(() => {});

  useEffect(() => {
    const root = rootRef.current;
    if (!root) return;

    let rafId = 0;
    let lastSizeKey = '';

    const report = () => {
      rafId = 0;
      // `.ams-map-hotend-page` is height:100% of the popup, so its scrollHeight
      // just echoes the current popup height (a feedback loop that never
      // shrinks). Measure the last child (the Wiki tip) bottom instead and add
      // a fixed CONTENT_BOTTOM_GAP below it so the popup fits the real content.
      const rootTop = root.getBoundingClientRect().top;
      const last = root.lastElementChild;
      const contentHeight = last
        ? (last.getBoundingClientRect().bottom - rootTop) / documentZoom() + CONTENT_BOTTOM_GAP
        : root.scrollHeight;
      const { width, height } = resolveHostContentSize({
        scrollWidth: root.scrollWidth,
        scrollHeight: contentHeight,
        offsetHeight: contentHeight,
      });
      if (width <= 0 || height <= 0) return;

      const sizeKey = `${width}x${height}`;
      if (sizeKey === lastSizeKey) return;
      lastSizeKey = sizeKey;

      send(
        {
          version: '1.0',
          type: 'request',
          seq: seqRef.current++,
          ts: Date.now(),
        },
        {
          module: HOST_LAYOUT_MODULE,
          submod: 'layout',
          action: 'content_size',
          payload: { width, height },
        },
      );
    };

    const scheduleReport = () => {
      if (rafId) return;
      rafId = window.requestAnimationFrame(report);
    };

    scheduleRef.current = scheduleReport;
    scheduleReport();
    window.addEventListener('resize', scheduleReport);

    const observer = typeof ResizeObserver === 'function' ? new ResizeObserver(scheduleReport) : null;
    observer?.observe(root);

    return () => {
      if (rafId) window.cancelAnimationFrame(rafId);
      scheduleRef.current = () => {};
      observer?.disconnect();
      window.removeEventListener('resize', scheduleReport);
    };
  }, [rootRef, send]);

  // `root` is height:100% of the popup, so the observer above never ticks on a
  // pure content change. Re-measure per render; report() dedups by size.
  useEffect(() => {
    scheduleRef.current();
  });
}

export function AmsMappingHotendPage() {
  const pageRef = useRef<HTMLDivElement>(null);
  const { t } = useTranslation();
  const { state, error, selectSlot, editSlot, openWiki, showNozzleError } = useAmsMappingHotendBridge();
  // Track the entered sub-AMS page by its slot key rather than a snapshot of
  // the slot object: an edit inside the sub-page re-publishes fresh state, and
  // we must re-derive the slot from the latest state so the sub-page reflects
  // the new color/type live (a stored snapshot would stay stale until re-open).
  const [subAmsKey, setSubAmsKey] = useState<string | null>(null);
  useReportHostContentSize(pageRef);
  useFitHostWidth(HOST_WIDTH_DIP);

  // The PopupWindow on the wx side merely hides/shows this React tree on
  // dismiss/re-open; component state otherwise survives across opens. The
  // C++ ViewModel bumps `session_seq` on every Refresh() so we can detect
  // a fresh popup-open and reset transient navigation (e.g. a previously
  // entered sub-AMS sub-page) so the user always lands on the main view.
  useEffect(() => {
    setSubAmsKey(null);
  }, [state.session_seq]);

  const topSlots = state.hotend_slots.filter((s) => s.is_top_tier);
  const bottomSlots = state.hotend_slots.filter((s) => !s.is_top_tier);
  const rows = [topSlots, bottomSlots].filter((r) => r.length > 0);

  // Resolve the entered sub-AMS parent slot from the current state every render
  // so edits sync live; falls back to the main view if the slot is gone.
  const subAmsSlot = subAmsKey
    ? state.hotend_slots.find((s) => `${s.ams_id}:${s.slot_id}` === subAmsKey) ?? null
    : null;

  if (subAmsSlot) {
    return (
      <div ref={pageRef} className="ams-map-hotend-page" data-testid="ams-mapping-hotend-page">
        <SubAmsPage
          slot={subAmsSlot}
          materialIndex={state.material_index}
          onSelect={(sub) => {
            void selectSlot(sub);
            setSubAmsKey(null);
          }}
          onEdit={(sub) => void editSlot(sub)}
          onBack={() => setSubAmsKey(null)}
          onOpenWiki={() => void openWiki()}
        />
      </div>
    );
  }

  const unknownTypeSelected = hasUnknownTypeSelected(state.hotend_slots);

  return (
    <div ref={pageRef} className="ams-map-hotend-page" data-testid="ams-mapping-hotend-page">
      {error && <Tips text={error} />}
      {unknownTypeSelected && (
        <Tips text={t('The selected filament type is unknown. Please edit the filament first.')} />
      )}

      <div className="ams-map-hotend-page__grid">
        {rows.map((row, rowIdx) => {
          // Center every tier (not just the top one): the popup container can be
          // wider than the widest row (e.g. a 7-slot machine whose bottom row has
          // 4 cards), so a left-aligned bottom row would stick to the left while
          // the centered top row floats in the middle. Centering both keeps the
          // interleaved "brick" offset (the shorter top row naturally lands
          // between the bottom cards) and stays balanced at any container width.
          return (
            <div key={rowIdx} className="ams-map-hotend-page__row-group">
              <div
                className="ams-map-hotend-page__row ams-map-hotend-page__row--centered"
              >
                {row.map((slot) => {
                  const checkedSub = findCheckedSubSlot(slot);
                  const displaySlot = resolveLevelOneSlot(slot, checkedSub);
                  const activeTarget = checkedSub ?? slot;
                  return (
                    <SlotCard
                      key={`${slot.ams_id}:${slot.slot_id}`}
                      slot={displaySlot}
                      materialIndex={state.material_index}
                      onClick={() => void selectSlot(activeTarget)}
                      onEnterSubAms={() => setSubAmsKey(`${slot.ams_id}:${slot.slot_id}`)}
                      onEdit={() => void editSlot(activeTarget)}
                      onNozzleError={() => void showNozzleError()}
                    />
                  );
                })}
              </div>
            </div>
          );
        })}
      </div>

      <BottomWikiTip onOpenWiki={() => void openWiki()} />
    </div>
  );
}
