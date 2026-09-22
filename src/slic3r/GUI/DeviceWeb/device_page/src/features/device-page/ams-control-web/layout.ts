import type { AmsExtArea, PanelView, PreviewItem, SlotView, UnitView } from './types';

// Columns to render, in draw order. left_right keeps both sides even when one
// has no AMS (CreateAmsDoubleNozzle still Shows the empty simplebook). A payload
// that carries no layout still has to show something, so everything falls into
// one column.
export function panelColumns(area: AmsExtArea): PanelView[] {
  const panels = area.layout.panels;
  if (area.layout.layout_style === 'left_right' && panels.length >= 2) return panels;
  const visible = panels.filter((panel) => panel.visible);
  if (visible.length > 0) return visible;
  return [
    {
      pos: 'single',
      visible: true,
      active_ams_id: area.units[0]?.ams_id ?? '',
      groups: [
        ...area.units.map((unit) => ({ ams_ids: [unit.ams_id] })),
        ...area.ext_slots.map((slot) => ({ ams_ids: [slot.ams_id] })),
      ],
    },
  ];
}

function columnAmsIds(panel: PanelView) {
  return new Set(panel.groups.flatMap((group) => group.ams_ids));
}

// A column pages through its cells one at a time, so the cell holding the open
// unit is the only one drawn. A cell shared by two single-slot units draws both.
function activeCellIds(panel: PanelView): string[] {
  const cell = panel.groups.find((group) => group.ams_ids.includes(panel.active_ams_id));
  return cell?.ams_ids ?? (panel.active_ams_id ? [panel.active_ams_id] : []);
}

export function columnUnits(panel: PanelView, units: UnitView[]): UnitView[] {
  return activeCellIds(panel).flatMap((amsId) => units.filter((unit) => unit.ams_id === amsId));
}

export function columnExtSlots(panel: PanelView, extSlots: SlotView[]): SlotView[] {
  const amsIds = new Set(activeCellIds(panel));
  return extSlots.filter((slot) => amsIds.has(slot.ams_id));
}

// The strip is how a column is switched, so it lists every cell, not just the open one.
export function columnPreviewItems(panel: PanelView, items: PreviewItem[]): PreviewItem[] {
  const amsIds = columnAmsIds(panel);
  return items.filter((item) => amsIds.has(item.ams_id));
}

// AMSControl::IsAmsMixed: generic AMS and AMS_LITE on the same single-nozzle machine.
export function isLiteMixedMachine(units: UnitView[]): boolean {
  const hasLite = units.some((unit) => unit.ams_type_name === 'AMS_LITE');
  const hasOther = units.some((unit) => unit.ams_type_name !== 'AMS_LITE');
  return hasLite && hasOther;
}

// The open mixed page draws AMS + Lite (+ Ext) together, the way the classic
// panel keeps three simplebooks visible.
export function isLiteMixedBodyRow(units: UnitView[]): boolean {
  return isLiteMixedMachine(units);
}

export function splitMixedPreview(items: PreviewItem[], extAmsIds: Set<string>) {
  const ams: PreviewItem[] = [];
  const lite: PreviewItem[] = [];
  const ext: PreviewItem[] = [];
  for (const item of items) {
    if (extAmsIds.has(item.ams_id) || item.ams_type_name === 'EXT_SPOOL') ext.push(item);
    else if (item.ams_type_name === 'AMS_LITE') lite.push(item);
    else ams.push(item);
  }
  return { ams, lite, ext };
}

// AMSControl::CreateAmsSingleNozzle hides the right strip unless that side has
// more than one chip. Dual-nozzle shows both strips whenever any AMS exists.
export function columnShowsPreviewStrip(panel: PanelView, items: PreviewItem[], dualNozzle: boolean) {
  // Dual-nozzle always shows both strips so an empty side still occupies its half.
  if (dualNozzle) return true;
  if (items.length === 0) return false;
  if (panel.pos === 'right') return items.length > 1;
  return true;
}
