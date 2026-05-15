// Spool icon SVG — matches the old fila_manager buildSpoolSvg
function isLightColor(color: string): boolean {
  const hex = color.trim().replace(/^#/, '').slice(0, 6);
  if (!/^[0-9a-fA-F]{6}$/.test(hex)) return false;
  const r = parseInt(hex.slice(0, 2), 16) / 255;
  const g = parseInt(hex.slice(2, 4), 16) / 255;
  const b = parseInt(hex.slice(4, 6), 16) / 255;
  return (0.2126 * r + 0.7152 * g + 0.0722 * b) > 0.86;
}

export function SpoolSvg({ color, size = 40 }: { color?: string; size?: number }) {
  const c = color || '#888';
  const lightColorStroke = isLightColor(c) ? '#8A8A8A' : 'none';
  return (
    <svg width={size} height={size} viewBox="0 0 40 40" fill="none">
      <path d="M5 8C5 5.8 6.8 4 9 4h5v32H9C6.8 36 5 34.2 5 32V8z" fill={c} opacity={0.55} stroke={lightColorStroke} strokeWidth="0.8" />
      <rect x="10" y="7" width="8" height="26" rx="1" fill={c} opacity={0.75} stroke={lightColorStroke} strokeWidth="0.8" />
      <rect x="14" y="4" width="5" height="32" rx="1" fill={c} opacity={0.45} stroke={lightColorStroke} strokeWidth="0.8" />
      <ellipse cx="24" cy="20" rx="12" ry="16" fill={c} stroke={lightColorStroke} strokeWidth="0.9" />
      <ellipse cx="24" cy="20" rx="8" ry="10.5" fill={c} opacity={0.55} stroke={lightColorStroke} strokeWidth="0.8" />
      <ellipse cx="25" cy="20" rx="3" ry="4" fill="#1a1a1a" opacity={0.85} />
      <ellipse cx="24.5" cy="19" rx="1.5" ry="2" fill="rgba(255,255,255,0.1)" />
    </svg>
  );
}
