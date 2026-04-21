import * as React from 'react'
import * as Dialog from '@radix-ui/react-dialog'

/* ===================== utils & atoms ===================== */
const cn = (...a: (string | false | null | undefined)[]) => a.filter(Boolean).join(' ')

function IconButton({
  ariaLabel,
  className,
  children,
  onClick,
}: React.PropsWithChildren<{ ariaLabel: string; className?: string; onClick?: () => void }>) {
  return (
    <button
      type="button"
      aria-label={ariaLabel}
      onClick={onClick}
      className={cn(
        'inline-flex items-center justify-center rounded-full focus:outline-none',
        'focus:ring-2 focus:ring-black/10 hover:bg-black/[0.05] active:bg-black/[0.10]',
        className,
      )}
    >
      {children}
    </button>
  )
}

function FooterButton(props: React.ButtonHTMLAttributes<HTMLButtonElement>) {
  const { className, ...rest } = props
  return (
    <button
      {...rest}
      className={cn(
        'h-[2rem] rounded-[0.375rem] border border-[#C2C2C2]',
        'px-[0.75rem] text-[0.875rem] leading-[1.375rem] text-[#1F1F1F]',
        'hover:bg-black/[0.02] focus:outline-none focus:ring-2 focus:ring-gray-200',
        className,
      )}
    />
  )
}

function SegmentedRadio({
  label,
  active,
  onClick,
}: { label: string; active: boolean; onClick: () => void }) {
  return (
    <button
      onClick={onClick}
      className={cn(
        'flex flex-1 items-center justify-center gap-[0.25rem] rounded-[0.5rem]',
        'h-[2rem] px-[1rem] border transition-colors',
        active ? 'border-[#1F1F1F]' : 'border-[#DBDBDB]',
      )}
    >
      <span className="text-[#1F1F1F] text-[0.875rem] leading-[1.375rem]">{label}</span>
      {active ? (
        <span className="relative inline-block h-[1rem] w-[1rem]">
          <span className="absolute inset-0 rounded-full bg-[#B6F34F]" />
          <svg className="absolute left-[0.25rem] top-[0.3125rem]" width="0.5rem" height="0.4375rem" viewBox="0 0 8 7" fill="none">
            <path d="M2.83 6.37L0 3.54L1.41 2.13L2.82 3.54L6.36 0L7.77 1.41L2.83 6.37Z" fill="black"/>
          </svg>
        </span>
      ) : (
        <svg width="1rem" height="1rem" viewBox="0 0 17 16" fill="none">
          <circle cx="8.5" cy="8" r="7.5" stroke="#C2C2C2"/>
        </svg>
      )}
    </button>
  )
}

/* ===================== icons ===================== */
function CloseIcon({ className = 'h-[1rem] w-[1rem]' }: { className?: string }) {
  return (
    <svg viewBox="0 0 24 24" className={className} aria-hidden>
      <path d="M6 6l12 12M18 6 6 18" stroke="currentColor" strokeWidth="2" strokeLinecap="round" />
    </svg>
  )
}
function ChevronUp() {
  return (
    <svg width="1.5rem" height="0.9375rem" viewBox="0 0 26 16" fill="none">
      <path d="M11.3659 2.31579C12.1627 1.18658 13.8373 1.18658 14.6341 2.31579L20.3013 10.3469C21.2362 11.6717 20.2887 13.5 18.6672 13.5H7.33276C5.71129 13.5 4.76376 11.6717 5.69871 10.3469L11.3659 2.31579Z" fill="#5C5C5C"/>
    </svg>
  )
}
function ChevronDown() {
  return (
    <svg width="1.5rem" height="0.9375rem" viewBox="0 0 26 16" fill="none">
      <path d="M14.6341 13.6842C13.8373 14.8134 12.1627 14.8134 11.3659 13.6842L5.69871 5.65309C4.76376 4.32833 5.71129 2.5 7.33276 2.5L18.6672 2.5C20.2887 2.5 21.2362 4.32833 20.3013 5.65309L14.6341 13.6842Z" fill="#5C5C5C"/>
    </svg>
  )
}

