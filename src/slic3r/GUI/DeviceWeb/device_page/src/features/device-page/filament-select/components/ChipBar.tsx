import { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState, type TransitionEvent } from 'react';
import { useTranslation } from 'react-i18next';
import arrowLeftUrl from '../assets/chip_arrow_left.svg';
import arrowRightUrl from '../assets/chip_arrow_right.svg';
import { chipWindowGeometry, chipsWithResults, pageChips } from '../chipPaging';
import type { FilamentSelectChip, FilamentSelectRow } from '../types';
import { MaskIcon } from './MaskIcon';

/** Native chip spacing: wxRIGHT FromDIP(6) after every chip. */
const CHIP_GAP = 6;
/** Fallback end of a slide in case transitionend never fires (keep > CSS duration). */
const SLIDE_FALLBACK_MS = 300;

interface ChipBarProps {
  chips: FilamentSelectChip[];
  rows: FilamentSelectRow[];
  normalizedQuery: string;
  activeKey: string;
  onSelect: (key: string) => void;
  offset: number;
  onOffsetChange: (offset: number) => void;
}

function useChipLabel() {
  const { t } = useTranslation();
  return (chip: FilamentSelectChip) => {
    switch (chip.kind) {
      case 'all': return t('All');
      case 'other': return t('Other');
      case 'unsupported': return t('Unsupported Filaments');
      default: return chip.label;
    }
  };
}

export function ChipBar({ chips, rows, normalizedQuery, activeKey, onSelect, offset, onOffsetChange }: ChipBarProps) {
  const chipLabel = useChipLabel();
  const stripRef = useRef<HTMLDivElement>(null);
  const measureRef = useRef<HTMLDivElement>(null);
  const [widths, setWidths] = useState<number[]>([]);
  const [available, setAvailable] = useState(0);

  const labels = chips.map(chipLabel);
  const labelsKey = labels.join('\u0000');

  // Every chip is rendered once off-screen so paging works on real widths,
  // like the native GetTextRect() measurement.
  useLayoutEffect(() => {
    const measure = measureRef.current;
    if (!measure) return;
    // Fractional widths: offsetWidth rounds, and the rounding error would add
    // up along the track and leave a sliver of the next chip in the window.
    // getBoundingClientRect() is in viewport px, so undo any document zoom.
    const zoom = Number(document.documentElement.style.zoom) || 1;
    setWidths(Array.from(measure.children).map((el) => el.getBoundingClientRect().width / zoom + CHIP_GAP));
  }, [labelsKey]);

  useLayoutEffect(() => {
    const strip = stripRef.current;
    if (!strip) return;
    const update = () => setAvailable(strip.clientWidth);
    update();
    if (typeof ResizeObserver !== 'function') return;
    const observer = new ResizeObserver(update);
    observer.observe(strip);
    return () => observer.disconnect();
  }, []);

  const allIndex = chips.findIndex((c) => c.kind === 'all');
  const hasResults = useMemo(
    () => chipsWithResults(chips, rows, normalizedQuery),
    [chips, rows, normalizedQuery],
  );
  const paging = widths.length === chips.length
    ? pageChips({ widths, allIndex, hasResults, offset, available })
    : pageChips({ widths: chips.map(() => 0), allIndex, hasResults, offset, available: 0 });

  // The native dialog writes the clamped offset back (m_chip_offset).
  useEffect(() => {
    if (paging.offset !== offset) onOffsetChange(paging.offset);
  }, [offset, onOffsetChange, paging.offset]);

  // Transitions are only switched on while an arrow click is sliding the
  // strip; search, data or resize re-layouts jump straight to the new place.
  const [sliding, setSliding] = useState(false);
  const slideTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  useEffect(() => () => {
    if (slideTimerRef.current) clearTimeout(slideTimerRef.current);
  }, []);

  const slideTo = useCallback((next: number) => {
    setSliding(true);
    if (slideTimerRef.current) clearTimeout(slideTimerRef.current);
    slideTimerRef.current = setTimeout(() => setSliding(false), SLIDE_FALLBACK_MS);
    onOffsetChange(next);
  }, [onOffsetChange]);

  const onTrackTransitionEnd = useCallback((e: TransitionEvent<HTMLDivElement>) => {
    if (e.target !== e.currentTarget || e.propertyName !== 'transform') return;
    if (slideTimerRef.current) clearTimeout(slideTimerRef.current);
    slideTimerRef.current = null;
    setSliding(false);
  }, []);

  // All brand chips sit on one track inside a clipping window. Paging shifts
  // the track and resizes the window to the chips inside it, so only whole
  // chips are ever shown, exactly as with the native show/hide paging.
  const geometry = paging.measured
    ? chipWindowGeometry(widths, paging.surviving, paging.offset, paging.last)
    : null;

  const renderChip = (i: number, pagedOut: boolean) => {
    const chip = chips[i];
    return (
      <button
        key={chip.key}
        type="button"
        className={chip.key === activeKey ? 'fs-chip fs-chip--active' : 'fs-chip'}
        data-testid={`filament-select-chip-${chip.key}`}
        data-paged-out={pagedOut ? 'true' : undefined}
        aria-hidden={pagedOut ? true : undefined}
        tabIndex={pagedOut ? -1 : undefined}
        aria-pressed={chip.key === activeKey}
        onClick={() => onSelect(chip.key)}
      >
        {labels[i]}
      </button>
    );
  };

  return (
    <div className={sliding ? 'fs-chip-row fs-chip-row--sliding' : 'fs-chip-row'}>
      <div className="fs-chip-strip" ref={stripRef} data-testid="filament-select-chip-strip">
        {allIndex >= 0 && renderChip(allIndex, false)}
        <div
          className="fs-chip-viewport"
          data-testid="filament-select-chip-viewport"
          style={geometry ? { width: geometry.windowWidth } : undefined}
        >
          <div
            className="fs-chip-track"
            data-testid="filament-select-chip-track"
            style={geometry ? { transform: `translateX(${-geometry.trackShift}px)` } : undefined}
            onTransitionEnd={onTrackTransitionEnd}
          >
            {paging.surviving.map((i, k) =>
              renderChip(i, paging.measured && (k < paging.offset || k > paging.last)))}
          </div>
        </div>
        <div className="fs-chip-measure" ref={measureRef} aria-hidden="true">
          {chips.map((chip, i) => (
            <span key={chip.key} className="fs-chip">{labels[i]}</span>
          ))}
        </div>
      </div>
      <button
        type="button"
        className="fs-chip-arrow fs-chip-arrow--left"
        data-testid="filament-select-chip-left"
        disabled={!paging.canLeft}
        onClick={() => slideTo(paging.offset - 1)}
      >
        <MaskIcon src={arrowLeftUrl} width={16} height={16} className="fs-chip-arrow__icon" />
      </button>
      <button
        type="button"
        className="fs-chip-arrow"
        data-testid="filament-select-chip-right"
        disabled={!paging.canRight}
        onClick={() => slideTo(paging.offset + 1)}
      >
        <MaskIcon src={arrowRightUrl} width={16} height={16} className="fs-chip-arrow__icon" />
      </button>
    </div>
  );
}
