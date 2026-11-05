import { useLayoutEffect, useRef, useState } from 'react';
import type { MouseEvent as ReactMouseEvent, MutableRefObject } from 'react';
import { createPortal } from 'react-dom';
import { documentZoom } from '../../../../hooks/useFitHostWidth';

const TIP_MAX_WIDTH = 288;
const TIP_MARGIN = 8;
const CURSOR_OFFSET_X = 14;
const CURSOR_OFFSET_Y = 18;

// Replaces the native `title` tooltip. OS tooltips ignore CSS and also wait
// for their own hover delay, so Win and Mac neither wrap at the same width
// nor show up as soon as the pointer enters the card. This bubble is shown on
// enter, tracks the pointer, and hides on leave. The 288px wrap cap lives in
// `.ams-map-hotend__hover-tip`.
export function useHoverTip(text: string) {
  const [visible, setVisible] = useState(false);
  const shown = useRef(false);
  const point = useRef({ x: 0, y: 0 });
  const nodeRef = useRef<HTMLDivElement | null>(null);

  const track = (event: ReactMouseEvent<HTMLElement>) => {
    if (!text) return;
    point.current = { x: event.clientX, y: event.clientY };
    if (!shown.current) {
      shown.current = true;
      setVisible(true);
      return;
    }
    const node = nodeRef.current;
    if (node) placeAtCursor(node, event.clientX, event.clientY);
  };

  const hoverProps = {
    onMouseEnter: track,
    onMouseMove: track,
    onMouseLeave: () => {
      shown.current = false;
      setVisible(false);
    },
  };

  const tip = text && visible
    ? createPortal(
        <HoverTipBubble nodeRef={nodeRef} text={text} x={point.current.x} y={point.current.y} />,
        document.body,
      )
    : null;

  return { hoverProps, tip };
}

function HoverTipBubble({
  nodeRef,
  text,
  x,
  y,
}: {
  nodeRef: MutableRefObject<HTMLDivElement | null>;
  text: string;
  x: number;
  y: number;
}) {
  const [node, setNode] = useState<HTMLDivElement | null>(null);

  useLayoutEffect(() => {
    if (!node) return;
    nodeRef.current = node;
    // Lock the wrap width before measuring height. WebKit (Mac) does not wrap
    // a max-content box at max-width; an explicit 288px width does, on both
    // WebView2 and WKWebView.
    fitWrapWidth(node);
    placeAtCursor(node, x, y);
    return () => {
      if (nodeRef.current === node) nodeRef.current = null;
    };
  }, [node, nodeRef, text, x, y]);

  return (
    <div
      ref={setNode}
      className="ams-map-hotend__hover-tip"
      style={{ left: x + CURSOR_OFFSET_X, top: y + CURSOR_OFFSET_Y }}
      role="tooltip"
    >
      {text}
    </div>
  );
}

function fitWrapWidth(node: HTMLDivElement) {
  node.style.width = 'max-content';
  node.style.maxWidth = 'none';
  node.style.whiteSpace = 'nowrap';
  const natural = node.offsetWidth;
  node.style.whiteSpace = 'pre-wrap';
  node.style.maxWidth = `${TIP_MAX_WIDTH}px`;
  node.style.width = natural > TIP_MAX_WIDTH ? `${TIP_MAX_WIDTH}px` : 'max-content';
}

function placeAtCursor(node: HTMLDivElement, x: number, y: number) {
  const zoom = documentZoom();
  const width = node.offsetWidth * zoom;
  const height = node.offsetHeight * zoom;
  let left = x + CURSOR_OFFSET_X;
  let top = y + CURSOR_OFFSET_Y;
  if (left + width > window.innerWidth - TIP_MARGIN) {
    left = x - CURSOR_OFFSET_X - width;
  }
  if (left < TIP_MARGIN) left = TIP_MARGIN;
  if (top + height > window.innerHeight - TIP_MARGIN) {
    top = y - CURSOR_OFFSET_Y - height;
  }
  if (top < TIP_MARGIN) top = TIP_MARGIN;
  node.style.left = `${left / zoom}px`;
  node.style.top = `${top / zoom}px`;
}
