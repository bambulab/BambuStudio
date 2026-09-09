import type {
  AmsControlDisplay,
  AmsControlWebViewModel,
  AmsListData,
  AmsListTray,
  PanelLayout,
  SlotLink,
  SlotView,
} from './types';

function tray(
  amsId: string,
  slotId: string,
  trayLabel: string,
  color: string,
  filaType: string,
  extra: Partial<AmsListTray> = {},
): AmsListTray {
  return {
    ams_id: amsId,
    slot_id: slotId,
    tray_label: trayLabel,
    is_exists: true,
    fila_type: filaType,
    sub_brands: '',
    setting_id: '',
    tag_uid: '',
    color,
    colors: [color],
    color_type: 2,
    remain: 100,
    remain_g: -1,
    is_bbl: true,
    reading: false,
    info_ready: true,
    binded_extruder_ids: [0],
    current_extruder_id: -1,
    switcher_port: '',
    ...extra,
  };
}

const mockData: AmsListData = {
  selected_dev_id: 'mock-printer',
  ams_units: [
    {
      ams_id: '0',
      ams_type: 1,
      ams_type_name: 'AMS',
      is_ams_lite_mixed: false,
      humidity_level: 5,
      humidity_percent: -1,
      humidity_display_type: 'level',
      binded_extruder_ids: [0],
      switcher_port: '',
      trays: [
        tray('0', '0', 'A1', '#00AE42', 'PLA', { remain: 62 }),
        tray('0', '1', 'A2', '#FFFFFF', 'PLA', { remain: 35 }),
        tray('0', '2', 'A3', '#C4A574', 'PETG', { is_bbl: false, info_ready: false }),
        tray('0', '3', 'A4', '#F5C400', '', { is_exists: false, info_ready: false }),
      ],
    },
  ],
  ext_slots: [
    tray('255', '0', 'Ext', '#F48FB1', 'TPU', { is_bbl: false }),
  ],
  detect_remain_enabled: true,
  support_ams_humidity: true,
};

function slotView(
  item: AmsListTray,
  isExt: boolean,
  selected: { amsId: string; slotId: string },
  loaded: { amsId: string; slotId: string },
): SlotView {
  const state = isExt
    ? 'virtual'
    : !item.is_exists
      ? 'empty'
      : item.is_bbl && item.info_ready
        ? 'brand'
        : 'third_brand';
  const hasSpool = state !== 'empty';
  const unknown = hasSpool && !item.info_ready;
  const showRead = state === 'brand';
  const showRemain = !isExt && item.is_bbl && item.info_ready && mockData.detect_remain_enabled;
  return {
    ams_id: item.ams_id,
    slot_id: item.slot_id,
    label: item.tray_label,
    slot_state: state,
    color: unknown ? '#FFFFFF' : item.color,
    colors: unknown ? ['#FFFFFF'] : item.colors,
    color_type: unknown ? 0 : item.color_type,
    remain: item.remain,
    show_remain: showRemain,
    show_remain_height: false,
    fila_type: unknown ? '' : item.fila_type,
    selected: item.ams_id === selected.amsId && item.slot_id === selected.slotId,
    loaded: item.ams_id === loaded.amsId && item.slot_id === loaded.slotId,
    reading: item.reading,
    show_rfid: !isExt,
    show_unknown: unknown,
    k_text: '',
    k_loading: false,
    k_loading_text: '',
    menu_actions: {
      show_edit: hasSpool && !showRead,
      show_read: hasSpool && showRead,
      show_filament_mgr_hint: false,
    },
    slot_remain_line: {
      show_line: showRemain && hasSpool,
      remain_percent: item.remain,
    },
  };
}

function slotLink(item: AmsListTray, loaded: { amsId: string; slotId: string }): SlotLink {
  const isLoaded = item.ams_id === loaded.amsId && item.slot_id === loaded.slotId;
  return {
    ams_id: item.ams_id,
    slot_id: item.slot_id,
    switcher_port: item.switcher_port,
    extruder_ids: item.binded_extruder_ids,
    state: isLoaded ? 'loaded' : 'idle',
    color: isLoaded ? item.color : '',
  };
}

function buildLayout(activeAmsId: string): PanelLayout {
  const leftGroups = mockData.ams_units.map((unit) => ({ ams_ids: [unit.ams_id] }));
  const rightGroups = mockData.ext_slots.map((item) => ({ ams_ids: [item.ams_id] }));
  const leftIds = leftGroups.flatMap((group) => group.ams_ids);
  const rightIds = rightGroups.flatMap((group) => group.ams_ids);
  const both = leftGroups.length > 0 && rightGroups.length > 0;
  const panels: PanelLayout['panels'] = [];
  if (leftGroups.length > 0) {
    panels.push({
      pos: both ? 'left' : 'single',
      visible: true,
      active_ams_id: leftIds.includes(activeAmsId) ? activeAmsId : (leftIds[0] ?? ''),
      groups: leftGroups,
    });
  }
  if (rightGroups.length > 0) {
    panels.push({
      pos: both ? 'right' : 'single',
      visible: true,
      active_ams_id: rightIds.includes(activeAmsId) ? activeAmsId : (rightIds[0] ?? ''),
      groups: rightGroups,
    });
  }
  return {
    layout_style: both ? 'left_right' : 'single',
    panels,
  };
}

