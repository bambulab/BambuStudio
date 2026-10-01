import React, { useState } from 'react';
import * as Dialog from '@radix-ui/react-dialog';

// 自定义图标组件，使用img标签作为占位符
const Icon: React.FC<{ src: string; alt: string; size?: number; className?: string }> = ({
  src,
  alt,
  size = 18,
  className = ""
}) => {
  return (
    <img
      src={src}
      alt={alt}
      width={size}
      height={size}
      className={`w-[${size}px] h-[${size}px] ${className}`}
    />
  );
};

// ChipToggle 组件
interface ChipToggleProps {
  selected: boolean;
  children: React.ReactNode;
  onClick: () => void;
}

const ChipToggle: React.FC<ChipToggleProps> = ({ selected, children, onClick }) => {
  return (
    <button
      onClick={onClick}
      className={`
        h-9 px-3.5 rounded-lg flex items-center gap-2 font-semibold text-base leading-6
        ${selected
          ? 'bg-[#EAF8F0] border-2 border-[#16A34A] text-[#15803D]'
          : 'bg-white border border-[#EBEBEB] text-[#6B6B6B]'
        }
      `}
    >
      {children}
      {selected && (
        <Icon
          src="/icons/check-circle.svg"
          alt="Check"
          size={16}
          className="text-[#15803D]"
        />
      )}
    </button>
  );
};

// IconButton 组件
interface IconButtonProps {
  onClick: () => void;
  iconSrc: string;
  alt: string;
}

const IconButton: React.FC<IconButtonProps> = ({ onClick, iconSrc, alt }) => {
  return (
    <button
      onClick={onClick}
      className="w-7 h-7 rounded-lg bg-white flex items-center justify-center hover:bg-gray-50 transition-colors"
    >
      <Icon src={iconSrc} alt={alt} size={18} />
    </button>
  );
};

// TextButton 组件
interface TextButtonProps {
  onClick: () => void;
  children: React.ReactNode;
}

const TextButton: React.FC<TextButtonProps> = ({ onClick, children }) => {
  return (
    <button
      onClick={onClick}
      className="h-9 px-2.5 text-base leading-6 font-normal text-[#6B6B6B] hover:text-[#1F1F1F] transition-colors"
    >
      {children}
    </button>
  );
};

// NumericInput 组件
interface NumericInputProps {
  value: number;
  onChange: (value: number) => void;
  min?: number;
  max?: number;
  step?: number;
}

const NumericInput: React.FC<NumericInputProps> = ({
  value,
  onChange,
  min = 0,
  max = 100,
  step = 1
}) => {
  return (
    <div className="relative">
      <input
        type="number"
        value={value}
        onChange={(e) => onChange(Number(e.target.value))}
        min={min}
        max={max}
        step={step}
        className="h-10 w-20 rounded-md border-2 border-[#E5E7EB] px-3 text-base leading-6 font-bold text-center focus:outline-none focus:border-[#16A34A]"
      />
      <span className="absolute right-2 top-1/2 transform -translate-y-1/2 text-base leading-6 text-[#6B6B6B]">
        %
      </span>
    </div>
  );
};

// StatRow 组件
interface StatRowProps {
  icon: string;
  label: string;
  children: React.ReactNode;
}

const StatRow: React.FC<StatRowProps> = ({ icon, label, children }) => {
  return (
    <div className="flex items-center gap-4">
      <Icon src={icon} alt={label} size={18} className="text-[#6B6B6B]" />
      <span className="text-base leading-6 text-[#6B6B6B]">{label}</span>
      <div className="flex items-center gap-2">
        {children}
      </div>
    </div>
  );
};

// KeyValue 组件
interface KeyValueProps {
  icon: string;
  label: string;
  value: string;
  onClick?: () => void;
}

const KeyValue: React.FC<KeyValueProps> = ({ icon, label, value, onClick }) => {
  return (
    <div
      className={`flex items-center gap-3 ${onClick ? 'cursor-pointer hover:bg-gray-50 p-2 rounded-md transition-colors' : ''}`}
      onClick={onClick}
    >
      <Icon src={icon} alt={label} size={18} className="text-[#6B6B6B]" />
      <span className="text-base leading-6 text-[#6B6B6B]">{label}</span>
      <Icon src="/icons/chevron-right.svg" alt="Expand" size={16} className="text-[#6B6B6B]" />
      <span className="text-2xl leading-7 font-extrabold text-[#1F1F1F]">{value}</span>
    </div>
  );
};

// 主对话框组件
interface AirConditionDialogProps {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  onReset?: () => void;
  onClose?: () => void;
  onPartPercentChange?: (value: number) => void;
}

