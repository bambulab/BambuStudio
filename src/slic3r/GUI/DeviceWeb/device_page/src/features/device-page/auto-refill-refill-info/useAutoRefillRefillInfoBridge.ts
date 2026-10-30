import { useCallback, useEffect, useRef, useState } from 'react';
import { useDeviceBridge } from '../../../hooks/Bridge';
import useStore from '../../../store/AppStore';
import { mockAutoRefillRefillInfoState } from './mockData';
import type { AutoRefillRefillInfoViewModel, BridgeResponseBody, RefillToast } from './types';
import type {
  AutoRefillAddRefillFilamentsMode,
  AutoRefillAddRefillFilamentsSlotRef,
  AutoRefillAddRefillFilamentsViewModel,
} from './add-refill-filaments/types';

export const MODULE = 'device_page_auto_refill_refill_info';

// Which picker the page is showing in place of the group list, or null for the
// management view. The picker shares the dialog (and the view-model) with the
// management page, so it is plain state rather than a separate host window.
export interface AutoRefillPickerConfig {
  mode: AutoRefillAddRefillFilamentsMode;
  to?: AutoRefillAddRefillFilamentsSlotRef;
  baselineSlots?: AutoRefillAddRefillFilamentsSlotRef[];
}

// The picker slot list comes from an MQTT round-trip, so C++ answers the
// `query` request immediately and pushes the outcome as a report later. This
// watchdog only covers a lost report (e.g. a closed host page); the device
// timeout itself is enforced by the C++ control token.
const QUERY_WATCHDOG_MS = 8000;

// A write-back is only settled when the printer acknowledges it, which arrives
// as a report. This watchdog only covers a lost report; the device timeout
// itself is enforced by the C++ control token.
const EDIT_WATCHDOG_MS = 8000;

const FRIENDLY_SEND_FAILURE = 'Failed to send the command to the printer. Please check the printer connection and try again.';
const FRIENDLY_TIMEOUT = 'The printer did not respond in time. Please try again.';
const FRIENDLY_INVALID_FILAMENT_INFO = 'Failed to get valid filament information. Please refresh and try again.';
const FRIENDLY_SLOT_UNAVAILABLE = 'The selected filament slot is currently unavailable. Please check the slot status and try again.';
const FRIENDLY_SWITCH_OFF = 'Auto Refill is turned off. Turn it on and try again.';
const FRIENDLY_GROUP_STALE = 'The refill group information is out of date. Please reopen Auto Refill and try again.';
const FRIENDLY_GROUP_UPDATE_FAILED = 'Failed to update the refill group. Please try again.';
const FRIENDLY_GENERIC_FAILURE = 'The operation could not be completed. Please try again.';

const PASS_THROUGH_ERROR_KEYS = new Set<string>([
  'Invalid refill group target.',
  'Failed to get filament information.',
  'Failed to open refill filament dialog.',
  'Failed to create the refill group.',
  'Auto-refill state failed',
  'Auto-refill command failed',
  'Add refill filaments state failed',
  'Add refill filaments failed',
  'Reset refill group failed',
  'Remove refill slot failed',
  'Enable auto refill failed',
  'Move refill slot failed',
  'The printer did not respond to the refill command.',
]);

// `object` instead of `Record<string, unknown>`: named interfaces (e.g. the
// slot ref) have no implicit index signature and would be rejected.
function makeBody(submod: string, action: string, payload?: object) {
  return { module: MODULE, submod, action, payload: payload ?? {} };
}

// Extract the theme field stamped onto the first-paint responses of both hosts.
function pickTheme(payload: unknown): 'dark' | 'light' | null {
  if (!payload || typeof payload !== 'object') return null;
  const value = (payload as { theme?: unknown }).theme;
  return value === 'dark' || value === 'light' ? value : null;
}

function isRefillInfoBody(body: unknown): body is BridgeResponseBody<unknown> {
  return !!body
    && typeof body === 'object'
    && (body as { module?: unknown }).module === MODULE;
}

