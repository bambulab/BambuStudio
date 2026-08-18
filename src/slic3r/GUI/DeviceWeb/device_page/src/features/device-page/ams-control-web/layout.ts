import type { AmsExtArea, PanelView, PreviewItem, SlotView, UnitView } from './types';

// Columns to render, in draw order. A payload that carries no layout still has
// to show something, so everything falls into one column.
export function panelColumns(area: AmsExtArea): PanelView[] {
  const panels = area.layout.panels.filter((panel) => panel.visible);
  if (panels.length > 0) return panels;
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
