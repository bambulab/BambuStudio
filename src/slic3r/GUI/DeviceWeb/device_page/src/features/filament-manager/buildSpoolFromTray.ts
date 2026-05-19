// STUDIO-18344: shared AMS tray -> FilamentSpool mapping.
//
// This module factors out the logic that previously lived inside
// `selectAmsSlot` and the `handleSubmit` AMS branch in `AddEditDialog.tsx`, so
// the single-select form path and the new multi-select batch path can agree
// on every field they put on the wire.
//
// Two outputs:
//   - `resolved`: the per-tray field values used to seed the form when the
//     dialog is in single-select mode (brand, material type, series, colour,
//     weights, lock flags). This preserves the current "user can review and
//     edit before saving" UX for a single slot.
//   - `payload`: a self-sufficient `Partial<Spool>` that mirrors what the
//     existing AMS submit branch builds. Multi-select skips the form and
//     pushes this directly to the bridge, so the saved spool stays byte
//     identical to a single-slot save the user did not edit.
//
// `existingSpoolId` is the spool whose `tag_uid` already matches the tray.
// Single-select uses it to flip add -> update on save; multi-select uses it
// to route the payload into the `updates[]` bucket of the new
// `spool/batch_create` bridge action (with default semantics: overwrite the
// existing record, per the STUDIO-18344 UX confirmation).

import type { AmsTray, AmsUnit, PresetVendor, Spool } from './types';
import { BAMBU_COLORS, formatTypeSeries } from './constants';
import { canonicalizeHex } from './colors';

const MAX_NET_WEIGHT_GRAMS = 999_999_999;

const normalizeColorCode = canonicalizeHex;

function isValidTagUid(tagUid: string): boolean {
  return tagUid.length > 0 && /[^0]/.test(tagUid);
}

function normalizePresetFilamentName(
  name: string | undefined,
  vendor: string,
  type: string,
  series: string,
): string {
  let value = (name || '').trim().replace(/\s+@.*$/, '');
  const fallback = formatTypeSeries(type, series);
  if (!value) return fallback;

  const aliases = [vendor, vendor.replace(/\s+/g, '')];
  if (vendor === 'Bambu Lab') aliases.push('Bambu');
  for (const alias of aliases.filter(Boolean)) {
    if (value === alias) return fallback;
    if (value.startsWith(`${alias} `)) {
      value = value.substring(alias.length + 1).trim();
      break;
    }
  }
  return value || fallback;
}

// Mirrors AddEditDialog.getTrayCurrentNetWeight so callers that want the
// "current grams" reading (rather than initial * remain%) can pull from one
// place. Kept here so consumers do not need to import private helpers from
// AddEditDialog.
export function getTrayCurrentNetWeight(tray: AmsTray): number {
  const init = parseInt(String(tray.weight ?? '0'), 10) || 0;
  const remain = typeof tray.remain === 'number' ? tray.remain : 0;
  if (init <= 0) return 0;
  if (remain <= 0) return init;
  return Math.round(init * remain / 100);
}

export interface TrayResolution {
  brandName: string;
  typeName: string;
  seriesName: string;
  filamentName: string;
  sanitizedColor: string;
  traySanitizedColors: string[];
  trayNetInit: number;
  currentNet: number;
  matchedSettingId: string;
}

export interface BuildSpoolFromTrayInput {
  tray: AmsTray;
  unit: AmsUnit;
  devId: string;
  presets: PresetVendor[];
  spools: Spool[];
}

export interface BuildSpoolFromTrayResult {
  resolved: TrayResolution;
  payload: Partial<Spool>;
  existingSpoolId: string;
  isMultiHex: boolean;
}

// Resolve (brand, material_type, series, filament display name) from the
// AMS tray's setting_id with a sub_brands / fila_type fallback. Pure: no
// side effects, no React state, safe to call inside hot render paths.
//
// Strategy mirrors the previous `selectAmsSlot`:
//   1. Prefer the preset reverse-lookup by setting_id / filament_id. Skip
//      `is_user` items so AMS RFID never resolves to a user-derived alias
//      (STUDIO-18110 / STUDIO-18117).
//   2. Fall back to tray.sub_brands for the brand name and tray.fila_type
//      string matching for the material type, peeling off the longest
//      candidate prefix so the remainder becomes the series.
export function resolveTrayPreset(
  tray: AmsTray,
  presets: PresetVendor[],
): TrayResolution {
  let brandName = '';
  let typeName = '';
  let seriesName = '';
  let filamentName = '';

  const settingId = tray.setting_id || '';
  if (settingId) {
    outer: for (const vendor of presets) {
      for (const tp of vendor.types) {
        const hit = tp.items?.find(
          (it) => !it.is_user && (it.setting_id === settingId || it.filament_id === settingId),
        );
        if (hit) {
          brandName = vendor.name;
          typeName = tp.name;
          seriesName = hit.series || '';
          filamentName = normalizePresetFilamentName(hit.name, vendor.name, tp.name, seriesName);
          break outer;
        }
      }
    }
  }

  if (!brandName) brandName = tray.sub_brands || '';
  if (!typeName) {
    const fullType = tray.fila_type || '';
    const typeCandidates = new Set<string>();
    const vendorsForMatch = brandName ? presets.filter((v) => v.name === brandName) : presets;
    vendorsForMatch.forEach((v) => v.types.forEach((tp) => { if (tp.name) typeCandidates.add(tp.name); }));
    let matchedType = '';
    let seriesRemainder = '';
    for (const cand of typeCandidates) {
      if (fullType === cand) { matchedType = cand; seriesRemainder = ''; break; }
      if (fullType.startsWith(cand + ' ') && cand.length > matchedType.length) {
        matchedType = cand;
        seriesRemainder = fullType.substring(cand.length + 1).trim();
      } else if (fullType.startsWith(cand) && cand.length > matchedType.length) {
        matchedType = cand;
        seriesRemainder = fullType.substring(cand.length).trim();
      }
    }
    typeName = matchedType;
    if (!seriesName) seriesName = seriesRemainder;
  }

  const sanitizedColor = normalizeColorCode(tray.color);
  const traySanitizedColors = Array.isArray(tray.colors)
    ? tray.colors
        .map((c) => (typeof c === 'string' ? c : ''))
        .filter((c) => !!c)
        .map((c) => normalizeColorCode(c))
        .filter((c) => !!c)
    : [];

  const trayNetInit = parseInt(String(tray.weight ?? '0'), 10) || 0;
  const currentNet = getTrayCurrentNetWeight(tray);

  return {
    brandName,
    typeName,
    seriesName,
    filamentName,
    sanitizedColor,
    traySanitizedColors,
    trayNetInit,
    currentNet,
    matchedSettingId: settingId,
  };
}

