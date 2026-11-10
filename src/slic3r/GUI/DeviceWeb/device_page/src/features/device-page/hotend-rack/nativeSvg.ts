// Mirrors BitmapCache::load_svg: in dark mode, light SVGs without a *_dark variant are
// recolored with this fixed map before rendering.
const NATIVE_DARK_REPLACES: ReadonlyArray<[string, string]> = [
  ['"#262E30"', '"#EFEFF0"'],
  ['"#323A3D"', '"#B3B3B5"'],
  ['"#808080"', '"#818183"'],
  ['"#CECECE"', '"#54545B"'],
  ['"#6B6B6B"', '"#818182"'],
  ['"#909090"', '"#FFFFFF"'],
  ['"#00FF00"', '"#FF0000"'],
  ['"#F1F1F1"', '"#36363B"'],
  ['"#DBDBDB"', '"#4A4A51"'],
];

// Yellow placeholder that the native rack swaps for the filament color.
const FILAMENT_PLACEHOLDER = /#FFF735/g;
const HEX_COLOR = /^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$/;

export function nativeSvgUrl(svg: string, dark: boolean, filamentColor = '') {
  let out = svg;
  if (dark) {
    for (const [from, to] of NATIVE_DARK_REPLACES) out = out.split(from).join(to);
  }
  if (HEX_COLOR.test(filamentColor)) out = out.replace(FILAMENT_PLACEHOLDER, filamentColor);
  return `data:image/svg+xml;charset=utf-8,${encodeURIComponent(out)}`;
}
