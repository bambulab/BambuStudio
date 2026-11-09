export const HOTEND_PAGE_MIN_WIDTH = 560;
export const HOTEND_PAGE_BASE_HEIGHT = 440;

export interface HostContentSizeSource {
  scrollWidth: number;
  scrollHeight: number;
  offsetHeight: number;
}

export function resolveHostContentSize(source: HostContentSizeSource) {
  const width = Math.ceil(Math.max(source.scrollWidth, HOTEND_PAGE_MIN_WIDTH));
  const rawHeight = source.scrollHeight || source.offsetHeight;
  const height = Math.ceil(Math.max(rawHeight, HOTEND_PAGE_BASE_HEIGHT));

  return { width, height };
}
