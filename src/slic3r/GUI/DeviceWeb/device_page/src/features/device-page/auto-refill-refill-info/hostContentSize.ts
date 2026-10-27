// Logical-pixel baselines reported to the wx host. The host wraps these values
// with FromDIP() so 640 here becomes 640 * DPI scale on the screen.
//
// MIN_WIDTH keeps the management page wide enough for three 204px refill
// columns plus their 12px gaps and the page's horizontal padding.
// BASE_HEIGHT keeps the dialog usable before the first ResizeObserver tick;
// MAX_HEIGHT caps growth for tall groups, beyond which the columns area
// scrolls internally.
export const REFILL_PAGE_MIN_WIDTH = 668;
export const REFILL_PAGE_BASE_HEIGHT = 360;
export const REFILL_PAGE_MAX_HEIGHT = 620;

export interface HostContentSizeSource {
  scrollWidth: number;
  scrollHeight: number;
  offsetHeight: number;
}

export function resolveHostContentSize(source: HostContentSizeSource) {
  const width = Math.ceil(Math.max(source.scrollWidth, REFILL_PAGE_MIN_WIDTH));
  const rawHeight = source.scrollHeight || source.offsetHeight;
  const height = Math.ceil(
    Math.min(Math.max(rawHeight, REFILL_PAGE_BASE_HEIGHT), REFILL_PAGE_MAX_HEIGHT)
  );

  return { width, height };
}
