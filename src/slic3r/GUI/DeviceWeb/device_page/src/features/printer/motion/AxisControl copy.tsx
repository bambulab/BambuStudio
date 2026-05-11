import * as React from 'react'
import * as Dialog from '@radix-ui/react-dialog'

// ===== Types =====
type Axis = 'x' | 'y' | 'z'

// ===== Icons =====
function CloseIcon({ className = 'h-4 w-4' }: { className?: string }) {
  return (
    <svg viewBox="0 0 24 24" className={className} aria-hidden>
      <path d="M6 6l12 12M18 6 6 18" stroke="currentColor" strokeWidth="2" strokeLinecap="round" />
    </svg>
  )
}

function ArrowIcon({ dir = 'up', className = 'h-4 w-4' }: { dir?: 'up' | 'down'; className?: string }) {
  const rot = dir === 'up' ? 0 : 180
  return (
    <svg viewBox="0 0 24 24" className={className} style={{ transform: `rotate(${rot}deg)` }} aria-hidden>
      <path d="M12 5l6 6M12 5L6 11" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" />
      <path d="M12 5v14" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" />
    </svg>
  )
}

// ===== Dial =====
function AxisDisc({ sizeRem = 13.75, onHome, onJog }: { sizeRem?: number; onHome?: () => void; onJog?: (axis: Axis, delta: number) => void }) {
  // 两层 8 个扇形：外层 4 个浅灰扇形，内层 4 个深灰扇形
  const VB = 220
  const C = VB / 2
  const R = VB * 0.45
  const fontAxis = 14.5
  const textGrey = '#6B6B6B'
  const textDark = '#323A3D'

  const toRad = (deg: number) => (deg * Math.PI) / 180
  const P = (deg: number, r: number) => ({ x: C + r * Math.cos(toRad(deg)), y: C + r * Math.sin(toRad(deg)) })

  // 环形扇形路径（r0=内半径, r1=外半径, a0->a1 角度）
  const ringSectorPath = (r0: number, r1: number, a0: number, a1: number) => {
    // 规范化：保证 a1 > a0
    let _a0 = a0
    let _a1 = a1
    if (_a1 <= _a0) _a1 += 360
    const p0 = P(_a0, r0)
    const p1 = P(_a0, r1)
    const p2 = P(_a1, r1)
    const p3 = P(_a1, r0)
    const large = _a1 - _a0 > 180 ? 1 : 0
    const sweep = 1
    const sweepInner = 0
    return [
      `M ${p0.x} ${p0.y}`,
      `L ${p1.x} ${p1.y}`,
      `A ${r1} ${r1} 0 ${large} ${sweep} ${p2.x} ${p2.y}`,
      `L ${p3.x} ${p3.y}`,
      `A ${r0} ${r0} 0 ${large} ${sweepInner} ${p0.x} ${p0.y}`,
      'Z',
    ].join(' ')
  }

  // 以对角线为分割（旋转 45° 的四象限）：边界角度
  const edges = [-135, -45, 45, 135, 225]

  // 半径：内环(深灰) 与 外环(浅灰)
  const R_INNER_IN = R * 0.28
  const R_INNER_OUT = R * 0.58
  const R_OUTER_IN = R_INNER_OUT
  const R_OUTER_OUT = R

  // 点击映射（粗略）：根据中心角触发 X/Y
  const handleClick = (mid: number) => {
    if (!onJog) return
    const step = 1
    // 右(+X)/左(-X)
    if ((mid > -90 && mid < 90) || mid === 0) onJog('x', +step)
    if (mid <= -90 || mid >= 90) onJog('x', -step)
    // 上(+Y)/下(-Y)
    if (mid > 0 && mid < 180) onJog('y', +step)
    if (mid < 0 || mid === 180) onJog('y', -step)
  }

  // 标签位置（沿右下对角线）
  const midOuterNE = 45
  const midInnerNE = 45
  const midInnerSW = 225
  const midOuterSW = 225

  return (
    <div className="relative" style={{ width: `${sizeRem}rem`, height: `${sizeRem}rem` }}>
      <svg width="100%" height="100%" viewBox={`0 0 ${VB} ${VB}`} className="block">
        {/* 白色底盘 */}
        <circle cx={C} cy={C} r={R} fill="#FFFFFF" />

        {/* 外环：4 个浅灰扇形 */}
        {edges.slice(0, -1).map((a0, i) => {
          const a1 = edges[i + 1]
          const d = ringSectorPath(R_OUTER_IN, R_OUTER_OUT, a0, a1)
          const mid = (a0 + a1) / 2
          return (
            <path key={`outer-${i}`} d={d} fill="#EEEEEE" className="cursor-pointer" onClick={() => handleClick(mid)} />
          )
        })}

        {/* 内环：4 个深灰扇形 */}
        {edges.slice(0, -1).map((a0, i) => {
          const a1 = edges[i + 1]
          const d = ringSectorPath(R_INNER_IN, R_INNER_OUT, a0, a1)
          const mid = (a0 + a1) / 2
          return (
            <path key={`inner-${i}`} d={d} fill="#CECECE" className="cursor-pointer" onClick={() => handleClick(mid)} />
          )
        })}

        {/* 轴向字母 */}
        <text x={C} y={C - R + 20} textAnchor="middle" fontSize={fontAxis} fill={textDark}>Y</text>
        <text x={C} y={C + R - 10} textAnchor="middle" fontSize={fontAxis} fill={textDark}>−Y</text>
        <text x={C - R + 20} y={C + 5} textAnchor="start" fontSize={fontAxis} fill={textDark}>−X</text>
        <text x={C + R - 20} y={C + 5} textAnchor="end" fontSize={fontAxis} fill={textDark}>X</text>

        {/* 对角文字：右下线附近，外环±10，内环±1，并沿 45° 方向微旋转 */}
        <text x={P(midOuterNE, (R_OUTER_IN + R_OUTER_OUT) / 2).x} y={P(midOuterNE, (R_OUTER_IN + R_OUTER_OUT) / 2).y} textAnchor="middle" fontSize={12.5} fill={textGrey} transform={`rotate(45 ${P(midOuterNE, (R_OUTER_IN + R_OUTER_OUT) / 2).x} ${P(midOuterNE, (R_OUTER_IN + R_OUTER_OUT) / 2).y})`}>+10</text>
        <text x={P(midInnerNE, (R_INNER_IN + R_INNER_OUT) / 2).x} y={P(midInnerNE, (R_INNER_IN + R_INNER_OUT) / 2).y} textAnchor="middle" fontSize={12.5} fill={textGrey} transform={`rotate(45 ${P(midInnerNE, (R_INNER_IN + R_INNER_OUT) / 2).x} ${P(midInnerNE, (R_INNER_IN + R_INNER_OUT) / 2).y})`}>+1</text>
        <text x={P(midInnerSW, (R_INNER_IN + R_INNER_OUT) / 2).x} y={P(midInnerSW, (R_INNER_IN + R_INNER_OUT) / 2).y} textAnchor="middle" fontSize={12.5} fill={textGrey} transform={`rotate(45 ${P(midInnerSW, (R_INNER_IN + R_INNER_OUT) / 2).x} ${P(midInnerSW, (R_INNER_IN + R_INNER_OUT) / 2).y})`}>−1</text>
        <text x={P(midOuterSW, (R_OUTER_IN + R_OUTER_OUT) / 2).x} y={P(midOuterSW, (R_OUTER_IN + R_OUTER_OUT) / 2).y} textAnchor="middle" fontSize={12.5} fill={textGrey} transform={`rotate(45 ${P(midOuterSW, (R_OUTER_IN + R_OUTER_OUT) / 2).x} ${P(midOuterSW, (R_OUTER_IN + R_OUTER_OUT) / 2).y})`}>−10</text>

        {/* 中央 Home 带阴影 */}
        <defs>
          <filter id="discShadow" x="-50%" y="-50%" width="200%" height="200%">
            <feDropShadow dx="0" dy="1.2" stdDeviation="1.2" floodColor="rgba(0,0,0,0.15)" />
          </filter>
        </defs>
        <g filter="url(#discShadow)">
          <circle cx={C} cy={C} r={18} fill="#FFFFFF" stroke="#D9DDE1" strokeWidth={1} />
        </g>
        <g transform={`translate(${C}, ${C})`}>
          <path d="M -7 2 L 0 -5 L 7 2 V 9 H 2.2 V 5.2 h-4.4 V 9 H -7 Z" fill="#00AE42" />
        </g>
      </svg>
      <button
        type="button"
        aria-label="Home"
        onClick={onHome}
        className="absolute left-1/2 top-1/2 h-[3rem] w-[3rem] -translate-x-1/2 -translate-y-1/2 rounded-full"
      />
    </div>
  )
}