/* ===================== header ===================== */
function DialogHeader({ title, onClose }: { title: string; onClose: () => void }) {
  return (
    <div className="flex h-[2.875rem] items-center gap-[0.5rem] rounded-t-[0.5rem] bg-[#F8F8F8] px-[0.75rem]">
      <div className="flex h-[1.375rem] w-[1.375rem] items-center justify-center text-black/80">
        <svg viewBox="0 0 22 22" className="h-[1.375rem] w-[1.375rem]">
          <path d="M15.2462 10.4205C15.5654 10.7396 15.5654 11.2557 15.2462 11.5715L8.72713 18.094C8.40796 18.4131 7.89187 18.4131 7.5761 18.094C7.26033 17.7748 7.25694 17.2587 7.5761 16.9429L13.518 11.0011L7.57271 5.05579C7.25354 4.73663 7.25354 4.22054 7.57271 3.90477C7.89187 3.589 8.40796 3.5856 8.72373 3.90477L15.2462 10.4205Z" fill="black"/>
        </svg>
      </div>
      <Dialog.Title asChild>
        <div className="pl-[0.25rem]">
          <span className="font-bold text-[0.875rem] leading-[1.375rem] tracking-tight text-[#1F1F1F]">
            {title}
          </span>
        </div>
      </Dialog.Title>
      <div className="ml-auto">
        <Dialog.Close asChild>
          <IconButton ariaLabel="Close" onClick={onClose} className="h-[1.5rem] w-[1.5rem]">
            <CloseIcon />
          </IconButton>
        </Dialog.Close>
      </div>
    </div>
  )
}

/* ===================== parts ===================== */
function NozzleSpec({ align = 'left' as 'left' | 'right' }) {
  const right = align === 'right'
  return (
    <div className={cn('flex flex-col', right && 'items-end text-right')}>
      <div className="text-[#6B6B6B] text-[0.75rem] leading-[0.875rem]">Nozzle</div>
      <div className="mt-[0.25rem] flex items-end">
        <span className="text-[#1F1F1F] text-[1.125rem] leading-[1.5rem] font-bold">{right ? '45' : '210'}</span>
        <span className="text-[#1F1F1F] text-[0.875rem] leading-[1.375rem]">℃</span>
        <span className="ml-[0.25rem] text-[#262E30] text-[0.875rem] leading-[1.375rem]">/{right ? '45' : '210'}℃</span>
      </div>
      <div className="mt-[0.5rem] text-[#262E30] text-[0.75rem] leading-[1rem]">
        0.4 mm<br/>Hardened<br/>High Flow
      </div>
    </div>
  )
}

function HeadBlock() {
  // 中部双色块（整体宽度固定，便于与箭头中心对齐）
  return (
    <div className="relative h-[6.5rem] w-[4.5rem]">
      <div className="absolute left-0 top-0 h-[6.5rem] w-[2.25rem] rounded bg-black/10" />
      <div className="absolute left-[2.25rem] top-0 h-[6.5rem] w-[2.25rem] rounded bg-black/40">
        <span className="absolute left-1/2 top-1/2 block h-[0.75rem] w-[0.75rem] -translate-x-1/2 -translate-y-1/2 rounded-full bg-[#B6F34F]" />
      </div>
    </div>
  )
}

