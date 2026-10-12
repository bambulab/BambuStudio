import { useLayoutEffect, useState } from 'react';

// Keep the AMS page at 100% whenever the WebView2 viewport has room for the
// natural layout; shrink proportionally if it does not. The design positions
// slot cards, road paths and nozzle icons at fixed pixel coordinates, so a
// CSS transform on a single wrapper is the only safe way to downscale. A
// partial rescale (e.g. only changing CONTENT_WIDTH) would move the overlay
// geometry off the slot bodies.
//
// Why `transform: scale` plus a sized placeholder is required:
//   * `transform` does not shrink the element's layout box. If we scaled the
//     content without resizing its placeholder, WebView2 would still see the
//     natural width/height and keep both scrollbars.
//   * Measuring with `getBoundingClientRect()` after the transform returns
//     the *scaled* size, which collapses the computation. We measure the
//     intrinsic content with `offsetWidth/Height` instead, and we read the
//     viewport from the outer sentinel so the measurement is independent of
//     the scale we just applied.

// Floor on the auto scale. Below this the slot cards and footer buttons get
// too small to tap comfortably; prefer letting the host crop over producing
// an unreadable UI.
const MIN_SCALE = 0.5;

// Keep a few pixels off the viewport before computing the fit ratio. DPR
// rounding at non-integer scale factors (125% / 150% Windows scaling) and
// the exact value of the WebView2 scrollbar gutter can round the content
// box 1 px wider than the viewport and re-trigger a scrollbar at scale 1.
const VIEWPORT_SAFETY_PX = 2;

export interface AutoScaleResult {
  // Callback refs so the effect re-runs whenever the measured nodes remount
  // (e.g. the page toggling visibility).
  viewportRef: (node: HTMLDivElement | null) => void;
  contentRef: (node: HTMLDivElement | null) => void;
  scale: number;
  contentWidth: number;
  contentHeight: number;
}

export function useAutoScaleToViewport(): AutoScaleResult {
  const [viewport, setViewport] = useState<HTMLDivElement | null>(null);
  const [content, setContent] = useState<HTMLDivElement | null>(null);
  const [scale, setScale] = useState(1);
  const [contentSize, setContentSize] = useState({ width: 0, height: 0 });

  useLayoutEffect(() => {
    if (!viewport || !content) return;

    let frame = 0;

    const measure = () => {
      frame = 0;
      // Natural (unscaled) size: offsetWidth/Height ignores the CSS transform
      // currently applied to `content`, so the ratio stays stable across runs.
      const nextWidth = content.offsetWidth;
      const nextHeight = content.offsetHeight;
      // Viewport is the outer sentinel. clientWidth/Height already excludes
      // any scrollbar, so when fit becomes 1 again the measurement does not
      // flap because of a scrollbar that was there a frame ago.
      const availWidth = Math.max(0, viewport.clientWidth - VIEWPORT_SAFETY_PX);
      const availHeight = Math.max(0, viewport.clientHeight - VIEWPORT_SAFETY_PX);

      let next = 1;
      if (nextWidth > 0 && nextHeight > 0 && availWidth > 0 && availHeight > 0) {
        const fit = Math.min(availWidth / nextWidth, availHeight / nextHeight);
        if (fit < 1) {
          next = Math.max(MIN_SCALE, fit);
        }
      }

      setScale((prev) => (Math.abs(prev - next) < 0.001 ? prev : next));
      setContentSize((prev) =>
        prev.width === nextWidth && prev.height === nextHeight
          ? prev
          : { width: nextWidth, height: nextHeight },
      );
    };

    const schedule = () => {
      if (frame !== 0) return;
      frame = requestAnimationFrame(measure);
    };

    // First pass synchronously so the initial paint already uses the final
    // scale (avoids a one-frame flash at scale 1 before shrinking).
    measure();

    const viewportObserver = new ResizeObserver(schedule);
    const contentObserver = new ResizeObserver(schedule);
    viewportObserver.observe(viewport);
    contentObserver.observe(content);

    window.addEventListener('resize', schedule);

    return () => {
      if (frame !== 0) cancelAnimationFrame(frame);
      viewportObserver.disconnect();
      contentObserver.disconnect();
      window.removeEventListener('resize', schedule);
    };
  }, [viewport, content]);

  return {
    viewportRef: setViewport,
    contentRef: setContent,
    scale,
    contentWidth: contentSize.width,
    contentHeight: contentSize.height,
  };
}
