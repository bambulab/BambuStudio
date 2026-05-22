import { useEffect, useRef, useState, type RefObject } from 'react';
import { useTranslation } from 'react-i18next';
import type {
  AmsFilamentHotendSlotActionPayload,
  AmsFilamentHotendSlotViewModel,
  HotendOpState,
  HotendSlotViewModel,
  HotendToolKind,
} from '../types';

import hotendNozzleUrl        from '../assets/hotend_nozzle.svg';
import hotendNozzleOutlineUrl from '../assets/hotend_nozzle_outline.svg';
import hotendCutterUrl        from '../assets/hotend_cutter.svg';
import hotendPenUrl           from '../assets/hotend_pen.svg';
import hotendPlacingArrowUrl  from '../assets/hotend_placing_arrow.svg';
import hotendErrorBadgeUrl    from '../assets/hotend_error_badge.svg';
import hotendReadingArcUrl    from '../assets/hotend_reading_arc.png';

export function HotendCard({
  slot,
  globalBusy,
  onAction,
}: {
  slot: HotendSlotViewModel;
  globalBusy?: boolean;
  // HotendCard reuses the same onAction shape used by the Filament tab so a
  // single handler can serve both pages. Only identity + hotend fields are
  // read from the slot; the union keeps callers strongly typed.
  onAction: (
    action: string,
    slot: AmsFilamentHotendSlotViewModel | HotendSlotViewModel,
    payload?: AmsFilamentHotendSlotActionPayload,
  ) => void;
}) {
  const { t } = useTranslation();
  const hot = slot.hotend ?? {};
  const op: HotendOpState = hot.op_state ?? 'Idle';
  const isTaken = !!slot.taken;
  const isSelected = isTaken || !!hot.selected;
  const isStatusReplaced = op === 'Reading' || op === 'Placing' || op === 'Removing' || op === 'Error';
  const [menuOpen, setMenuOpen] = useState(false);
  const [editingTemp, setEditingTemp] = useState(false);
  const [draftTemp, setDraftTemp] = useState('');
  const menuRef = useRef<HTMLSpanElement>(null);
  const tempInputRef = useRef<HTMLInputElement>(null);
  const skipBlurCommitRef = useRef(false);
  const isBusy = op === 'Reading' || op === 'Placing' || op === 'Removing';
  const canOperate = slot.enabled && !isBusy && !globalBusy;
  const canSetTemperature = canOperate && hot.tool_kind !== 'empty' && op !== 'Error';

  useEffect(() => {
    if (!menuOpen) return;
    const onPointerDown = (event: PointerEvent) => {
      if (!menuRef.current?.contains(event.target as Node)) setMenuOpen(false);
    };
    document.addEventListener('pointerdown', onPointerDown);
    return () => document.removeEventListener('pointerdown', onPointerDown);
  }, [menuOpen]);

  useEffect(() => {
    if (!editingTemp) return;
    skipBlurCommitRef.current = false;
    const target = typeof hot.target_temp === 'number' ? hot.target_temp : 0;
    setDraftTemp(String(target));
    queueMicrotask(() => {
      tempInputRef.current?.focus();
      tempInputRef.current?.select();
    });
  }, [editingTemp, hot.target_temp]);

  const runMenuAction = (action: string) => {
    setMenuOpen(false);
    if (action === 'adjust_temperature') {
      if (canSetTemperature) setEditingTemp(true);
      return;
    }
    onAction(action, slot);
  };

  const submitTemperature = () => {
    if (!editingTemp) return;
    const parsed = Number(draftTemp.trim());
    if (!Number.isInteger(parsed) || parsed < 0 || parsed > 350) {
      setEditingTemp(false);
      return;
    }
    setEditingTemp(false);
    if (parsed !== (hot.target_temp ?? 0)) {
      onAction('set_hotend_temperature', slot, { target_temp: parsed });
    }
  };

  const cancelTemperatureEdit = () => {
    skipBlurCommitRef.current = true;
    setDraftTemp(String(hot.target_temp ?? 0));
    setEditingTemp(false);
  };

  const cardCls = ['hotend-card'];
  cardCls.push(`hotend-card-${op.toLowerCase()}`);
  if (isSelected) cardCls.push('selected');
  if (!slot.enabled) cardCls.push('disabled');
  if (isTaken) cardCls.push('taken');

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
              <button type="button" role="menuitem" disabled={!canOperate} onClick={() => runMenuAction('read_hotend')}>{t('Read')}</button>
              <button type="button" role="menuitem" disabled={!canSetTemperature} onClick={() => runMenuAction('adjust_temperature')}>{t('Set Temperature')}</button>
              <button type="button" role="menuitem" disabled={!canOperate} onClick={() => runMenuAction('place_hotend')}>{t('Place')}</button>
              <button type="button" role="menuitem" onClick={() => runMenuAction('hotend_detail')}>{t('Details')}</button>
            </div>
          )}
        </span>
      </div>

      <div className="hotend-card-body">
        {isTaken ? (
          <TakenReadout />
        ) : isStatusReplaced ? (
          <StatusReadout op={op} />
        ) : (
          <TempReadout
            op={op}
            current={hot.current_temp}
            target={hot.target_temp}
            editable={canSetTemperature}
            editing={editingTemp}
            draft={draftTemp}
            inputRef={tempInputRef}
            onStartEdit={() => setEditingTemp(true)}
            onDraftChange={setDraftTemp}
            onSubmit={submitTemperature}
            onCancel={cancelTemperatureEdit}
            onSkipBlurCommit={(skip) => { skipBlurCommitRef.current = skip; }}
            shouldSkipBlurCommit={() => skipBlurCommitRef.current}
          />
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
  editable,
  editing,
  draft,
  inputRef,
  onStartEdit,
  onDraftChange,
  onSubmit,
  onCancel,
  onSkipBlurCommit,
  shouldSkipBlurCommit,
}: {
  op: HotendOpState;
  current?: number | null;
  target?: number | null;
  editable: boolean;
  editing: boolean;
  draft: string;
  inputRef: RefObject<HTMLInputElement | null>;
  onStartEdit: () => void;
  onDraftChange: (value: string) => void;
  onSubmit: () => void;
  onCancel: () => void;
  onSkipBlurCommit: (skip: boolean) => void;
  shouldSkipBlurCommit: () => boolean;
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
        {editing ? (
          <span className="hotend-temp-target-edit">
            /
            <input
              ref={inputRef}
              value={draft}
              inputMode="numeric"
              pattern="[0-9]*"
              aria-label="hotend target temperature"
              onClick={(event) => event.stopPropagation()}
              onChange={(event) => onDraftChange(event.target.value.replace(/[^\d]/g, ''))}
              onBlur={() => {
                if (shouldSkipBlurCommit()) {
                  onSkipBlurCommit(false);
                  return;
                }
                onSubmit();
              }}
              onKeyDown={(event) => {
                if (event.key === 'Enter') {
                  event.preventDefault();
                  onSkipBlurCommit(true);
                  onSubmit();
                } else if (event.key === 'Escape') {
                  event.preventDefault();
                  onCancel();
                }
              }}
            />
            ℃
          </span>
        ) : (
          <button
            type="button"
            className={`hotend-temp-target-button${editable ? '' : ' disabled'}`}
            disabled={!editable}
            onClick={(event) => {
              event.stopPropagation();
              if (editable) onStartEdit();
            }}
          >
            /{tgt}℃
          </button>
        )}
      </div>
    </div>
  );
}