// Reports that only signal progress (a scheduled prompt, a cancellation) carry
// a payload without `extruders`. Applying those would wipe the group list.
function isStatePayload(payload: unknown): payload is AutoRefillRefillInfoViewModel {
  return !!payload
    && typeof payload === 'object'
    && Array.isArray((payload as { extruders?: unknown }).extruders);
}

function normalizeState(payload: AutoRefillRefillInfoViewModel): AutoRefillRefillInfoViewModel {
  return {
    ...mockAutoRefillRefillInfoState,
    ...payload,
    extruders: Array.isArray(payload.extruders) ? payload.extruders : [],
  };
}

function normalizeRefillErrorMessage(message: string | null | undefined): string {
  const trimmed = message?.trim();
  if (!trimmed) return FRIENDLY_GENERIC_FAILURE;
  if (PASS_THROUGH_ERROR_KEYS.has(trimmed)) return trimmed;

  const lower = trimmed.toLowerCase();
  if (lower.includes('publish failed')
    || lower.includes('network')
    || lower.includes('offline')
    || lower.includes('disconnect')
    || lower.includes('refused')
    || lower.includes('unreachable')) {
    return FRIENDLY_SEND_FAILURE;
  }
  if (lower.includes('no reply')
    || lower.includes('did not respond')
    || lower.includes('timeout')
    || lower.includes('timed out')) {
    return FRIENDLY_TIMEOUT;
  }
  if (lower.includes('request parse failed')
    || lower.includes('reply is invalid')
    || lower.includes('failed to append remaining slots')) {
    return FRIENDLY_INVALID_FILAMENT_INFO;
  }
  if (lower.includes('slot cannot be operated')
    || lower.includes('cannot operate an empty slot')) {
    return FRIENDLY_SLOT_UNAVAILABLE;
  }
  if (lower.includes('auto-refill switch is off')) {
    return FRIENDLY_SWITCH_OFF;
  }
  if (lower.includes('invalid pos')
    || lower.includes('missing group_tag')
    || lower.includes('invalid slot')) {
    return FRIENDLY_GROUP_STALE;
  }
  if (lower.includes('failed to configure group head or target chain')) {
    return FRIENDLY_GROUP_UPDATE_FAILED;
  }
  return FRIENDLY_GENERIC_FAILURE;
}

function readMockErrorPreview(): string | null {
  if (!import.meta.env.DEV) return null;
  const query = window.location.hash.split('?')[1];
  const token = new URLSearchParams(query ?? '').get('mock_error');
  switch (token) {
    case 'publish_failed': return 'publish failed';
    case 'query_timeout': return 'refill query got no reply';
    case 'query_invalid': return 'refill query reply is invalid';
    case 'slot_unavailable': return 'slot cannot be operated';
    case 'switch_off': return 'auto-refill switch is off';
    case 'group_stale': return 'invalid pos, to_pos, or group_tag';
    case 'group_update_failed': return 'failed to configure group head or target chain';
    default: return null;
  }
}

