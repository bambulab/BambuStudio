// Spool icon SVG — matches the old fila_manager buildSpoolSvg
export function SpoolSvg({ color, size = 40 }: { color?: string; size?: number }) {
  const c = color || '#888';
  return (
    <svg width={size} height={size} viewBox="0 0 40 40" fill="none">
      <path d="M5 8C5 5.8 6.8 4 9 4h5v32H9C6.8 36 5 34.2 5 32V8z" fill={c} opacity={0.55} />
      <rect x="10" y="7" width="8" height="26" rx="1" fill={c} opacity={0.75} />
      <rect x="14" y="4" width="5" height="32" rx="1" fill={c} opacity={0.45} />
      <ellipse cx="24" cy="20" rx="12" ry="16" fill={c} />
      <ellipse cx="24" cy="20" rx="8" ry="10.5" fill={c} opacity={0.55} />
      <ellipse cx="25" cy="20" rx="3" ry="4" fill="#1a1a1a" opacity={0.85} />
      <ellipse cx="24.5" cy="19" rx="1.5" ry="2" fill="rgba(255,255,255,0.1)" />
    </svg>
  );
}
