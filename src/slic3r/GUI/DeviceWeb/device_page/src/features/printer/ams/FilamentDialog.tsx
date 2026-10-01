import React from 'react';
import * as Dialog from '@radix-ui/react-dialog';
import { CheckIcon, ChevronRightIcon, Cross2Icon } from '@radix-ui/react-icons';

// 类型定义
interface StepItemProps {
  state: 'done' | 'active' | 'pending';
  text: string;
  stepNumber?: number;
}

interface LoadingFilamentDialogProps {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  onRetry?: () => void;
  onStop?: () => void;
  onClose?: () => void;
}

// Badge组件
const Badge: React.FC<{ variant: 'done' | 'active' | 'pending'; children: React.ReactNode }> = ({
  variant,
  children
}) => {
  const baseClasses = "w-[22px] h-[22px] rounded-full flex items-center justify-center text-sm font-semibold";

  const variantClasses = {
    done: "bg-[#D9EFE1] text-[#1F1F1F]",
    active: "bg-[#B6F34F] text-[#1F1F1F]",
    pending: "bg-[#DBDBDB] text-[#1F1F1F]"
  };

  return (
    <div className={`${baseClasses} ${variantClasses[variant]}`}>
      {children}
    </div>
  );
};

// StepItem组件
const StepItem: React.FC<StepItemProps> = ({ state, text, stepNumber }) => {
  const isActive = state === 'active';
  const textClasses = isActive
    ? "text-[#1F1F1F] font-semibold"
    : "text-[#1F1F1F] font-normal";

  return (
    <div className="flex items-center gap-4 h-12">
      <Badge variant={state}>
        {state === 'done' ? <CheckIcon className="w-3 h-3" /> : stepNumber}
      </Badge>
      <span className={`text-base leading-6 ${textClasses}`}>
        {text}
      </span>
    </div>
  );
};

// 主对话框组件
export const LoadingFilamentDialog: React.FC<LoadingFilamentDialogProps> = ({
  open,
  onOpenChange,
  onRetry,
  onStop,
  onClose
}) => {
  const handleRetry = () => {
    onRetry?.();
  };

  const handleStop = () => {
    onStop?.();
  };

  const handleClose = () => {
    onClose?.();
    onOpenChange(false);
  };

  return (
    <Dialog.Root open={open} onOpenChange={onOpenChange}>
      <Dialog.Portal>
        <Dialog.Overlay className="fixed inset-0 bg-black/50 backdrop-blur-sm z-50" />
        <Dialog.Content className="fixed top-4 left-1/2 transform -translate-x-1/2 z-50 w-[1140px] max-w-[95vw]">
          <div className="bg-white rounded-2xl shadow-[0_10px_24px_rgba(0,0,0,0.24),0_2px_6px_rgba(0,0,0,0.12)]">
            {/* Header */}
            <div className="bg-[#F8F8F8] rounded-t-2xl px-6 py-3 flex items-center gap-4">
              <ChevronRightIcon className="w-4 h-4 text-[#1F1F1F]" />
              <Dialog.Title className="text-lg font-semibold leading-6 text-[#1F1F1F] flex-1">
                Loading Filament
              </Dialog.Title>
              <Dialog.Close asChild>
                <button
                  onClick={handleClose}
                  className="w-6 h-6 flex items-center justify-center text-[#1F1F1F] hover:bg-gray-200 rounded transition-colors"
                  aria-label="Close"
                >
                  <Cross2Icon className="w-4 h-4" />
                </button>
              </Dialog.Close>
            </div>

            {/* Divider */}
            <div className="h-px bg-[#EBEBEB]" />

            {/* Content */}
            <div className="px-6 py-6 flex gap-8 items-start">
              {/* Steps */}
              <div className="flex-1 space-y-4">
                <StepItem state="done" text="Heat the nozzle" />
                <StepItem state="active" text="Cut filament" stepNumber={2} />
                <StepItem state="pending" text="Pull back current filament" stepNumber={3} />
                <StepItem state="pending" text="Push new filament into the extruder" stepNumber={4} />
              </div>

              {/* Media placeholder */}
              <div className="w-[260px] flex-shrink-0">
                <div className="w-full aspect-square bg-[#F8F8F8] rounded-lg border border-[#EBEBEB] flex items-center justify-center">
                  <div className="text-[#1F1F1F] text-sm opacity-60">
                    Device Extruder
                  </div>
                </div>
              </div>
            </div>

            {/* Divider */}
            <div className="h-px bg-[#EBEBEB]" />

            {/* Footer */}
            <div className="px-6 py-4 flex gap-4 border-t border-[#EBEBEB] rounded-b-2xl">
              <button
                onClick={handleRetry}
                className="h-11 px-[18px] bg-white text-[#1F1F1F] border border-[#EBEBEB] rounded-lg font-semibold text-base leading-5 hover:bg-gray-50 transition-colors"
              >
                Retry
              </button>
              <button
                onClick={handleStop}
                className="h-11 px-[18px] bg-white text-[#1F1F1F] border border-[#EBEBEB] rounded-lg font-semibold text-base leading-5 hover:bg-gray-50 transition-colors"
              >
                Stop
              </button>
            </div>
          </div>
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog.Root>
  );
};



export default function FilamentDemo() {
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
          打开 Filament Dialog
        </button>
        <LoadingFilamentDialog
          open={open}
          onClose={() => setOpen(false)}
          onOpenChange={(open) => setOpen(open)}
        />
      </div>
    </div>
  )
}