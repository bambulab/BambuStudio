import { useCallback, useEffect, useRef } from 'react';
import { useTranslation } from 'react-i18next';
import { useDeviceBridge } from '../../hooks/Bridge';
import useStore from '../../store/AppStore';
import type {
  BridgeResponseBody,
  Spool,
  PresetOptions,
  MachineItem,
  AmsData,
  InitData,
  CloudSyncState,
  CloudFilamentConfig,
  CloudAutoPushSummary,
  DebugLogEntry,
} from './types';

function makeBody(submod: string, action: string, payload?: Record<string, unknown>) {
  return { module: 'filament', submod, action, payload: payload ?? {} };
}

function isFilamentManagerVisible() {
  const hashPath = window.location.hash.replace(/^#/, '').split('?')[0] || '';
  const route = hashPath.startsWith('/') ? hashPath : `/${hashPath}`;
  return route === '/filament_manager' && document.visibilityState !== 'hidden';
}

function normalizeCloudFilamentConfig(payload: unknown): CloudFilamentConfig {
  const raw = (payload && typeof payload === 'object') ? payload as Record<string, unknown> : {};
  const settings = Array.isArray(raw.filamentSettings)
    ? raw.filamentSettings as Array<Record<string, unknown>>
    : [];

  const vendors = new Set<string>();
  const seriesByVendorType = new Map<string, Set<string>>();

  settings.forEach((item) => {
    const vendor = typeof item.filamentVendor === 'string' ? item.filamentVendor : '';
    const type = typeof item.filamentType === 'string' ? item.filamentType : '';
    const name = typeof item.filamentName === 'string' ? item.filamentName : '';
    if (vendor) vendors.add(vendor);
    if (!vendor || !type) return;

    const key = `${vendor}\u0000${type}`;
    const bucket = seriesByVendorType.get(key) ?? new Set<string>();
    const trimmedName = name.trim();
    const prefix = `${type} `;
    if (trimmedName && trimmedName !== type && trimmedName.startsWith(prefix)) {
      bucket.add(trimmedName.slice(prefix.length).trim());
    }
    seriesByVendorType.set(key, bucket);
  });

  return {
    ...raw,
    vendors: raw.vendors instanceof Array
      ? raw.vendors.filter((n): n is string => typeof n === 'string' && n.length > 0)
      : [...vendors].sort(),
    types: raw.types instanceof Array
      ? raw.types as Array<{ vendor: string; type: string; series?: string[] }>
      : [...seriesByVendorType.entries()].map(([key, value]) => {
          const [vendor, type] = key.split('\u0000');
          return { vendor, type, series: [...value].filter(Boolean).sort() };
        }).sort((a, b) =>
          a.vendor.localeCompare(b.vendor) || a.type.localeCompare(b.type)
        ),
  };
}

export function useFilamentManagerBridge() {
  const { t } = useTranslation();
  const request = useDeviceBridge();
  const setSpools    = useStore((s) => s.filament.setSpools);
  const setPresets   = useStore((s) => s.filament.setPresets);
  const setMachines  = useStore((s) => s.filament.setMachines);
  const setAmsData   = useStore((s) => s.filament.setAmsData);
  const setSelectedMachineDevId = useStore((s) => s.filament.setSelectedMachineDevId);
  const setTheme     = useStore((s) => s.filament.setTheme);
  const setLoading   = useStore((s) => s.filament.setLoading);
  const setError     = useStore((s) => s.filament.setError);
  const setCloudSync   = useStore((s) => s.filament.setCloudSync);
  const setCloudConfig = useStore((s) => s.filament.setCloudConfig);
  const pushToast      = useStore((s) => s.filament.pushToast);
  const appendCloudSyncHistory = useStore((s) => s.filament.appendCloudSyncHistory);
  const setCloudAutoPushSummary = useStore((s) => s.filament.setCloudAutoPushSummary);
  const setDebugEnabled = useStore((s) => s.filament.setDebugEnabled);
  const appendDebugLog  = useStore((s) => s.filament.appendDebugLog);

  // STUDIO-17977: prefetch per-fila_id colour candidates for every spool
  // currently in the list so SpoolTable can render the official colour
  // name and BBL fila code on the first paint, without waiting for the
  // user to open the Add/Edit dialog. The cache key is the same fila_id
  // resolution AddEditDialog uses on save (cloud filamentId fallback to
  // preset setting_id), and we treat both "list with hits" and "empty
  // list" as cached (negative cache) so we never refetch the same id.
  const spoolsForPrefetch = useStore((s) => s.filament.spools);
  const candidatesByFilaId = useStore((s) => s.filament.candidatesByFilaId);
  const setColorCandidates = useStore((s) => s.filament.setColorCandidates);
  const candidatePrefetchInflight = useRef<Set<string>>(new Set());

  // STUDIO-17956: coalesce per-spool push_done toasts into a single batch
  // toast. Adding N spools in quick succession used to spam N identical
  // "Filament synced to cloud." toasts (one per push_done). We now accumulate
  // the counts and either (a) flush on the trailing pull_done that follows a
  // batch or (b) fall back to a 1s timeout if no pull_done arrives (e.g.
  // offline). The per-op history entries are still appended one-by-one so
  // Sync History keeps its fine-grained audit trail.
  const pushDoneAggRef = useRef<{
    timer: ReturnType<typeof setTimeout> | null;
    counts: { create: number; update: number; delete: number; other: number };
  }>({ timer: null, counts: { create: 0, update: 0, delete: 0, other: 0 } });

  const flushPushDoneAgg = useCallback(() => {
    const agg = pushDoneAggRef.current;
    if (agg.timer) { clearTimeout(agg.timer); agg.timer = null; }
    const { create, update, delete: del, other } = agg.counts;
    const total = create + update + del + other;
    if (total === 0) return;
    agg.counts = { create: 0, update: 0, delete: 0, other: 0 };

    let text: string;
    let op = '';
    if (total === 1) {
      if (create)      { text = t('Filament synced to cloud.');           op = 'create'; }
      else if (update) { text = t('Filament change synced to cloud.');    op = 'update'; }
      else if (del)    { text = t('Filament deletion synced to cloud.');  op = 'delete'; }
      else               text = t('Filament synced to cloud.');
    } else if (create > 0 && update === 0 && del === 0 && other === 0) {
      text = t('Synced {{count}} new filaments to cloud.', { count: create });
      op = 'create';
    } else if (update > 0 && create === 0 && del === 0 && other === 0) {
      text = t('Synced {{count}} filament changes to cloud.', { count: update });
      op = 'update';
    } else if (del > 0 && create === 0 && update === 0 && other === 0) {
      text = t('Synced {{count}} filament deletions to cloud.', { count: del });
      op = 'delete';
    } else {
      // Mixed ops in one batch — rare, but stay informative.
      text = t('Synced {{count}} filament changes to cloud.', { count: total });
    }
    pushToast({ level: 'info', text, op });
  }, [t, pushToast]);

  useEffect(() => {
    const handler = (e: Event) => {
      const detail = (e as CustomEvent).detail;
      if (!detail?.body) return;
      const body = detail.body;
      if (body.module !== 'filament' || detail.head?.type !== 'report') return;
      if (!isFilamentManagerVisible()) return;

      if (body.submod === 'spool') {
        const spools = body.payload as Spool[] | undefined;
        if (spools) setSpools(spools);
        return;
      }

      if (body.submod === 'debug' && body.action === 'log') {
        const payload = body.payload as Omit<DebugLogEntry, 'id'> | undefined;
        if (payload) appendDebugLog(payload);
        return;
      }

      if (body.submod === 'sync') {
        const payload = body.payload as Record<string, unknown>;
        if (body.action === 'state') {
          setCloudSync(payload as unknown as CloudSyncState);
        } else if (body.action === 'pull_done') {
          const spools = payload.spools as Spool[] | undefined;
          if (spools) setSpools(spools);
          const st = payload.state as CloudSyncState | undefined;
          if (st) setCloudSync(st);
          // STUDIO-17956: a pull_done fired by the dispatcher right after a
          // batch of push_create ops signals "the batch is done". Flush any
          // accumulated push_done counts now so the user sees the batch
          // summary immediately rather than waiting on the 1s timeout.
          flushPushDoneAgg();
          const added = (payload.added as number) || 0;
          const updated = (payload.updated as number) || 0;
          pushToast({
            level: 'info',
            text: t('Cloud sync: +{{added}} added, {{updated}} up-to-date', { added, updated }),
            op: 'pull',
          });
          appendCloudSyncHistory({
            ts: Date.now(),
            kind: 'pull',
            op: '',
            status: 'ok',
            summary: t('Pulled from cloud: +{{added}} added, {{updated}} up-to-date', { added, updated }),
            detail: { added, updated },
          });
        } else if (body.action === 'push_done') {
          // F4.5 / F4.6: surface an explicit success signal for create /
          // update / delete so users know the change reached the cloud.
          // STUDIO-17956: batch adds fire many push_done callbacks in quick
          // succession — coalesce their toasts into one. The per-op history
          // entry below is still appended individually so Sync History keeps
          // full detail.
          const st = payload.state as CloudSyncState | undefined;
          if (st) setCloudSync(st);
          const op = (payload.op as string) || '';
          const agg = pushDoneAggRef.current;
          const key = (op === 'create' || op === 'update' || op === 'delete') ? op : 'other';
          agg.counts[key] += 1;
          if (agg.timer) clearTimeout(agg.timer);
          agg.timer = setTimeout(() => flushPushDoneAgg(), 1000);
          const histSummary =
            op === 'create' ? t('Pushed to cloud: added a filament')
            : op === 'update' ? t('Pushed to cloud: updated a filament')
            : op === 'delete' ? t('Pushed to cloud: deleted a filament')
            : t('Pushed to cloud.');
          appendCloudSyncHistory({
            ts: Date.now(),
            kind: 'push',
            op: (op === 'create' || op === 'update' || op === 'delete') ? op : '',
            status: 'ok',
            summary: histSummary,
            detail: { op, spool_id: payload.spool_id as string | undefined },
          });
        } else if (body.action === 'push_failed') {
          const opLabel = (payload.op as string) || '';
          const errMsg  = (payload.message as string) || t('unknown error');
          pushToast({
            level: 'error',
            text: t('Filament operation failed. This feature currently requires a network connection.'),
            op: opLabel,
            spool_id: payload.spool_id as string,
          });
          appendCloudSyncHistory({
            ts: Date.now(),
            kind: 'push',
            op: (opLabel === 'create' || opLabel === 'update' || opLabel === 'delete') ? opLabel : '',
            status: 'error',
            summary: t('Cloud push failed: {{op}} — {{message}}', { op: opLabel, message: errMsg }),
            detail: { op: opLabel, message: errMsg, spool_id: payload.spool_id as string | undefined },
          });
        } else if (body.action === 'auto_push_summary') {
          // STUDIO-18155：AMS 自动同步 / 手动 push_all_now 完成后的决策摘要。
          // C++ 在 auto 模式下仅当 pushed > 0 才发；manual 模式总是发。
          const summary: CloudAutoPushSummary = {
            trigger:       (payload.trigger as 'auto' | 'manual') ?? 'auto',
            device_state:  (payload.device_state as 'busy' | 'idle' | 'manual') ?? 'idle',
            pushed:           Number(payload.pushed ?? 0),
            skipped_cooldown: Number(payload.skipped_cooldown ?? 0),
            skipped_no_diff:  Number(payload.skipped_no_diff ?? 0),
            skipped_no_rfid:  Number(payload.skipped_no_rfid ?? 0),
            skipped_no_total_nw: payload.skipped_no_total_nw != null
              ? Number(payload.skipped_no_total_nw) : undefined,
            observed_at: Date.now(),
          };
          setCloudAutoPushSummary(summary);
          // 手动按钮：toast 反馈"已入队 N 卷"。auto 路径不打 toast 避免噪声，
          // 由 CloudBadge tooltip 静默展示摘要即可。
          if (summary.trigger === 'manual') {
            const text = summary.pushed > 0
              ? t('Pushed {{n}} spools to cloud', { n: summary.pushed })
              : t('No spools to push (need RFID + total weight).');
            pushToast({
              level: summary.pushed > 0 ? 'info' : 'warn',
              text,
              op: 'update',
            });
          }
          appendCloudSyncHistory({
            ts: Date.now(),
            kind: 'push',
            op: 'update',
            status: 'ok',
            summary: summary.trigger === 'manual'
              ? t('Manual push: enqueued {{n}} spools', { n: summary.pushed })
              : t('AMS auto-push: pushed {{p}}, skipped {{s}}',
                  { p: summary.pushed,
                    s: summary.skipped_cooldown + summary.skipped_no_diff + summary.skipped_no_rfid }),
            detail: summary as unknown as Record<string, unknown>,
          });
        }
        // 注：AMS auto-push 失败统一走既有 dispatcher push_failed observer
        // → publish_push_failed → action='push_failed' 分支，不再独立加
        // auto_push_error 路径，避免失败语义重复。
        return;
      }

      if (body.submod === 'config' && body.action === 'fetched') {
        setCloudConfig(normalizeCloudFilamentConfig(body.payload));
        return;
      }

      // F4.7: DeviceManager::OnSelectedMachineChanged -> DeviceWebPage ->
      // FilamentManagerVM::ReportState forwards the new global machine to the
      // web layer. Refresh the store mirrors so any open AddEditDialog can
      // react to the change without re-opening.
      if (body.submod === 'machine' && body.action === 'selected_changed') {
        const payload = body.payload as Record<string, unknown>;
        const selDev = typeof payload.selected_dev_id === 'string' ? payload.selected_dev_id : '';
        const machList = payload.machines as { machines?: MachineItem[] } | undefined;
        if (machList && Array.isArray(machList.machines)) {
          setMachines(machList.machines);
        }
        const ams = payload.ams as AmsData | undefined;
        if (ams) setAmsData(ams);
        setSelectedMachineDevId(selDev);
        return;
      }
    };
    document.addEventListener('cpp:device', handler);
    return () => {
      document.removeEventListener('cpp:device', handler);
      // STUDIO-17956: drop any pending batch-push timer so an unmount
      // mid-batch doesn't fire a toast from a stale closure.
      const agg = pushDoneAggRef.current;
      if (agg.timer) { clearTimeout(agg.timer); agg.timer = null; }
    };
  }, [setSpools, setCloudSync, setCloudConfig, pushToast, appendCloudSyncHistory, appendDebugLog, setMachines, setAmsData, setSelectedMachineDevId, setCloudAutoPushSummary, t, flushPushDoneAgg]);

  // STUDIO-17977: candidate prefetch — runs whenever the spool list changes.
  // Drives the list row tail (colorName + BBL fila code) without waiting on
  // the user to open AddEditDialog.  Each unique setting_id is queried once
  // (negative cache via empty array marks it as "queried, none"), and
  // in-flight ids tracked in `candidatePrefetchInflight` so a re-render in
  // the middle of the prefetch does not re-issue the same RPC.
  useEffect(() => {
    if (!Array.isArray(spoolsForPrefetch) || spoolsForPrefetch.length === 0) return;
    const ids = new Set<string>();
    for (const s of spoolsForPrefetch) {
      const id = (s.setting_id || '').trim();
      if (!id) continue;
      if (id in candidatesByFilaId) continue;
      if (candidatePrefetchInflight.current.has(id)) continue;
      ids.add(id);
    }
    if (ids.size === 0) return;
    let cancelled = false;
    (async () => {
      for (const id of ids) {
        if (cancelled) return;
        candidatePrefetchInflight.current.add(id);
        const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
          makeBody('colors', 'query_for_id', { fila_id: id }),
        );
        candidatePrefetchInflight.current.delete(id);
        if (cancelled) return;
        // STUDIO-17977: never clobber an already-populated cache entry with
        // an empty list. Two effects can race: AddEditDialog opens and
        // queues `loadCandidates(filaId)` (lands a populated list), and the
        // prefetch effect queues another query for the same fila_id which
        // can come back later with an empty `candidates` array (e.g. C++
        // FilamentColorCodeQuery index missed the cloud_filamentId, or the
        // cloud catalogue was still warming up). Whichever response lands
        // second wins `setColorCandidates`, so a delayed empty response
        // would overwrite a good list and the row tail would lose its
        // colorName / fila code mid-flight.  Keep the existing list when
        // the new payload is empty; only refresh on non-empty data.
        const arr = (res.ok && res.value.error_code === 0)
          ? ((res.value.payload as { candidates?: unknown[] } | undefined)?.candidates ?? [])
          : [];
        const cur = useStore.getState().filament.candidatesByFilaId[id];
        if (Array.isArray(arr) && arr.length === 0
            && Array.isArray(cur) && cur.length > 0) {
          continue; // keep the populated list
        }
        setColorCandidates(id, Array.isArray(arr) ? (arr as never) : []);
      }
    })();
    return () => { cancelled = true; };
  }, [spoolsForPrefetch, candidatesByFilaId, setColorCandidates, request]);

  // ---- Init ----
  const init = useCallback(async () => {
    setLoading(true);
    setError(null);
    const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
      makeBody('init', 'init')
    );
    setLoading(false);
    if (res.ok && res.value.error_code === 0) {
      const data = res.value.payload as unknown as InitData;
      setSpools(data.spools ?? []);
      setPresets(data.presets ?? { vendors: [] });
      if (data.theme) setTheme(data.theme);
      // Apply login/sync state from init so page renders correctly immediately
      // without waiting for a separate fetchCloudSyncStatus() round-trip.
      if (data.cloud_sync) setCloudSync(data.cloud_sync);
      setDebugEnabled(Boolean(data.debug_enabled));
    } else {
      setError(res.ok ? res.value.message : res.error);
    }
  }, [request, setSpools, setPresets, setTheme, setLoading, setError, setCloudSync, setDebugEnabled]);

  // ---- Spool CRUD ----
  const fetchSpools = useCallback(async () => {
    setLoading(true);
    const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
      makeBody('spool', 'list')
    );
    setLoading(false);
    if (res.ok && res.value.error_code === 0) {
      setSpools(res.value.payload as unknown as Spool[]);
    }
  }, [request, setSpools, setLoading]);

  // F4.5 / F4.6: Surface cloud-sync intent right after a local mutation.  The
  // C++ dispatcher already enqueues a push when the user is logged in (see
  // FilamentManagerVM::HandleSpool), so this toast is purely UX feedback to
  // confirm the push was queued — or to warn that the change only lives
  // locally because no cloud session is active.  Success / failure of the
  // actual HTTP round-trip is surfaced separately via `sync/push_done` and
  // `sync/push_failed` Reports.
  const notifyCloudQueued = useCallback((op: 'create' | 'update' | 'delete', count: number) => {
    const state = useStore.getState().filament.cloudSync;
    const logged = !!state?.logged_in;
    if (logged) {
      const label =
        op === 'create' ? (count > 1
          ? t('Syncing {{count}} new filaments to cloud…', { count })
          : t('Syncing new filament to cloud…'))
        : op === 'update' ? t('Syncing filament change to cloud…')
        : (count > 1
          ? t('Syncing deletion of {{count}} filaments to cloud…', { count })
          : t('Syncing filament deletion to cloud…'));
      pushToast({ level: 'info', text: label, op });
    } else {
      pushToast({
        level: 'warn',
        text: t('Saved locally only — sign in to sync to cloud.'),
        op,
      });
    }
  }, [pushToast, t]);

  const notifyCloudWriteFailed = useCallback((op: 'create' | 'update' | 'delete') => {
    pushToast({
      level: 'error',
      text: t('Filament operation failed. This feature currently requires a network connection.'),
      op,
    });
  }, [pushToast, t]);

  const addSpool = useCallback(async (spool: Partial<Spool>) => {
    const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
      makeBody('spool', 'add', spool as Record<string, unknown>)
    );
    if (res.ok && res.value.error_code === 0) {
      setSpools(res.value.payload as unknown as Spool[]);
      notifyCloudQueued('create', 1);
    }
    if (!res.ok || res.value.error_code !== 0) notifyCloudWriteFailed('create');
    return res.ok && res.value.error_code === 0;
  }, [request, setSpools, notifyCloudQueued, notifyCloudWriteFailed]);

  const batchAddSpool = useCallback(async (spool: Partial<Spool>, quantity: number) => {
    const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
      makeBody('spool', 'batch_add', { spool, quantity } as unknown as Record<string, unknown>)
    );
    if (res.ok && res.value.error_code === 0) {
      setSpools(res.value.payload as unknown as Spool[]);
      notifyCloudQueued('create', quantity);
    }
    if (!res.ok || res.value.error_code !== 0) notifyCloudWriteFailed('create');
    return res.ok && res.value.error_code === 0;
  }, [request, setSpools, notifyCloudQueued, notifyCloudWriteFailed]);

  // STUDIO-18344: AMS multi-select batch path. Distinct payloads in
  // `creates[]` go through the create dispatcher; `updates[]` carry an
  // existing spool_id and go through the update dispatcher. The C++ side
  // returns the post-batch spool list on success and we route the toast
  // through `notifyCloudQueued` with the total combined count.
  //
  // Empty arrays are handled gracefully (no-op short-circuit). Mixed
  // outcomes (e.g. create succeeds but update fails on one row) still
  // return true here because the wire response only reports a global
  // status; individual per-row outcomes surface through the existing
  // per-op push_done / push_failed reports.
  const batchCreateSpools = useCallback(async (
    creates: Partial<Spool>[],
    updates: Partial<Spool>[],
  ) => {
    const totalCount = creates.length + updates.length;
    if (totalCount === 0) return true;
    const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
      makeBody('spool', 'batch_create', { creates, updates } as unknown as Record<string, unknown>)
    );
    if (res.ok && res.value.error_code === 0) {
      setSpools(res.value.payload as unknown as Spool[]);
      // The cloud queue accepts both buckets via the dispatcher's create
      // and update paths respectively, so the toast count is the union.
      // Calling notifyCloudQueued twice (one per op) would double-toast
      // for the same user action.
      notifyCloudQueued('create', totalCount);
    }
    if (!res.ok || res.value.error_code !== 0) notifyCloudWriteFailed('create');
    return res.ok && res.value.error_code === 0;
  }, [request, setSpools, notifyCloudQueued, notifyCloudWriteFailed]);

  const updateSpool = useCallback(async (spool: Partial<Spool>) => {
    const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
      makeBody('spool', 'update', spool as Record<string, unknown>)
    );
    if (res.ok && res.value.error_code === 0) {
      setSpools(res.value.payload as unknown as Spool[]);
      notifyCloudQueued('update', 1);
    }
    if (!res.ok || res.value.error_code !== 0) notifyCloudWriteFailed('update');
    return res.ok && res.value.error_code === 0;
  }, [request, setSpools, notifyCloudQueued, notifyCloudWriteFailed]);

  const removeSpool = useCallback(async (spoolId: string) => {
    const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
      makeBody('spool', 'remove', { spool_id: spoolId })
    );
    if (res.ok && res.value.error_code === 0) {
      setSpools(res.value.payload as unknown as Spool[]);
      notifyCloudQueued('delete', 1);
    }
    if (!res.ok || res.value.error_code !== 0) notifyCloudWriteFailed('delete');
    return res.ok && res.value.error_code === 0;
  }, [request, setSpools, notifyCloudQueued, notifyCloudWriteFailed]);

  const batchRemoveSpool = useCallback(async (spoolIds: string[]) => {
    const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
      makeBody('spool', 'batch_remove', { spool_ids: spoolIds })
    );
    if (res.ok && res.value.error_code === 0) {
      setSpools(res.value.payload as unknown as Spool[]);
      notifyCloudQueued('delete', spoolIds.length);
    }
    if (!res.ok || res.value.error_code !== 0) notifyCloudWriteFailed('delete');
    return res.ok && res.value.error_code === 0;
  }, [request, setSpools, notifyCloudQueued, notifyCloudWriteFailed]);

  const markEmpty = useCallback(async (spoolId: string) => {
    const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
      makeBody('spool', 'mark_empty', { spool_id: spoolId })
    );
    if (res.ok && res.value.error_code === 0) {
      setSpools(res.value.payload as unknown as Spool[]);
    }
    return res.ok && res.value.error_code === 0;
  }, [request, setSpools]);

  const toggleFavorite = useCallback(async (spoolId: string) => {
    const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
      makeBody('spool', 'toggle_favorite', { spool_id: spoolId })
    );
    if (res.ok && res.value.error_code === 0) {
      setSpools(res.value.payload as unknown as Spool[]);
    }
    return res.ok && res.value.error_code === 0;
  }, [request, setSpools]);

  const archiveSpool = useCallback(async (spoolId: string) => {
    const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
      makeBody('spool', 'archive', { spool_id: spoolId })
    );
    if (res.ok && res.value.error_code === 0) {
      setSpools(res.value.payload as unknown as Spool[]);
    }
    return res.ok && res.value.error_code === 0;
  }, [request, setSpools]);

  // ---- Preset / Machine / AMS ----
  const fetchPresets = useCallback(async () => {
    const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
      makeBody('preset', 'list')
    );
    if (res.ok && res.value.error_code === 0) {
      setPresets(res.value.payload as unknown as PresetOptions);
    }
  }, [request, setPresets]);

  const fetchMachines = useCallback(async () => {
    const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
      makeBody('machine', 'list')
    );
    if (res.ok && res.value.error_code === 0) {
      const payload = res.value.payload as unknown as { machines: MachineItem[]; selected_dev_id?: string };
      setMachines(payload.machines ?? []);
      // F4.7: keep the store mirror of Studio's selected machine fresh on
      // every machine list fetch so AMS-tab defaults stay consistent.
      if (typeof payload.selected_dev_id === 'string') {
        setSelectedMachineDevId(payload.selected_dev_id);
      }
      return payload.machines ?? [];
    }
    return [] as MachineItem[];
  }, [request, setMachines, setSelectedMachineDevId]);

  // Ask a specific (or the currently selected) printer to resend its full
  // state package. Used by the refresh button next to the Printer dropdown
  // in the "Read from AMS" dialog so the operator can force a fresh AMS
  // tray snapshot without waiting for the next spontaneous push. This is
  // deliberately read-only with respect to DeviceManager's selected
  // machine -- changing the selection still goes through ams/list with
  // switch_selected:true.
  const requestMachinePushall = useCallback(async (devId?: string) => {
    const params: Record<string, unknown> = {};
    if (devId) params.dev_id = devId;
    const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
      makeBody('machine', 'request_pushall', params)
    );
    return res.ok && res.value.error_code === 0;
  }, [request]);

  // `switchSelected`: pass true only when the caller represents an
  // explicit user-initiated machine switch; the C++ side will then call
  // DeviceManager::set_selected_machine. Poll refreshes, mirror effects
  // for external switches, and the initial open where the default
  // machine already matches Studio's global selection must pass false
  // (or omit it) so we don't flood the log with "set current printer"
  // requests on every poll tick.
  const fetchAmsData = useCallback(async (
    devId?: string,
    switchSelected: boolean = false,
  ) => {
    const params: Record<string, unknown> = {};
    if (devId) params.dev_id = devId;
    if (switchSelected) params.switch_selected = true;
    const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
      makeBody('ams', 'list', params)
    );
    if (res.ok && res.value.error_code === 0) {
      const data = res.value.payload as unknown as AmsData;
      setAmsData(data);
      // Only mirror the selected dev id into the store when we actually
      // asked C++ to switch; read-only snapshots must not race with
      // OnSelectedMachineChanged and clobber a pending update.
      if (switchSelected && data && typeof data.selected_dev_id === 'string' && data.selected_dev_id) {
        setSelectedMachineDevId(data.selected_dev_id);
      }
      return data;
    }
    return null;
  }, [request, setAmsData, setSelectedMachineDevId]);

  // ---- Cloud sync ----
  const fetchCloudSyncStatus = useCallback(async () => {
    const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
      makeBody('sync', 'status')
    );
    if (res.ok && res.value.error_code === 0) {
      setCloudSync(res.value.payload as unknown as CloudSyncState);
    }
  }, [request, setCloudSync]);

  const triggerCloudPull = useCallback(async () => {
    const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
      makeBody('sync', 'pull')
    );
    if (res.ok && res.value.error_code === 0) {
      setCloudSync(res.value.payload as unknown as CloudSyncState);
    }
    return res.ok && res.value.error_code === 0;
  }, [request, setCloudSync]);

  // STUDIO-18155：手动"推送本地到云端"。绕过 throttle，把所有"有 RFID +
  // 有整卷净重"的 spool 全部入 push 队列。立即返回 sync_state（is_syncing
  // 标志可能尚未升起，因为入队是同步的、push 本身是异步的）；真正的
  // 入队结果通过 sync/auto_push_summary ReportMsg 返回，由上面的 handler
  // 转 toast + history。
  const pushAllNow = useCallback(async () => {
    const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
      makeBody('sync', 'push_all_now')
    );
    if (res.ok && res.value.error_code === 0) {
      setCloudSync(res.value.payload as unknown as CloudSyncState);
      return true;
    }
    pushToast({
      level: 'error',
      text: t('Push to cloud failed: {{reason}}',
              { reason: res.ok ? res.value.message : res.error }),
      op: 'update',
    });
    return false;
  }, [request, setCloudSync, pushToast, t]);

  // ---- Cloud filament config ----
  const fetchCloudFilamentConfig = useCallback(async (options?: { force?: boolean }) => {
    const res = await request<ReturnType<typeof makeBody>, BridgeResponseBody>(
      makeBody('config', 'fetch', options?.force ? { force: true } : undefined)
    );
    // Immediate response carries the cached cfg when available; the async
    // fetch result arrives via a 'config/fetched' report handled by the
    // useEffect handler above.
    if (res.ok && res.value.error_code === 0 &&
        res.value.message === 'cached' && res.value.payload) {
      setCloudConfig(normalizeCloudFilamentConfig(res.value.payload));
    }
  }, [request, setCloudConfig]);

  return {
    init,
    fetchSpools,
    addSpool,
    batchAddSpool,
    batchCreateSpools,
    updateSpool,
    removeSpool,
    batchRemoveSpool,
    markEmpty,
    toggleFavorite,
    archiveSpool,
    fetchPresets,
    fetchMachines,
    requestMachinePushall,
    fetchAmsData,
    fetchCloudSyncStatus,
    triggerCloudPull,
    pushAllNow,
    fetchCloudFilamentConfig,
  };
}
