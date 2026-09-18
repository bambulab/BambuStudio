// Structural defaults that store.normalizeState() merges every pushed payload
// against, so consumers never have to null-check a field the host chose to omit.
//
// Deliberate divergence from the React build: device_page's mockData.ts carries a
// full nine-slot demo dataset and normalizeState() falls back to it whenever the
// host omits `hotend_slots`. Inside Studio the host always pushes, so that data is
// only ever visible for a few hundred milliseconds - but it also means a genuinely
// empty device state renders as fabricated slots. This front-end exists to make
// failures visible, so the skeleton below stays empty and only supplies shape.
(function (global) {
  'use strict';

  var BBL = global.BBL || (global.BBL = {});

  BBL.defaults = {
    amsFilamentHotendState: {
      filament_page: {
        mode: 'normal',
        actions: {
          can_load: false,
          can_load_external: false,
          can_show_load_auto: false,
          can_unload: false,
          can_dry: false,
          show_ams_settings: false,
          show_auto_refill: false,
          show_read_all: false,
          can_read_all_slots: false,
          tips: '',
        },
        filament_change_status: { active: false, mode: 'idle' },
        ams_dry_zones: [],
        hotend_slots: [],
      },
      hotend_page: {
        actions: {
          can_read_all_hotends: false,
          read_all_disabled_reason: '',
        },
        hotend_rack_status: {
          active: false,
          mode: 'idle',
          display_index: 0,
          current: 0,
          total: 0,
          message: '',
          tips: '',
        },
        hotend_slots: [],
      },
    },
  };
})(window);