const AirConditionDialog: React.FC<AirConditionDialogProps> = ({
  open,
  onOpenChange,
  onReset,
  onClose,
  onPartPercentChange
}) => {
  const [mode, setMode] = useState<'cooling' | 'heating'>('cooling');
  const [partPercent, setPartPercent] = useState(10);

  const handleReset = () => {
    setMode('cooling');
    setPartPercent(10);
    onReset?.();
  };

  const handleClose = () => {
    onOpenChange(false);
    onClose?.();
  };

  const handlePartPercentChange = (value: number) => {
    setPartPercent(value);
    onPartPercentChange?.(value);
  };

  return (
    <Dialog.Root open={open} onOpenChange={onOpenChange}>
      <Dialog.Portal>
        <Dialog.Overlay className="fixed inset-0 bg-black/20 backdrop-blur-sm z-50" />
        <Dialog.Content className="fixed top-1/2 left-1/2 transform -translate-x-1/2 -translate-y-1/2 z-50">
          <div className="w-[1106px] bg-white rounded-2xl shadow-[0_12px_28px_rgba(0,0,0,0.2),0_2px_8px_rgba(0,0,0,0.12)]">
            {/* Header */}
            <div className="h-16 bg-[#F8F8F8] rounded-t-2xl flex items-center gap-4 px-6 py-3">
              <Dialog.Title className="text-lg leading-6 font-semibold text-[#1F1F1F]">
                Air Condition
              </Dialog.Title>

              <div className="w-3"></div>

              <ChipToggle
                selected={mode === 'cooling'}
                onClick={() => setMode('cooling')}
              >
                Cooling
              </ChipToggle>

              <ChipToggle
                selected={mode === 'heating'}
                onClick={() => setMode('heating')}
              >
                Heating
              </ChipToggle>

              <Icon src="/icons/help-circle.svg" alt="Help" size={24} className="text-[#6B6B6B]" />

              <div className="flex-1"></div>

              <TextButton onClick={handleReset}>
                Reset
              </TextButton>

              <IconButton
                onClick={handleClose}
                iconSrc="/icons/close.svg"
                alt="Close"
              />
            </div>

            {/* Divider */}
            <div className="h-px bg-[#EBEBEB]"></div>

            {/* Content */}
            <div className="p-6">
              <div className="flex gap-8 items-start">
                {/* Left Column */}
                <div className="flex-1 flex flex-col gap-6">
                  <StatRow icon="/icons/fan.svg" label="Part">
                    <NumericInput
                      value={partPercent}
                      onChange={handlePartPercentChange}
                      min={0}
                      max={100}
                      step={1}
                    />
                    <span className="text-base leading-6 text-[#6B6B6B]">%</span>
                  </StatRow>

                  <StatRow icon="/icons/fan.svg" label="Aux">
                    <span className="text-2xl leading-7 font-extrabold text-[#1F1F1F]">100</span>
                    <span className="text-base leading-6 text-[#6B6B6B]">%</span>
                  </StatRow>
                </div>

                {/* Media Section */}
                <div className="w-[420px] relative">
                  {/* 占位图片 */}
                  <div className="w-full h-64 bg-gray-100 rounded-lg flex items-center justify-center">
                    <span className="text-gray-400">机器空调占位图</span>
                  </div>

                  {/* 虚线连接器 - 使用CSS边框模拟 */}
                  <div className="absolute inset-0 pointer-events-none">
                    {/* Part 连接线 */}
                    <div className="absolute left-0 top-8 w-16 h-px border-t-2 border-dashed border-gray-200 transform -translate-x-full"></div>
                    {/* Aux 连接线 */}
                    <div className="absolute left-0 top-32 w-16 h-px border-t-2 border-dashed border-gray-200 transform -translate-x-full"></div>
                    {/* Exhaust 连接线 */}
                    <div className="absolute right-0 top-8 w-16 h-px border-t-2 border-dashed border-gray-200 transform translate-x-full"></div>
                    {/* Heat 连接线 */}
                    <div className="absolute right-0 top-32 w-16 h-px border-t-2 border-dashed border-gray-200 transform translate-x-full"></div>
                    {/* Chamber 连接线 */}
                    <div className="absolute right-0 top-56 w-16 h-px border-t-2 border-dashed border-gray-200 transform translate-x-full"></div>
                  </div>
                </div>

                {/* Right Column */}
                <div className="flex-1 flex flex-col gap-6">
                  <StatRow icon="/icons/fan.svg" label="Exhauxt">
                    <span className="text-2xl leading-7 font-extrabold text-[#1F1F1F]">100</span>
                    <span className="text-base leading-6 text-[#6B6B6B]">%</span>
                  </StatRow>

                  <StatRow icon="/icons/flame.svg" label="Heat">
                    <span className="text-2xl leading-7 font-extrabold text-[#1F1F1F]">OFF</span>
                  </StatRow>

                  <KeyValue
                    icon="/icons/thermometer.svg"
                    label="Chamber"
                    value="45°C/65°C"
                    onClick={() => console.log('Chamber clicked')}
                  />
                </div>
              </div>
            </div>
          </div>
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog.Root>
  );
};



export default function AirConditionDemo() {
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
          打开 Fans Dialog
        </button>
        <AirConditionDialog
          open={open}
          onClose={() => setOpen(false)}
          onOpenChange={(open) => setOpen(open)}
        />
      </div>
    </div>
  )
}