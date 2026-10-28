import { useEffect, useRef, useState, type ReactNode, type RefObject } from 'react';
import { useTranslation } from 'react-i18next';
import type {
  AmsFilamentHotendSlotActionPayload,
  AmsFilamentHotendSlotViewModel,
  FilamentNozzleInfo,
  HotendOpState,
  HotendSlotViewModel,
  HotendTempTrend,
  HotendToolKind,
} from '../types';
import { STANDBY_COLOR, visibleFilamentColor } from '../visibleFilamentColor';
import { useNozzleImage } from '../nozzleFilamentImage';

import hotendNozzleErrUrl     from '../assets/hotend_nozzle_err.png';
import hotendNozzleLostUrl    from '../assets/hotend_nozzle_lost.png';
import hotendNozzleUnknownUrl from '../assets/hotend_nozzle_unknow.png';
import hotendNozzleDisableUrl from '../assets/hotend_nozzle_disable.png';
import hotendForbiddenUrl     from '../assets/hotend_forbidden.svg';
import hotendPlacingArrowUrl  from '../assets/hotend_placing_arrow.svg';
import hotendTakingArrowUrl   from '../assets/hotend_taking_arrow.svg';
import hotendTakenUrl         from '../assets/hotend_taken.svg';
import hotend2dBadgeUrl       from '../assets/hotend_2d_badge.svg';
import hotendReadingArcUrl    from '../assets/hotend_reading_arc.png';

// `#D9D9D9` is the C++ sentinel for "colour unknown"; it renders as standby grey.
const UNKNOWN_COLOR_SENTINEL = '#D9D9D9';

// Colour of the filament sitting inside the nozzle, or standby grey when the
// nozzle is empty or its colour has not been read yet. The swatch must stay
// opaque: it paints over the green bar baked into the nozzle asset, which
// would otherwise show through a translucent filament colour.
function nozzleFilamentColor(nozzle?: FilamentNozzleInfo): string {
  const color = nozzle?.filament_color?.trim();
  if (!nozzle?.has_filament || !color) return STANDBY_COLOR;
  const rgb = /^#?([0-9a-fA-F]{6})/.exec(color);
  if (!rgb) return STANDBY_COLOR;
  const opaque = `#${rgb[1]}`;
  if (opaque.toLowerCase() === UNKNOWN_COLOR_SENTINEL.toLowerCase()) return STANDBY_COLOR;
  return visibleFilamentColor(opaque);
}

