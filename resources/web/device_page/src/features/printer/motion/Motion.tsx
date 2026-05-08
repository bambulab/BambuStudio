import * as React from 'react'
import * as Dialog from '@radix-ui/react-dialog'

// ===== Types =====
type Axis = 'x' | 'y' | 'z'



/* note: from 0 clockwise down to 360 */
function SectorPath(cx: number, cy: number, rInner: number, rOuter: number, startDeg: number, sweepDeg: number, offset: number) {
  const inner_ofs_rad = Math.asin(offset / rInner);
  const outer_ofs_rad = Math.asin(offset / rOuter);

  const rad = (d: number) => (d * Math.PI) / 180;
  const pt = (r: number, deg: number, ofs: number) => [cx + r * Math.cos(rad(deg) + ofs), cy + r * Math.sin(rad(deg) + ofs)] as const;
  const [x1, y1] = pt(rOuter, startDeg, outer_ofs_rad);
  const [x2, y2] = pt(rOuter, startDeg + sweepDeg, - outer_ofs_rad);
  const [x3, y3] = pt(rInner, startDeg + sweepDeg, -inner_ofs_rad);
  const [x4, y4] = pt(rInner, startDeg, inner_ofs_rad);
  const large = sweepDeg > 180 ? 1 : 0;
  return `M ${x1} ${y1} A ${rOuter} ${rOuter} 0 ${large} 1 ${x2} ${y2}
          L ${x3} ${y3} A ${rInner} ${rInner} 0 ${large} 0 ${x4} ${y4} Z`;
}

export function XYControl() {
  const inner_radius = 20;
  const middle_radius = 65;
  const outer_radius = 110;

  const offset = 4;
  const textTrans = `7/220`;

  const hoverStyle = 'hover:stroke-green-500 hover:stroke-2';

  return (
    <div className='relative aspect-square w-[13.75rem] '>

      <svg viewBox="0 0 220 220" className={`absolute w-full h-full`}>
        {/* outer right */}
        <path d={SectorPath(110, 110, middle_radius, outer_radius, -45, 90, offset)} fill="#EEEEEE" className={`${hoverStyle}`} onClick={() => console.log('clicked')} />
        <text x={`${110 + (middle_radius + outer_radius) / 2}`} y="110" textAnchor="middle" dominantBaseline="middle" fontSize="18" fill="#111"> +X </text>

        {/* outer down */}
        <path d={SectorPath(110, 110, middle_radius, outer_radius, 45, 90, offset)} fill="#EEEEEE" className={`${hoverStyle}`} onClick={() => console.log('clicked')} />
        <text x="110" y={`${110 + (middle_radius + outer_radius) / 2}`} textAnchor="middle" dominantBaseline="middle" fontSize="18" fill="#111"> -Y </text>

        {/* outer left */}
        <path d={SectorPath(110, 110, middle_radius, outer_radius, 135, 90, offset)} fill="#EEEEEE" className={`${hoverStyle}`} onClick={() => console.log('clicked')} />
        <text x={`${110 - (middle_radius + outer_radius) / 2}`} y="110" textAnchor="middle" dominantBaseline="middle" fontSize="18" fill="#111"> -X </text>

        {/* outer up */}
        <path d={SectorPath(110, 110, middle_radius, outer_radius, 225, 90, offset)} fill="#EEEEEE" className={`${hoverStyle}`} onClick={() => console.log('clicked')} />
        <text x="110" y={`${110 - (middle_radius + outer_radius) / 2}`} textAnchor="middle" dominantBaseline="middle" fontSize="18" fill="#111"> +Y </text>

        {/* inner right */}
        <path d={SectorPath(110, 110, inner_radius, middle_radius, -45, 90, offset)} fill="#CECECE" className={`${hoverStyle}`} onClick={() => console.log('clicked')} />

        {/* inner down */}
        <path d={SectorPath(110, 110, inner_radius, middle_radius, 45, 90, offset)} fill="#CECECE" className={`${hoverStyle}`} onClick={() => console.log('clicked')} />

        {/* inner left */}
        <path d={SectorPath(110, 110, inner_radius, middle_radius, 135, 90, offset)} fill="#CECECE" className={`${hoverStyle}`} onClick={() => console.log('clicked')} />

        {/* inner up */}
        <path d={SectorPath(110, 110, inner_radius, middle_radius, 225, 90, offset)} fill="#CECECE" className={`${hoverStyle}`} onClick={() => console.log('clicked')} />
      </svg>

      {/* text */}
      <div className={`flex flex-col h-full transform translate-x-${textTrans}  translate-y-${textTrans} rotate-45 origin-center pointer-events-none`}>
        <div className='absolute left-1/2 top-0 h-9/44 flex items-center'>
          <p>+10</p>
        </div>

        <div className='absolute left-1/2 top-9/44 h-9/44 flex items-center'>
          <p>+1</p>
        </div>

        <div className='absolute left-1/2 bottom-9/44 h-9/44 flex items-center'>
          <p>-1</p>
        </div>

        <div className='absolute left-1/2 bottom-0 h-9/44 flex items-center'>
          <p>-10</p>
        </div>
      </div>
    </div>
  );
}