/* ===================== dialog ===================== */
function NozzleExtruderDialog({
  open,
  onClose,
  title = 'Nozzle & Extruder',
  portalContainer,
}: {
  open: boolean
  title?: string
  onClose: () => void
  portalContainer?: HTMLElement | null
}) {
  const panelRef = React.useRef<HTMLDivElement | null>(null)
  const [selectedSide, setSelectedSide] = React.useState<'left' | 'right'>('right')

  React.useEffect(() => {
    if (!open) return
    const t = setTimeout(() => panelRef.current?.focus(), 0)
    return () => clearTimeout(t)
  }, [open])

  return (
    <Dialog.Root open={open} onOpenChange={(n) => { if (!n) onClose() }} modal={false}>
      <Dialog.Portal container={portalContainer || undefined}>
        <Dialog.Content
          onEscapeKeyDown={(e) => e.preventDefault()}
          onInteractOutside={(e) => e.preventDefault()}
          onPointerDownOutside={(e) => e.preventDefault()}
          onFocusOutside={(e) => e.preventDefault()}
          onOpenAutoFocus={(e) => { e.preventDefault(); panelRef.current?.focus() }}
          ref={panelRef as any}
          className={cn(
            'absolute left-0 top-0 z-[1000]',
            'w-[34.125rem] min-h-[20.8125rem]', // 546 x 333（高度自适应）
            'rounded-[0.5rem] bg-white border border-[#EBEBEB]',
            'shadow-[0_0.5rem_1.5rem_rgba(0,0,0,0.12)] outline-none',
            'flex flex-col overflow-hidden',
          )}
        >
          <DialogHeader title={title} onClose={onClose} />
          <div className="h-[0.0625rem] w-full bg-[#EBEBEB]" />

          {/* segmented */}
          <div className="flex gap-[1rem] px-[1rem] pt-[0.5rem]">
            <SegmentedRadio label="Left"  active={selectedSide === 'left'}  onClick={() => setSelectedSide('left')} />
            <SegmentedRadio label="Right" active={selectedSide === 'right'} onClick={() => setSelectedSide('right')} />
          </div>

          {/* main: 3 columns */}
          <div className="grid grid-cols-[1fr_auto_1fr] gap-[1.25rem] px-[1rem] pt-[0.75rem]">
            <NozzleSpec align="left" />

            {/* 中间列：整列 items-center，确保上下箭头与双色块严格居中 */}
            <div className="flex flex-col items-center">
              <div className="mb-[0.25rem] text-[0.75rem] leading-[0.875rem] text-[#6B6B6B]">Extruder</div>

              <button
                type="button"
                className="grid h-[2.25rem] w-[2.25rem] place-items-center rounded-[0.625rem] bg-[#DBDBDB] active:translate-y-[0.0625rem]"
                aria-label="Extruder up"
              >
                <ChevronUp />
              </button>

              <div className="mt-[0.375rem]">
                <HeadBlock />
              </div>

              <button
                type="button"
                className="mt-[0.375rem] grid h-[2.25rem] w-[2.25rem] place-items-center rounded-[0.625rem] bg-[#DBDBDB] active:translate-y-[0.0625rem]"
                aria-label="Extruder down"
              >
                <ChevronDown />
              </button>
            </div>

            <NozzleSpec align="right" />
          </div>

          {/* status */}
          <div className="px-[1rem] pt-[0.5rem]">
            <p className="text-center text-[0.875rem] leading-[1.375rem] text-[#1F1F1F]">
              Switching Extruder…
            </p>
          </div>

          {/* footer：3列 Grid -> 中间按钮永远居中 */}
          <div className="mt-auto grid grid-cols-[1fr_auto_1fr] items-center px-[1rem] pb-[1rem] pt-[0.5rem]">
            <div className="justify-self-start">
              <FooterButton onClick={() => console.log('Read Nozzle Info')}>Read Nozzle Info</FooterButton>
            </div>
            <div className="flex items-center gap-[0.75rem] justify-self-center">
              <FooterButton onClick={() => console.log('Stop')}>Stop</FooterButton>
              <FooterButton onClick={() => console.log('Retry')}>Retry</FooterButton>
            </div>
            <div className="justify-self-end" />
          </div>
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog.Root>
  )
}

/* ===================== demo ===================== */
function AMSControl() {
  const [open, setOpen] = React.useState(false)
  const anchorRef = React.useRef<HTMLDivElement | null>(null)

  return (
    <div className="p-[1.5rem]">
      <div ref={anchorRef} className="relative inline-block">
        <button
          type="button"
          onClick={() => setOpen(true)}
          className="inline-flex items-center gap-[0.5rem] rounded-[0.5rem] bg-black px-[1rem] py-[0.5rem] text-white hover:opacity-90 focus:outline-none focus:ring-2 focus:ring-black/20"
        >
          打开 Nozzle & Extruder
        </button>
        {/* Dialog 左上角与按钮容器左上角对齐 */}
        <NozzleExtruderDialog open={open} onClose={() => setOpen(false)} portalContainer={anchorRef.current} />
      </div>
    </div>
  )
}

export default AMSControl


