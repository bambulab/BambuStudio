import type { CSSProperties } from 'react';
import { px } from '../dip';
import { cn } from './style';

export function ThemeImage({
  light,
  dark,
  width,
  height,
  className,
  style,
}: {
  light: string;
  dark: string;
  width: number;
  height: number;
  className?: string;
  style?: CSSProperties;
}) {
  const size = { width: px(width), height: px(height), ...style };
  return (
    <>
      <img src={light} alt="" aria-hidden className={cn('ams-img-light', className)} style={size} />
      <img src={dark} alt="" aria-hidden className={cn('ams-img-dark', className)} style={size} />
    </>
  );
}
