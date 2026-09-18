// /device_page/ams_ext_filament_edit - embedded batch external-filament editor.
// Ported from ams-ext-filament-edit/AmsExtFilamentEditPage.tsx +
// components/BatchExternalFilamentDialog.tsx.
(function (global) {
  'use strict';

  var BBL = global.BBL || (global.BBL = {});
  var el = BBL.dom.el;
  var t = BBL.t;

  function parseSlots(raw) {
    if (!raw) return [];
    return raw.split(',').map(function (part) {
      var bits = part.split('|');
      return {
        ams_id: bits[0] || '',
        slot_id: bits[1] || '',
        display_index: Number(bits[2] || 0),
      };
    }).filter(function (item) {
      return item.ams_id && item.slot_id && item.display_index > 0;
    });
  }

  function placeholderSlot(q) {
    return {
      addr_id: 0,
      display_index: q.display_index,
      ams_id: q.ams_id,
      slot_id: q.slot_id,
      is_top_tier: false,
      enabled: true,
      is_external_spool: true,
      filament_card: {
        state: 'Unknown', color: '#D9D9D9', color_type: 'single',
        text: '', name: '', type: '', remain: 100, status_text: '',
      },
      filament_line: { state: 'no_filament', has_filament: false, filament_color: '' },
      filament_nozzle: { state: 'Normal', has_filament: false, filament_color: '', on_extruder: false },
      select_action: { can_select: true, reason: '', ext_label: true },
    };
  }

  function mount(container, params) {
    var intent = params.intent === 'run' ? 'run' : 'edit';
    var mode = params.mode === 'batch_ams_load' ? 'batch_ams_load' : 'batch_ext_load';
    var querySlots = parseSlots(params.slots || '');
    var runSlots = parseSlots(params.run_slots || '');
    var queryKeys = {};
    querySlots.forEach(function (s) { queryKeys[s.ams_id + '::' + s.slot_id] = true; });

    var page = el('div', { class: 'batch-ext-edit-page' });
    container.appendChild(page);

    var stopLayout = BBL.layout.observe(page);

    BBL.store.refresh(mode);

    function closeDialog() { BBL.store.runSlotAction('close_dialog'); }

    function editSlot(slot) {
      // C++ opens the native filament-settings dialog *after* this action has
      // already returned, so one refresh in the callback usually lands before
      // the user saves. Stagger a few more.
      BBL.store.runSlotAction('edit', slot).then(function () {
        BBL.store.refresh(mode);
        global.setTimeout(function () { BBL.store.refresh(mode); }, 500);
        global.setTimeout(function () { BBL.store.refresh(mode); }, 1500);
      });
    }

    function resolveSlots(state) {
      var all = state.filament_page.hotend_slots || [];
      var selected = all.filter(function (slot) {
        return queryKeys[slot.ams_id + '::' + slot.slot_id];
      });
      return selected.length ? selected : querySlots.map(placeholderSlot);
    }

    function confirmRun(slots) {
      if (slots.some(function (s) { return s.filament_card.state === 'Unknown'; })) return;
      // Slots were just edited here, so tell C++ to run without re-checking the
      // still-lagging device tray identity.
      BBL.store.runBatchAction(mode, runSlots.length ? runSlots : slots, true);
      // Ask the main page to leave batch selection and return to Idle.
      BBL.store.runSlotAction('close_dialog_after_run');
    }

    function render() {
      var state = BBL.store.getState();
      if (!state) return;

      var slots = resolveSlots(state);
      var hasUnknown = slots.some(function (s) { return s.filament_card.state === 'Unknown'; });
      var title = t('Edit Filament');
      var confirmLabel = intent === 'run'
        ? t(mode === 'batch_ams_load' ? 'Auto Load' : 'Load')
        : t('Confirm');

      BBL.dom.clear(page);

      var dialog = el('section', {
        class: 'batch-ext-dialog embedded',
        role: 'dialog',
        'aria-modal': 'true',
        'aria-label': title,
        'data-testid': 'ams-hotend-batch-ext-dialog',
      });

      dialog.appendChild(el('header', { class: 'batch-ext-dialog-titlebar' }, [
        el('span', { class: 'batch-ext-dialog-dot red' }),
        el('span', { class: 'batch-ext-dialog-dot' }),
        el('span', { class: 'batch-ext-dialog-dot' }),
        el('h3', { text: title }),
      ]));

      if (hasUnknown) {
        dialog.appendChild(el('div', {
          class: 'batch-ext-dialog-warning',
          'data-testid': 'ams-hotend-batch-ext-warning',
        }, [
          el('span', { class: 'batch-ext-dialog-warning-icon', text: '!' }),
          el('span', { text: t('Please finish editing filament information before starting filament load.') }),
          el('button', { type: 'button', 'aria-label': t('Close'), text: '\u00D7', onClick: closeDialog }),
        ]));
      }

      var table = el('div', { class: 'batch-ext-dialog-table', role: 'table' },
        el('div', { class: 'batch-ext-dialog-row header', role: 'row' }, [
          el('span', { text: t('Slot') }),
          el('span', { text: t('Filament Type') }),
          el('span', { text: t('Color') }),
          el('span', { text: t('Edit Filament') }),
        ]));

      slots.forEach(function (slot) {
        var card = slot.filament_card;
        var unknown = card.state === 'Unknown';
        var typeText = unknown ? t('Please select') : (card.name || card.type || t('Unknown'));

        var colorBtn = el('button', {
          type: 'button',
          class: 'batch-ext-color' + (unknown ? ' unknown' : ''),
          'aria-label': t('Edit color'),
          text: unknown ? '?' : '',
          onClick: function () { editSlot(slot); },
        });
        if (!unknown) colorBtn.style.setProperty('--ext-color', card.color);

        table.appendChild(el('div', { class: 'batch-ext-dialog-row', role: 'row' }, [
          el('span', { text: String(slot.display_index) }),
          el('button', {
            type: 'button',
            class: 'batch-ext-select' + (unknown ? ' placeholder' : ''),
            title: unknown ? t('Edit Filament') : typeText,
            onClick: function () { editSlot(slot); },
          }, [
            el('span', { text: typeText }),
            el('span', { class: 'batch-ext-select-arrow', text: '\u2304' }),
          ]),
          colorBtn,
          el('button', {
            type: 'button', class: 'batch-ext-row-edit', text: t('Edit'),
            onClick: function () { editSlot(slot); },
          }),
        ]));
      });
      dialog.appendChild(table);

      dialog.appendChild(el('footer', { class: 'batch-ext-dialog-actions' }, [
        el('button', { type: 'button', class: 'batch-ext-cancel', text: t('Close'), onClick: closeDialog }),
        el('button', {
          type: 'button',
          class: 'batch-ext-confirm',
          disabled: intent === 'run' && hasUnknown,
          text: confirmLabel,
          onClick: function () {
            if (intent === 'run') confirmRun(slots);
            else closeDialog();
          },
        }),
      ]));

      page.appendChild(dialog);
    }

    // Same rationale as editSlot(): the native edit dialog runs outside this
    // WebView, so nothing reports back when the user saves.
    var refreshing = false;
    function refreshExtRows() {
      if (refreshing) return;
      refreshing = true;
      BBL.store.refresh(mode).then(function () { refreshing = false; },
                                   function () { refreshing = false; });
    }
    var timer = global.setInterval(refreshExtRows, 1000);
    global.addEventListener('focus', refreshExtRows);
    document.addEventListener('visibilitychange', refreshExtRows);

    var unsubscribe = BBL.store.subscribe(render);
    render();

    return function teardown() {
      global.clearInterval(timer);
      global.removeEventListener('focus', refreshExtRows);
      document.removeEventListener('visibilitychange', refreshExtRows);
      unsubscribe();
      stopLayout();
    };
  }

  BBL.pages = BBL.pages || {};
  BBL.pages.extFilamentEdit = { mount: mount };
})(window);
