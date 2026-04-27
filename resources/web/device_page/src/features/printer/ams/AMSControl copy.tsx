import * as React from 'react'
import * as Dialog from '@radix-ui/react-dialog'

function DialogHeader({ title, onClose }: { title: string; onClose: () => void }) {
  return (
    <div className="absolute left-0 top-0 flex h-[46px] w-full items-center gap-2 rounded-t-lg bg-[#F8F8F8] px-3">
      <div className="flex h-[22px] w-[22px] items-center justify-center text-black/80">
        <svg viewBox="0 0 22 22" className="h-[22px] w-[22px]">
          <path d="M15.2462 10.4205C15.5654 10.7396 15.5654 11.2557 15.2462 11.5715L8.72713 18.094C8.40796 18.4131 7.89187 18.4131 7.5761 18.094C7.26033 17.7748 7.25694 17.2587 7.5761 16.9429L13.518 11.0011L7.57271 5.05579C7.25354 4.73663 7.25354 4.22054 7.57271 3.90477C7.89187 3.589 8.40796 3.5856 8.72373 3.90477L15.2462 10.4205Z" fill="black"/>
        </svg>
      </div>
      <Dialog.Title asChild>
        <div className="relative pl-1">
          <span className="font-bold text-[14px] leading-[22px] tracking-tight text-[#1F1F1F]">{title}</span>
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
            <svg width="22" height="22" viewBox="0 0 22 22" fill="none" xmlns="http://www.w3.org/2000/svg">
              <path d="M11 3.5524C12.9753 3.5524 14.8697 4.33715 16.2665 5.73403C17.6632 7.13091 18.4479 9.02549 18.4479 11.001C18.4479 12.9765 17.6632 14.871 16.2665 16.2679C14.8697 17.6648 12.9753 18.4495 11 18.4495C9.02467 18.4495 7.13026 17.6648 5.73351 16.2679C4.33675 14.871 3.55206 12.9765 3.55206 11.001C3.55206 9.02549 4.33675 7.13091 5.73351 5.73403C7.13026 4.33715 9.02467 3.5524 11 3.5524ZM11 20.1684C13.4311 20.1684 15.7627 19.2026 17.4818 17.4834C19.2009 15.7641 20.1666 13.4323 20.1666 11.001C20.1666 8.5696 19.2009 6.23782 17.4818 4.51859C15.7627 2.79935 13.4311 1.8335 11 1.8335C8.56883 1.8335 6.23725 2.79935 4.51817 4.51859C2.79908 6.23782 1.83331 8.5696 1.83331 11.001C1.83331 13.4323 2.79908 15.7641 4.51817 17.4834C6.23725 19.2026 8.56883 20.1684 11 20.1684ZM8.09959 8.10032C7.763 8.43694 7.763 8.98126 8.09959 9.3143L9.78253 10.9974L8.09959 12.6805C7.763 13.0171 7.763 13.5614 8.09959 13.8945C8.43618 14.2275 8.98045 14.2311 9.31346 13.8945L10.9964 12.2114L12.6793 13.8945C13.0159 14.2311 13.5602 14.2311 13.8932 13.8945C14.2262 13.5578 14.2298 13.0135 13.8932 12.6805L12.2103 10.9974L13.8932 9.3143C14.2298 8.97768 14.2298 8.43336 13.8932 8.10032C13.5566 7.76729 13.0123 7.7637 12.6793 8.10032L10.9964 9.78341L9.31346 8.10032C8.97687 7.7637 8.4326 7.7637 8.09959 8.10032Z" fill="black"/>
            </svg>
          </button>
        </Dialog.Close>
      </div>
    </div>
  )
}

