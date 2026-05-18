import { useEffect, useRef, useState } from 'react';
import type { AmsFilamentHotendSlotViewModel, HotendOpState, HotendToolKind } from '../types';

import hotendNozzleUrl        from '../assets/hotend_nozzle.svg';
import hotendNozzleOutlineUrl from '../assets/hotend_nozzle_outline.svg';
import hotendCutterUrl        from '../assets/hotend_cutter.svg';
import hotendPenUrl           from '../assets/hotend_pen.svg';
import hotendPlacingArrowUrl  from '../assets/hotend_placing_arrow.svg';
import hotendErrorBadgeUrl    from '../assets/hotend_error_badge.svg';
import hotendReadingArcUrl    from '../assets/hotend_reading_arc.png';
import hotendTempArrowUrl     from '../assets/hotend_temp_arrow.svg';

export function HotendCard({
  slot,
  onAction,
}: {
  slot: AmsFilamentHotendSlotViewModel;
  onAction: (action: string, slot: AmsFilamentHotendSlotViewModel) => void;
}) {
  const hot = slot.hotend ?? {};
  const op: HotendOpState = hot.op_state ?? 'Idle';
  const isSelected = !!hot.selected || !!slot.selected;
  const isStatusReplaced = op === 'Reading' || op === 'Placing' || op === 'Removing' || op === 'Error';
  const [menuOpen, setMenuOpen] = useState(false);
  const menuRef = useRef<HTMLSpanElement>(null);
  const isBusy = op === 'Reading' || op === 'Placing' || op === 'Removing';
  const canOperate = slot.enabled && !isBusy;

  useEffect(() => {
    if (!menuOpen) return;
    const onPointerDown = (event: PointerEvent) => {
      if (!menuRef.current?.contains(event.target as Node)) setMenuOpen(false);
    };
    document.addEventListener('pointerdown', onPointerDown);
    return () => document.removeEventListener('pointerdown', onPointerDown);
  }, [menuOpen]);

  const runMenuAction = (action: string) => {
    setMenuOpen(false);
    onAction(action, slot);
  };

  const cardCls = ['hotend-card'];
  cardCls.push(`hotend-card-${op.toLowerCase()}`);
  if (isSelected) cardCls.push('selected');
  if (!slot.enabled) cardCls.push('disabled');

  const indexCls = ['hotend-card-index'];
  if (isSelected) indexCls.push('selected');

  return (
    <div
      className={cardCls.join(' ')}
      data-testid={`ams-hotend-hotend-card-${slot.display_index}`}
      data-op-state={op}
      onClick={() => slot.enabled && onAction('select_hotend', slot)}
    >
      <div className="hotend-card-head">
        <span className={indexCls.join(' ')} data-testid={`ams-hotend-hotend-index-${slot.display_index}`}>
          {slot.display_index}
        </span>
        <span className="hotend-card-menu-wrap" ref={menuRef}>
          <button
            className={`hotend-card-menu${menuOpen ? ' open' : ''}`}
            type="button"
            aria-label={`hotend menu ${slot.display_index}`}
            aria-haspopup="menu"
            aria-expanded={menuOpen}
            onClick={(e) => {
              e.stopPropagation();
              setMenuOpen((value) => !value);
            }}
          >
            <MenuDots />
          </button>
          {menuOpen && (
            <div className="hotend-menu-popover" role="menu">
              <button type="button" role="menuitem" disabled={!canOperate} onClick={() => runMenuAction('read_hotend')}>读取</button>
              <button type="button" role="menuitem" disabled={!canOperate} onClick={() => runMenuAction('place_hotend')}>取用</button>
              <button type="button" role="menuitem" onClick={() => runMenuAction('hotend_detail')}>详情</button>
            </div>
          )}
        </span>
      </div>

      <div className="hotend-card-body">
        {isStatusReplaced ? (
          <StatusReadout op={op} />
        ) : (
          <TempReadout op={op} current={hot.current_temp} target={hot.target_temp} />
        )}

        <ToolIcon kind={hot.tool_kind ?? 'empty'} op={op} />

        <ToolLabel
          kind={hot.tool_kind ?? 'empty'}
          label={hot.nozzle_label}
          subtext={hot.nozzle_subtext}
          op={op}
        />
      </div>
    </div>
  );
}

