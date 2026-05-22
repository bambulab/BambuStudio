import { useCallback, useEffect, useMemo, useState } from 'react';
import { useDeviceBridge } from '../../../../hooks/Bridge';

const MODULE = 'device_page_ams_filament_hotend';

type Snapshot = {
  created_ts?: number;
  dialog_snapshot_ts?: number;
  cpp_state?: unknown;
  web_state?: unknown;
  web_trace?: unknown[];
  cpp_trace?: unknown[];
  dialog_cpp_state?: unknown;
  dialog_cpp_trace?: unknown[];
};

type BridgeResponseBody<T> = {
  error_code: number;
  message?: string;
  payload?: T;
};

function makeBody(submod: string, action: string, payload: Record<string, unknown> = {}) {
  return { module: MODULE, submod, action, payload };
}

// Copy `text` to the clipboard. WebView2 supports navigator.clipboard for
// localhost/file origins; the textarea + execCommand path is a Safari/older
// WebView fallback when the modern API is unavailable.
async function copyTextToClipboard(text: string): Promise<boolean> {
  if (typeof navigator !== 'undefined' && navigator.clipboard?.writeText) {
    try {
      await navigator.clipboard.writeText(text);
      return true;
    } catch {
      // fall through to the textarea fallback below
    }
  }
  if (typeof document === 'undefined') return false;
  const ta = document.createElement('textarea');
  ta.value = text;
  ta.setAttribute('readonly', '');
  ta.style.position = 'fixed';
  ta.style.opacity = '0';
  document.body.appendChild(ta);
  ta.select();
  let ok = false;
  try {
    ok = document.execCommand('copy');
  } catch {
    ok = false;
  }
  document.body.removeChild(ta);
  return ok;
}

function CopyButton({ text }: { text: string }) {
  const [copied, setCopied] = useState(false);
  const handleClick = useCallback(async () => {
    const ok = await copyTextToClipboard(text);
    setCopied(ok);
    if (ok) setTimeout(() => setCopied(false), 1200);
  }, [text]);
  return (
    <button
      type="button"
      className="rounded border border-neutral-600 bg-neutral-800 px-2 py-0.5 text-[11px] text-neutral-100 hover:bg-neutral-700"
      onClick={() => void handleClick()}
    >
      {copied ? 'Copied' : 'Copy'}
    </button>
  );
}

function JsonBlock({ title, value }: { title: string; value: unknown }) {
  const text = useMemo(() => JSON.stringify(value ?? null, null, 2), [value]);
  return (
    <section className="min-h-0 rounded-lg border border-neutral-700 bg-neutral-950 overflow-hidden">
      <div className="flex items-center justify-between gap-2 px-3 py-2 text-xs font-semibold text-neutral-100 border-b border-neutral-700 bg-neutral-900">
        <span className="truncate">{title}</span>
        <CopyButton text={text} />
      </div>
      <pre className="m-0 max-h-[280px] overflow-auto p-3 text-[11px] leading-4 text-neutral-200 whitespace-pre-wrap break-all">
        {text}
      </pre>
    </section>
  );
}

