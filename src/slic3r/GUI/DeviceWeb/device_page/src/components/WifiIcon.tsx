import * as React from 'react'


export function arcPath(
  cx: number, cy: number,
  r: number,
  startDeg: number, endDeg: number,
  clockwise = true
) {
  const rad = (a: number) => (Math.PI / 180) * a
  const toXY = (deg: number) => [cx + r * Math.cos(rad(deg)), cy + r * Math.sin(rad(deg))] as const
  const [x1, y1] = toXY(startDeg)
  const [x2, y2] = toXY(endDeg)

  let delta = ((endDeg - startDeg) % 360 + 360) % 360
  if (!clockwise) delta = 360 - delta

  const largeArc = delta > 180 ? 1 : 0
  const sweep = clockwise ? 1 : 0

  return `M ${x1} ${y1} A ${r} ${r} 0 ${largeArc} ${sweep} ${x2} ${y2}`
}

type ArcProps = {
  cx: number; cy: number; r: number;
  start: number; end: number;
  stroke?: string; strokeWidth?: number;
  rounded?: boolean; clockwise?: boolean;
  className?: string;
}

export function Arc({
  cx, cy, r, start, end,
  stroke = 'currentColor',
  strokeWidth = 3,
  rounded = true,
  clockwise = true,
  className,
}: ArcProps) {
  const d = arcPath(cx, cy, r, start, end, clockwise)
  return (
    <path
      d={d}
      fill="none"
      stroke={stroke}
      strokeWidth={strokeWidth}
      strokeLinecap={rounded ? 'round' : 'butt'}
      className={className}
    />
  )
}


type Level = 0 | 1 | 2 | 3
interface WifiIconProps extends React.SVGProps<SVGSVGElement> {
  level?: Level
  activeColor?: string           // 激活段颜色（默认跟随 currentColor）
  inactiveColor?: string         // 未激活段颜色
  hideInactive?: boolean         // 是否隐藏未激活段（默认 false=灰显）
  innerWidth?: number            // 内弧粗细
  outerWidth?: number            // 外弧粗细
  dotRadius?: number
}

export default function WiFiIcon({
  level = 3,
  activeColor = 'currentColor',
  inactiveColor = '#DBDBDB',
  hideInactive = false,
  innerWidth = 4,
  outerWidth = 4,
  dotRadius = 4,
  width = 25,
  ...svgProps
}: WifiIconProps) {
  const lv = Math.max(0, Math.min(3, level)) as Level

  const cx = 30, cy = 30
  const start = 225, end = 315

  const show = (need: Level) => lv >= need
  const color = (need: Level) => (show(need) ? activeColor : inactiveColor)

  return (
    <svg viewBox="0 0 60 40" width={width} aria-label={`Wi-Fi level ${lv} of 3`} {...svgProps}>
      {(show(3) || !hideInactive) && (
        <Arc cx={cx} cy={cy} r={22} start={start} end={end}
          stroke={color(3)} strokeWidth={outerWidth} />
      )}

      {(show(2) || !hideInactive) && (
        <Arc cx={cx} cy={cy} r={12} start={start} end={end}
          stroke={color(2)} strokeWidth={innerWidth} />
      )}

      {(show(1) || !hideInactive) && (
        <circle cx={cx} cy={cy} r={dotRadius} fill={show(1) ? activeColor : inactiveColor} />
      )}
    </svg>
  )
}