export function HotendCard({
  slot,
  globalBusy,
  onAction,
}: {
  slot: HotendSlotViewModel;
  globalBusy?: boolean;
  // Shared action handler for both tabs.
  onAction: (
    action: string,
    slot: AmsFilamentHotendSlotViewModel | HotendSlotViewModel,
    payload?: AmsFilamentHotendSlotActionPayload,
  ) => void;
}) {
  const { t } = useTranslation();
  const hot = slot.hotend ?? {};
  const op: HotendOpState = hot.op_state ?? 'None';
  const tempTrend: HotendTempTrend = hot.temp_trend ?? 'Steady';
  // A slot mid take/place still reports its nozzle as on-extruder, but the moving
  // nozzle visual owns the card instead of the muted taken UX (Figma 5157:26131 / 5157:26134).
  const isMoving = op === 'Taking' || op === 'Placing';
  const isTaken = !!slot.taken && !isMoving;
  // Taken (on-extruder) uses its own muted UX; do not paint the green selected chrome.
  const isSelected = !isTaken && !isMoving && !!hot.selected;
  // `None` is authoritative for "no tool"; tool_kind may still say "nozzle".
  // An Unknown nozzle is physically present but not identified yet, so it must
  // not fall through to the empty-slot readout just because tool_kind has not
  // been resolved.
  const isEmpty = op !== 'Unknown' && (op === 'None' || (hot.tool_kind ?? 'empty') === 'empty');
  // Fault states (Error / Lost / Unknown) keep the temperature readout; only the icon changes.
  const isStatusReplaced = op === 'Reading' || op === 'Taking' || op === 'Placing' || op === 'Removing';
  const [menuOpen, setMenuOpen] = useState(false);
  const [editingTemp, setEditingTemp] = useState(false);
  const [draftTemp, setDraftTemp] = useState('');
  const cardRef = useRef<HTMLDivElement>(null);
  const menuRef = useRef<HTMLSpanElement>(null);
  const tempInputRef = useRef<HTMLInputElement>(null);
  const skipBlurCommitRef = useRef(false);
  const isBusy = op === 'Reading' || op === 'Taking' || op === 'Placing' || op === 'Removing';
  const canOperate = slot.enabled && !isBusy && !globalBusy;
  const isNozzleTool = hot.tool_kind === 'nozzle';
  const menuActions = slot.menu_actions;
  const canRead = menuActions?.can_read ?? canOperate;
  const canDetail = menuActions?.can_detail ?? slot.enabled;
  const canSetTemperature = menuActions?.can_set_temperature ?? (canOperate && isNozzleTool && op !== 'Error');
  const canTakePlace = menuActions?.can_take_place ?? canOperate;
  const disabledReason = menuActions?.disabled_reason || undefined;
  const hotendMoveAction = isTaken ? 'place_hotend' : 'take_hotend';
  const hotendMoveLabel = isTaken ? t('Place') : t('Take');

  useEffect(() => {
    if (!menuOpen) return;
    // The whole card is now a menu trigger, so outside-close must exclude the
    // card itself (not just the three-dot span) or the card's own left-click
    // would fight this handler.
    const onPointerDown = (event: PointerEvent) => {
      if (!cardRef.current?.contains(event.target as Node)) setMenuOpen(false);
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
    let parsed = Number(draftTemp.trim());
    if (!Number.isInteger(parsed) || parsed < 0) {
      setEditingTemp(false);
      return;
    }
    parsed = Math.min(parsed, hot.max_temp ?? 350);
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
  if (hot.tool_kind === 'cutter' || hot.tool_kind === 'pen') cardCls.push('unsupported-tool');

  const indexCls = ['hotend-card-index'];
  if (isSelected) indexCls.push('selected');

  const handleCardClick = (event: React.MouseEvent<HTMLDivElement>) => {
    // Interactive descendants (three-dot button, menu items, temperature input)
    // manage their own click semantics; skip them so left-click on the card
    // body opens the menu without hijacking those controls.
    if ((event.target as HTMLElement).closest('input, button, [role="menuitem"]')) return;
    setMenuOpen((value) => !value);
  };

  return (
    <div
      ref={cardRef}
      className={cardCls.join(' ')}
      data-testid={`ams-hotend-hotend-card-${slot.display_index}`}
      data-op-state={op}
      title={hot.tooltip || undefined}
      onClick={handleCardClick}
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
              <HotendMenuItem enabled={canSetTemperature} disabledReason={disabledReason} onSelect={() => runMenuAction('adjust_temperature')}>{t('Set Temperature')}</HotendMenuItem>
              <HotendMenuItem enabled={canTakePlace} disabledReason={disabledReason} onSelect={() => runMenuAction(hotendMoveAction)}>{hotendMoveLabel}</HotendMenuItem>
              <HotendMenuItem enabled={canRead} disabledReason={disabledReason} onSelect={() => runMenuAction('read_hotend')}>{t('Refresh')}</HotendMenuItem>
              <HotendMenuItem enabled={canDetail} disabledReason={disabledReason} onSelect={() => runMenuAction('hotend_detail')}>{t('Details')}</HotendMenuItem>
            </div>
          )}
        </span>
      </div>

      <div className="hotend-card-body">
        {isStatusReplaced ? (
          <StatusReadout op={op} />
        ) : isTaken ? (
          <TakenReadout />
        ) : isEmpty ? (
          <EmptyReadout />
        ) : isNozzleTool ? (
          <TempReadout
            trend={tempTrend}
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
        ) : (
          <div className="hotend-readout hotend-readout-empty" aria-hidden="true" />
        )}

        <ToolIcon
          kind={hot.tool_kind ?? 'empty'}
          op={op}
          withError={op === 'Error'}
          taken={isTaken}
          filamentColor={nozzleFilamentColor(slot.filament_nozzle)}
        />

        <ToolLabel
          kind={hot.tool_kind ?? 'empty'}
          label={hot.nozzle_label}
          subtext={hot.nozzle_subtext}
          op={op}
          taken={isTaken}
        />
      </div>
    </div>
  );
}

function TempReadout({
  trend,
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
  trend: HotendTempTrend;
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
  if (trend === 'Heating') cls.push('heating');
  if (trend === 'Cooling') cls.push('cooling');
  return (
    <div className={cls.join(' ')}>
      <div className="hotend-readout-line">
        <span className="hotend-temp-current">{cur === null ? '--' : cur}</span>
        {trend === 'Heating' && <ArrowIcon dir="up" />}
        {trend === 'Cooling' && <ArrowIcon dir="down" />}
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

// Empty rack position (tool_kind === 'empty'): grey "Empty" text where the
// temperature normally is, per Figma 1882:112071.
function EmptyReadout() {
  const { t } = useTranslation();
  return (
    <div className="hotend-readout status status-empty" data-testid="ams-hotend-empty">
      <span className="hotend-readout-text">{t('Empty')}</span>
    </div>
  );
}

// Rack slot whose nozzle is currently mounted on the extruder (Figma Taken UX).
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
    op === 'Taking'   ? t('Taking') :
    op === 'Placing'  ? t('Placing') :
    op === 'Removing' ? t('Removing') : '';
  return (
    <div className={`hotend-readout status status-${op.toLowerCase()}`}>
      <span className="hotend-readout-text">{text}</span>
    </div>
  );
}

// Uses aria-disabled instead of `disabled`: Chromium drops mouse events on
// disabled buttons, which would suppress the reason tooltip.
function HotendMenuItem({
  enabled,
  disabledReason,
  onSelect,
  children,
}: {
  enabled: boolean;
  disabledReason?: string;
  onSelect: () => void;
  children: ReactNode;
}) {
  return (
    <button
      type="button"
      role="menuitem"
      aria-disabled={!enabled}
      title={!enabled ? disabledReason : undefined}
      onClick={(event) => {
        if (!enabled) {
          event.stopPropagation();
          return;
        }
        onSelect();
      }}
    >
      {children}
    </button>
  );
}

function MenuDots() {
  return (
    <svg width={16} height={16} viewBox="0 0 16 16" aria-hidden="true">
      <circle cx={3} cy={8} r={1.2} fill="var(--ams-text-card-menu-dot)" />
      <circle cx={8} cy={8} r={1.2} fill="var(--ams-text-card-menu-dot)" />
      <circle cx={13} cy={8} r={1.2} fill="var(--ams-text-card-menu-dot)" />
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
  taken,
  filamentColor,
}: {
  kind: HotendToolKind;
  op: HotendOpState;
  withError?: boolean;
  taken?: boolean;
  filamentColor: string;
}) {
  if (op === 'Reading') {
    return (
      <div className="hotend-tool-icon-wrap">
        <img className="hotend-icon-loading" src={hotendReadingArcUrl} width={24} height={24} alt="reading" />
      </div>
    );
  }
  // Taken rack slot: upload/out-of-tray icon instead of the nozzle body.
  if (taken) {
    return (
      <div className="hotend-tool-icon-wrap">
        <img className="hotend-icon-taken" src={hotendTakenUrl} width={20} height={20} alt="taken" />
      </div>
    );
  }
  if (op === 'Unknown') {
    return (
      <div className="hotend-tool-icon-wrap hotend-icon-nozzle-wrap">
        <img className="hotend-icon-nozzle" src={hotendNozzleUnknownUrl} width={36} height={42} alt="unknown" />
      </div>
    );
  }
  if (withError) {
    return (
      <div className="hotend-tool-icon-wrap hotend-icon-nozzle-wrap">
        <img className="hotend-icon-nozzle" src={hotendNozzleErrUrl} width={36} height={42} alt="error" />
      </div>
    );
  }
  if (op === 'Lost') {
    return (
      <div className="hotend-tool-icon-wrap hotend-icon-nozzle-wrap">
        <img className="hotend-icon-nozzle" src={hotendNozzleLostUrl} width={36} height={42} alt="lost" />
      </div>
    );
  }
  if (op === 'None' || kind === 'empty') {
    return (
      <div className="hotend-tool-icon-wrap">
        <img className="hotend-icon-forbidden" src={hotendForbiddenUrl} width={20} height={20} alt="empty" />
      </div>
    );
  }
  if (kind === 'cutter' || kind === 'pen') {
    // 2D tools: badge marks "not configurable"; reason rides on the card tooltip.
    return (
      <div className="hotend-tool-icon-wrap hotend-icon-nozzle-wrap">
        <img className="hotend-icon-nozzle" src={hotendNozzleDisableUrl} width={36} height={42} alt={kind} />
        <img className="hotend-icon-2d-badge" src={hotend2dBadgeUrl} width={24} height={24} alt="" />
      </div>
    );
  }
  if (kind === 'laser' || kind === 'fan' || kind === 'unknown') {
    return (
      <div className="hotend-tool-icon-wrap">
        <div className={`hotend-tool-icon-generic hotend-tool-icon-${kind}`} aria-hidden="true">
          {kind === 'laser' ? 'L' : kind === 'fan' ? 'F' : '?'}
        </div>
      </div>
    );
  }
  return <NozzleIcon op={op} filamentColor={filamentColor} />;
}

function NozzleIcon({ op, filamentColor }: { op: HotendOpState; filamentColor: string }) {
  // The asset bakes in a green filament bar; swap it for the real colour.
  const nozzleSrc = useNozzleImage(filamentColor);
  // Taking overlays the sleeve on the nozzle; Placing tucks it behind (Figma 1882:112011 / 1882:112034).
  const arrowSrc = op === 'Taking' ? hotendTakingArrowUrl : hotendPlacingArrowUrl;
  const sleeve = (op === 'Taking' || op === 'Placing') ? (
    <img
      className={`hotend-placing-arrow hotend-placing-arrow-${op.toLowerCase()}`}
      src={arrowSrc}
      width={9}
      height={32}
      alt={op.toLowerCase()}
    />
  ) : null;
  return (
    <div className="hotend-tool-icon-wrap hotend-icon-nozzle-wrap">
      {op === 'Placing' && sleeve}
      <img className="hotend-icon-nozzle" src={nozzleSrc} width={36} height={42} alt="nozzle" />
      {op === 'Taking' && sleeve}
    </div>
  );
}

function ToolLabel({
  kind,
  label,
  subtext,
  op,
  taken,
}: {
  kind: HotendToolKind;
  label?: string;
  subtext?: string;
  op: HotendOpState;
  taken?: boolean;
}) {
  const { t } = useTranslation();
  const isMuted = taken || kind === 'empty' || kind === 'unknown' || op === 'Reading' || op === 'Unknown' || op === 'None';
  // Unknown and None have no spec to show. Taken slots still show diameter /
  // flow when present. Unknown still names itself so the card does not read as
  // an empty slot.
  const blanked = op === 'Unknown' || op === 'None';
  const primary = op === 'Unknown' ? t('Unknown') : (blanked ? '--' : (label ?? (isMuted ? '--' : '')));
  const secondary = blanked ? '--' : (subtext ?? (isMuted ? '--' : ''));
  return (
    <div className={`hotend-tool-label${isMuted ? ' muted' : ''}`}>
      <ToolLabelRow className="hotend-tool-name" text={primary} />
      <ToolLabelRow className="hotend-tool-sub" text={secondary} />
    </div>
  );
}

// A label row stays on one line and ellipsises, so a flow wider than the card
// shows up as "TPU ...". Hand the full text to the native tooltip, but only
// while the row is actually cut off, so a row that fits stays tooltip-free.
function ToolLabelRow({ className, text }: { className: string; text: string }) {
  const ref = useRef<HTMLDivElement>(null);
  const [clipped, setClipped] = useState(false);
  const compact = text === 'TPU 285';

  useEffect(() => {
    const node = ref.current;
    if (!node) return;
    // Sub-pixel layout can leave scrollWidth a hair above clientWidth on a row
    // that is not truncated at all.
    const measure = () => setClipped(node.scrollWidth > node.clientWidth + 1);
    measure();
    if (typeof ResizeObserver !== 'function') return;
    const observer = new ResizeObserver(measure);
    observer.observe(node);
    return () => observer.disconnect();
  }, [text]);

  return (
    <div
      ref={ref}
      className={className}
      title={clipped ? text : undefined}
      style={compact ? { fontSize: '10px', lineHeight: '14px' } : undefined}
    >
      {text || '\u00A0'}
    </div>
  );
}