function TempReadout({
  op,
  current,
  target,
}: {
  op: HotendOpState;
  current?: number | null;
  target?: number | null;
}) {
  const cur = typeof current === 'number' ? current : null;
  const tgt = typeof target === 'number' ? target : 0;
  const cls = ['hotend-readout', 'temp'];
  if (op === 'Heating') cls.push('heating');
  if (op === 'Cooling') cls.push('cooling');
  return (
    <div className={cls.join(' ')}>
      <div className="hotend-readout-line">
        <span className="hotend-temp-current">{cur === null ? '--' : cur}</span>
        {op === 'Heating' && <ArrowIcon dir="up" />}
        {op === 'Cooling' && <ArrowIcon dir="down" />}
      </div>
      <div className="hotend-readout-line">
        <span className="hotend-temp-target">/{tgt}℃</span>
      </div>
    </div>
  );
}

function StatusReadout({ op }: { op: HotendOpState }) {
  const text =
    op === 'Reading'  ? 'Reading' :
    op === 'Placing'  ? '放置中' :
    op === 'Removing' ? '取下中' :
    op === 'Error'    ? '错误' : '';
  return (
    <div className={`hotend-readout status status-${op.toLowerCase()}`}>
      <span className="hotend-readout-text">{text}</span>
    </div>
  );
}

function MenuDots() {
  return (
    <svg width={16} height={16} viewBox="0 0 16 16" aria-hidden="true">
      <circle cx={3} cy={8} r={1.2} fill="#5c5c5c" />
      <circle cx={8} cy={8} r={1.2} fill="#5c5c5c" />
      <circle cx={13} cy={8} r={1.2} fill="#5c5c5c" />
    </svg>
  );
}

function ArrowIcon({ dir }: { dir: 'up' | 'down' }) {
  return (
    <img
      className={`hotend-temp-arrow hotend-temp-arrow-${dir}`}
      src={hotendTempArrowUrl}
      width={12}
      height={16}
      alt={`temp ${dir}`}
    />
  );
}

function ToolIcon({
  kind,
  op,
  withError,
}: {
  kind: HotendToolKind;
  op: HotendOpState;
  withError?: boolean;
}) {
  if (op === 'Reading') {
    return (
      <div className="hotend-tool-icon-wrap">
        <img className="hotend-icon-loading" src={hotendReadingArcUrl} width={28} height={28} alt="reading" />
      </div>
    );
  }
  if (kind === 'empty') {
    return (
      <div className="hotend-tool-icon-wrap">
        <div className="hotend-tool-icon-empty" aria-hidden="true" />
      </div>
    );
  }
  if (kind === 'cutter') {
    return (
      <div className="hotend-tool-icon-wrap">
        <img className="hotend-icon-cutter" src={hotendCutterUrl} width={24} height={42} alt="cutter" />
      </div>
    );
  }
  if (kind === 'pen') {
    return (
      <div className="hotend-tool-icon-wrap">
        <img className="hotend-icon-pen" src={hotendPenUrl} width={24} height={42} alt="pen" />
      </div>
    );
  }
  const nozzleSrc = withError ? hotendNozzleOutlineUrl : hotendNozzleUrl;
  return (
    <div className="hotend-tool-icon-wrap hotend-icon-nozzle-wrap">
      <img className="hotend-icon-nozzle" src={nozzleSrc} width={20} height={36} alt="nozzle" />
      {withError && (
        <img className="hotend-icon-error-badge" src={hotendErrorBadgeUrl} width={16} height={16} alt="error" />
      )}
      {op === 'Placing' && (
        <img className="hotend-placing-arrow" src={hotendPlacingArrowUrl} width={9} height={32} alt="placing" />
      )}
    </div>
  );
}

function ToolLabel({
  kind,
  label,
  subtext,
  op,
}: {
  kind: HotendToolKind;
  label?: string;
  subtext?: string;
  op: HotendOpState;
}) {
  const isMuted = kind === 'empty' || kind === 'unknown' || op === 'Reading';
  const primary = label ?? (isMuted ? '--' : '');
  const secondary = subtext ?? (isMuted ? '--' : '');
  return (
    <div className={`hotend-tool-label${isMuted ? ' muted' : ''}`}>
      <div className="hotend-tool-name">{primary || '\u00A0'}</div>
      <div className="hotend-tool-sub">{secondary || '\u00A0'}</div>
    </div>
  );
}
