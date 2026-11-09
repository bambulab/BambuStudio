import { useState } from 'react';

interface DebugPanelProps {
  load: () => Promise<unknown>;
}

// Debug builds / vite dev only. The toggle floats in the empty left corner of
// the footer and never takes part in the layout.
export function DebugPanel({ load }: DebugPanelProps) {
  const [text, setText] = useState<string | null>(null);

  const toggle = async () => {
    if (text !== null) {
      setText(null);
      return;
    }
    setText('...');
    setText(JSON.stringify(await load(), null, 2));
  };

  return (
    <>
      <button type="button" className="fs-debug-toggle" onClick={() => void toggle()}>
        {text === null ? 'Debug' : 'Hide'}
      </button>
      {text !== null && <pre className="fs-debug-panel">{text}</pre>}
    </>
  );
}
