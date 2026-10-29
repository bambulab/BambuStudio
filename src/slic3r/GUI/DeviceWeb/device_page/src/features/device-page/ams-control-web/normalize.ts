import type {
  AmsColorType,
  AmsControlActions,
  AmsControlDisplay,
  AmsControlWebViewModel,
  AmsExtArea,
  AmsListData,
  AmsListTray,
  AmsListUnit,
  AmsPreviewArea,
  ExtruderArea,
  ExtruderState,
  ExtruderView,
  FilamentLineArea,
  HubKind,
  HubLinkState,
  HumidityDisplayType,
  HumidityView,
  LayoutStyle,
  LinkState,
  MenuActions,
  PanelGroup,
  PanelLayout,
  PanelPos,
  PanelView,
  PreviewCube,
  PreviewItem,
  SlotLink,
  SlotRemainLine,
  SlotState,
  SlotView,
  SwitcherArea,
  UnitHub,
  UnitView,
} from './types';

const DEFAULT_COLOR = '#D9D9D9';

function emptyPanelLayout(): PanelLayout {
  return { layout_style: 'single', panels: [] };
}

export const emptyAmsControlWebState: AmsControlWebViewModel = {
  data: {
    selected_dev_id: '',
    ams_units: [],
    ext_slots: [],
    detect_remain_enabled: false,
    support_ams_humidity: false,
  },
  display: {
    visible: false,
    ams_preview_area: { visible: false, layout: emptyPanelLayout(), items: [] },
    ams_ext_area: {
      visible: false,
      lite_style: false,
      layout: emptyPanelLayout(),
      units: [],
      ext_slots: [],
    },
    filament_line_area: { visible: false, links: [] },
    switcher_area: {
      visible: false,
      installed: false,
      ready: false,
      show_setup_hint: false,
      setup_hint: '',
    },
    extruder_area: { visible: false, extruders: [] },
  },
  selected_ams_id: '',
  selected_slot_id: '',
  actions: {
    show_auto_refill: false,
    show_settings: false,
    can_load: false,
    can_unload: false,
    load_tips: '',
    unload_tips: '',
  },
};

function asString(value: unknown, fallback = '') {
  return typeof value === 'string' ? value : fallback;
}

function asNumber(value: unknown, fallback: number) {
  const n = Number(value);
  return Number.isFinite(n) ? n : fallback;
}

function asList<T>(value: unknown, map: (item: unknown) => T): T[] {
  return Array.isArray(value) ? value.map(map) : [];
}

function asColorType(value: unknown): AmsColorType {
  if (value === 0 || value === 1 || value === 2) return value;
  return 2;
}

function asHumidityDisplayType(value: unknown): HumidityDisplayType {
  return value === 'level' || value === 'percent' ? value : 'none';
}

function asSlotState(value: unknown): SlotState {
  return value === 'brand' || value === 'third_brand' || value === 'empty' || value === 'virtual'
    ? value
    : 'none';
}

function asLinkState(value: unknown): LinkState {
  return value === 'loaded' || value === 'loading' || value === 'unloading' ? value : 'idle';
}

function asHubLinkState(value: unknown): HubLinkState {
  return value === 'loaded' || value === 'loading' || value === 'unloading' ? value : 'idle';
}

function asHubKind(value: unknown): HubKind {
  return value === 'cross4' || value === 'passthrough' ? value : 'merge4';
}

function asLayoutStyle(value: unknown): LayoutStyle {
  return value === 'left_right' ? value : 'single';
}

function asPanelPos(value: unknown): PanelPos {
  return value === 'left' || value === 'right' ? value : 'single';
}

function asExtruderState(value: unknown): ExtruderState {
  return value === 'active' || value === 'loading' ? value : 'idle';
}

function asColorList(value: unknown, color: string) {
  const colors = Array.isArray(value)
    ? value.filter((item): item is string => typeof item === 'string' && item.length > 0)
    : [];
  return colors.length > 0 ? colors : [color];
}

function asNumberList(value: unknown) {
  return asList(value, (item) => asNumber(item, -1)).filter((item) => item >= 0);
}

export function normalizeTray(raw: Partial<AmsListTray> | undefined): AmsListTray {
  const color = asString(raw?.color, DEFAULT_COLOR) || DEFAULT_COLOR;
  return {
    ams_id: asString(raw?.ams_id),
    slot_id: asString(raw?.slot_id),
    tray_label: asString(raw?.tray_label),
    is_exists: !!raw?.is_exists,
    fila_type: asString(raw?.fila_type),
    sub_brands: asString(raw?.sub_brands),
    setting_id: asString(raw?.setting_id),
    tag_uid: asString(raw?.tag_uid),
    color,
    colors: asColorList(raw?.colors, color),
    color_type: asColorType(raw?.color_type),
    remain: asNumber(raw?.remain, 100),
    remain_g: asNumber(raw?.remain_g, -1),
    is_bbl: !!raw?.is_bbl,
    reading: !!raw?.reading,
    info_ready: !!raw?.info_ready,
    binded_extruder_ids: asNumberList(raw?.binded_extruder_ids),
    current_extruder_id: asNumber(raw?.current_extruder_id, -1),
    switcher_port: asString(raw?.switcher_port),
  };
}

