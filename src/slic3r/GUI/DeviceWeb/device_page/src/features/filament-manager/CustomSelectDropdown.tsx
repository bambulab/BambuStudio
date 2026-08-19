import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';

interface Props {
  value: string;
  options: string[];
  placeholder?: string;
  disabled?: boolean;
  addPlaceholder?: string;
  duplicateTooltip?: string;  // tooltip shown on the disabled Add button when input duplicates an existing option
  onSelect: (value: string) => void;
  onAddOption: (value: string) => void;
  'data-testid'?: string;
}

export function CustomSelectDropdown({
  value,
  options,
  placeholder = '',
  disabled = false,
  addPlaceholder = '',
  duplicateTooltip = '',
  onSelect,
  onAddOption,
  'data-testid': testId,
}: Props) {
  const { t } = useTranslation();
  const [open, setOpen] = useState(false);
  const [inputVal, setInputVal] = useState('');
  const containerRef = useRef<HTMLDivElement>(null);
  const inputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    if (!open) return;
    const handler = (e: MouseEvent) => {
      if (containerRef.current && !containerRef.current.contains(e.target as Node)) {
        setOpen(false);
      }
    };
    document.addEventListener('mousedown', handler);
    return () => document.removeEventListener('mousedown', handler);
  }, [open]);

  const displayValue = value || placeholder;
  const hasValue = !!value;

  // Edit-mode fallback: if current value is not in options list, show it anyway
  const showFallback = hasValue && !options.includes(value);

  const handleSelect = (v: string) => {
    onSelect(v);
    setOpen(false);
  };

  const trimmed = inputVal.trim();
  const isDuplicate = trimmed.length > 0 && options.includes(trimmed);
  const addDisabled = !trimmed || isDuplicate;

  const handleAdd = () => {
    if (addDisabled) return;
    onAddOption(trimmed);
    setInputVal('');
  };

  const handleInputKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      handleAdd();
    }
    // Prevent the dropdown from closing when typing
    e.stopPropagation();
  };

  return (
    <div ref={containerRef} className="relative w-full">
      {/* Trigger */}
      <div
        data-testid={testId}
        role="combobox"
        aria-expanded={open}
        aria-haspopup="listbox"
        tabIndex={disabled ? -1 : 0}
        className={[
          'bg-fm-inner2 rounded-[6px] h-[32px] pl-[8px] pr-[24px] text-[12px] leading-[19px] outline-none w-full',
          'flex items-center relative select-none',
          'focus:shadow-[0_0_0_1px_var(--color-fm-brand)]',
          disabled
            ? 'cursor-not-allowed opacity-60'
            : 'cursor-pointer',
          hasValue ? 'text-fm-text-strong' : 'text-fm-text-detail',
        ].join(' ')}
        onClick={() => { if (!disabled) setOpen(o => !o); }}
        onKeyDown={(e) => {
          if (disabled) return;
          if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); setOpen(o => !o); }
          if (e.key === 'Escape') setOpen(false);
        }}
      >
        <span className="truncate flex-1">{displayValue}</span>
        {/* Chevron arrow — mirrors fm-select-arrow SVG direction */}
        <span
          className="absolute right-[6px] top-1/2 -translate-y-1/2 pointer-events-none text-fm-text-detail text-[10px]"
          aria-hidden
        >
          ▾
        </span>
      </div>

      {/* Dropdown panel */}
      {open && (
        <div
          role="listbox"
          className="absolute left-0 right-0 top-[34px] z-50 rounded-[6px] border border-fm-border-focus bg-fm-base shadow-lg overflow-hidden"
        >
          {/* Options list — max height with scroll */}
          <div className="max-h-[180px] overflow-y-auto">
            {showFallback && (
              <div
                role="option"
                aria-selected
                className="px-[10px] py-[6px] text-[12px] leading-[19px] text-fm-text-strong cursor-pointer bg-fm-selected"
                onMouseDown={(e) => { e.preventDefault(); handleSelect(value); }}
              >
                {value}
              </div>
            )}
            {options.map((opt) => (
              <div
                key={opt}
                role="option"
                aria-selected={opt === value}
                className={[
                  'px-[10px] py-[6px] text-[12px] leading-[19px] cursor-pointer',
                  opt === value
                    ? 'text-fm-text-strong bg-fm-selected'
                    : 'text-fm-text-strong hover:bg-fm-hover',
                ].join(' ')}
                onMouseDown={(e) => { e.preventDefault(); handleSelect(opt); }}
              >
                {opt}
              </div>
            ))}
            {options.length === 0 && !showFallback && (
              <div className="px-[10px] py-[6px] text-[12px] text-fm-text-detail">
                {t('No options')}
              </div>
            )}
          </div>

          {/* Footer: input + add button */}
          <div className="border-t border-fm-border px-[8px] py-[6px] flex items-center gap-[6px]">
            <input
              ref={inputRef}
              type="text"
              className="bg-fm-inner rounded-[4px] h-[28px] px-[8px] text-[12px] leading-[19px] text-fm-text-strong outline-none flex-1 min-w-0 placeholder:text-fm-text-detail focus:shadow-[0_0_0_1px_var(--color-fm-brand)] border-none"
              placeholder={addPlaceholder}
              value={inputVal}
              onChange={(e) => setInputVal(e.target.value)}
              onKeyDown={handleInputKeyDown}
              onMouseDown={(e) => e.stopPropagation()}
            />
            <button
              type="button"
              title={isDuplicate ? (duplicateTooltip || t('Already exists')) : undefined}
              className="text-[12px] leading-[19px] text-fm-brand cursor-pointer shrink-0 bg-transparent border-none p-0 hover:text-fm-brand-hover disabled:opacity-40 disabled:cursor-not-allowed whitespace-nowrap"
              disabled={addDisabled}
              onMouseDown={(e) => { e.preventDefault(); handleAdd(); }}
            >
              + {t('Add Option')}
            </button>
          </div>
        </div>
      )}
    </div>
  );
}
