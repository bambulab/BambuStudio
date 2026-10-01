import * as React from "react"
import * as Dialog from "@radix-ui/react-dialog"

// ========= Icons =========
function CloseIcon({ className = 'h-4 w-4' }: { className?: string }) {
  return (
    <svg viewBox="0 0 24 24" className={className} aria-hidden>
      <path d="M6 6l12 12M18 6 6 18" stroke="currentColor" strokeWidth="2" strokeLinecap="round" />
    </svg>
  )
}
function BackIcon({ className = 'h-4 w-4' }: { className?: string }) {
  return (
    <svg viewBox="0 0 24 24" className={className} aria-hidden>
      <path d="M9 6l6 6-6 6" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  )
}
function CircleIcon({ className = 'h-3 w-3' }: { className?: string }) {
  return (
    <svg viewBox="0 0 24 24" className={className} aria-hidden>
      <circle cx="12" cy="12" r="9" stroke="currentColor" strokeWidth="2" fill="none" />
    </svg>
  )
}
function CheckDotIcon({ className = 'h-3 w-3' }: { className?: string }) {
  return (
    <svg viewBox="0 0 24 24" className={className} aria-hidden>
      <circle cx="12" cy="12" r="9" stroke="currentColor" strokeWidth="2" fill="none" />
      <circle cx="12" cy="12" r="5" fill="#8BC34A" />
    </svg>
  )
}
function PencilIcon({ className = 'h-3 w-3' }: { className?: string }) {
  return (
    <svg viewBox="0 0 24 24" className={className} aria-hidden>
      <path d="M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25z" fill="currentColor" />
      <path d="M20.71 7.04a1 1 0 0 0 0-1.41L18.37 3.29a1 1 0 0 0-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z" fill="currentColor" />
    </svg>
  )
}
function TriangleUp({ className = 'h-4 w-4' }: { className?: string }) {
  return (
    <svg viewBox="0 0 24 24" className={className} aria-hidden>
      <path d="M12 6l8 12H4z" fill="currentColor" />
    </svg>
  )
}
function TriangleDown({ className = 'h-4 w-4' }: { className?: string }) {
  return (
    <svg viewBox="0 0 24 24" className={className} aria-hidden>
      <path d="M12 18L4 6h16z" fill="currentColor" />
    </svg>
  )
}