function DialogHeader({ title, onClose }: { title: string; onClose: () => void }) {
  return (
    <div className="absolute left-0 top-0 flex h-[2.875rem] w-full items-center gap-2 rounded-t-lg bg-[#F8F8F8] px-3">
      <div className="flex h-[1.375rem] w-[1.375rem] items-center justify-center text-black/80">
        <svg viewBox="0 0 24 24" className="h-[1.125rem] w-[1.125rem]">
          <path d="M9 6l6 6-6 6" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" />
        </svg>
      </div>
      <Dialog.Title asChild>
        <div className="relative pl-1">
          <span className="font-bold text-[0.875rem] leading-[1.375rem] tracking-tight text-[#1F1F1F]">{title}</span>
        </div>
      </Dialog.Title>
      <div className="ml-auto flex items-center">
        <Dialog.Close asChild>
          <button
            type="button"
            aria-label="Close"
            onClick={onClose}
            className="inline-flex h-6 w-6 items-center justify-center rounded-full hover:bg-black/5 active:bg-black/10 focus:outline-none focus:ring-2 focus:ring-black/10"
          >
            <CloseIcon className="h-4 w-4 text-black" />
          </button>
        </Dialog.Close>
      </div>
    </div>
  )
}

function BedControls({ onJog }: { onJog?: (axis: Axis, delta: number) => void }) {
  const Row = ({ dir, label, tone }: { dir: 'up' | 'down'; label: string; tone: 'light' | 'dark' }) => (
    <button
      type="button"
      onClick={() => onJog?.('z', (dir === 'up' ? 1 : -1) * parseInt(label))}
      className={[
        'flex w-[9.5625rem] h-[3.125rem] items-center justify-center gap-2 text-[#323A3D]',
        tone === 'light' ? 'bg-[#EEEEEE] px-3' : 'bg-[#CECECE] px-4',
      ].join(' ')}
    >
      <ArrowIcon dir={dir} className="h-4 w-4" />
      <span className="text-[0.875rem] leading-4">{label}</span>
    </button>
  )

  return (
    <div className="absolute left-[19.875rem] top-[5rem]">
      <div className="mx-auto mb-2 text-center text-[0.875rem] leading-[1.375rem] text-[#6B6B6B]">Bed</div>
      {/* 外层仅负责阴影与整体轮廓，不再裁剪，便于中间分隔 */}
      <div className="rounded-[0.625rem] shadow-[0_0.375rem_1rem_rgba(0,0,0,0.12)]">
        {/* 上组（增加） */}
        <div className="overflow-hidden rounded-t-[0.625rem]">
          <Row dir="up" label="+10" tone="light" />
          <Row dir="up" label="+1" tone="dark" />
        </div>
        {/* 中间分隔（12px） */}
        <div className="h-[0.625rem] bg-white" />
        {/* 下组（减少） */}
        <div className="overflow-hidden rounded-b-[0.625rem]">
          <Row dir="down" label="-1" tone="dark" />
          <Row dir="down" label="-10" tone="light" />
        </div>
      </div>
    </div>
  )
}

