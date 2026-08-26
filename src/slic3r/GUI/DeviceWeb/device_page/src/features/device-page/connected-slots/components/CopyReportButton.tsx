import { useCallback, useState } from 'react';

// WebView2 supports navigator.clipboard on the localhost/file origins used by
// the device page; the textarea + execCommand path covers hosts where the
// modern API is missing or rejected without a permission grant.
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

export function CopyReportButton({ getReport }: { getReport: () => string }) {
  const [copied, setCopied] = useState(false);

  const handleClick = useCallback(async () => {
    const ok = await copyTextToClipboard(getReport());
    if (!ok) return;
    setCopied(true);
    setTimeout(() => setCopied(false), 1200);
  }, [getReport]);

  return (
    <button
      type="button"
      className="cs-copy-report"
      data-testid="connected-slots-copy-report"
      title="Copy the raw JSON pushed to Connected Slots"
      onClick={() => void handleClick()}
    >
      {copied ? 'Copied' : 'Copy Report'}
    </button>
  );
}