// ===== Dial =====
// function AxisDisc({ sizeRem = 13.75, onHome, onJog }: { sizeRem?: number; onHome?: () => void; onJog?: (axis: Axis, delta: number) => void }) {
//   const VB = 220
//   const C = VB / 2
//   const R = VB * 0.45
//   const fontAxis = 14.5
//   const textGrey = '#6B6B6B'
//   const textDark = '#323A3D'

//   const toRad = (deg: number) => (deg * Math.PI) / 180
//   const P = (deg: number, r: number) => ({ x: C + r * Math.cos(toRad(deg)), y: C + r * Math.sin(toRad(deg)) })

//   const ringSectorPath = (r0: number, r1: number, a0: number, a1: number) => {
//     let _a0 = a0
//     let _a1 = a1
//     if (_a1 <= _a0) _a1 += 360
//     const p0 = P(_a0, r0)
//     const p1 = P(_a0, r1)
//     const p2 = P(_a1, r1)
//     const p3 = P(_a1, r0)
//     const large = _a1 - _a0 > 180 ? 1 : 0
//     const sweep = 1
//     const sweepInner = 0
//     return [
//       `M ${p0.x} ${p0.y}`,
//       `L ${p1.x} ${p1.y}`,
//       `A ${r1} ${r1} 0 ${large} ${sweep} ${p2.x} ${p2.y}`,
//       `L ${p3.x} ${p3.y}`,
//       `A ${r0} ${r0} 0 ${large} ${sweepInner} ${p0.x} ${p0.y}`,
//       'Z',
//     ].join(' ')
//   }

//   const edges = [-135, -45, 45, 135, 225]

//   const R_INNER_IN = R * 0.28
//   const R_INNER_OUT = R * 0.58
//   const R_OUTER_IN = R_INNER_OUT
//   const R_OUTER_OUT = R

//   const handleClick = (mid: number) => {
//     if (!onJog) return
//     const step = 1
//     if ((mid > -90 && mid < 90) || mid === 0) onJog('x', +step)
//     if (mid <= -90 || mid >= 90) onJog('x', -step)
//     if (mid > 0 && mid < 180) onJog('y', +step)
//     if (mid < 0 || mid === 180) onJog('y', -step)
//   }

//   const midOuterNE = 35
//   const midInnerNE = 45
//   const midInnerSW = 225
//   const midOuterSW = 215

