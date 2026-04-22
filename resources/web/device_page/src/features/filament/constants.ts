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

// 统一的"耗材类型名"显示：material_type + series（相同时去重，避免 "ABS ABS"）。
// 例：("ABS", "ABS")       -> "ABS"
//     ("PLA", "Basic")      -> "PLA Basic"
//     ("PLA", "")           -> "PLA"
//     ("", "Something")     -> "Something"
export function formatTypeSeries(materialType?: string, series?: string): string {
  const t = (materialType || '').trim();
  const s = (series || '').trim();
  if (!t) return s;
  if (!s || s === t) return t;
  return `${t} ${s}`.trim();
}

// 统一的耗材显示名：品牌 + 耗材类型名（series 与 material_type 相同时去重）
// 例：brand="Bambu Lab", material_type="ABS", series="ABS" -> "Bambu Lab ABS"
//     brand="Bambu Lab", material_type="PLA", series="Basic" -> "Bambu Lab PLA Basic"
export function formatSpoolDisplayName(s: { brand?: string; material_type?: string; series?: string }): string {
  const brand = (s.brand || '').trim();
  const typePart = formatTypeSeries(s.material_type, s.series);
  return [brand, typePart].filter(Boolean).join(' ');
}