function NozzleExtruderDialog({ open, onClose, title = 'Nozzle & Extruder', portalContainer }: { open: boolean; title?: string; onClose: () => void; portalContainer?: HTMLElement | null }) {
  const panelRef = React.useRef<HTMLDivElement | null>(null)
  const [selectedSide, setSelectedSide] = React.useState<'left' | 'right'>('right')

  React.useEffect(() => {
    if (!open) return
    const t = setTimeout(() => panelRef.current?.focus(), 0)
    return () => clearTimeout(t)
  }, [open])

  return (
    <Dialog.Root open={open} onOpenChange={(next) => { if (!next) onClose() }} modal={false}>
      <Dialog.Portal container={portalContainer || undefined}>
        <Dialog.Content
          onEscapeKeyDown={(e) => e.preventDefault()}
          onInteractOutside={(e) => e.preventDefault()}
          onPointerDownOutside={(e) => e.preventDefault()}
          onFocusOutside={(e) => e.preventDefault()}
          onOpenAutoFocus={(e) => { e.preventDefault(); panelRef.current?.focus() }}
          className={[
            'absolute left-0 top-0 z-[1000]',
            'w-[546px] h-[333px]',
            'rounded-lg bg-white shadow-lg outline-none',
            'focus-visible:ring-2 focus-visible:ring-black/10',
            'relative flex flex-col',
          ].join(' ')}
          ref={panelRef as any}
        >
          <DialogHeader title={title} onClose={onClose} />
          <div className="w-full h-px bg-[#EBEBEB]"></div>
          
          {/* Tab Selection */}
          <div className="flex gap-8 px-4 pt-4 mt-[46px]">
            <button
              onClick={() => setSelectedSide('left')}
              className={`flex h-8 px-4 items-center justify-center gap-1 flex-1 rounded-lg border transition-colors ${
                selectedSide === 'left' ? 'border-[#1F1F1F]' : 'border-[#DBDBDB]'
              }`}
            >
              <span className="text-[#1F1F1F] text-[14px] leading-[22px]">Left</span>
              {selectedSide === 'left' ? (
                <div className="relative w-4 h-4">
                  <div className="w-4 h-4 rounded-full bg-[#B6F34F]"></div>
                  <svg className="absolute left-1 top-[5px]" width="8" height="7" viewBox="0 0 8 7" fill="none" xmlns="http://www.w3.org/2000/svg">
                    <path d="M2.83 6.37L0 3.54L1.41 2.13L2.82 3.54L6.36 0L7.77 1.41L2.83 6.37Z" fill="black"/>
                  </svg>
                </div>
              ) : (
                <svg width="16" height="16" viewBox="0 0 17 16" fill="none" xmlns="http://www.w3.org/2000/svg">
                  <circle cx="8.5" cy="8" r="7.5" stroke="#C2C2C2"/>
                </svg>
              )}
            </button>
            <button
              onClick={() => setSelectedSide('right')}
              className={`flex h-8 px-4 items-center justify-center gap-1 flex-1 rounded-lg border transition-colors ${
                selectedSide === 'right' ? 'border-[#1F1F1F]' : 'border-[#DBDBDB]'
              }`}
            >
              <span className="text-[#1F1F1F] text-[14px] leading-[22px]">Right</span>
              {selectedSide === 'right' ? (
                <div className="relative w-4 h-4">
                  <div className="w-4 h-4 rounded-full bg-[#B6F34F]"></div>
                  <svg className="absolute left-1 top-[5px]" width="8" height="7" viewBox="0 0 8 7" fill="none" xmlns="http://www.w3.org/2000/svg">
                    <path d="M2.83 6.37L0 3.54L1.41 2.13L2.82 3.54L6.36 0L7.77 1.41L2.83 6.37Z" fill="black"/>
                  </svg>
                </div>
              ) : (
                <svg width="16" height="16" viewBox="0 0 17 16" fill="none" xmlns="http://www.w3.org/2000/svg">
                  <circle cx="8.5" cy="8" r="7.5" stroke="#C2C2C2"/>
                </svg>
              )}
            </button>
          </div>

          {/* Main Content */}
          <div className="relative flex flex-col px-4 pt-8">
            {/* Left Nozzle Info */}
            <div className="absolute left-4 top-8 flex flex-col gap-1 w-[70px]">
              <div className="text-[#6B6B6B] text-[12px] leading-[14px]">Nozzle</div>
              <div className="flex items-end">
                <span className="text-[#1F1F1F] text-[18px] font-bold leading-6">210</span>
                <span className="text-[#1F1F1F] text-[14px] leading-[22px]">℃</span>
                <span className="text-[#262E30] text-[14px] leading-[22px]">/210℃</span>
              </div>
            </div>

            {/* Right Nozzle Info */}
            <div className="absolute right-4 top-8 flex flex-col gap-1 w-[70px]">
              <div className="text-[#6B6B6B] text-[12px] leading-[14px]">Nozzle</div>
              <div className="flex items-end">
                <span className="text-[#1F1F1F] text-[18px] font-bold leading-6">45</span>
                <span className="text-[#1F1F1F] text-[14px] leading-[22px]">℃</span>
                <span className="text-[#262E30] text-[14px] leading-[22px]">/45℃</span>
              </div>
            </div>

            {/* Extruder Label */}
            <div className="absolute left-[438px] top-[453px] text-[#6B6B6B] text-[12px] leading-[14px]">Extruder</div>

            {/* Left Nozzle Details */}
            <div className="absolute left-4 top-[80px] w-[74px]">
              <div className="text-[#262E30] text-[12px] leading-4">
                0.4 mm<br/>
                Hardened<br/>
                High Flow
              </div>
              <div className="absolute right-0 bottom-0 w-4 h-4 flex items-center justify-center">
                <svg width="12" height="12" viewBox="0 0 12 12" fill="none" xmlns="http://www.w3.org/2000/svg">
                  <path d="M0.279164 10.3505L0.758331 8.6277C0.845831 8.31667 1.00416 8.03211 1.22083 7.8027L7.55625 1.09241C8.07708 0.540935 8.92083 0.540935 9.44167 1.09241L10.2625 1.96152C10.3271 2.02991 10.3854 2.10491 10.4333 2.18211C10.775 2.72917 10.7187 3.47476 10.2625 3.95785L3.92708 10.6681C3.9 10.6968 3.87291 10.7255 3.84375 10.752C3.64375 10.9395 3.40625 11.0784 3.14791 11.1578L1.52083 11.6652L0.641665 11.9387C0.466665 11.9939 0.277081 11.9431 0.147915 11.8042C0.0187478 11.6652 -0.0312522 11.4689 0.0208312 11.2814L0.279164 10.3505ZM1.575 9.44167L1.2375 10.6505L2.37916 10.2953L2.86666 10.1431C3 10.1012 3.12291 10.024 3.22083 9.92035L7.97917 4.87991L6.6875 3.51226L1.92708 8.55049C1.91458 8.56373 1.90208 8.57696 1.89166 8.5902C1.81041 8.68726 1.75208 8.80196 1.71666 8.92549L1.57291 9.44167H1.575ZM5.16666 10.902H11.5C11.7771 10.902 12 11.138 12 11.4314C12 11.7248 11.7771 11.9608 11.5 11.9608H5.16666C4.88958 11.9608 4.66666 11.7248 4.66666 11.4314C4.66666 11.138 4.88958 10.902 5.16666 10.902Z" fill="#5C5C5C"/>
                </svg>
              </div>
            </div>

            {/* Right Nozzle Details */}
            <div className="absolute right-4 top-[80px] w-[74px]">
              <div className="text-[#262E30] text-[12px] leading-4">
                0.4 mm<br/>
                Hardened<br/>
                High Flow
              </div>
              <div className="absolute right-0 bottom-0 w-4 h-4 flex items-center justify-center">
                <svg width="12" height="12" viewBox="0 0 12 12" fill="none" xmlns="http://www.w3.org/2000/svg">
                  <path d="M0.279164 10.3505L0.758331 8.6277C0.845831 8.31667 1.00416 8.03211 1.22083 7.8027L7.55625 1.09241C8.07708 0.540935 8.92083 0.540935 9.44167 1.09241L10.2625 1.96152C10.3271 2.02991 10.3854 2.10491 10.4333 2.18211C10.775 2.72917 10.7187 3.47476 10.2625 3.95785L3.92708 10.6681C3.9 10.6968 3.87291 10.7255 3.84375 10.752C3.64375 10.9395 3.40625 11.0784 3.14791 11.1578L1.52083 11.6652L0.641665 11.9387C0.466665 11.9939 0.277081 11.9431 0.147915 11.8042C0.0187478 11.6652 -0.0312522 11.4689 0.0208312 11.2814L0.279164 10.3505ZM1.575 9.44167L1.2375 10.6505L2.37916 10.2953L2.86666 10.1431C3 10.1012 3.12291 10.024 3.22083 9.92035L7.97917 4.87991L6.6875 3.51226L1.92708 8.55049C1.91458 8.56373 1.90208 8.57696 1.89166 8.5902C1.81041 8.68726 1.75208 8.80196 1.71666 8.92549L1.57291 9.44167H1.575ZM5.16666 10.902H11.5C11.7771 10.902 12 11.138 12 11.4314C12 11.7248 11.7771 11.9608 11.5 11.9608H5.16666C4.88958 11.9608 4.66666 11.7248 4.66666 11.4314C4.66666 11.138 4.88958 10.902 5.16666 10.902Z" fill="#5C5C5C"/>
                </svg>
              </div>
            </div>

            {/* Extruder Controls */}
            <div className="absolute left-[334px] top-0 flex flex-col gap-3 mb-auto w-[65px]">
              <div className="relative mx-auto">
                Extruder
              </div>
              <div className="w-10 h-10 bg-[#DBDBDB] rounded-[10px] flex items-center justify-center mx-auto">
                <svg width="26" height="16" viewBox="0 0 26 16" fill="none" xmlns="http://www.w3.org/2000/svg">
                  <path d="M11.3659 2.31579C12.1627 1.18658 13.8373 1.18658 14.6341 2.31579L20.3013 10.3469C21.2362 11.6717 20.2887 13.5 18.6672 13.5H7.33276C5.71129 13.5 4.76376 11.6717 5.69871 10.3469L11.3659 2.31579Z" fill="#5C5C5C"/>
                </svg>
              </div>
              <div className="w-10 h-10 bg-[#DBDBDB] rounded-[10px] flex items-center justify-center mx-auto">
                <svg width="26" height="16" viewBox="0 0 26 16" fill="none" xmlns="http://www.w3.org/2000/svg">
                  <path d="M14.6341 13.6842C13.8373 14.8134 12.1627 14.8134 11.3659 13.6842L5.69871 5.65309C4.76376 4.32833 5.71129 2.5 7.33276 2.5L18.6672 2.5C20.2887 2.5 21.2362 4.32833 20.3013 5.65309L14.6341 13.6842Z" fill="#5C5C5C"/>
                </svg>
              </div>
            </div>

            {/* Central Extruder Image */}
            <div className="absolute left-[228px] top-0 w-[90px] h-[124px]">
              <img 
                src="https://api.builder.io/api/v1/image/assets/TEMP/9e95541fc5b1bbf7887a52714f19e33d49e350e5?width=90" 
                alt="Component 17" 
                className="w-[45px] h-[124px] absolute left-0 top-0 opacity-40"
              />
              <img 
                src="https://api.builder.io/api/v1/image/assets/TEMP/f5c19004addc20295a085117fe3b5b1ce33bed63?width=90" 
                alt="Component 16" 
                className="w-[45px] h-[124px] absolute left-[45px] top-0"
              />
            </div>

            {/* Status Text */}
            <div className="absolute left-[208px] top-[132px] text-[#1F1F1F] text-[14px] leading-[22px]">
              Switching Extruder...
            </div>
          </div>

          {/* Bottom Buttons */}
          <div className="flex justify-center items-start w-full mt-auto px-4 pt-[15px] pb-4">
            <button
              type="button"
              onClick={() => console.log('Read Nozzle Info clicked')}
              className="h-8 px-3 rounded-md border border-[#C2C2C2] text-[#1F1F1F] text-[14px] leading-[22px] mr-auto transition-all hover:bg-gray-50 active:bg-gray-100 focus:outline-none focus:ring-2 focus:ring-gray-200"
            >
              Read Nozzle Info
            </button>
            <button
              type="button"
              onClick={() => console.log('Stop clicked')}
              className="h-8 px-3 rounded-md border border-[#C2C2C2] text-[#1F1F1F] text-[14px] leading-[22px] mx-auto transition-all hover:bg-gray-50 active:bg-gray-100 focus:outline-none focus:ring-2 focus:ring-gray-200"
            >
              Stop
            </button>
            <button
              type="button"
              onClick={() => console.log('Retry clicked')}
              className="h-8 px-3 rounded-md border border-[#C2C2C2] text-[#1F1F1F] text-[14px] leading-[22px] mr-auto transition-all hover:bg-gray-50 active:bg-gray-100 focus:outline-none focus:ring-2 focus:ring-gray-200"
            >
              Retry
            </button>
            <div className="mx-auto"></div>
          </div>
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog.Root>
  )
}

function AMSControl() {
  const [open, setOpen] = React.useState(false)
  const anchorRef = React.useRef<HTMLDivElement | null>(null)

  return (
    <div className="p-6">
      <div ref={anchorRef} className="relative inline-block">
        <button
          type="button"
          onClick={() => setOpen(true)}
          className="inline-flex items-center gap-2 rounded-lg px-4 py-2 bg-black text-white hover:opacity-90 focus:outline-none focus:ring-2 focus:ring-black/20"
        >
          打开 Nozzle & Extruder
        </button>
        <NozzleExtruderDialog
          open={open}
          onClose={() => setOpen(false)}
          portalContainer={anchorRef.current}
        />
      </div>
    </div>
  )
}

export default AMSControl
