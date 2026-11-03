// /device_page/hotend_detail - read-only detail sheet for one hotend slot.
// Ported from hotend-detail/HotendDetailPage.tsx.
(function (global) {
  'use strict';

  var BBL = global.BBL || (global.BBL = {});
  var el = BBL.dom.el;
  var t = BBL.t;

  var NOZZLE_URL = './assets/hotend_detail_nozzle.png';
  var NOZZLE_ERR_URL = './assets/hotend_detail_nozzle_err.png';
  var NOZZLE_UNKNOWN_URL = './assets/hotend_detail_nozzle_unkown.png';
  var NOZZLE_2D_URL = './assets/hotend_detail_nozzle_unkownedited.png';
  var UNOWNEDITED_TOOL_KINDS = { laser: 1, cutter: 1, pen: 1, empty: 1, unknown: 1 };

  function textOrDash(value) {
    return value && String(value).trim() ? String(value) : '--';
  }

  function filamentSwatchColor(color) {
    var raw = (color || '').trim();
    if (!raw) return '#D9D9D9';
    return raw.charAt(0) === '#' ? raw : '#' + raw;
  }

  function toolImageSrc(toolKind, opState) {
    if (opState === 'Error') return NOZZLE_ERR_URL;
    if (opState === 'Unknown') return NOZZLE_UNKNOWN_URL;
    if (!toolKind || UNOWNEDITED_TOOL_KINDS[toolKind]) return NOZZLE_2D_URL;
    return NOZZLE_URL;
  }

  function infoLine(label, value) {
    return el('div', { class: 'hotend-detail-line' }, [
      el('span', { text: label }),
      el('strong', { text: value }),
    ]);
  }

  function infoBlock(label, child) {
    return el('div', { class: 'hotend-detail-block' }, [
      el('h2', { text: label }),
      el('div', { class: 'hotend-detail-value' }, child),
    ]);
  }

  function mount(container, params) {
    var selectedDisplayIndex = Number(params.display_index || 0) || 1;

    var page = el('div', {
      class: 'hotend-detail-page',
      'data-testid': 'hotend-detail-page',
    });
    container.appendChild(page);

    var stopLayout = BBL.layout.observe(page);

    function render() {
      var state = BBL.store.getState();
      if (!state) return;

      var slots = (state.hotend_page.hotend_slots || []).slice().sort(function (a, b) {
        return a.display_index - b.display_index;
      });
      var selected = slots.filter(function (s) {
        return s.display_index === selectedDisplayIndex;
      })[0] || slots[0];

      var hotend = (selected && selected.hotend) || {};
      var detail = (selected && selected.hotend_detail_dialog) || {};
      var showFilament = detail.show_filament !== undefined
        ? detail.show_filament
        : hotend.tool_kind === 'nozzle';
      var toolTypeText = detail.tool_type !== undefined ? detail.tool_type : detail.nozzle_type;

      BBL.dom.clear(page);

      page.appendChild(el('div', {
        class: 'hotend-detail-tabs',
        role: 'tablist',
        'aria-label': t('Hotend list'),
      }, slots.map(function (slot) {
        return el('button', {
          type: 'button',
          class: selected && slot.display_index === selected.display_index ? 'active' : '',
          text: String(slot.display_index),
          onClick: function () {
            selectedDisplayIndex = slot.display_index;
            render();
          },
        });
      })));

      var image = el('div', { class: 'hotend-detail-image' },
        el('img', { src: toolImageSrc(hotend.tool_kind, hotend.op_state), alt: '' }));

      var aside = el('aside', { class: 'hotend-detail-card' }, [
        image,
        infoLine('SN', textOrDash(detail.serial_number)),
        infoLine(t('Usedtime'), textOrDash(detail.used_time_hours)),
        infoLine(t('Switch Count'),
          detail.switch_count === null || detail.switch_count === undefined
            ? '--' : String(detail.switch_count)),
      ]);

      var info = el('section', { class: 'hotend-detail-info' });
      if (showFilament) {
        info.appendChild(infoBlock(t('Nozzle Filament'),
          el('div', { class: 'hotend-detail-filament' }, [
            el('span', {
              class: 'hotend-detail-color',
              style: { backgroundColor: filamentSwatchColor(detail.filament_color) },
            }),
            el('span', { text: textOrDash(detail.filament_name) }),
          ])));
      }
      info.appendChild(infoBlock(t('Type'), el('span', { text: textOrDash(toolTypeText) })));
      info.appendChild(infoBlock(t('Version'), el('span', { text: textOrDash(detail.firmware_version) })));

      page.appendChild(el('div', { class: 'hotend-detail-content' }, [aside, info]));
    }

    var unsubscribe = BBL.store.subscribe(render);
    render();

    return function teardown() {
      unsubscribe();
      stopLayout();
    };
  }

  BBL.pages = BBL.pages || {};
  BBL.pages.hotendDetail = { mount: mount };
})(window);
