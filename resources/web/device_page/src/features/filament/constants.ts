// Bambu preset color palette
export const BAMBU_COLORS = [
  '#000000','#333333','#555555','#808080','#BBBBBB','#FFFFFF',
  '#FF0000','#CC3333','#FF6666','#FF3300','#FF6600','#FF9900',
  '#FFCC00','#FFFF00','#CCFF00','#66FF00','#00CC00','#009933',
  '#006633','#00CCCC','#0099CC','#0066CC','#0033CC','#0000FF',
  '#3300CC','#6600CC','#9900CC','#CC00CC','#FF00FF','#FF66CC',
  '#FF99CC','#CC6666','#996633','#663300','#CCCC99','#99CC66',
  '#66CCCC','#6699FF','#CC99FF','#FFCC99',
];

// i18n source strings — localized via en/zh_CN locale files. Previously used
// placeholder keys ("entry_manual" / "entry_ams_sync" / "entry_rfid") which
// had no translations, so the raw key leaked through to the UI (F3.1).
export const ENTRY_METHOD_LABELS: Record<string, string> = {
  manual: 'Manual Entry',
  ams_sync: 'AMS Sync',
  rfid: 'RFID',
};

export const PAGE_SIZES = [20, 50, 100] as const;
export const DEFAULT_PAGE_SIZE = 50;

// 统一的"耗材类型名"显示：material_type + series。
// 本地 series 常见为短名（"Basic"/"95A"/"HF"），而云端 filamentName 同步回来的
// 值是完整名（"PLA Basic"），所以这里同时处理两种形态：短 series 时前缀 type，
// 完整 series 时识别出 type 前缀避免重复（历史的 "ABS ABS" / "PLA PLA Basic"）。
// 例：("ABS", "ABS")        -> "ABS"
//     ("PLA", "Basic")      -> "PLA Basic"
//     ("PLA", "PLA Basic")  -> "PLA Basic"（series 已含 type 前缀，不重复）
//     ("TPU", "95A")        -> "TPU 95A"
//     ("PLA", "")           -> "PLA"
//     ("", "Something")     -> "Something"
export function formatTypeSeries(materialType?: string, series?: string): string {
  const t = (materialType || '').trim();
  const s = (series || '').trim();
  if (!t) return s;
  if (!s || s === t) return t;
  if (t && (s === t || s.startsWith(t + ' '))) return s;
  return `${t} ${s}`.trim();
}

// 统一的耗材显示名：filamentVendor + 空格 + filamentName。
// 本地 series 字段直接对齐云端 filamentName 语义（完整名如 "PLA Basic" /
// "PETG HF" / "PLA-S Support For PLA/PETG"），无需再与 material_type 拼接；
// series 缺失时回退到仅显示 material_type（如 series="" type="ABS"）。
// 例：brand="Bambu Lab", series="PLA Basic"           -> "Bambu Lab PLA Basic"
//     brand="Bambu Lab", material_type="ABS"           -> "Bambu Lab ABS"
export function formatSpoolDisplayName(s: { brand?: string; material_type?: string; series?: string }): string {
  const brand = (s.brand || '').trim();
  const series = (s.series || '').trim();
  const type = (s.material_type || '').trim();
  const namePart = series || type;
  return [brand, namePart].filter(Boolean).join(' ');
}