function buildDisplay(
  selected: { amsId: string; slotId: string },
  loaded: { amsId: string; slotId: string },
): AmsControlDisplay {
  const cellIds = [
    ...mockData.ams_units.map((unit) => unit.ams_id),
    ...mockData.ext_slots.map((item) => item.ams_id),
  ];
  const activeAmsId = cellIds.includes(selected.amsId) ? selected.amsId : cellIds[0] ?? '';
  const loadedTray = [
    ...mockData.ams_units.flatMap((unit) => unit.trays),
    ...mockData.ext_slots,
  ].find((item) => item.ams_id === loaded.amsId && item.slot_id === loaded.slotId);
  const layout = buildLayout(activeAmsId);

  return {
    visible: true,
    ams_preview_area: {
      visible: true,
      layout,
      items: [
        ...mockData.ams_units.map((unit) => ({
          ams_id: unit.ams_id,
          ams_type_name: unit.ams_type_name,
          active: unit.ams_id === activeAmsId,
          slot_count: unit.trays.length,
          cubes: unit.trays.map((item) => ({
            color: item.color,
            colors: item.colors,
            color_type: item.color_type,
            is_exists: item.is_exists,
          })),
        })),
        ...mockData.ext_slots.map((item) => ({
          ams_id: item.ams_id,
          ams_type_name: 'EXT_SPOOL',
          active: item.ams_id === activeAmsId,
          slot_count: 1,
          cubes: [
            {
              color: item.color,
              colors: item.colors,
              color_type: item.color_type,
              is_exists: item.is_exists,
            },
          ],
        })),
      ],
    },
    ams_ext_area: {
      visible: true,
      lite_style: false,
      layout,
      units: mockData.ams_units.map((unit) => ({
        ams_id: unit.ams_id,
        ams_type_name: unit.ams_type_name,
        active: unit.ams_id === activeAmsId,
        slot_count: unit.trays.length,
        humidity: {
          display_type: unit.humidity_display_type,
          level: unit.humidity_level,
          percent: unit.humidity_percent,
          display_idx: unit.humidity_level,
          drying: false,
          left_dry_time: 0,
          support_drying: unit.ams_type_name === 'N3F' || unit.ams_type_name === 'N3S',
        },
        slots: unit.trays.map((item) => slotView(item, false, selected, loaded)),
      })),
      ext_slots: mockData.ext_slots.map((item) => slotView(item, true, selected, loaded)),
    },
    filament_line_area: {
      visible: true,
      links: [
        ...mockData.ams_units.flatMap((unit) => unit.trays.map((item) => slotLink(item, loaded))),
        ...mockData.ext_slots.map((item) => slotLink(item, loaded)),
      ],
    },
    switcher_area: {
      visible: false,
      installed: false,
      ready: false,
      show_setup_hint: false,
      setup_hint: '',
    },
    extruder_area: {
      visible: true,
      extruders: [
        {
          id: 0,
          state: 'active',
          has_filament: !!loadedTray,
          filament_color: loadedTray?.color ?? '',
          icon: 'single_nozzle_xp',
        },
      ],
    },
  };
}

const MOCK_LOADED = { amsId: '0', slotId: '0' };

export const mockAmsControlWebState: AmsControlWebViewModel = {
  data: mockData,
  display: buildDisplay({ amsId: '0', slotId: '0' }, MOCK_LOADED),
  selected_ams_id: '0',
  selected_slot_id: '0',
  actions: {
    show_auto_refill: true,
    show_settings: true,
    can_load: false,
    can_unload: true,
    load_tips: 'Current slot has already been loaded',
    unload_tips: '',
  },
};

export function applySlotSelection(
  state: AmsControlWebViewModel,
  amsId: string,
  slotId: string,
): AmsControlWebViewModel {
  const trays = [
    ...state.data.ams_units.flatMap((unit) => unit.trays),
    ...state.data.ext_slots,
  ];
  const selected = trays.find((item) => item.ams_id === amsId && item.slot_id === slotId);
  const loaded = MOCK_LOADED.amsId === amsId && MOCK_LOADED.slotId === slotId;
  const canLoad = !!selected?.is_exists && !!selected.fila_type && !loaded && !selected.reading;

  return {
    ...state,
    display: buildDisplay({ amsId, slotId }, MOCK_LOADED),
    selected_ams_id: amsId,
    selected_slot_id: slotId,
    actions: {
      ...state.actions,
      can_unload: loaded,
      can_load: canLoad,
      load_tips: canLoad ? '' : 'This slot cannot be loaded right now',
      unload_tips: loaded ? '' : 'The selected slot is not loaded in the extruder.',
    },
  };
}
