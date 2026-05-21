// Logical-pixel baselines reported to the wx host. The host wraps these values
// with FromDIP() so 500 here becomes 500 * DPI scale on the screen.
//
// MIN_WIDTH covers the widest mapping layout (5 cards + gaps + padding).
// BASE_HEIGHT covers the smallest case (two-row main view) so the popup does
// not collapse below a usable size before the first ResizeObserver tick.
// MAX_HEIGHT caps how tall the popup may grow when many sub-AMS groups are
// present. Beyond this cap, .ams-map-hotend__sub-ams-page__groups scrolls
// internally (max-height + overflow-y: auto) while the back button, parent
// card and bottom tip remain visible.
export const MAPPING_PAGE_MIN_WIDTH = 640;
export const MAPPING_PAGE_BASE_HEIGHT = 280;
export const MAPPING_PAGE_MAX_HEIGHT = 500;

export interface HostContentSizeSource {
  scrollWidth: number;
  scrollHeight: number;
  offsetHeight: number;
}

export function resolveHostContentSize(source: HostContentSizeSource) {
  const width = Math.ceil(Math.max(source.scrollWidth, MAPPING_PAGE_MIN_WIDTH));
  const rawHeight = source.scrollHeight || source.offsetHeight;
  const height = Math.ceil(
    Math.min(Math.max(rawHeight, MAPPING_PAGE_BASE_HEIGHT), MAPPING_PAGE_MAX_HEIGHT)
  );

  return { width, height };
}