//   return (
//     <div className="relative" style={{ width: `${sizeRem}rem`, height: `${sizeRem}rem` }}>
//       <svg width="100%" height="100%" viewBox={`0 0 ${VB} ${VB}`} className="block">
//         <circle cx={C} cy={C} r={R} fill="#FFFFFF" />
//         {edges.slice(0, -1).map((a0, i) => {
//           const a1 = edges[i + 1]
//           const d = ringSectorPath(R_OUTER_IN, R_OUTER_OUT, a0, a1)
//           const mid = (a0 + a1) / 2
//           return (
//             <path key={`outer-${i}`} d={d} fill="#EEEEEE" className="cursor-pointer" onClick={() => handleClick(mid)} />
//           )
//         })}
//         {(() => {
//           const gapRem = 0.5
//           const gapStroke = (gapRem / sizeRem) * VB
//           const angles = [-135, -45, 45, 135]
//           return (
//             <g stroke="#FFFFFF" strokeWidth={gapStroke} strokeLinecap="butt">
//               {angles.map((a) => {
//                 const s = P(a, R_OUTER_IN)
//                 const e = P(a, R_OUTER_OUT)
//                 return <line key={`sep-${a}`} x1={s.x} y1={s.y} x2={e.x} y2={e.y} />
//               })}
//             </g>
//           )
//         })()}
//         {edges.slice(0, -1).map((a0, i) => {
//           const a1 = edges[i + 1]
//           const d = ringSectorPath(R_INNER_IN, R_INNER_OUT, a0, a1)
//           const mid = (a0 + a1) / 2
//           return (
//             <path key={`inner-${i}`} d={d} fill="#CECECE" className="cursor-pointer" onClick={() => handleClick(mid)} />
//           )
//         })}
//         {(() => {
//           const gapRem = 0.5
//           const gapStroke = (gapRem / sizeRem) * VB
//           const angles = [-135, -45, 45, 135]
//           return (
//             <g stroke="#FFFFFF" strokeWidth={gapStroke} strokeLinecap="butt">
//               {angles.map((a) => {
//                 const s = P(a, R_INNER_IN)
//                 const e = P(a, R_INNER_OUT)
//                 return <line key={`sep-inner-${a}`} x1={s.x} y1={s.y} x2={e.x} y2={e.y} />
//               })}
//             </g>
//           )
//         })()}
//         <text x={C} y={C - R + 20} textAnchor="middle" fontSize={fontAxis} fill={textDark}>Y</text>
//         <text x={C} y={C + R - 10} textAnchor="middle" fontSize={fontAxis} fill={textDark}>−Y</text>
//         <text x={C - R + 20} y={C + 5} textAnchor="start" fontSize={fontAxis} fill={textDark}>−X</text>
//         <text x={C + R - 20} y={C + 5} textAnchor="end" fontSize={fontAxis} fill={textDark}>X</text>
//         <text x={P(midOuterNE, (R_OUTER_IN + R_OUTER_OUT) / 2).x + 7} y={P(midOuterNE, (R_OUTER_IN + R_OUTER_OUT) / 2).y + 7} textAnchor="middle" fontSize={12.5} fill={textGrey} transform={`rotate(45 ${P(midOuterNE, (R_OUTER_IN + R_OUTER_OUT) / 2).x + 7} ${P(midOuterNE, (R_OUTER_IN + R_OUTER_OUT) / 2).y + 7})`}>+10</text>
//         <text x={P(midInnerNE, (R_INNER_IN + R_INNER_OUT) / 2).x + 7} y={P(midInnerNE, (R_INNER_IN + R_INNER_OUT) / 2).y + 7} textAnchor="middle" fontSize={12.5} fill={textGrey} transform={`rotate(45 ${P(midInnerNE, (R_INNER_IN + R_INNER_OUT) / 2).x + 7} ${P(midInnerNE, (R_INNER_IN + R_INNER_OUT) / 2).y + 7})`}>+1</text>
//         <text x={P(midInnerSW, (R_INNER_IN + R_INNER_OUT) / 2).x + 7} y={P(midInnerSW, (R_INNER_IN + R_INNER_OUT) / 2).y + 7} textAnchor="middle" fontSize={12.5} fill={textGrey} transform={`rotate(45 ${P(midInnerSW, (R_INNER_IN + R_INNER_OUT) / 2).x + 7} ${P(midInnerSW, (R_INNER_IN + R_INNER_OUT) / 2).y + 7})`}>−1</text>
//         <text x={P(midOuterSW, (R_OUTER_IN + R_OUTER_OUT) / 2).x + 7} y={P(midOuterSW, (R_OUTER_IN + R_OUTER_OUT) / 2).y + 7} textAnchor="middle" fontSize={12.5} fill={textGrey} transform={`rotate(45 ${P(midOuterSW, (R_OUTER_IN + R_OUTER_OUT) / 2).x + 7} ${P(midOuterSW, (R_OUTER_IN + R_OUTER_OUT) / 2).y + 7})`}>−10</text>
//         <g transform={`translate(${C}, ${C})`}>
//           <path d="M -7 2 L 0 -5 L 7 2 V 9 H 2.2 V 5.2 h-4.4 V 9 H -7 Z" fill="#00AE42" />
//         </g>
//       </svg>
//       <button
//         type="button"
//         aria-label="Home"
//         onClick={onHome}
//         className="absolute left-1/2 top-1/2 h-[3rem] w-[3rem] -translate-x-1/2 -translate-y-1/2 rounded-full"
//       />
//     </div>
//   )
// }

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
            {/* <CloseIcon className="h-4 w-4 text-black" /> */}
          </button>
        </Dialog.Close>
      </div>
    </div>
  )
}

