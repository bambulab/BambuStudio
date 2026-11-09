import { useCallback, useEffect, useRef, useState } from 'react';
import type { TouchEvent as ReactTouchEvent, MouseEvent as ReactMouseEvent } from 'react';

const LONG_PRESS_MS = 500;
// Touchmove tolerance squared (px²). Larger movements during a long-press
// cancel the gesture so the user can still scroll without summoning the menu.
const LONG_PRESS_MOVE_TOLERANCE_SQ = 100;

export interface CardContextMenuAnchor {
  x: number;
  y: number;
}

/**
 * Replaces the per-card three-dot menu button with a context-menu interaction
 * triggered by right-click (desktop) or 500ms long-press (touch). The hook
 * exposes the open state, anchor coordinates, and pre-bound handlers; the
 * caller is responsible for rendering the menu (e.g. CardContextMenu).
 */
export function useCardContextMenu({
  disabled = false,
}: { disabled?: boolean } = {}) {
  const [anchor, setAnchor] = useState<CardContextMenuAnchor | null>(null);
  const open = anchor !== null;
  const longPressTimer = useRef<number | null>(null);
  const touchOrigin = useRef<CardContextMenuAnchor | null>(null);

  const close = useCallback(() => setAnchor(null), []);

  const openAt = useCallback(
    (clientX: number, clientY: number) => {
      if (disabled) return;
      setAnchor({ x: clientX, y: clientY });
    },
    [disabled],
  );

  const handleContextMenu = useCallback(
    (event: ReactMouseEvent) => {
      // Always suppress the native browser context menu on the card; even when
      // disabled we don't want the default WebView menu to appear.
      event.preventDefault();
      event.stopPropagation();
      if (disabled) return;
      openAt(event.clientX, event.clientY);
    },
    [disabled, openAt],
  );

  const cancelLongPress = useCallback(() => {
    if (longPressTimer.current) {
      window.clearTimeout(longPressTimer.current);
      longPressTimer.current = null;
    }
    touchOrigin.current = null;
  }, []);

  const handleTouchStart = useCallback(
    (event: ReactTouchEvent) => {
      if (disabled) return;
      if (event.touches.length !== 1) return;
      const touch = event.touches[0];
      touchOrigin.current = { x: touch.clientX, y: touch.clientY };
      longPressTimer.current = window.setTimeout(() => {
        openAt(touch.clientX, touch.clientY);
        longPressTimer.current = null;
      }, LONG_PRESS_MS);
    },
    [disabled, openAt],
  );

  const handleTouchMove = useCallback(
    (event: ReactTouchEvent) => {
      if (!longPressTimer.current || !touchOrigin.current) return;
      const touch = event.touches[0];
      const dx = touch.clientX - touchOrigin.current.x;
      const dy = touch.clientY - touchOrigin.current.y;
      if (dx * dx + dy * dy > LONG_PRESS_MOVE_TOLERANCE_SQ) {
        cancelLongPress();
      }
    },
    [cancelLongPress],
  );

  useEffect(() => {
    if (!open) return;
    // Close on any pointerdown outside the menu element itself (the menu
    // tags its root with data-card-context-menu="true" so we can identify it
    // without holding a ref).
    const onPointerDown = (event: PointerEvent) => {
      const target = event.target as HTMLElement | null;
      if (target?.closest('[data-card-context-menu]')) return;
      close();
    };
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') close();
    };
    const onScroll = () => close();
    document.addEventListener('pointerdown', onPointerDown);
    document.addEventListener('keydown', onKeyDown);
    window.addEventListener('scroll', onScroll, true);
    window.addEventListener('resize', onScroll);
    return () => {
      document.removeEventListener('pointerdown', onPointerDown);
      document.removeEventListener('keydown', onKeyDown);
      window.removeEventListener('scroll', onScroll, true);
      window.removeEventListener('resize', onScroll);
    };
  }, [open, close]);

  useEffect(() => () => cancelLongPress(), [cancelLongPress]);

  return {
    open,
    anchor,
    close,
    openAt,
    bind: {
      onContextMenu: handleContextMenu,
      onTouchStart: handleTouchStart,
      onTouchMove: handleTouchMove,
      onTouchEnd: cancelLongPress,
      onTouchCancel: cancelLongPress,
    },
  };
}