// Build a fully-formed `Partial<Spool>` payload from a single AMS tray. This
// is the multi-select "no review" path: every field comes straight from the
// tray as resolved above, no form intervention. The output shape is the
// same as the AMS branch of `AddEditDialog.handleSubmit`, so a multi-select
// save and a single-select save of an unedited slot land identical records
// on the cloud.
export function buildSpoolFromTray(input: BuildSpoolFromTrayInput): BuildSpoolFromTrayResult {
  const { tray, unit, devId, presets, spools } = input;
  const resolved = resolveTrayPreset(tray, presets);

  const trayTagUid = tray.tag_uid || '';
  const existingSpool = isValidTagUid(trayTagUid)
    ? spools.find((sp) => (sp.tag_uid || '') === trayTagUid)
    : undefined;
  const existingSpoolId = existingSpool?.spool_id || '';

  // Both weights default to the tray's reported net weight; the cloud schema
  // never round-trips spool_weight so we always pin it to 0.
  const initialWeight = resolved.trayNetInit > 0
    ? Math.min(resolved.trayNetInit, MAX_NET_WEIGHT_GRAMS)
    : 1000;
  const currentWeight = resolved.currentNet > 0
    ? Math.min(resolved.currentNet, initialWeight)
    : initialWeight;
  const remainPct = initialWeight > 0
    ? Math.min(100, Math.max(0, Math.round(currentWeight * 100 / initialWeight)))
    : 0;

  const filamentDisplay = resolved.filamentName
    || formatTypeSeries(resolved.typeName, resolved.seriesName);

  // Honour the same multi-hex / single-hex invariant the form uses: only
  // populate `colors[]` when the tray genuinely reports more than one hex.
  // A one-element list would later be downgraded to single in the resolver
  // effect, so emitting it on the wire just bloats the payload and trips
  // the cloud-side dedupe (STUDIO-17977 / STUDIO-18340).
  const isMultiHex = resolved.traySanitizedColors.length > 1;
  const colors = isMultiHex ? resolved.traySanitizedColors : [];
  const colorType: 0 | 1 | 2 = isMultiHex
    ? (tray.color_type === 1 ? 1 : 0)
    : 2;

  const colorCode = resolved.sanitizedColor
    || (isMultiHex ? colors[0] : '')
    || '#888888';

  const diaRaw = tray.diameter;
  const diaNum = typeof diaRaw === 'number' ? diaRaw : parseFloat(String(diaRaw ?? '0'));

  const payload: Partial<Spool> = {
    brand: resolved.brandName,
    material_type: resolved.typeName,
    series: filamentDisplay,
    color_code: colorCode,
    color_name: tray.color_name || '',
    colors,
    color_type: colorType,
    initial_weight: initialWeight,
    spool_weight: 0,
    net_weight: currentWeight,
    remain_percent: remainPct,
    note: '',
    entry_method: 'ams_sync',
    tag_uid: trayTagUid,
    setting_id: resolved.matchedSettingId || tray.setting_id || '',
    bound_ams_id: unit.ams_id,
    bound_dev_id: devId,
  };

  if (diaNum > 0) payload.diameter = diaNum;

  return { resolved, payload, existingSpoolId, isMultiHex };
}

// Build the (creates, updates) tuple the new `spool/batch_create` bridge
// action expects. `updates[]` entries already carry the `spool_id` of the
// existing record so the C++ side can route them through the dispatcher's
// update path without an extra lookup.
export function partitionTraysForBatchCreate(
  results: BuildSpoolFromTrayResult[],
): { creates: Partial<Spool>[]; updates: Partial<Spool>[] } {
  const creates: Partial<Spool>[] = [];
  const updates: Partial<Spool>[] = [];
  for (const r of results) {
    if (r.existingSpoolId) {
      updates.push({ ...r.payload, spool_id: r.existingSpoolId });
    } else {
      creates.push(r.payload);
    }
  }
  return { creates, updates };
}

// Re-export so consumers (tests / future helpers) can match the form's
// preset-colour rule without duplicating the constant.
export { BAMBU_COLORS };