export function normalizeUnit(raw: Partial<AmsListUnit> | undefined): AmsListUnit {
  return {
    ams_id: asString(raw?.ams_id),
    ams_type: asNumber(raw?.ams_type, 1),
    ams_type_name: asString(raw?.ams_type_name, 'UNKNOWN'),
    is_ams_lite_mixed: !!raw?.is_ams_lite_mixed,
    humidity_level: asNumber(raw?.humidity_level, -1),
    humidity_percent: asNumber(raw?.humidity_percent, -1),
    humidity_display_type: asHumidityDisplayType(raw?.humidity_display_type),
    binded_extruder_ids: asNumberList(raw?.binded_extruder_ids),
    switcher_port: asString(raw?.switcher_port),
    trays: asList(raw?.trays, (tray) => normalizeTray(tray as Partial<AmsListTray>)),
  };
}

function normalizeData(raw: Partial<AmsListData> | undefined): AmsListData {
  return {
    selected_dev_id: asString(raw?.selected_dev_id),
    ams_units: asList(raw?.ams_units, (unit) => normalizeUnit(unit as Partial<AmsListUnit>)),
    ext_slots: asList(raw?.ext_slots, (tray) => normalizeTray(tray as Partial<AmsListTray>)),
    detect_remain_enabled: !!raw?.detect_remain_enabled,
    support_ams_humidity: !!raw?.support_ams_humidity,
  };
}

function normalizeMenuActions(raw: Partial<MenuActions> | undefined): MenuActions {
  return {
    show_edit: !!raw?.show_edit,
    show_read: !!raw?.show_read,
    show_filament_mgr_hint: !!raw?.show_filament_mgr_hint,
  };
}

function normalizeSlotLine(raw: Partial<SlotRemainLine> | undefined): SlotRemainLine {
  return {
    show_line: !!raw?.show_line,
    remain_percent: asNumber(raw?.remain_percent, 100),
  };
}

function normalizeSlotView(raw: unknown): SlotView {
  const item = (raw ?? {}) as Partial<SlotView>;
  const color = asString(item.color, DEFAULT_COLOR) || DEFAULT_COLOR;
  return {
    ams_id: asString(item.ams_id),
    slot_id: asString(item.slot_id),
    label: asString(item.label),
    slot_state: asSlotState(item.slot_state),
    color,
    colors: asColorList(item.colors, color),
    color_type: asColorType(item.color_type),
    remain: asNumber(item.remain, 100),
    show_remain: !!item.show_remain,
    show_remain_height: !!item.show_remain_height,
    fila_type: asString(item.fila_type),
    selected: !!item.selected,
    loaded: !!item.loaded,
    reading: !!item.reading,
    show_rfid: !!item.show_rfid,
    show_unknown: !!item.show_unknown,
    k_text: asString(item.k_text),
    k_loading: !!item.k_loading,
    k_loading_text: asString(item.k_loading_text),
    menu_actions: normalizeMenuActions(item.menu_actions),
    slot_remain_line: normalizeSlotLine(item.slot_remain_line),
  };
}

function normalizeHumidityView(raw: Partial<HumidityView> | undefined): HumidityView {
  return {
    display_type: asHumidityDisplayType(raw?.display_type),
    level: asNumber(raw?.level, -1),
    percent: asNumber(raw?.percent, -1),
    display_idx: asNumber(raw?.display_idx, -1),
    drying: !!raw?.drying,
    left_dry_time: asNumber(raw?.left_dry_time, 0),
    support_drying: !!raw?.support_drying,
  };
}

function normalizeUnitHub(raw: Partial<UnitHub> | undefined): UnitHub {
  return {
    kind: asHubKind(raw?.kind),
    port_count: asNumber(raw?.port_count, 0),
    show_body: !!raw?.show_body,
    active_slot_id: asString(raw?.active_slot_id),
    state: asHubLinkState(raw?.state),
    color: asString(raw?.color),
  };
}

function normalizeUnitView(raw: unknown): UnitView {
  const item = (raw ?? {}) as Partial<UnitView>;
  return {
    ams_id: asString(item.ams_id),
    ams_type_name: asString(item.ams_type_name, 'AMS'),
    active: !!item.active,
    slot_count: asNumber(item.slot_count, 0),
    humidity: normalizeHumidityView(item.humidity),
    hub: normalizeUnitHub(item.hub),
    slots: asList(item.slots, normalizeSlotView),
  };
}

function normalizePreviewCube(raw: unknown): PreviewCube {
  const item = (raw ?? {}) as Partial<PreviewCube>;
  const color = asString(item.color, DEFAULT_COLOR) || DEFAULT_COLOR;
  return {
    color,
    colors: asColorList(item.colors, color),
    color_type: asColorType(item.color_type),
    is_exists: !!item.is_exists,
  };
}

