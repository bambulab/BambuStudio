import { useLayoutEffect, useRef, useState } from 'react';

// Measure the real rendered height of a DOM node and expose it as state.
//
// Why not the previous `amsControlWebContentHeight()` constant sum:
// the sum ignores text wrapping (SwitcherSetupHint in long translations),
// font metrics, actual AmsFooter chrome, and the `error` row. When the sum
// came out smaller than the live DOM, C++ clamped the WebView height too
// low and WebView2 produced a vertical scrollbar. Measuring the real node
// avoids that drift.
//
// Why observing a *fixed-width* child is safe (no ping-pong with C++):
// the measured node is the inner body that has `width: 578px` hard-coded,
// so its width does not react to WebView container width. Changing the
// reported height only changes the host height, which only changes the
// WebView container height, which does not feed back into the inner
// width. The two-dimensional feedback loop that forced the original code
// to use a constant sum does not apply here.

export interface MeasuredHeight {
  ref: (node: HTMLDivElement | null) => void;
  height: number;
}

// A small safety pad on top of offsetHeight. DPR rounding at non-integer
// scale factors can round the measured box 1 px below the live layout, and
// WebView2's intrinsic scrollbar heuristic triggers on even a 1 px shortfall.
const HEIGHT_SAFETY_PX = 2;

export function useMeasuredHeight(): MeasuredHeight {
  const [node, setNode] = useState<HTMLDivElement | null>(null);
  const [height, setHeight] = useState(0);
  const lastRef = useRef(0);

  useLayoutEffect(() => {
    if (!node) {
      lastRef.current = 0;
      setHeight(0);
      return;
    }

    let frame = 0;

    const measure = () => {
      frame = 0;
      // offsetHeight is layout height in CSS px; it ignores CSS transforms
      // applied to the same node, so the scale-aware wrapper above does not
      // skew the measurement.
      const next = node.offsetHeight + HEIGHT_SAFETY_PX;
      if (next === lastRef.current) return;
      lastRef.current = next;
      setHeight(next);
    };

    const schedule = () => {
      if (frame !== 0) return;
      frame = requestAnimationFrame(measure);
    };

    measure();

    const observer = new ResizeObserver(schedule);
    observer.observe(node);

    return () => {
      if (frame !== 0) cancelAnimationFrame(frame);
      observer.disconnect();
    };
  }, [node]);

  return { ref: setNode, height };
}