export function AmsBridgeDebugPage() {
  const request = useDeviceBridge();
  const [snapshot, setSnapshot] = useState<Snapshot>({});
  const [error, setError] = useState('');
  const [requestText, setRequestText] = useState(() => JSON.stringify(makeBody('state', 'get'), null, 2));
  const [sending, setSending] = useState(false);
  const [manualResponses, setManualResponses] = useState<Array<{ ts: number; request: unknown; response: unknown }>>([]);

  const loadSnapshot = useCallback(async () => {
    const resp = await request<ReturnType<typeof makeBody>, BridgeResponseBody<Snapshot>>(
      makeBody('debug', 'snapshot'),
      5000,
    );
    if (!resp.ok) {
      setError(resp.error);
      return;
    }
    if (resp.value.error_code !== 0) {
      setError(resp.value.message ?? 'Failed to load bridge snapshot');
      return;
    }
    setSnapshot(resp.value.payload ?? {});
    setError('');
  }, [request]);

  useEffect(() => {
    const load = async () => {
      await loadSnapshot();
    };
    void load();
  }, [loadSnapshot]);

  const runRequest = useCallback(async (body?: Record<string, unknown>) => {
    let reqBody: Record<string, unknown>;
    try {
      reqBody = body ?? JSON.parse(requestText);
    } catch (e) {
      setError(`Invalid request JSON: ${String(e)}`);
      return;
    }

    setSending(true);
    try {
      const resp = await request<Record<string, unknown>, unknown>(reqBody, 12000);
      const responseValue = resp.ok ? resp.value : { ok: false, error: resp.error };
      setManualResponses((prev) => [
        { ts: Date.now(), request: reqBody, response: responseValue },
        ...prev,
      ].slice(0, 20));
      await loadSnapshot();
      setError('');
    } finally {
      setSending(false);
    }
  }, [loadSnapshot, request, requestText]);

  const setPreset = (submod: string, action: string, payload: Record<string, unknown> = {}) => {
    setRequestText(JSON.stringify(makeBody(submod, action, payload), null, 2));
  };

  const createdAt = snapshot.created_ts
    ? new Date(snapshot.created_ts).toLocaleString()
    : 'N/A';
  const refreshedAt = snapshot.dialog_snapshot_ts
    ? new Date(snapshot.dialog_snapshot_ts).toLocaleString()
    : 'N/A';

  return (
    <div className="h-screen w-screen overflow-auto bg-neutral-950 text-neutral-100 p-4">
      <div className="mb-4 flex items-center justify-between gap-3">
        <div>
          <h1 className="m-0 text-base font-semibold">AMS Filament/Hotend Bridge Debug</h1>
          <p className="m-0 mt-1 text-xs text-neutral-400">
            Open snapshot: {createdAt} · Dialog refresh: {refreshedAt}
          </p>
        </div>
        {error && (
          <div className="rounded-md border border-red-500/50 bg-red-500/10 px-3 py-2 text-xs text-red-200">
            {error}
          </div>
        )}
      </div>

      <section className="mb-4 rounded-lg border border-neutral-700 bg-neutral-900 p-3">
        <div className="mb-3 flex items-center justify-between gap-3">
          <div>
            <div className="text-sm font-semibold">Trigger Request</div>
            <div className="text-xs text-neutral-400">Requests sent here use this dialog WebView's Bridge.</div>
          </div>
          <div className="flex items-center gap-2">
            <CopyButton text={requestText} />
            <button
              className="rounded border border-neutral-600 bg-neutral-800 px-3 py-1 text-xs hover:bg-neutral-700"
              onClick={() => void loadSnapshot()}
            >
              Refresh Snapshot
            </button>
            <button
              className="rounded border border-neutral-600 bg-neutral-800 px-3 py-1 text-xs hover:bg-neutral-700"
              onClick={() => setPreset('state', 'get')}
            >
              state/get
            </button>
            <button
              className="rounded border border-neutral-600 bg-neutral-800 px-3 py-1 text-xs hover:bg-neutral-700"
              onClick={() => setPreset('action', 'read_all_hotends')}
            >
              action/read_all_hotends
            </button>
            <button
              className="rounded border border-emerald-600 bg-emerald-900/40 px-3 py-1 text-xs text-emerald-100 hover:bg-emerald-800/60 disabled:opacity-60"
              disabled={sending}
              onClick={() => void runRequest()}
            >
              {sending ? 'Sending...' : 'Send'}
            </button>
          </div>
        </div>
        <textarea
          className="h-28 w-full rounded border border-neutral-700 bg-neutral-950 p-2 font-mono text-[11px] leading-4 text-neutral-100 outline-none"
          value={requestText}
          onChange={(e) => setRequestText(e.target.value)}
          spellCheck={false}
        />
      </section>

      <div className="grid grid-cols-2 gap-4">
        <JsonBlock title="Open-time C++ BuildState()" value={snapshot.cpp_state} />
        <JsonBlock title="Open-time Web displayed state" value={snapshot.web_state} />
        <JsonBlock title="Open-time Web request/response/report trace" value={snapshot.web_trace ?? []} />
        <JsonBlock title="Open-time C++ bridge request/response/report trace" value={snapshot.cpp_trace ?? []} />
        <JsonBlock title="Live dialog C++ BuildState() after refresh" value={snapshot.dialog_cpp_state} />
        <JsonBlock title="Live dialog C++ bridge trace after refresh" value={snapshot.dialog_cpp_trace ?? []} />
        <JsonBlock title="Manual request responses" value={manualResponses} />
      </div>
    </div>
  );
}
