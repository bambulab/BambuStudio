import { useState } from 'react';

// Diagnostics affordance: Debug builds and the vite dev server only, never an
// internal or public Release package (same gate as the Connected Slots
// copy-report button).
function isDebugBuild(): boolean {
  return import.meta.env.DEV
    || Boolean((window as Window & { __debugBuild?: boolean }).__debugBuild);
}

async function writeClipboard(text: string): Promise<boolean> {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch {
    return false;// Best-effort only for a debug-only affordance
  }
}

// Shows the view-model JSON that C++ pushed for the view currently on screen,
// so the rendered UI can be compared against the schema it came from.
// `onFetchSnapshot` additionally pulls the parsed Query / QueryNext reply from
// the host, which the page itself never receives.
export function SchemaDebugOverlay({
  view,
  data,
  onFetchSnapshot,
}: {
  view: string;
  data: unknown;
  onFetchSnapshot?: () => Promise<unknown>;
}) {
  const [open, setOpen] = useState(false);
  const [copied, setCopied] = useState('');

  if (!isDebugBuild()) return null;

  const json = JSON.stringify(data, null, 2);

  const flash = (label: string) => {
    setCopied(label);
    setTimeout(() => setCopied(''), 1200);
  };

  const copyView = async () => {
    if (await writeClipboard(json)) flash('view');
  };

  // Host snapshot first, page state second: the two are captured at the same
  // moment, so a mismatch between them is the interesting part.
  const copyAll = async () => {
    if (!onFetchSnapshot) return;
    const snapshot = await onFetchSnapshot();
    const payload = JSON.stringify({ view, host: snapshot, web_state: data }, null, 2);
    if (await writeClipboard(payload)) flash('all');
  };

  // Just the device side of the round-trip: the raw Query / QueryNext reply and
  // what it parsed into, without the schemas built on top of them.
  const copyQuery = async () => {
    if (!onFetchSnapshot) return;
    const snapshot = await onFetchSnapshot();
    const host = (snapshot ?? {}) as { query_reply?: unknown; query_result?: unknown };
    const payload = JSON.stringify(
      { query_reply: host.query_reply ?? null, query_result: host.query_result ?? null },
      null,
      2,
    );
    if (await writeClipboard(payload)) flash('query');
  };

  return (
    <>
      <button
        type="button"
        className="refill-debug-button"
        data-testid="auto-refill-debug-button"
        title="Show the view-model JSON pushed to this page"
        onClick={() => setOpen((prev) => !prev)}
      >
        Debug
      </button>
      {open && (
        <div className="refill-debug-panel" data-testid="auto-refill-debug-panel">
          <div className="refill-debug-panel__head">
            <span className="refill-debug-panel__view">{view}</span>
            {onFetchSnapshot && (
              <>
                <button
                  type="button"
                  className="refill-debug-panel__action"
                  data-testid="auto-refill-debug-copy-all"
                  title="Copy the device reply, the parsed result and both built schemas"
                  onClick={() => void copyAll()}
                >
                  {copied === 'all' ? 'Copied' : 'Copy all'}
                </button>
                <button
                  type="button"
                  className="refill-debug-panel__action"
                  data-testid="auto-refill-debug-copy-query"
                  title="Copy only the raw Query/QueryNext reply and its parsed result"
                  onClick={() => void copyQuery()}
                >
                  {copied === 'query' ? 'Copied' : 'Copy Query'}
                </button>
              </>
            )}
            <button type="button" className="refill-debug-panel__action" onClick={() => void copyView()}>
              {copied === 'view' ? 'Copied' : 'Copy'}
            </button>
            <button type="button" className="refill-debug-panel__action" onClick={() => setOpen(false)}>
              Close
            </button>
          </div>
          <pre className="refill-debug-panel__json">{json}</pre>
        </div>
      )}
    </>
  );
}
