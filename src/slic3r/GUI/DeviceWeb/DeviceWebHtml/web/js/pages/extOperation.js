// /device_page/ams_ext_operation - external AMS unit detail with per-tray menus.
// Ported from ams-ext-operation/AmsExtOperationPage.tsx.
(function (global) {
  'use strict';

  var BBL = global.BBL || (global.BBL = {});
  var el = BBL.dom.el;
  var t = BBL.t;

  var MODULE = 'device_page_ams_filament_hotend';

  var FALLBACK_COLORS = [
    ['#ff8181', '#b7f6ca', '#675aff', '#d9d9d9'],
    ['#f09a17', '#d7bafd', '#b6f34f', '#5c5c5c'],
    ['#f09a17', '#d7bafd', '#b6f34f', '#5c5c5c'],
    ['#f09a17', '#d7bafd', '#b6f34f', '#5c5c5c'],
  ];

  function parseBindAms(raw) {
    if (!raw) return [];
    return raw.split(',').map(function (part) {
      var bits = part.split('|');
      return { ams_id: bits[0] || '', ams_type: bits[1] || '', slot_count: Number(bits[2] || 0) };
    }).filter(function (item) { return item.ams_id; });
  }

  function formatAmsName(amsId) {
    if (!amsId) return 'AMS';
    var n = Number(amsId);
    if (isFinite(n) && n >= 0 && n < 26) return 'AMS-' + String.fromCharCode(65 + n);
    return 'AMS-' + amsId;
  }

  function formatHumidity(value) {
    return typeof value === 'number' && value >= 0 ? value + '%' : '--';
  }

  function colorsForAms(index, trays) {
    var colors = (trays || []).map(function (tray) { return tray.color; })
      .filter(Boolean).slice(0, 4);
    return colors.length ? colors : FALLBACK_COLORS[index % FALLBACK_COLORS.length];
  }

  function trayLabel(tray) {
    if (tray.tray_state === 'unknown') return '?';
    if (tray.tray_state === 'empty') return 'Empty';
    return tray.filament_type || tray.filament_name || '--';
  }

  function buildFallbackTrays() {
    return [
      { ams_id: '0', slot_id: '0', exists: true, tray_state: 'unknown', filament_type: '', filament_name: '', color: '#d9d9d9' },
      { ams_id: '0', slot_id: '1', exists: true, tray_state: 'ready', filament_type: 'PLA', filament_name: 'PLA Basic', color: '#00e64d' },
      { ams_id: '0', slot_id: '2', exists: true, tray_state: 'ready', filament_type: 'PLA', filament_name: 'PLA Basic', color: '#7a75ff' },
      { ams_id: '0', slot_id: '3', exists: true, tray_state: 'ready', filament_type: 'PLA', filament_name: 'PLA Basic', color: '#00e64d' },
    ];
  }

  function mount(container, params) {
    var query = {
      addr_id: params.addr_id || '',
      display_index: params.display_index || '',
      ams_id: params.ams_id || '',
      slot_id: params.slot_id || '',
      count: params.count || '0',
      bind_ams: parseBindAms(params.bind_ams || ''),
    };
    var selectedAmsId = (query.bind_ams[0] && query.bind_ams[0].ams_id) || query.ams_id;

    var page = el('div', { class: 'ext-ams-page' });
    container.appendChild(page);

    var stopLayout = BBL.layout.observe(page);

    function runTrayAction(action, tray, selectedSlot) {
      BBL.bridge.request({
        module: MODULE,
        submod: 'action',
        action: action,
        payload: {
          addr_id: Number(query.addr_id) || (selectedSlot && selectedSlot.addr_id) || 0,
          display_index: Number(query.display_index) || 0,
          ams_id: tray.ams_id,
          slot_id: tray.slot_id,
        },
      }, 8000);
    }

    function renderSpoolDisc(tray) {
      var unknown = tray.tray_state === 'unknown';
      var disc = el('div', {
        class: 'spool-disc' + (unknown ? ' unknown' : ''),
        'data-color-type': tray.color_type || 'single',
      });
      disc.style.setProperty('--spool-color', tray.color || '#d9d9d9');
      if (typeof tray.remain === 'number') disc.style.setProperty('--spool-remain', String(tray.remain));
      if (unknown) {
        disc.appendChild(el('img', {
          class: 'spool-unknown', src: './assets/spool_unknown_figma.svg',
          alt: '', 'aria-hidden': 'true',
        }));
      }
      return disc;
    }

    function renderTrayMenu(tray, selectedSlot) {
      var actions = tray.actions;
      var canLoad = !!(actions && actions.can_load);
      var canUnload = !!(actions && actions.can_unload);
      // Keep read visible for legacy payloads that carry no actions block.
      var canRead = actions ? !!actions.can_read : true;
      var readLabel = (actions && actions.read_label) || 'Read';
      if (!canLoad && !canUnload && !canRead) return null;

      var items = [];
      if (canLoad) items.push(['load', t('Load')]);
      if (canUnload) items.push(['unload', t('Unload')]);
      if (canRead) items.push(['read', t(readLabel)]);

      var popover = el('div', { class: 'ext-tray-menu-popover', role: 'menu', hidden: true },
        items.map(function (item) {
          return el('button', {
            type: 'button', role: 'menuitem', text: item[1],
            onClick: function () {
              popover.hidden = true;
              runTrayAction(item[0], tray, selectedSlot);
            },
          });
        }));

      var trigger = el('button', {
        type: 'button',
        class: 'ext-tray-menu',
        'aria-label': 'tray menu',
        'aria-haspopup': 'menu',
        'aria-expanded': 'false',
        onClick: function (e) {
          e.stopPropagation();
          var next = popover.hidden;
          closeAllTrayMenus();
          popover.hidden = !next;
          BBL.dom.toggleClass(trigger, 'open', next);
          BBL.dom.setAttr(trigger, 'aria-expanded', next ? 'true' : 'false');
        },
      }, [el('span'), el('span'), el('span')]);

      return el('span', {
        class: 'ext-tray-menu-wrap',
        onClick: function (e) { e.stopPropagation(); },
      }, [trigger, popover]);
    }

    function closeAllTrayMenus() {
      var popovers = page.querySelectorAll('.ext-tray-menu-popover');
      for (var i = 0; i < popovers.length; i++) popovers[i].hidden = true;
      var triggers = page.querySelectorAll('.ext-tray-menu.open');
      for (var j = 0; j < triggers.length; j++) {
        triggers[j].classList.remove('open');
        triggers[j].setAttribute('aria-expanded', 'false');
      }
    }
    document.addEventListener('pointerdown', closeAllTrayMenus);

    function renderRoute(count, activeIndex, selectedSlot) {
      var nozzle = (selectedSlot && selectedSlot.filament_nozzle) || {};
      var route = el('div', {
        class: 'ext-ams-route',
        'data-count': count,
        'data-active': activeIndex,
        'data-nozzle-state': nozzle.state || 'Normal',
      });
      if (nozzle.has_filament) {
        route.style.setProperty('--route-color',
          nozzle.filament_color || (selectedSlot && selectedSlot.filament_card.color) || '');
      }
      route.appendChild(el('span', { class: 'ext-ams-route-line' }));
      route.appendChild(el('img', {
        class: 'ext-ams-route-nozzle',
        src: nozzle.state === 'Active' ? './assets/hotend_nozzle_active.svg' : './assets/hotend_nozzle.svg',
        alt: '', 'aria-hidden': 'true',
      }));
      if (Number(query.display_index) > 0) {
        route.appendChild(el('span', {
          class: 'ext-ams-route-index', text: String(Number(query.display_index)),
        }));
      }
      return route;
    }

    function render() {
      var state = BBL.store.getState();
      if (!state) return;

      var addrId = Number(query.addr_id);
      var selectedSlot = (state.filament_page.hotend_slots || []).filter(function (s) {
        return s.addr_id === addrId;
      })[0];
      var operationSlot = ((state.ams_ext_operation_page && state.ams_ext_operation_page.slots) || [])
        .filter(function (s) { return s.addr_id === addrId; })[0];

      var bindAms =
        (operationSlot && operationSlot.bind_ams && operationSlot.bind_ams.length) ? operationSlot.bind_ams :
        (selectedSlot && selectedSlot.bind_ams && selectedSlot.bind_ams.length) ? selectedSlot.bind_ams :
        query.bind_ams.length ? query.bind_ams :
        [{ ams_id: query.ams_id || '0', ams_type: 'ZMULTI_A', slot_count: Number(query.count || 4) }];

      // Dialog AMS units come from the dedicated C++ operation-page payload;
      // fall back to slot/query reconstruction for payloads that predate it.
      var operationUnits = (operationSlot && operationSlot.units) || [];
      var amsUnits = operationUnits.length ? operationUnits : bindAms.map(function (ams) {
        return { ams_id: ams.ams_id, ams_type: ams.ams_type, slot_count: ams.slot_count,
                 humidity_raw: -1, dry_status: '', trays: [] };
      });

      if (amsUnits.length && (!selectedAmsId || !amsUnits.some(function (a) { return a.ams_id === selectedAmsId; }))) {
        selectedAmsId = amsUnits[0].ams_id;
      }

      var selectedAms = amsUnits.filter(function (a) { return a.ams_id === selectedAmsId; })[0] || amsUnits[0];
      var trays = (selectedAms && selectedAms.trays) || [];
      var displayTrays = trays.length ? trays : buildFallbackTrays();
      var activeTrayIndex = 0;
      displayTrays.forEach(function (tray, index) {
        if (selectedAms && selectedAms.ams_id === query.ams_id && tray.slot_id === query.slot_id) {
          activeTrayIndex = index;
        }
      });

      BBL.dom.clear(page);

      var tabs = el('div', { class: 'ext-ams-tabs' }, amsUnits.map(function (ams, index) {
        return el('button', {
          type: 'button',
          class: ams.ams_id === selectedAmsId ? 'active' : '',
          title: 'AMS ' + ams.ams_id + ', slots ' + ((ams.trays && ams.trays.length) || ams.slot_count || 0),
          onClick: function () { selectedAmsId = ams.ams_id; render(); },
        }, el('span', { class: 'mini-ams' }, colorsForAms(index, ams.trays).map(function (color) {
          return el('i', { style: { background: color } });
        })));
      }));

      var slotsEl = el('div', { class: 'ext-ams-slots' }, displayTrays.map(function (tray, index) {
        var menu = renderTrayMenu(tray, selectedSlot);
        var children = [el('span', { class: 'slot-index', text: String(Number(tray.slot_id) + 1 || index + 1) })];
        if (menu) children.push(menu);
        children.push(renderSpoolDisc(tray));
        children.push(el('span', { class: 'slot-label', text: trayLabel(tray) }));
        return el('div', { class: 'ext-ams-slot' }, children);
      }));

      var card = el('section', { class: 'ext-ams-card' }, [
        el('div', { class: 'ext-ams-card-head' }, [
          el('span', { text: formatAmsName(selectedAms && selectedAms.ams_id) }),
          el('span', {
            class: 'humidity-pill',
            text: '\uD83D\uDCA7 ' + formatHumidity(selectedAms && selectedAms.humidity_raw) +
                  '  \u263C ' + ((selectedAms && selectedAms.dry_status) || 'Idle'),
          }),
        ]),
        el('div', { class: 'ext-ams-flow' }, [
          slotsEl,
          renderRoute(displayTrays.length, activeTrayIndex, selectedSlot),
        ]),
      ]);

      page.appendChild(el('main', { class: 'ext-ams-body' }, [
        el('h1', { text: t('Ext AMS Details') }),
        tabs,
        card,
      ]));
    }

    var unsubscribe = BBL.store.subscribe(render);
    render();

    return function teardown() {
      document.removeEventListener('pointerdown', closeAllTrayMenus);
      unsubscribe();
      stopLayout();
    };
  }

  BBL.pages = BBL.pages || {};
  BBL.pages.extOperation = { mount: mount };
})(window);
