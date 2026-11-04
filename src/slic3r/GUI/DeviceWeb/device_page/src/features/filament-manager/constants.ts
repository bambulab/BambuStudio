import type { Spool } from './types';

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

// Map Filament Manager Brand + Material Type onto Create Custom Filament
// Step 1 (vendor / type / serial). FM stores Material Type as a combined
// filamentName (`series`, e.g. "PLA Basic") plus a split `material_type`
// ("PLA"); the create dialog wants the short series ("Basic").
export function toCreateFilamentPrefill(
  brand?: string,
  materialType?: string,
  series?: string,
): { vendor: string; type: string; serial: string } {
  const vendor = (brand || '').trim();
  const type = (materialType || '').trim();
  const full = (series || '').trim() || type;
  if (!type) {
    const first = full.split(/\s+/).filter(Boolean)[0] || '';
    return { vendor, type: first, serial: first ? full.slice(first.length).trim() : full };
  }
  if (!full || full === type) return { vendor, type, serial: '' };
  if (full.startsWith(`${type} `)) return { vendor, type, serial: full.slice(type.length + 1).trim() };
  return { vendor, type, serial: full };
}

// Maps numeric ams_type (C++ DevAmsType enum) to product display names.
export const AMS_TYPE_NAMES: Record<number, string> = {
  0: 'External Spool',
  1: 'AMS',
  2: 'AMS Lite',
  3: 'AMS 2 Pro',
  4: 'AMS HT',
  5: 'AMS Lite',
  6: 'AMS',
  7: 'AMS',
};

export function formatSlotLocation(
  deviceName: string | undefined,
  amsType: number | undefined,
  slotId: string | undefined,
  t: (key: string, opts?: Record<string, unknown>) => string,
  trayLabel?: string,
  multiExternal = false,
): string | null {
  if (!deviceName) return null;
  const isExternal = amsType === 0;
  let amsTypeName: string | null = null;
  let slotLabel: string | null = null;
  if (isExternal) {
    slotLabel = (multiExternal && slotId != null && slotId !== '')
      ? t('External Spool {{n}}', { n: 256 - Number(slotId) })
      : t('External Spool');
  } else {
    amsTypeName = (amsType != null && amsType >= 0)
      ? (AMS_TYPE_NAMES[amsType] ?? `AMS(${amsType})`)
      : null;
    slotLabel = trayLabel
      ? trayLabel
      : (slotId != null && slotId !== ''
        ? t('Slot {{n}}', { n: Number(slotId) + 1 })
        : null);
  }
  return [deviceName, amsTypeName, slotLabel].filter(Boolean).join(' · ');
}

export function multiExternalDevIds(spools: Spool[]): Set<string> {
  const ids = new Set<string>();
  for (const s of spools) {
    if (s.in_printer === true && s.ams_type === 0 && s.dev_id && s.slot_id !== '255') {
      ids.add(s.dev_id);
    }
  }
  return ids;
}
