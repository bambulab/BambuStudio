import { useLayoutEffect } from 'react';

// innerWidth may land a pixel short of the host width on fractional DPI.
const FIT_TOLERANCE = 2;

// Hosts are sized in DIP and assume 1 CSS px == 1 DIP, but WebView2 also
// applies Windows text scaling, so the page can outgrow its host. Zoom the
// document so the design width fits the viewport again. vh/vw get zoomed a
// second time, so size against the viewport with % of html/body/#root instead.
export function useFitHostWidth(designWidth: number) {
  useLayoutEffect(() => {
    const root = document.documentElement;
    const update = () => {
      const width = window.innerWidth;
      const scale = width > 0 && width < designWidth - FIT_TOLERANCE ? width / designWidth : 1;
      root.style.zoom = scale === 1 ? '' : String(scale);
    };
    update();
    window.addEventListener('resize', update);
    return () => {
      window.removeEventListener('resize', update);
      root.style.zoom = '';
    };
  }, [designWidth]);
}

// Zoom applied by useFitHostWidth. Event coordinates, innerWidth and
// getBoundingClientRect() are in viewport px, while left/top, offset sizes and
// SVG coordinates are in layout px: divide viewport px by this to convert.
export function documentZoom(): number {
  return Number(document.documentElement.style.zoom) || 1;
}
