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
  // True between publishing a write-back and its device acknowledgement, so
  // the page can show the "updating" state instead of a half-applied group.
  const [applyingRefillEdit, setApplyingRefillEdit] = useState(false);
  const [toasts, setToasts] = useState<RefillToast[]>([]);
  const toastSeqRef = useRef(0);
  const editWatchdogRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  // Success text of the in-flight write-back, chosen before publishing since
  // the report itself cannot tell a slot removal from a group removal.
  const pendingEditToastRef = useRef<string | null>(null);

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
      setError('The printer did not respond to the refill command.');
    }, EDIT_WATCHDOG_MS);
  }, [stopEditWatchdog]);

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
      setError(resp.error);
      return;
    }
    if (resp.value.error_code !== 0) {
      setError(resp.value.message ?? 'Auto-refill state failed');
      return;
    }
    if (resp.value.payload) {
      const theme = pickTheme(resp.value.payload);
      if (theme) setTheme(theme);
      applyState(resp.value.payload);
      setError('');
    }
  }, [applyState, request, setTheme]);

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
    setError(message);
  }, [stopQueryWatchdog]);

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
      setError(resp.ok ? (resp.value.message ?? 'Failed to open refill filament dialog.') : resp.error);
      return false;
    }
    setError('');
    return true;
  }, [request]);

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
          setError(body.message ?? 'Failed to create the refill group.');
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
        setError(body.message ?? 'Auto-refill command failed');
        return;
      }
      if (isStatePayload(body.payload)) {
        applyState(body.payload);
        setError('');
      }
    };

    document.addEventListener('cpp:device', handler);
    return () => document.removeEventListener('cpp:device', handler);
  }, [applyState, beginRefillEdit, endRefillEdit, failQuery, pushToast, stopQueryWatchdog]);

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
      setError(resp.error);
      return;
    }
    if (resp.value.error_code !== 0 || !resp.value.payload) {
      setError(resp.value.message ?? 'Add refill filaments state failed');
      return;
    }
    setError('');
    const theme = pickTheme(resp.value.payload);
    if (theme) setTheme(theme);
    setAddRefillFilaments(resp.value.payload);
  }, [request, setTheme]);

  // Enters the picker in place of the group list. `create_group` starts from
  // the unconstrained Query; `add_to_group` anchors the QueryNext on the slot
  // the group is grown from.
  const openPicker = useCallback((config: AutoRefillPickerConfig) => {
    setPickerConfig(config);
    if (config.mode === 'add_to_group' && config.to) {
      void queryNextAddRefillFilaments([config.to], config.to, config.mode);
    } else {
      void queryAddRefillFilaments();
    }
  }, [queryAddRefillFilaments, queryNextAddRefillFilaments]);

  const closeAddRefillFilaments = useCallback(() => {
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
    await request(makeBody('add_refill_filaments', 'dismiss_warning', { block_code: blockCode }), 5000);
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
      setError(resp.error);
      return false;
    }
    if (resp.value.error_code !== 0) {
      setConfirmingAddRefillFilaments(false);
      setError(resp.value.message ?? 'Add refill filaments failed');
      return false;
    }
    setError('');
    return true;
  }, [request]);

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
      setError(resp.error);
      return false;
    }
    if (resp.value.error_code !== 0) {
      endRefillEdit();
      setError(resp.value.message ?? failureText);
      return false;
    }
    return true;
  }, [beginRefillEdit, endRefillEdit, request]);

  // A reset drops every manually added member, so it is confirmed first. Same
  // shape as removeSlot: C++ owns the copy and publishes once confirmed, and
  // the reply here only means the prompt was scheduled.
  const resetGroup = useCallback(async (groupTag: string): Promise<boolean> => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<AutoRefillRefillInfoViewModel>>(
      makeBody('action', 'confirm_reset', { group_tag: groupTag }),
      5000,
    );
    if (!resp.ok) {
      setError(resp.error);
      return false;
    }
    if (resp.value.error_code !== 0) {
      setError(resp.value.message ?? 'Reset refill group failed');
      return false;
    }
    setError('');
    return true;
  }, [request]);

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
      setError(resp.error);
      return false;
    }
    if (resp.value.error_code !== 0) {
      setError(resp.value.message ?? 'Remove refill slot failed');
      return false;
    }
    setError('');
    return true;
  }, [request]);

  const openAmsSettings = useCallback(async (): Promise<boolean> => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<AutoRefillRefillInfoViewModel>>(
      makeBody('action', 'open_ams_settings'),
      5000,
    );
    if (!resp.ok) {
      setError(resp.error);
      return false;
    }
    if (resp.value.error_code !== 0) {
      setError(resp.value.message ?? 'Open AMS settings failed');
      return false;
    }
    setError('');
    return true;
  }, [request]);

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
    confirmAddRefillFilaments,
    resetGroup,
    removeSlot,
    openAmsSettings,
    moveSlot,
    applyingRefillEdit,
    toasts,
    pushToast,
    dismissToast,
  };
}
