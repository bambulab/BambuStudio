// /device_page/ams_filament_hotend - the main Filament / Hotend page.
//
// Ported from AmsFilamentHotendPage.tsx and its components/ tree. Class names and
// data-testid values are kept byte-identical so ams-filament-hotend.css applies
// unchanged and the existing Playwright selectors keep working.
(function (global) {
  'use strict';

  var BBL = global.BBL || (global.BBL = {});
  var el = BBL.dom.el;
  var patchList = BBL.dom.patchList;
  var t = BBL.t;
  var spool = BBL.components.spool;
  var hotend = BBL.components.hotend;

  // Top feed lines cross the lower row before reaching the shared nozzle row.
  var TOP_LINE_HEIGHT = 167;
  var BOTTOM_LINE_HEIGHT = 26;
  // Slot column width, kept in sync with `.slot-column` / `.filament-card`.
  var COLUMN_WIDTH = 80;

  var ACTION_TO_BATCH_MODE = {
    load_auto: 'batch_ams_load',
    load_external: 'batch_ext_load',
    unload: 'batch_unload',
  };

  // Stable key for the local selection set. ams_id + slot_id, because two slots
  // in different AMS units can share a display_index.
  function selectionKey(slot) {
    return slot.ams_id + '::' + slot.slot_id;
  }

  function mount(container) {
    // ---- local UI state (everything else lives in BBL.store) ----
    var tab = 'filament';
    var selectedKeys = {};      // selectionKey -> true
    var lastMode = null;
    var debugOpening = false;
    var extModePoll = null;

    var page = el('div', {
      class: 'ams-filament-hotend-page',
      'data-testid': 'ams-filament-hotend-page',
    });
    container.appendChild(page);

    // Children are appended straight onto `.ams-filament-hotend-page`: the CSS
    // lays the page out as a flex column and addresses rows positionally, so an
    // extra wrapper element silently changes the layout.
    var toolbarEl = el('header', { class: 'page-toolbar' });
    page.appendChild(toolbarEl);
    var errorEl = null;
    var bodyNodes = [];
    var footerNode = null;

    var stopLayout = BBL.layout.observe(page);

    // ---- helpers over current state ----
    function sortedSlots(list) {
      return (list || []).slice().sort(function (a, b) {
        return a.display_index - b.display_index;
      });
    }

    function selectableSlots(state) {
      return sortedSlots(state.filament_page.hotend_slots).filter(function (s) {
        return !!(s.select_action && s.select_action.can_select);
      });
    }

    function selectedCount(state) {
      return selectableSlots(state).filter(function (s) {
        return selectedKeys[selectionKey(s)];
      }).length;
    }

    // In batch modes fp.actions values are `{enabled, reason}` objects instead of
    // plain booleans. The toolbar and footer consume the boolean shape, so flatten
    // to `.enabled` exactly like the React build does.
    function actionsAsBool(actions) {
      var out = {};
      Object.keys(actions || {}).forEach(function (key) {
        var value = actions[key];
        out[key] = (value && typeof value === 'object' && 'enabled' in value)
          ? (value.enabled || false)
          : value;
      });
      return out;
    }

    // ---- actions ----
    function handleToolbarAction(action) {
      var state = BBL.store.getState();
      if (action === 'dry') {
        var zones = state.filament_page.ams_dry_zones || [];
        var zone = zones.filter(function (z) { return z.ams_id; })[0];
        BBL.store.runSlotAction(action, undefined, zone ? { ams_id: zone.ams_id } : {});
        return;
      }
      var nextMode = ACTION_TO_BATCH_MODE[action];
      if (nextMode) {
        BBL.store.refresh(nextMode);
        return;
      }
      BBL.store.runSlotAction(action);
    }

    function handleSlotAction(action, slot, payload) {
      BBL.store.runSlotAction(action, slot, payload);
    }

    function exitBatchMode() {
      BBL.store.refresh('normal');
    }
    // Dev-only escape hatch the bridge debug dialog can fall back to.
    global.__exitBatchMode = exitBatchMode;

    function toggleSelect(slot) {
      var key = selectionKey(slot);
      if (selectedKeys[key]) delete selectedKeys[key];
      else selectedKeys[key] = true;
      render();
    }

    // ---- toolbar ----
    function renderToolbar(state) {
      var mode = state.filament_page.mode || 'normal';
      var inBatchMode = mode !== 'normal';
      var actions = actionsAsBool(state.filament_page.actions);
      Object.keys(state.hotend_page.actions || {}).forEach(function (k) {
        actions[k] = state.hotend_page.actions[k];
      });

      BBL.dom.clear(toolbarEl);

      function tabButton(id, label, testid) {
        return el('button', {
          type: 'button',
          'data-testid': testid,
          class: 'tab' + (tab === id ? ' active' : ''),
          // Tab switching is hidden behind batch mode so the user cannot leave a
          // partially-selected batch by toggling tabs by accident.
          disabled: inBatchMode,
          onClick: function () {
            if (inBatchMode || tab === id) return;
            tab = id;
            render();
          },
          text: label,
        });
      }

      toolbarEl.appendChild(el('div', { class: 'tab-group' }, [
        tabButton('filament', t('Filament'), 'ams-hotend-tab-filament'),
        tabButton('hotend', t('Hotend'), 'ams-hotend-tab-hotend'),
      ]));

      if (tab === 'filament') {
        // In batch selection mode the top toolbar shows no actions - commit,
        // edit and exit all live on the batch footer.
        if (inBatchMode) return;

        var icons = el('div', { class: 'action-group action-group-icons' });
        if (actions.show_read_all) {
          icons.appendChild(el('button', {
            type: 'button',
            class: 'toolbar-icon-btn',
            'data-testid': 'ams-hotend-action-read-all',
            disabled: !(actions.can_read_all_slots || false),
            title: t('Read All'),
            onClick: function () { handleToolbarAction('read_all_slots_rfid'); },
          }, el('img', { src: './assets/toolbar_read_all.svg', alt: '', 'aria-hidden': 'true' })));
        }
        if (actions.show_ams_settings) {
          icons.appendChild(el('button', {
            type: 'button',
            class: 'toolbar-icon-btn',
            'data-testid': 'ams-hotend-action-settings',
            title: t('AMS Settings'),
            onClick: function () { handleToolbarAction('settings'); },
          }, el('img', { src: './assets/toolbar_ams_settings.svg', alt: '', 'aria-hidden': 'true' })));
        }
        icons.appendChild(hotend.renderToolbarMoreMenu(actions, handleToolbarAction));
        toolbarEl.appendChild(icons);
      } else {
        var canReadAll = actions.can_read_all_hotends !== undefined ? actions.can_read_all_hotends : true;
        toolbarEl.appendChild(el('div', { class: 'action-group hotend-actions' },
          el('button', {
            type: 'button',
            class: 'primary',
            'data-testid': 'ams-hotend-action-read-all',
            disabled: !canReadAll,
            title: canReadAll ? null : actions.read_all_disabled_reason,
            onClick: function () { handleToolbarAction('read_all_hotends'); },
            text: t('Read All'),
          })));
      }
    }

    // ---- stage (filament tab) ----
    //
    // The stage is built once and patched in place. Rebuilding it per push would
    // drop scroll position, restart the spool animations and blur any focused
    // control - the host re-sends a full ViewModel several times per second.
    var stageEl = el('section', { class: 'stage' });
    var topRow = el('div', { class: 'top-tier-row', 'data-testid': 'ams-hotend-top-tier' });
    var bottomRow = el('div', { class: 'bottom-tier-row', 'data-testid': 'ams-hotend-bottom-tier' });
    stageEl.appendChild(topRow);
    stageEl.appendChild(bottomRow);
    var dryerNodes = [];

    function updateStage(state) {
      var mode = state.filament_page.mode || 'normal';
      var slots = sortedSlots(state.filament_page.hotend_slots);
      var topSlots = slots.filter(function (s) { return s.is_top_tier; });
      var bottomSlots = slots.filter(function (s) { return !s.is_top_tier; });

      BBL.dom.toggleClass(stageEl, 'batch-mode', mode !== 'normal');
      // Both tiers form one evenly spaced sequence, so the pitch depends on the
      // total slot count. cqw resolves against `.stage` (see the CSS).
      var gaps = topSlots.length + bottomSlots.length - 1;
      stageEl.style.setProperty('--slot-pitch',
        gaps > 0 ? 'calc((100cqw - ' + COLUMN_WIDTH + 'px) / ' + gaps + ')' : '0px');

      // Dryer chambers are physically attached to the top tier in the current
      // hardware layout, so only top slots can fall under a chamber decoration.
      // They must stay ahead of the slot columns, hence the patchList offset.
      dryerNodes.forEach(function (node) {
        if (node.parentNode === topRow) topRow.removeChild(node);
      });
      dryerNodes = [];
      hotend.findDryerRuns(topSlots, state.filament_page.ams_dry_zones || [])
        .forEach(function (run) {
          var decoration = hotend.renderDryerChamberDecoration({
            startIndex: run.start,
            endIndex: run.end,
            totalSlots: topSlots.length,
            humidity: run.humidity,
            status: run.status,
            isDrying: run.isDrying,
          });
          if (!decoration) return;
          topRow.insertBefore(decoration, topRow.childNodes[dryerNodes.length] || null);
          dryerNodes.push(decoration);
        });

      patchList(topRow, topSlots, selectionKey, function (slot, node) {
        return renderSlotColumn(slot, TOP_LINE_HEIGHT, mode, node);
      }, dryerNodes.length);
      patchList(bottomRow, bottomSlots, selectionKey, function (slot, node) {
        return renderSlotColumn(slot, BOTTOM_LINE_HEIGHT, mode, node);
      });

      return stageEl;
    }

    function renderSlotColumn(slot, lineHeight, mode, existing) {
      var column = existing || el('div', { class: 'slot-column' });
      BBL.dom.setAttr(column, 'data-tier', slot.is_top_tier ? 'top' : 'bottom');
      BBL.dom.clear(column);
      column.appendChild(renderFilamentCard(slot, mode));
      // Resolved per call rather than captured at load time so the module load
      // order in ams_hotend.html stays free to change.
      var nz = BBL.components.nozzle;
      column.appendChild(nz.renderFeedLine({
        height: lineHeight,
        state: slot.filament_line.state,
        color: slot.filament_line.filament_color || slot.filament_card.color,
        hasFilamentInNozzle: slot.filament_line.has_filament,
        useFeedLineDisplay: mode === 'normal',
      }));
      column.appendChild(nz.renderNozzle({
        nozzleState: slot.filament_nozzle.state,
        displayIndex: slot.display_index,
        color: slot.filament_nozzle.filament_color || slot.filament_card.color,
        hasFilamentInNozzle: slot.filament_nozzle.has_filament,
        onExtruder: slot.filament_nozzle.on_extruder,
        toolKind: slot.filament_nozzle.tool_kind,
      }));
      return column;
    }

    function renderFilamentCard(slot, mode) {
      var inBatchMode = mode !== 'normal';
      var selectAction = slot.select_action;
      // C++ is authoritative on per-slot eligibility; the web side only renders.
      var canSelect = inBatchMode ? !!(selectAction && selectAction.can_select) : false;
      var notAllowed = inBatchMode && !canSelect;
      var batchChecked = !!selectedKeys[selectionKey(slot)];

      var lineState = slot.filament_line.state;
      var isTransporting = lineState === 'loading' || lineState === 'unloading';

      var cls = ['filament-card'];
      if (slot.selected || (inBatchMode && batchChecked)) cls.push('selected');
      if (slot.is_running) cls.push('running');
      if (!slot.enabled) cls.push('disabled');
      if (inBatchMode) cls.push('batch-mode');
      if (notAllowed) cls.push('not-allowed');

      var card = el('div', {
        class: cls.join(' '),
        'data-testid': 'ams-hotend-slot-' + slot.display_index,
        'data-card-state': slot.filament_card.state,
        'data-line-state': lineState,
        'data-batch-mode': inBatchMode ? mode : null,
        onClick: inBatchMode ? function () {
          if (!canSelect) return;
          toggleSelect(slot);
        } : null,
      });

      var prefeed = slot.filament_card.prefeed_indicator;
      var showPrefeed = !inBatchMode && !!(prefeed && prefeed.visible);

      var head = el('div', { class: 'card-head' }, spool.renderIndexPill(slot));
      // Card-head right slot priority: batch checkbox > loading / prefeed.
      // Loading is a transport state carried by filament_line, so it can
      // coexist with material identity states such as Unknown.
      if (inBatchMode) {
        head.appendChild(spool.renderBatchSlotCheckbox({
          checked: batchChecked,
          canSelect: canSelect,
          reason: selectAction && selectAction.reason,
          displayIndex: slot.display_index,
          onToggle: function (e) {
            if (e) e.stopPropagation();
            if (!canSelect) return;
            toggleSelect(slot);
          },
        }));
      } else if (isTransporting || showPrefeed) {
        var right = el('span', { class: 'card-head-right' });
        if (isTransporting) right.appendChild(spool.renderLoadingArcIcon());
        if (showPrefeed) {
          var indicator = spool.renderPrefeedIndicator({
            displayIndex: slot.display_index,
            colors: prefeed.colors,
            colorType: prefeed.color_type,
            filaType: prefeed.prefeed_fila_type,
          });
          if (indicator) right.appendChild(indicator);
        }
        head.appendChild(right);
      }

      card.appendChild(el('div', { class: 'card-spool-wrap' }, [head, spool.renderSpoolDisc(slot, notAllowed)]));
      card.appendChild(spool.renderCardText(slot));

      if (slot.filament_card.new_filament_hint) {
        card.appendChild(el('button', {
          type: 'button',
          'aria-label': 'New filament',
          class: 'filament-card-hint-badge',
          onClick: function (e) {
            e.stopPropagation();
            handleSlotAction('open_filament_mgr_hint', slot);
          },
        }, el('img', { src: './assets/ams_filament_hint.svg', alt: '', 'aria-hidden': 'true', width: 14, height: 14 })));
      }

      // Right-click menu is normal-mode only, and only when the schema exposes
      // at least one actionable item.
      if (!inBatchMode && slot.enabled) {
        card.addEventListener('contextmenu', function (e) {
          e.preventDefault();
          openCardContextMenu(slot, e.clientX, e.clientY);
        });
      }

      return card;
    }

    function checkmarkSvg() {
      return el('svg', { width: 12, height: 12, viewBox: '0 0 12 12' },
        el('path', {
          d: 'M2.5 6.2 5 8.7 9.5 3.6',
          fill: 'none',
          stroke: 'currentColor',
          'stroke-width': 1.6,
          'stroke-linecap': 'round',
          'stroke-linejoin': 'round',
        }));
    }

    // ---- card context menu ----
    var closeOpenMenu = null;

    function closeCardContextMenu() {
      if (closeOpenMenu) closeOpenMenu();
      closeOpenMenu = null;
    }

    function openCardContextMenu(slot, x, y) {
      closeCardContextMenu();
      if (!hotend.hasCardContextMenuItems(slot)) return;
      var node = hotend.renderCardContextMenu(slot, { x: x, y: y }, handleSlotAction, function () {
        closeOpenMenu = null;
      });
      if (!node) return;
      // renderCardContextMenu owns its own dismissal listeners; this teardown is
      // only for the page unmounting while a menu is still open.
      closeOpenMenu = function () {
        if (node.parentNode) node.parentNode.removeChild(node);
      };
    }

    // ---- hotend tab ----
    var hotendPanelEl = el('section', {
      class: 'hotend-panel',
      'data-testid': 'ams-hotend-hotend-panel',
    });
    var hotendStrip = el('div', { class: 'hotend-strip' });
    hotendPanelEl.appendChild(hotendStrip);
    var rackProgressNode = null;

    function updateHotendPanel(state) {
      var hp = state.hotend_page;
      var rackStatus = hp.hotend_rack_status || {};

      // A rack-wide job replaces the strip entirely; a single-slot job sits
      // underneath it.
      if (rackStatus.active && rackStatus.mode === 'all') {
        return hotend.renderHotendRackProgressPanel(rackStatus, handleToolbarAction);
      }

      patchList(hotendStrip, sortedSlots(hp.hotend_slots), function (slot) {
        return slot.ams_id + '::' + slot.slot_id;
      }, function (slot) {
        return hotend.renderHotendCard(slot, !!rackStatus.active, handleSlotAction);
      });

      if (rackProgressNode && rackProgressNode.parentNode === hotendPanelEl) {
        hotendPanelEl.removeChild(rackProgressNode);
      }
      rackProgressNode = null;
      if (rackStatus.active && rackStatus.mode === 'single') {
        rackProgressNode = hotend.renderHotendRackProgressPanel(rackStatus, handleToolbarAction);
        if (rackProgressNode) hotendPanelEl.appendChild(rackProgressNode);
      }
      return hotendPanelEl;
    }

    function renderManualPullBanner(status) {
      if (!status) return null;
      return el('div', {
        class: 'filament-manual-pull-banner',
        'data-testid': 'ams-hotend-manual-pull-banner',
      }, [
        el('span', { class: 'manual-pull-text', text: status.message || t('Manual pull required') }),
        el('button', {
          type: 'button',
          class: 'manual-pull-open',
          text: t('Details'),
          onClick: function () { BBL.store.runSlotAction('manual_pull_detail'); },
        }),
      ]);
    }

    // ---- footers ----
    function renderBatchFooter(state) {
      var mode = state.filament_page.mode;
      var selectable = selectableSlots(state);
      var count = selectedCount(state);
      var total = selectable.length;
      var allSelected = total > 0 && count === total;
      var someSelected = count > 0 && count < total;
      var canCommit = count > 0;
      var showEdit = mode === 'batch_ext_load' || mode === 'batch_ams_load';

      var commitLabel = mode === 'batch_ams_load' ? t('Auto Load')
        : mode === 'batch_ext_load' ? t('Manual Loading')
        : mode === 'batch_unload' ? t('Unload')
        : t('Confirm');

      var selectAllBox = el('span', { class: 'batch-select-all-box', 'aria-hidden': 'true' });
      if (allSelected) selectAllBox.appendChild(checkmarkSvg());
      else if (someSelected) selectAllBox.appendChild(el('span', { class: 'batch-select-all-dash' }));

      var main = el('div', { class: 'batch-mode-footer-main' }, [
        el('button', {
          type: 'button',
          role: 'checkbox',
          'aria-checked': allSelected ? 'true' : (someSelected ? 'mixed' : 'false'),
          class: 'batch-select-all' + (allSelected ? ' checked' : '') + (someSelected ? ' indeterminate' : ''),
          'data-testid': 'ams-hotend-batch-select-all',
          disabled: total === 0,
          onClick: function () {
            if (total === 0) return;
            selectedKeys = {};
            if (!allSelected) {
              selectable.forEach(function (s) { selectedKeys[selectionKey(s)] = true; });
            }
            render();
          },
        }, [selectAllBox, el('span', { class: 'batch-select-all-label', text: t('Select All') })]),

        el('span', {
          class: 'batch-select-count',
          'data-testid': 'ams-hotend-batch-count',
          text: t('{{n}} selected', { n: count }),
        }),

        el('button', {
          type: 'button',
          class: 'batch-commit' + (canCommit ? ' active' : ''),
          'data-testid': 'ams-hotend-batch-commit',
          disabled: !canCommit,
          text: commitLabel,
          onClick: function () {
            if (!canCommit) return;
            BBL.store.runBatchAction(mode, selectable.filter(function (s) {
              return selectedKeys[selectionKey(s)];
            }));
          },
        }),
      ]);

      if (showEdit) {
        main.appendChild(el('button', {
          type: 'button',
          class: 'batch-edit',
          'data-testid': 'ams-hotend-batch-edit',
          disabled: count === 0,
          text: t('Edit Filament'),
          onClick: function () {
            if (count === 0) return;
            BBL.store.runSlotAction('batch_filament_edit', undefined, {
              intent: 'edit',
              mode: mode,
              slots: selectable.filter(function (s) {
                return selectedKeys[selectionKey(s)];
              }).map(function (s) {
                return {
                  addr_id: s.addr_id, ams_id: s.ams_id,
                  slot_id: s.slot_id, display_index: s.display_index,
                };
              }),
            });
          },
        }));
      }

      return el('footer', {
        class: 'batch-mode-footer',
        'data-testid': 'ams-hotend-batch-footer',
        'data-mode': mode,
      }, [
        main,
        el('button', {
          type: 'button',
          class: 'batch-exit',
          'data-testid': 'ams-hotend-batch-exit',
          text: t('Exit'),
          onClick: exitBatchMode,
        }),
      ]);
    }

    function renderPageFooter(state) {
      var actions = actionsAsBool(state.filament_page.actions);
      var changeActive = !!(state.filament_page.filament_change_status &&
        state.filament_page.filament_change_status.active);
      var tips = changeActive ? '' : actions.tips;

      var footer = el('footer', { class: 'page-footer' });
      if (global.__internalBuild) {
        footer.appendChild(el('button', {
          type: 'button',
          class: 'bridge-debug-button',
          disabled: debugOpening,
          text: debugOpening ? 'Opening...' : 'Debug',
          onClick: function () {
            debugOpening = true;
            render();
            BBL.store.openBridgeDebugDialog().then(function () {
              debugOpening = false;
              render();
            });
          },
        }));
      }
      if (tips) footer.appendChild(el('span', { class: 'footer-tip', text: tips }));
      return footer;
    }

    // ---- render ----
    function render() {
      var state = BBL.store.getState();
      if (!state) return;
      var mode = state.filament_page.mode || 'normal';
      var inBatchMode = mode !== 'normal';

      // Reset the selection whenever C++ switches the page mode, so it cannot
      // leak stale slot ids into a later batch_run payload.
      if (lastMode !== null && lastMode !== mode) selectedKeys = {};
      lastMode = mode;

      // Drop keys whose slot is no longer selectable: eligibility flips during
      // batch mode as RFID reads finish or loads progress.
      if (inBatchMode) {
        var valid = {};
        selectableSlots(state).forEach(function (s) { valid[selectionKey(s)] = true; });
        Object.keys(selectedKeys).forEach(function (key) {
          if (!valid[key]) delete selectedKeys[key];
        });
      }

      renderToolbar(state);

      // React only mounts `.page-error` when there is a message, and the CSS
      // reserves no space for an empty one.
      var error = BBL.store.getError();
      if (error && !errorEl) {
        errorEl = el('div', { class: 'page-error' });
        page.insertBefore(errorEl, toolbarEl.nextSibling);
      } else if (!error && errorEl) {
        page.removeChild(errorEl);
        errorEl = null;
      }
      if (errorEl) BBL.dom.setText(errorEl, error);

      // Detach rather than discard: stageEl / hotendPanelEl are persistent and
      // must keep their patched children across tab switches.
      bodyNodes.forEach(function (node) {
        if (node.parentNode === page) page.removeChild(node);
      });
      bodyNodes = [];
      if (tab === 'filament') {
        bodyNodes.push(updateStage(state));
        var fcs = state.filament_page.filament_change_status;
        var progress = hotend.renderFilamentChangeProgressPanel(fcs, handleToolbarAction);
        if (progress) bodyNodes.push(progress);
        if (fcs && fcs.mode === 'batch_manual_pull' && fcs.manual_pull) {
          var banner = renderManualPullBanner(fcs.manual_pull);
          if (banner) bodyNodes.push(banner);
        }
      } else {
        bodyNodes.push(updateHotendPanel(state));
      }
      bodyNodes.forEach(function (node) { page.appendChild(node); });

      if (footerNode && footerNode.parentNode === page) page.removeChild(footerNode);
      footerNode = inBatchMode ? renderBatchFooter(state) : renderPageFooter(state);
      page.appendChild(footerNode);

      syncExtModePoll(mode);
    }

    // External-filament editing happens in a separate C++ modal with its own
    // WebView/bridge, so the report emitted when that dialog closes never reaches
    // this page. Poll and refresh on focus while a load-selection mode is active.
    function syncExtModePoll(mode) {
      var wanted = mode === 'batch_ext_load' || mode === 'batch_ams_load';
      if (wanted && !extModePoll) {
        var refreshing = false;
        var refreshExtMode = function () {
          if (refreshing) return;
          refreshing = true;
          BBL.store.refresh(mode).then(function () { refreshing = false; },
                                       function () { refreshing = false; });
        };
        var timer = global.setInterval(refreshExtMode, 2000);
        global.addEventListener('focus', refreshExtMode);
        document.addEventListener('visibilitychange', refreshExtMode);
        extModePoll = function () {
          global.clearInterval(timer);
          global.removeEventListener('focus', refreshExtMode);
          document.removeEventListener('visibilitychange', refreshExtMode);
        };
      } else if (!wanted && extModePoll) {
        extModePoll();
        extModePoll = null;
      }
    }

    var unsubscribe = BBL.store.subscribe(render);
    render();

    return function teardown() {
      unsubscribe();
      stopLayout();
      if (extModePoll) { extModePoll(); extModePoll = null; }
      closeCardContextMenu();
      delete global.__exitBatchMode;
    };
  }

  BBL.pages = BBL.pages || {};
  BBL.pages.main = { mount: mount, selectionKey: selectionKey };
})(window);