function normalizePreviewItem(raw: unknown): PreviewItem {
  const item = (raw ?? {}) as Partial<PreviewItem>;
  return {
    ams_id: asString(item.ams_id),
    ams_type_name: asString(item.ams_type_name, 'AMS'),
    active: !!item.active,
    slot_count: asNumber(item.slot_count, 0),
    cubes: asList(item.cubes, normalizePreviewCube),
  };
}

function normalizePanelGroup(raw: unknown): PanelGroup {
  const item = (raw ?? {}) as Partial<PanelGroup>;
  return {
    ams_ids: asList(item.ams_ids, (id) => asString(id)).filter((id) => id.length > 0),
  };
}

function normalizePanelView(raw: unknown): PanelView {
  const item = (raw ?? {}) as Partial<PanelView>;
  return {
    pos: asPanelPos(item.pos),
    visible: !!item.visible,
    active_ams_id: asString(item.active_ams_id),
    groups: asList(item.groups, normalizePanelGroup),
  };
}

function normalizePanelLayout(raw: Partial<PanelLayout> | undefined): PanelLayout {
  return {
    layout_style: asLayoutStyle(raw?.layout_style),
    panels: asList(raw?.panels, normalizePanelView),
  };
}

function normalizePreviewArea(raw: Partial<AmsPreviewArea> | undefined): AmsPreviewArea {
  return {
    visible: !!raw?.visible,
    layout: normalizePanelLayout(raw?.layout),
    items: asList(raw?.items, normalizePreviewItem),
  };
}

function normalizeAmsExtArea(raw: Partial<AmsExtArea> | undefined): AmsExtArea {
  return {
    visible: !!raw?.visible,
    lite_style: !!raw?.lite_style,
    layout: normalizePanelLayout(raw?.layout),
    units: asList(raw?.units, normalizeUnitView),
    ext_slots: asList(raw?.ext_slots, normalizeSlotView),
  };
}

function normalizeSlotLink(raw: unknown): SlotLink {
  const item = (raw ?? {}) as Partial<SlotLink>;
  return {
    ams_id: asString(item.ams_id),
    slot_id: asString(item.slot_id),
    switcher_port: asString(item.switcher_port),
    extruder_ids: asNumberList(item.extruder_ids),
    state: asLinkState(item.state),
    color: asString(item.color),
  };
}

function normalizeLineArea(raw: Partial<FilamentLineArea> | undefined): FilamentLineArea {
  return {
    visible: !!raw?.visible,
    links: asList(raw?.links, normalizeSlotLink),
  };
}

function normalizeSwitcherArea(raw: Partial<SwitcherArea> | undefined): SwitcherArea {
  return {
    visible: !!raw?.visible,
    installed: !!raw?.installed,
    ready: !!raw?.ready,
    show_setup_hint: !!raw?.show_setup_hint,
    setup_hint: asString(raw?.setup_hint),
  };
}

function normalizeExtruderView(raw: unknown): ExtruderView {
  const item = (raw ?? {}) as Partial<ExtruderView>;
  return {
    id: asNumber(item.id, 0),
    state: asExtruderState(item.state),
    has_filament: !!item.has_filament,
    filament_color: asString(item.filament_color),
    icon: asString(item.icon),
  };
}

function normalizeExtruderArea(raw: Partial<ExtruderArea> | undefined): ExtruderArea {
  return {
    visible: !!raw?.visible,
    extruders: asList(raw?.extruders, normalizeExtruderView),
  };
}

function normalizeDisplay(raw: Partial<AmsControlDisplay> | undefined): AmsControlDisplay {
  return {
    visible: !!raw?.visible,
    ams_preview_area: normalizePreviewArea(raw?.ams_preview_area),
    ams_ext_area: normalizeAmsExtArea(raw?.ams_ext_area),
    filament_line_area: normalizeLineArea(raw?.filament_line_area),
    switcher_area: normalizeSwitcherArea(raw?.switcher_area),
    extruder_area: normalizeExtruderArea(raw?.extruder_area),
  };
}

function normalizeActions(raw: Partial<AmsControlActions> | undefined): AmsControlActions {
  return {
    show_auto_refill: !!raw?.show_auto_refill,
    show_settings: !!raw?.show_settings,
    can_load: !!raw?.can_load,
    can_unload: !!raw?.can_unload,
    load_tips: asString(raw?.load_tips),
    unload_tips: asString(raw?.unload_tips),
  };
}

export function normalizeState(payload: Partial<AmsControlWebViewModel> | undefined): AmsControlWebViewModel {
  if (!payload) return emptyAmsControlWebState;
  return {
    data: normalizeData(payload.data),
    display: normalizeDisplay(payload.display),
    selected_ams_id: asString(payload.selected_ams_id),
    selected_slot_id: asString(payload.selected_slot_id),
    actions: normalizeActions(payload.actions),
  };
}

export function findSlotLink(area: FilamentLineArea, amsId: string, slotId: string) {
  return area.links.find((link) => link.ams_id === amsId && link.slot_id === slotId);
}