// The hook pulls state, mirrors C++ reports, and publishes the refill
// write-backs (add / remove / move / reset).
export function useAutoRefillRefillInfoBridge() {
  const request = useDeviceBridge();
  const setTheme = useStore((s) => s.filament.setTheme);
  const [state, setState] = useState<AutoRefillRefillInfoViewModel>(mockAutoRefillRefillInfoState);
  const [error, setError] = useState('');
  const stateKeyRef = useRef(JSON.stringify(mockAutoRefillRefillInfoState));
  const [addRefillFilaments, setAddRefillFilaments] = useState<AutoRefillAddRefillFilamentsViewModel | null>(null);
  const [pickerConfig, setPickerConfig] = useState<AutoRefillPickerConfig | null>(null);
  const [queryingAddRefillFilaments, setQueryingAddRefillFilaments] = useState(false);
  const [confirmingAddRefillFilaments, setConfirmingAddRefillFilaments] = useState(false);
  const queryWatchdogRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const suppressedWarningCodesRef = useRef<Set<number>>(new Set());
  // True between publishing a write-back and its device acknowledgement, so
  // the page can show the "updating" state instead of a half-applied group.
  const [applyingRefillEdit, setApplyingRefillEdit] = useState(false);
  const [toasts, setToasts] = useState<RefillToast[]>([]);
  const toastSeqRef = useRef(0);
  const editWatchdogRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  // Success text of the in-flight write-back, chosen before publishing since
  // the report itself cannot tell a slot removal from a group removal.
  const pendingEditToastRef = useRef<string | null>(null);
  const setFriendlyError = useCallback((message: string | null | undefined) => {
    setError(normalizeRefillErrorMessage(message));
  }, []);

  const pushToast = useCallback((text: string) => {
    toastSeqRef.current += 1;
    const id = toastSeqRef.current;
    setToasts((prev) => [...prev, { id, text }]);
  }, []);

  const dismissToast = useCallback((id: number) => {
    setToasts((prev) => prev.filter((toast) => toast.id !== id));
  }, []);

  const stopEditWatchdog = useCallback(() => {
    if (editWatchdogRef.current === null) return;
    clearTimeout(editWatchdogRef.current);
    editWatchdogRef.current = null;
  }, []);

  // Marks a write-back as in flight. `successText` is shown as a toast once the
  // printer acknowledges it.
  const beginRefillEdit = useCallback((successText: string) => {
    stopEditWatchdog();
    pendingEditToastRef.current = successText;
    setApplyingRefillEdit(true);
    setError('');
    editWatchdogRef.current = setTimeout(() => {
      editWatchdogRef.current = null;
      pendingEditToastRef.current = null;
      setApplyingRefillEdit(false);
      setFriendlyError('The printer did not respond to the refill command.');
    }, EDIT_WATCHDOG_MS);
  }, [setFriendlyError, stopEditWatchdog]);

  const endRefillEdit = useCallback(() => {
    stopEditWatchdog();
    pendingEditToastRef.current = null;
    setApplyingRefillEdit(false);
  }, [stopEditWatchdog]);

  useEffect(() => stopEditWatchdog, [stopEditWatchdog]);

  const applyState = useCallback((payload: AutoRefillRefillInfoViewModel) => {
    const next = normalizeState(payload);
    const stateKey = JSON.stringify(next);
    if (stateKey === stateKeyRef.current) return;
    stateKeyRef.current = stateKey;
    setState(next);
  }, []);

  const refresh = useCallback(async () => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<AutoRefillRefillInfoViewModel>>(
      makeBody('state', 'get'),
      5000,
    );
    if (!resp.ok) {
      setFriendlyError(resp.error);
      return;
    }
    if (resp.value.error_code !== 0) {
      setFriendlyError(resp.value.message ?? 'Auto-refill state failed');
      return;
    }
    if (resp.value.payload) {
      const theme = pickTheme(resp.value.payload);
      if (theme) setTheme(theme);
      applyState(resp.value.payload);
      setError('');
    }
  }, [applyState, request, setFriendlyError, setTheme]);

  useEffect(() => {
    void refresh();
  }, [refresh]);

  // Acknowledge to C++ once a real (session_seq > 0) state has been committed
  // to the DOM, so the dialog can defer its first show until the freshly
  // pushed machine data is on screen (no stale-then-fresh flicker on open).
  const ackedSeqRef = useRef(-1);
  useEffect(() => {
    const seq = state.session_seq;
    if (seq > 0 && seq !== ackedSeqRef.current) {
      ackedSeqRef.current = seq;
      void request<ReturnType<typeof makeBody>, BridgeResponseBody<AutoRefillRefillInfoViewModel>>(
        makeBody('action', 'rendered', { session_seq: seq }),
        2000,
      );
    }
  }, [state.session_seq, request]);

  const stopQueryWatchdog = useCallback(() => {
    if (queryWatchdogRef.current === null) return;
    clearTimeout(queryWatchdogRef.current);
    queryWatchdogRef.current = null;
  }, []);

  // Any query failure leaves the picker and surfaces the reason on the
  // management page.
  const failQuery = useCallback((message: string) => {
    stopQueryWatchdog();
    setQueryingAddRefillFilaments(false);
    setAddRefillFilaments(null);
    setFriendlyError(message);
  }, [setFriendlyError, stopQueryWatchdog]);

  useEffect(() => stopQueryWatchdog, [stopQueryWatchdog]);

  const openAddRefillDialog = useCallback(async (
    action: 'open_add_group' | 'open_add_filament',
    payload?: AutoRefillAddRefillFilamentsSlotRef,
  ): Promise<boolean> => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<unknown>>(
      makeBody('action', action, payload),
      5000,
    );
    if (!resp.ok || resp.value.error_code !== 0) {
      setFriendlyError(resp.ok ? (resp.value.message ?? 'Failed to open refill filament dialog.') : resp.error);
      return false;
    }
    setError('');
    return true;
  }, [request, setFriendlyError]);

  useEffect(() => {
    const handler = (e: Event) => {
      const detail = (e as CustomEvent).detail;
      const body = detail?.body;
      if (!isRefillInfoBody(body) || detail.head?.type !== 'report') return;

      if (body.submod === 'add_refill_filaments') {
        if (body.action === 'query_result') {
          stopQueryWatchdog();
          setQueryingAddRefillFilaments(false);
          if (body.payload) {
            setAddRefillFilaments(body.payload as AutoRefillAddRefillFilamentsViewModel);
            setError('');
          }
        } else if (body.action === 'query_failed') {
          failQuery(body.message ?? 'Failed to get filament information.');
        } else if (body.action === 'confirm_result') {
          stopQueryWatchdog();
          setQueryingAddRefillFilaments(false);
          setConfirmingAddRefillFilaments(false);
          setAddRefillFilaments(null);
          // Leaves the picker: the dialog goes back to the group list.
          setPickerConfig(null);
          setError('');
          if (isStatePayload(body.payload)) {
            applyState(body.payload);
          }
        } else if (body.action === 'confirm_failed') {
          stopQueryWatchdog();
          setQueryingAddRefillFilaments(false);
          setConfirmingAddRefillFilaments(false);
          setFriendlyError(body.message ?? 'Failed to create the refill group.');
        }
        return;
      }

      // The reset prompt runs outside the bridge request, so its outcome
      // arrives here rather than as a response.
      if (body.submod === 'action' && body.action === 'reset_started') {
        const toast = (body.payload as { toast?: string } | undefined)?.toast;
        beginRefillEdit(toast ?? 'Reset successfully');
        return;
      }
      if (body.submod === 'action' && body.action === 'reset_cancelled') {
        endRefillEdit();
        return;
      }

      // The removal prompt runs outside the bridge request, so its outcome
      // arrives here rather than as a response.
      if (body.submod === 'action' && body.action === 'remove_started') {
        const toast = (body.payload as { toast?: string } | undefined)?.toast;
        beginRefillEdit(toast ?? 'Removed successfully');
        return;
      }
      if (body.submod === 'action' && body.action === 'remove_cancelled') {
        endRefillEdit();
        return;
      }

      // Acknowledgement of a write-back: leave the "updating" state and, on
      // success, surface the toast queued when the command was published.
      const isEditAck = body.submod === 'action'
        && (body.action === 'remove' || body.action === 'reset' || body.action === 'edit');
      if (isEditAck) {
        const successText = pendingEditToastRef.current;
        endRefillEdit();
        if (body.error_code === 0 && successText) pushToast(successText);
      }

      if (body.error_code !== 0) {
        setFriendlyError(body.message ?? 'Auto-refill command failed');
        return;
      }
      if (isStatePayload(body.payload)) {
        applyState(body.payload);
        setError('');
      }
    };

    document.addEventListener('cpp:device', handler);
    return () => document.removeEventListener('cpp:device', handler);
  }, [applyState, beginRefillEdit, endRefillEdit, failQuery, pushToast, setFriendlyError, stopQueryWatchdog]);

  // Opens the picker: publishes the device query and shows the loading state
  // until the `query_result` / `query_failed` report lands.
  const queryAddRefillFilaments = useCallback(async () => {
    stopQueryWatchdog();
    setError('');
    setAddRefillFilaments(null);
    setQueryingAddRefillFilaments(true);
    queryWatchdogRef.current = setTimeout(
      () => failQuery('Failed to get filament information.'),
      QUERY_WATCHDOG_MS,
    );

    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<unknown>>(
      makeBody('add_refill_filaments', 'query'),
      5000,
    );
    if (!resp.ok) {
      failQuery(resp.error);
      return;
    }
    if (resp.value.error_code !== 0) {
      failQuery(resp.value.message ?? 'Failed to get filament information.');
    }
    // Success only means the command was published; keep waiting for the report.
  }, [failQuery, request, stopQueryWatchdog]);

  // Picking the first slot of the group narrows the remaining candidates on
  // the device side, so it needs a QueryNext round-trip rather than a local
  // rebuild. Same loading state and reports as the initial query.
  const queryNextAddRefillFilaments = useCallback(async (
    slots: AutoRefillAddRefillFilamentsSlotRef[],
    to: AutoRefillAddRefillFilamentsSlotRef,
    mode: AutoRefillAddRefillFilamentsMode = 'create_group',
  ) => {
    stopQueryWatchdog();
    setError('');
    setQueryingAddRefillFilaments(true);
    queryWatchdogRef.current = setTimeout(
      () => failQuery('Failed to get filament information.'),
      QUERY_WATCHDOG_MS,
    );

    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<unknown>>(
      makeBody('add_refill_filaments', 'query_next', { mode, slots, to }),
      5000,
    );
    if (!resp.ok) {
      failQuery(resp.error);
      return;
    }
    if (resp.value.error_code !== 0) {
      failQuery(resp.value.message ?? 'Failed to get filament information.');
    }
  }, [failQuery, request, stopQueryWatchdog]);

  // Re-asks C++ for the picker state after a selection change. The device
  // query result is cached on the C++ side, so this stays a plain request.
  const updateAddRefillFilaments = useCallback(async (
    slots: AutoRefillAddRefillFilamentsSlotRef[] = [],
    mode: AutoRefillAddRefillFilamentsMode = 'create_group',
  ) => {
    const resp = await request<
      ReturnType<typeof makeBody>,
      BridgeResponseBody<AutoRefillAddRefillFilamentsViewModel>
    >(
      makeBody('add_refill_filaments', 'get', { mode, slots }),
      5000,
    );
    if (!resp.ok) {
      setFriendlyError(resp.error);
      return;
    }
    if (resp.value.error_code !== 0 || !resp.value.payload) {
      setFriendlyError(resp.value.message ?? 'Add refill filaments state failed');
      return;
    }
    setError('');
    const theme = pickTheme(resp.value.payload);
    if (theme) setTheme(theme);
    setAddRefillFilaments(resp.value.payload);
  }, [request, setFriendlyError, setTheme]);

  // Enters the picker in place of the group list. `create_group` starts from
  // the unconstrained Query; `add_to_group` anchors the QueryNext on the slot
  // the group is grown from.
  const openPicker = useCallback((config: AutoRefillPickerConfig) => {
    suppressedWarningCodesRef.current.clear();
    setPickerConfig(config);
    if (config.mode === 'add_to_group' && config.to) {
      void queryNextAddRefillFilaments(
        config.baselineSlots?.length ? config.baselineSlots : [config.to],
        config.to,
        config.mode,
      );
    } else {
      void queryAddRefillFilaments();
    }
  }, [queryAddRefillFilaments, queryNextAddRefillFilaments]);

  const closeAddRefillFilaments = useCallback(() => {
    suppressedWarningCodesRef.current.clear();
    stopQueryWatchdog();
    setQueryingAddRefillFilaments(false);
    setConfirmingAddRefillFilaments(false);
    setAddRefillFilaments(null);
    setPickerConfig(null);
    void request(makeBody('add_refill_filaments', 'cancel'), 2000);
  }, [request, stopQueryWatchdog]);

  // Debug-only: the parsed Query / QueryNext reply plus the schemas C++ built
  // from it, so a wrong card can be traced back to the device payload.
  const fetchDebugSnapshot = useCallback(async (): Promise<unknown> => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<unknown>>(
      makeBody('debug', 'snapshot'),
      5000,
    );
    if (!resp.ok) return { error: resp.error };
    if (resp.value.error_code !== 0) return { error: resp.value.message ?? 'debug snapshot failed' };
    return resp.value.payload ?? null;
  }, [request]);

  // Follow-up button of a block-code dialog. The page owns the dialog (the
  // copy is in the payload); the host only performs the jump.
  const followUpAddRefillBlock = useCallback(async (
    blockCode: number,
    slot: AutoRefillAddRefillFilamentsSlotRef,
    displayIndex: number,
  ) => {
    await request(
      makeBody('add_refill_filaments', 'follow_up', {
        block_code: blockCode,
        ams_id: slot.ams_id,
        slot_id: slot.slot_id,
        display_index: displayIndex,
      }),
      5000,
    );
  }, [request]);

  // Persists a "Don't show again" tick. The suppression is applied by C++ on
  // the next build, which stops marking the slot as a warning.
  const dismissAddRefillWarning = useCallback(async (blockCode: number) => {
    suppressedWarningCodesRef.current.add(blockCode);
    await request(makeBody('add_refill_filaments', 'dismiss_warning', { block_code: blockCode }), 5000);
  }, [request]);

  const isAddRefillWarningSuppressed = useCallback(async (blockCode: number): Promise<boolean> => {
    if (suppressedWarningCodesRef.current.has(blockCode)) return true;
    const resp = await request<
      ReturnType<typeof makeBody>,
      BridgeResponseBody<{ suppressed?: boolean }>
    >(
      makeBody('add_refill_filaments', 'check_warning_suppressed', { block_code: blockCode }),
      5000,
    );
    if (!resp.ok || resp.value.error_code !== 0) return false;
    const suppressed = Boolean(resp.value.payload?.suppressed);
    if (suppressed) suppressedWarningCodesRef.current.add(blockCode);
    return suppressed;
  }, [request]);

  const confirmAddRefillFilaments = useCallback(async (
    slots: AutoRefillAddRefillFilamentsSlotRef[],
    mode: AutoRefillAddRefillFilamentsMode = 'create_group',
    to?: AutoRefillAddRefillFilamentsSlotRef,
  ): Promise<boolean> => {
    setConfirmingAddRefillFilaments(true);
    const resp = await request<
      ReturnType<typeof makeBody>,
      BridgeResponseBody<AutoRefillAddRefillFilamentsViewModel>
    >(
      makeBody('add_refill_filaments', 'confirm', { mode, slots, to }),
      5000,
    );
    if (!resp.ok) {
      setConfirmingAddRefillFilaments(false);
      setFriendlyError(resp.error);
      return false;
    }
    if (resp.value.error_code !== 0) {
      setConfirmingAddRefillFilaments(false);
      setFriendlyError(resp.value.message ?? 'Add refill filaments failed');
      return false;
    }
    setError('');
    return true;
  }, [request, setFriendlyError]);

  // Publishes a write-back and keeps the "updating" state until the printer
  // acknowledges it. Resolving `true` only means the command went out.
  const publishRefillEdit = useCallback(async (
    action: 'reset' | 'remove' | 'edit',
    payload: object,
    successText: string,
    failureText: string,
  ): Promise<boolean> => {
    beginRefillEdit(successText);
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<AutoRefillRefillInfoViewModel>>(
      makeBody('action', action, payload),
      5000,
    );
    if (!resp.ok) {
      endRefillEdit();
      setFriendlyError(resp.error);
      return false;
    }
    if (resp.value.error_code !== 0) {
      endRefillEdit();
      setFriendlyError(resp.value.message ?? failureText);
      return false;
    }
    return true;
  }, [beginRefillEdit, endRefillEdit, request, setFriendlyError]);

  // A reset drops every manually added member, so it is confirmed first. Same
  // shape as removeSlot: C++ owns the copy and publishes once confirmed, and
  // the reply here only means the prompt was scheduled.
  const resetGroup = useCallback(async (groupTag: string): Promise<boolean> => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<AutoRefillRefillInfoViewModel>>(
      makeBody('action', 'confirm_reset', { group_tag: groupTag }),
      5000,
    );
    if (!resp.ok) {
      setFriendlyError(resp.error);
      return false;
    }
    if (resp.value.error_code !== 0) {
      setFriendlyError(resp.value.message ?? 'Reset refill group failed');
      return false;
    }
    setError('');
    return true;
  }, [request, setFriendlyError]);

  // Asks the host to prompt for confirmation. C++ owns the copy, decides
  // whether the group itself disappears, and publishes the removal once
  // confirmed. The reply here only means the prompt was scheduled: the
  // decision arrives as a `remove_started` / `remove_cancelled` report because
  // the native dialog cannot block the bridge request.
  const removeSlot = useCallback(async (
    slot: { ams_id: number; slot_id: number },
  ): Promise<boolean> => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<AutoRefillRefillInfoViewModel>>(
      makeBody('action', 'confirm_remove', { ams_id: slot.ams_id, slot_id: slot.slot_id }),
      5000,
    );
    if (!resp.ok) {
      setFriendlyError(resp.error);
      return false;
    }
    if (resp.value.error_code !== 0) {
      setFriendlyError(resp.value.message ?? 'Remove refill slot failed');
      return false;
    }
    setError('');
    return true;
  }, [request, setFriendlyError]);

  const enableAutoRefill = useCallback(async (): Promise<boolean> => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<AutoRefillRefillInfoViewModel>>(
      makeBody('action', 'enable_auto_refill'),
      5000,
    );
    if (!resp.ok) {
      setFriendlyError(resp.error);
      return false;
    }
    if (resp.value.error_code !== 0) {
      setFriendlyError(resp.value.message ?? 'Enable auto refill failed');
      return false;
    }
    if (isStatePayload(resp.value.payload)) applyState(resp.value.payload);
    setError('');
    return true;
  }, [applyState, request, setFriendlyError]);

  useEffect(() => {
    const mockError = readMockErrorPreview();
    if (!mockError) return;
    const timer = window.setTimeout(() => setFriendlyError(mockError), 0);
    return () => window.clearTimeout(timer);
  });

  const moveSlot = useCallback((
    slot: { ams_id: number; slot_id: number },
    member: { ams_id: number; slot_id: number },
    asHead: boolean,
  ): Promise<boolean> => publishRefillEdit(
    'edit',
    {
      ams_id: slot.ams_id,
      slot_id: slot.slot_id,
      to_ams_id: member.ams_id,
      to_slot_id: member.slot_id,
      as_head: asHead,
    },
    'Moved successfully',
    'Move refill slot failed',
  ), [publishRefillEdit]);

  return {
    state,
    error,
    refresh,
    openAddRefillDialog,
    pickerConfig,
    openPicker,
    addRefillFilaments,
    queryingAddRefillFilaments,
    confirmingAddRefillFilaments,
    queryAddRefillFilaments,
    queryNextAddRefillFilaments,
    updateAddRefillFilaments,
    closeAddRefillFilaments,
    fetchDebugSnapshot,
    followUpAddRefillBlock,
    dismissAddRefillWarning,
    isAddRefillWarningSuppressed,
    confirmAddRefillFilaments,
    resetGroup,
    removeSlot,
    enableAutoRefill,
    moveSlot,
    applyingRefillEdit,
    toasts,
    pushToast,
    dismissToast,
  };
}
