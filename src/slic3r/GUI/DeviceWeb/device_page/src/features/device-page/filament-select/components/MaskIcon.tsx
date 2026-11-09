import type { CSSProperties } from 'react';

interface MaskIconProps {
  /** Statically imported asset URL (single-colour wx SVG). */
  src: string;
  width: number;
  height: number;
  className: string;
}

// Paints a single-colour wx SVG with the CSS `background-color` of its class,
// so the light/dark recolouring BitmapCache does for wx icons can be expressed
// as theme tokens instead of duplicate asset files.
export function MaskIcon({ src, width, height, className }: MaskIconProps) {
  const style: CSSProperties = {
    width,
    height,
    WebkitMaskImage: `url("${src}")`,
    maskImage: `url("${src}")`,
  };
  return <span aria-hidden="true" className={`fs-mask-icon ${className}`} style={style} />;
}
