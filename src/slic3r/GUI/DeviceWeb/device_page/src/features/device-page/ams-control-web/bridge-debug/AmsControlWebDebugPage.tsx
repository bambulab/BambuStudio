import { useCallback, useEffect, useMemo, useState } from 'react';
import { useDeviceBridge } from '../../../../hooks/Bridge';

const MODULE = 'device_page_ams_control_web';

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
    <section className="min-h-0 overflow-hidden rounded-lg border border-neutral-700 bg-neutral-950">
      <div className="flex items-center justify-between gap-2 border-b border-neutral-700 bg-neutral-900 px-3 py-2 text-xs font-semibold text-neutral-100">
        <span className="truncate">{title}</span>
        <CopyButton text={text} />
      </div>
      <pre className="m-0 max-h-[420px] overflow-auto whitespace-pre-wrap break-all p-3 text-[11px] leading-4 text-neutral-200">
        {text}
      </pre>
    </section>
  );
}

function topLevelBlocks(value: unknown): Array<{ title: string; value: unknown }> {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    return [{ title: 'cpp_state', value }];
  }

  return Object.entries(value as Record<string, unknown>).map(([title, blockValue]) => ({
    title,
    value: blockValue,
  }));
}

export function AmsControlWebDebugPage() {
  const request = useDeviceBridge();
  const [snapshot, setSnapshot] = useState<Snapshot>({});
  const [error, setError] = useState('');

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
    void loadSnapshot();
  }, [loadSnapshot]);

  const createdAt = snapshot.created_ts
    ? new Date(snapshot.created_ts).toLocaleString()
    : 'N/A';
  const refreshedAt = snapshot.dialog_snapshot_ts
    ? new Date(snapshot.dialog_snapshot_ts).toLocaleString()
    : 'N/A';
  const cppToWebState = snapshot.dialog_cpp_state ?? snapshot.cpp_state ?? null;
  const blocks = topLevelBlocks(cppToWebState);

  return (
    <div className="h-screen w-screen overflow-auto bg-neutral-950 p-4 text-neutral-100">
      <div className="mb-4 flex items-center justify-between gap-3">
        <div>
          <h1 className="m-0 text-base font-semibold">AMS Control Web Bridge Debug</h1>
          <p className="m-0 mt-1 text-xs text-neutral-400">
            Open snapshot: {createdAt} · Dialog refresh: {refreshedAt}
          </p>
        </div>
        <div className="flex items-center gap-2">
          {error && (
            <div className="rounded-md border border-red-500/50 bg-red-500/10 px-3 py-2 text-xs text-red-200">
              {error}
            </div>
          )}
          <button
            className="rounded border border-neutral-600 bg-neutral-800 px-3 py-1 text-xs hover:bg-neutral-700"
            onClick={() => void loadSnapshot()}
          >
            Refresh
          </button>
        </div>
      </div>

      <div className="grid grid-cols-1 gap-4 xl:grid-cols-2">
        {blocks.map((block) => (
          <JsonBlock key={block.title} title={block.title} value={block.value} />
        ))}
      </div>
    </div>
  );
}