function XYZDialog({ open, onClose, onHome, onJog, title = 'Motion:XYZ' }: { open: boolean; title?: string; onClose: () => void; onHome?: () => void; onJog?: (axis: Axis, delta: number) => void }) {
  const panelRef = React.useRef<HTMLDivElement | null>(null)
  React.useEffect(() => {
    if (!open) return
    const t = setTimeout(() => panelRef.current?.focus(), 0)
    return () => clearTimeout(t)
  }, [open])
  const onKeyDown = (e: React.KeyboardEvent<HTMLDivElement>) => {
    const base = e.shiftKey ? 10 : 1
    if (e.key === 'ArrowUp') onJog?.('y', +base)
    else if (e.key === 'ArrowDown') onJog?.('y', -base)
    else if (e.key === 'ArrowLeft') onJog?.('x', -base)
    else if (e.key === 'ArrowRight') onJog?.('x', +base)
    else if (e.key.toLowerCase() === 'h') onHome?.()
  }
  return (
    <Dialog.Root open={open} onOpenChange={(next) => { if (!next) onClose() }} modal={false}>
      <Dialog.Portal>
        <Dialog.Content
          onEscapeKeyDown={(e) => e.preventDefault()}
          onInteractOutside={(e) => e.preventDefault()}
          onPointerDownOutside={(e) => e.preventDefault()}
          onFocusOutside={(e) => e.preventDefault()}
          onOpenAutoFocus={(e) => { e.preventDefault(); panelRef.current?.focus() }}
          className={[
            'fixed left-1/2 -translate-x-1/2 top-[7.1875rem] z-[1000]',
            'w-[34.125rem] h-[20.8125rem]',
            'rounded-lg bg-white shadow-[0_0.375rem_1rem_rgba(0,0,0,0.12)] outline-none',
            'focus-visible:ring-2 focus-visible:ring-black/10',
            'relative',
          ].join(' ')}
          ref={panelRef as any}
          onKeyDown={onKeyDown}
        >
          <DialogHeader title={title} onClose={onClose} />
          <div className="absolute left-0 top-[2.875rem] h-px w-full border-t border-[#EBEBEB]" />
          {/* 标题：Toolhead */}
          <div className="absolute left-[9.625rem] top-[3.8125rem] h-[1.375rem] w-[3.6875rem] text-center text-[0.875rem] leading-[1.375rem] text-[#6B6B6B]">Toolhead</div>
          {/* 圆盘 */}
          <div className="absolute left-[4.625rem] top-[5.5rem]">
            <AxisDisc sizeRem={13.75} onHome={onHome} onJog={onJog} />
          </div>
          {/* 右侧 Bed 控件 */}
          <BedControls onJog={onJog} />
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog.Root>
  )
}

export default function Demo() {
  const [open, setOpen] = React.useState(false)
  return (
    <div className="p-6">
      <button
        type="button"
        onClick={() => setOpen(true)}
        className="inline-flex items-center gap-2 rounded-lg px-4 py-2 bg-black text-white hover:opacity-90 focus:outline-none focus:ring-2 focus:ring-black/20"
      >
        打开 Motion: XYZ
      </button>
      <XYZDialog open={open} onClose={() => setOpen(false)} onHome={() => console.log('home')} onJog={(axis, d) => console.log('jog', axis, d)} />
    </div>
  )
}