// Shown when the hotend rack nozzle is currently mounted on the extruder; the
// Hotend tab card replaces live temperature with a "Taken" overlay so the
// operator does not try to control the same nozzle twice.
function TakenReadout() {
  const { t } = useTranslation();
  return (
    <div className="hotend-readout status status-taken" data-testid="ams-hotend-taken">
      <span className="hotend-readout-text">{t('Taken')}</span>
    </div>
  );
}

function StatusReadout({ op }: { op: HotendOpState }) {
  const { t } = useTranslation();
  const text =
    op === 'Reading'  ? t('Reading') :
    op === 'Placing'  ? t('Placing') :
    op === 'Removing' ? t('Removing') :
    op === 'Error'    ? t('Error') : '';
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
    <svg
      className={`hotend-temp-arrow hotend-temp-arrow-${dir}`}
      width={12}
      height={16}
      viewBox="0 0 16 12"
      fill="none"
      aria-hidden="true"
    >
      <path
        d="M13.8018 6.50384C13.9277 6.37403 14 6.19228 14 6.00189C14 5.81149 13.9277 5.63263 13.8018 5.49993L9.0875 0.653472C8.83036 0.388071 8.42321 0.39961 8.17946 0.67655C7.93571 0.953491 7.94375 1.39198 8.20089 1.6545L11.7527 5.30954H2.64286C2.28661 5.30954 2 5.61821 2 6.00189C2 6.38557 2.28661 6.69424 2.64286 6.69424H11.7527L8.19821 10.3464C7.94107 10.6118 7.93304 11.0474 8.17679 11.3243C8.42054 11.6013 8.82768 11.6099 9.08482 11.3474L13.7991 6.50096L13.8018 6.50384Z"
        fill="currentColor"
      />
    </svg>
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