// ========= Header =========
function DesignDialogHeader({ title, onClose }: { title: string; onClose: () => void }) {
  return (
    <div className="absolute left-0 top-0 flex h-[2.875rem] w-full items-center gap-2 rounded-t-lg bg-[#F8F8F8] px-3">
      <div className="flex h-[1.375rem] w-[1.375rem] items-center justify-center text-black/80">
        <BackIcon className="h-[1.125rem] w-[1.125rem]" />
      </div>
      <Dialog.Title asChild>
        <div className="relative pl-1">
          <span className="font-semibold text-[0.875rem] leading-[1.375rem] tracking-tight text-[#1F1F1F]">{title}</span>
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

// ========= Types =========
export interface DesignDialogProps {
  open: boolean
  onClose: () => void
  portalContainer?: HTMLElement | null
  title?: string
}

// ========= Dialog =========
export function DesignDialog({ open, onClose, portalContainer, title = 'Nozzle & Extruder' }: DesignDialogProps) {
  const panelRef = React.useRef<HTMLDivElement | null>(null)
  const [side, setSide] = React.useState<'left' | 'right'>('right')

  React.useEffect(() => {
    if (!open) return
    const t = setTimeout(() => panelRef.current?.focus(), 0)
    return () => clearTimeout(t)
  }, [open])

  const SegBtn = ({ which }: { which: 'left' | 'right' }) => {
    const active = side === which
    const base = 'flex-1 h-[2rem] rounded-[0.5rem] text-[0.875rem] flex items-center justify-center gap-2 transition-colors'
    const cls = active
      ? 'border border-[#2E2E2E] text-[#111] shadow-[0_0_0_1px_#2E2E2E_inset] bg-white'
      : 'border border-[#E4E4E4] text-[#444] bg-white shadow-[inset_0_0_0_1px_rgba(0,0,0,0.02)]'
    return (
      <button
        type="button"
        aria-pressed={active}
        onClick={() => setSide(which)}
        className={[base, cls].join(' ')}
      >
        <span className="capitalize">{which}</span>
        {active ? (
          <CheckDotIcon className="h-3.5 w-3.5" />
        ) : (
          <CircleIcon className="h-3.5 w-3.5 opacity-70" />
        )}
      </button>
    )
  }

  const handleReadNozzleInfo = () => {
    console.log('Read Nozzle Info clicked')
  }
  const handleStop = () => {
    console.log('Stop clicked')
  }
  const handleRetry = () => {
    console.log('Retry clicked')
  }

  const btnBase = "h-[1.875rem] rounded-[0.375rem] border text-[0.875rem] bg-white transition active:scale-95 active:bg-gray-100 hover:bg-gray-50 focus:outline-none focus:ring-2 focus:ring-offset-1 focus:ring-gray-300"

  return (
    <Dialog.Root open={open} onOpenChange={(n) => { if (!n) onClose() }} modal={false}>
      <Dialog.Portal container={portalContainer || undefined}>
        <Dialog.Content
          ref={panelRef as any}
          onEscapeKeyDown={(e) => e.preventDefault()}
          onInteractOutside={(e) => e.preventDefault()}
          onPointerDownOutside={(e) => e.preventDefault()}
          onFocusOutside={(e) => e.preventDefault()}
          onOpenAutoFocus={(e) => { e.preventDefault(); panelRef.current?.focus() }}
          className="absolute left-0 top-0 z-[1000] w-[34.125rem] h-[20.8125rem] rounded-lg bg-white shadow-[0_0.5rem_1.5rem_rgba(0,0,0,0.12)] outline-none"
        >
          <DesignDialogHeader title={title} onClose={onClose} />
          <div className="absolute left-0 top-[2rem] h-px w-full border-t border-[#EBEBEB]" />

          {/* Top segmented */}
          <div className="absolute left-[1.25rem] right-[1.25rem] top-[3.5rem] flex items-center justify-between gap-[1.5rem]">
            <SegBtn which="left" />
            <SegBtn which="right" />
          </div>

          {/* Middle content */}
          <div className="absolute left-[1.25rem] right-[1.25rem] top-[6rem] bottom-[4.25rem] grid grid-cols-3 gap-[1.25rem] text-[0.875rem] text-[#333]">
            {/* Left Nozzle */}
            <section className="self-center">
              <div className="text-[#6B7280] mb-1">Nozzle</div>
              <div className="flex items-baseline gap-1 mb-2">
                <span className="text-[2rem] leading-none font-extrabold tracking-tight">210</span>
                <span className="text-[1rem]">℃</span>
                <span className="text-[#6B7280]">/210℃</span>
              </div>
              <ul className="space-y-[0.125rem] text-[#4B5563]">
                <li>0.4 mm</li>
                <li>Hardened</li>
                <li className="inline-flex items-center gap-1">High Flow <PencilIcon className="h-3 w-3 opacity-80" /></li>
              </ul>
            </section>

            {/* Center Extruder */}
            <section className="relative flex flex-col items-center justify-center">
              <div className="relative w-[8.75rem] h-[7rem] opacity-95">
                <div className="absolute left-1/2 -translate-x-1/2 top-0 w-10 h-2 rounded bg-[#BDBDBD]" />
                <div className="absolute left-1/2 -translate-x-1/2 top-3 w-24 h-4 rounded bg-[#AFAFAF]" />
                <div className="absolute left-1/2 -translate-x-1/2 top-7 w-28 h-16 rounded bg-[#D3D3D3]" />
                <div className="absolute left-1/2 -translate-x-1/2 top-[3.9rem] w-3 h-9 rounded bg-[#555]" />
                <div className="absolute left-1/2 -translate-x-1/2 top-[2.6rem] w-6 h-6 rounded-full bg-[#1F1F1F]" />
                <div className="absolute left-[55%] top-[3.1rem] w-3 h-3 rounded-full" style={{ backgroundColor: '#9ED36A' }} />
              </div>
              {/* Extruder label + Up/Down controls shifted right to avoid overlap */}
              <div className="absolute right-[-2rem] top-1/2 -translate-y-1/2 flex flex-col items-center gap-2">
                <div className="text-[0.75rem] text-[#6B7280] mb-1">Extruder</div>
                <button className="h-8 w-8 rounded-[0.5rem] bg-[#EDEDED] text-[#333] grid place-items-center shadow active:scale-95">
                  <TriangleUp className="h-4 w-4" />
                </button>
                <button className="h-8 w-8 rounded-[0.5rem] bg-[#EDEDED] text-[#333] grid place-items-center shadow active:scale-95">
                  <TriangleDown className="h-4 w-4" />
                </button>
              </div>
            </section>

            {/* Right Nozzle */}
            <section className="self-center text-right">
              <div className="text-[#6B7280] mb-1">Nozzle</div>
              <div className="flex items-baseline justify-end gap-1 mb-2">
                <span className="text-[2rem] leading-none font-extrabold tracking-tight">45</span>
                <span className="text-[1rem]">℃</span>
                <span className="text-[#6B7280]">/45℃</span>
              </div>
              <ul className="space-y-[0.125rem] text-[#4B5563]">
                <li>0.4 mm</li>
                <li>Hardened</li>
                <li className="inline-flex items-center gap-1 justify-end">High Flow <PencilIcon className="h-3 w-3 opacity-80" /></li>
              </ul>
            </section>
          </div>

          {/* Bottom status */}
          <div className="absolute left-1/2 -translate-x-1/2 bottom-[3.75rem] text-[0.875rem] text-[#2B2B2B]">Switching Extruder...</div>

          {/* Bottom actions */}
          <div className="absolute left-[1.25rem] right-[1.25rem] bottom-[1.25rem] grid grid-cols-3 items-center">
            <div className="justify-self-start">
              <button onClick={handleReadNozzleInfo} className={`${btnBase} px-[0.75rem] border-[#D5D5D5] shadow-sm`}>Read Nozzle Info</button>
            </div>
            <div className="justify-self-center flex items-center gap-2">
              <button onClick={handleStop} className={`${btnBase} w-[4.25rem] border-[#C2C2C2]`}>Stop</button>
              <button onClick={handleRetry} className={`${btnBase} w-[4.25rem] border-[#C2C2C2]`}>Retry</button>
            </div>
            <div />
          </div>
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog.Root>
  )
}

// ========= Demo (default export for canvas preview) =========
export default function DesignDialogDemo() {
  const [open, setOpen] = React.useState(false)
  const anchorRef = React.useRef<HTMLDivElement | null>(null)
  return (
    <div className="p-6">
      <div ref={anchorRef} className="relative inline-block">
        <button type="button" onClick={() => setOpen(true)} className="inline-flex items-center gap-2 rounded-lg px-4 py-2 bg-black text-white hover:opacity-90 focus:outline-none focus:ring-2 focus:ring-black/20">
          打开设计稿弹窗
        </button>
        <DesignDialog open={open} onClose={() => setOpen(false)} portalContainer={anchorRef.current} />
      </div>
    </div>
  )
}