export function ZControl() {
  return (
    <div className="flex flex-col w-[9.75rem] gap-[0.75rem]">
      <div className="text-center text-[0.875rem] leading-[1.375rem] text-[#6B6B6B]">Bed</div>

      <div className="flex flex-col">
        <div className='flex flex-row  h-[3.25rem] rounded-t-md  bg-gray-400 items-center justify-center'>
          <p>Up</p>
          <p>+10</p>
        </div>
        <div className=' flex flex-row   h-[3.25rem] bg-gray-200 items-center justify-center'>
          <p>Up</p>
          <p>+1</p>
        </div>
      </div>

      <div className='flex flex-col'>
        <div className='flex flex-row   h-[3.25rem] bg-gray-200 items-center justify-center'>
          <p>Down</p>
          <p>-1</p>
        </div>

        <div className='flex flex-row   h-[3.25rem] rounded-b-md bg-gray-400 items-center justify-center'>
          <p>Down</p>
          <p>-10</p>
        </div>
      </div>
    </div>
  );
}

function XYZDialog({ open, onClose, onHome, onJog, title = 'Motion:XYZ', portalContainer }: { open: boolean; title?: string; onClose: () => void; onHome?: () => void; onJog?: (axis: Axis, delta: number) => void; portalContainer?: HTMLElement | null }) {
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
      {/* 将 Portal 挂到靠近触发按钮的容器上，便于就近绝对定位 */}
      <Dialog.Portal container={portalContainer || undefined}>
        <Dialog.Content
          onEscapeKeyDown={(e) => e.preventDefault()}
          onInteractOutside={(e) => e.preventDefault()}
          onPointerDownOutside={(e) => e.preventDefault()}
          onFocusOutside={(e) => e.preventDefault()}
          onOpenAutoFocus={(e) => { e.preventDefault(); panelRef.current?.focus() }}
          className={[
            // 关键：相对于触发按钮容器（relative）定位
            'absolute left-0 top-0 z-[1000]',
            'w-[34.125rem] h-[20.8125rem]',
            'rounded-lg bg-white shadow-[0_0.375rem_1rem_rgba(0,0,0,0.12)] outline-none',
            'focus-visible:ring-2 focus-visible:ring-black/10'
          ].join(' ')}
          ref={panelRef as any}
          onKeyDown={onKeyDown}
        >

          <DialogHeader title={title} onClose={onClose} />

          <div className='flex flex-row'>
            <XYControl />
            <ZControl />
          </div>


          <div className="absolute left-0 top-[2.875rem] h-px w-full border-t border-[#EBEBEB]" />
          <div className="absolute left-[9.625rem] top-[3.8125rem] h-[1.375rem] w-[3.6875rem] text-center text-[0.875rem] leading-[1.375rem] text-[#6B6B6B]">Toolhead</div>
          <div className="absolute left-[4.625rem] top-[5.5rem]">
            {/* <AxisDisc sizeRem={13.75} onHome={onHome} onJog={onJog} /> */}
          </div>




        </Dialog.Content>
      </Dialog.Portal>
    </Dialog.Root>
  )
}

export default function Demo() {
  const [open, setOpen] = React.useState(false)
  // 作为定位锚点的容器（relative + inline-block），Portal 会挂到这里
  const anchorRef = React.useRef<HTMLDivElement | null>(null)

  return (
    <div className="p-6">
      <div ref={anchorRef} className="relative inline-block">
        <button
          type="button"
          onClick={() => setOpen(true)}
          className="inline-flex items-center gap-2 rounded-lg px-4 py-2 bg-black text-white hover:opacity-90 focus:outline-none focus:ring-2 focus:ring-black/20"
        >
          打开 Motion: XYZ
        </button>
        <XYZDialog
          open={open}
          onClose={() => setOpen(false)}
          onHome={() => console.log('home')}
          onJog={(axis, d) => console.log('jog', axis, d)}
          portalContainer={anchorRef.current}
        />
      </div>
    </div>
  )
}
