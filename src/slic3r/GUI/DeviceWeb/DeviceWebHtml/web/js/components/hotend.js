// Hotend-tab and progress-panel components.
//
// Ported from the React tree under ams-filament-hotend/components/:
// DryerChamber.tsx, HotendCard.tsx, HotendRackProgressPanel.tsx,
// HotendRackProgressVisual.tsx, FilamentChangeProgressPanel.tsx,
// BatchProgressStrip.tsx, ToolbarMoreMenu.tsx, CardContextMenu.tsx and
// useCardContextMenu.ts. Class names, data-testid values and SVG geometry are
// kept byte-identical so ams-filament-hotend.css applies unchanged.
(function (global) {
  'use strict';

  var BBL = global.BBL || (global.BBL = {});
  var el = BBL.dom.el;
  var t = BBL.t;

  // `a ?? b` equivalent: only null/undefined fall through to the fallback.
  function pick(value, fallback) {
    return value === undefined || value === null ? fallback : value;
  }

  // ==========================================================================
  // DryerChamber.tsx
  // ==========================================================================

  var COL = 80;

  function renderDryerChamberDecoration(options) {
    var startIndex = options.startIndex;
    var endIndex = options.endIndex;
    var totalSlots = options.totalSlots;
    var humidity = options.humidity;
    var status = options.status;
    var isDrying = options.isDrying;

    if (totalSlots !== 4) return null;
    // Top columns sit at odd multiples of `--slot-pitch` (see Stage.tsx).
    var startStr = 'calc(' + (1 + 2 * startIndex) + ' * var(--slot-pitch) - 4px)';
    var endStr = 'calc(' + (1 + 2 * endIndex) + ' * var(--slot-pitch) + ' + (COL + 4) + 'px)';

    var node = el('div', {
      class: 'dryer-chamber-bg' + (isDrying ? ' is-drying' : ''),
      'data-testid': 'ams-hotend-dryer-chamber',
    }, renderDryerInfo(humidity, status));
    node.style.setProperty('--chamber-left', startStr);
    node.style.setProperty('--chamber-right', endStr);
    return node;
  }

  function renderDryerInfo(humidity, status) {
    return el('div', { class: 'dryer-info', 'aria-label': 'dryer humidity and status' }, [
      el('div', { class: 'dryer-info-row' }, [
        dropletIcon(),
        el('span', { class: 'dryer-value' }, [
          String(humidity >= 0 ? humidity : '--'),
          el('small', { text: '%' }),
        ]),
      ]),
      el('div', { class: 'dryer-info-row' }, [
        dryerIcon(),
        el('span', { class: 'dryer-text', text: status || 'Idle' }),
      ]),
    ]);
  }

  function dropletIcon() {
    return el('svg', { width: 14, height: 14, viewBox: '0 0 16 16', 'aria-hidden': 'true' },
      el('path', {
        d: 'M8 2 C5 6 3 8.5 3 11 a5 5 0 1 0 10 0 c0 -2.5 -2 -5 -5 -9 z',
        fill: 'none',
        stroke: 'var(--ams-text-secondary)',
        'stroke-width': 1.2,
      }));
  }

  function dryerIcon() {
    return el('svg', { width: 14, height: 14, viewBox: '0 0 16 16', 'aria-hidden': 'true' }, [
      el('circle', { cx: 8, cy: 8, r: 6.2, fill: 'none', stroke: 'var(--ams-text-secondary)', 'stroke-width': 1.2 }),
      el('path', {
        d: 'M5 9 q1.5 -2 3 0 t3 0',
        fill: 'none',
        stroke: 'var(--ams-text-secondary)',
        'stroke-width': 1.2,
        'stroke-linecap': 'round',
      }),
    ]);
  }

  // Non-idle dryer states are shown as active.
  function isDryingStatus(status) {
    return !!status && status !== 'Idle' && status !== 'Off';
  }

  // Split each dryer zone into contiguous rendered slot runs.
  function findDryerRuns(slots, zones) {
    if (!zones || !zones.length) return [];
    var runs = [];

    zones.forEach(function (zone) {
      var ids = (zone.zone_slots || []).map(function (v) { return Number(v); })
        .filter(function (n) { return !isNaN(n); });
      if (!ids.length) return;
      var idSet = {};
      ids.forEach(function (n) { idSet[n] = true; });

      var matchedCols = [];
      (slots || []).forEach(function (slot, idx) {
        if (zone.ams_id && slot.ams_id && zone.ams_id !== slot.ams_id) return;
        if (idSet[Number(slot.slot_id)]) matchedCols.push(idx);
      });
      if (!matchedCols.length) return;

      var humidity = typeof zone.humidity_raw === 'number' && zone.humidity_raw >= 0 ? zone.humidity_raw : -1;
      var status = zone.dry_status_text || '';
      var isDrying = isDryingStatus(zone.dry_status) || (typeof zone.dry_time === 'number' && zone.dry_time > 0);

      var runStart = matchedCols[0];
      var prev = matchedCols[0];
      for (var i = 1; i <= matchedCols.length; i++) {
        var cur = matchedCols[i];
        if (cur === prev + 1) {
          prev = cur;
          continue;
        }
        runs.push({ start: runStart, end: prev, humidity: humidity, status: status, isDrying: isDrying });
        if (cur === undefined) break;
        runStart = cur;
        prev = cur;
      }
    });

    return runs;
  }

  // ==========================================================================
  // HotendCard.tsx
  // ==========================================================================

  function renderHotendCard(slot, globalBusy, onAction) {
    var hot = slot.hotend || {};
    var op = pick(hot.op_state, 'Idle');
    var isTaken = !!slot.taken;
    // Taken (on-extruder) uses its own muted UX; do not paint the green selected chrome.
    var isSelected = !isTaken && !!hot.selected;
    var toolKind = pick(hot.tool_kind, 'empty');
    var isEmpty = toolKind === 'empty';
    var isStatusReplaced = op === 'Reading' || op === 'Taking' || op === 'Placing' || op === 'Removing' || op === 'Error' || op === 'Unknown';
    var menuOpen = false;
    var editingTemp = false;
    var draftTemp = '';
    var tempInput = null;
    var skipBlurCommit = false;
    var isBusy = op === 'Reading' || op === 'Taking' || op === 'Placing' || op === 'Removing';
    var canOperate = slot.enabled && !isBusy && !globalBusy;
    var isNozzleTool = hot.tool_kind === 'nozzle';
    var menuActions = slot.menu_actions;
    var canRead = pick(menuActions && menuActions.can_read, canOperate);
    var canDetail = pick(menuActions && menuActions.can_detail, slot.enabled);
    var canSetTemperature = pick(menuActions && menuActions.can_set_temperature, canOperate && isNozzleTool && op !== 'Error');
    var canTakePlace = pick(menuActions && menuActions.can_take_place, canOperate);
    var hotendMoveAction = isTaken ? 'place_hotend' : 'take_hotend';
    var hotendMoveLabel = isTaken ? t('Place') : t('Take');

    var onDocPointerDown = function (event) {
      if (!menuWrap.contains(event.target)) setMenuOpen(false);
    };

    function setMenuOpen(next) {
      if (menuOpen === next) return;
      menuOpen = next;
      BBL.dom.toggleClass(menuButton, 'open', menuOpen);
      BBL.dom.setAttr(menuButton, 'aria-expanded', String(menuOpen));
      if (menuOpen) {
        menuWrap.appendChild(buildMenuPopover());
        document.addEventListener('pointerdown', onDocPointerDown);
      } else {
        document.removeEventListener('pointerdown', onDocPointerDown);
        var popover = menuWrap.querySelector('.hotend-menu-popover');
        if (popover) menuWrap.removeChild(popover);
      }
    }

    function setEditingTemp(next) {
      if (editingTemp === next) return;
      editingTemp = next;
      if (editingTemp) {
        skipBlurCommit = false;
        var target = typeof hot.target_temp === 'number' ? hot.target_temp : 0;
        draftTemp = String(target);
      }
      renderBody();
      if (editingTemp) {
        Promise.resolve().then(function () {
          if (!tempInput) return;
          tempInput.focus();
          tempInput.select();
        });
      }
    }

    function runMenuAction(action) {
      setMenuOpen(false);
      if (action === 'adjust_temperature') {
        if (canSetTemperature) setEditingTemp(true);
        return;
      }
      onAction(action, slot);
    }

    function submitTemperature() {
      if (!editingTemp) return;
      var parsed = Number(String(draftTemp).trim());
      if (!isFinite(parsed) || Math.floor(parsed) !== parsed || parsed < 0) {
        setEditingTemp(false);
        return;
      }
      parsed = Math.min(parsed, pick(hot.max_temp, 350));
      setEditingTemp(false);
      if (parsed !== pick(hot.target_temp, 0)) {
        onAction('set_hotend_temperature', slot, { target_temp: parsed });
      }
    }

    function cancelTemperatureEdit() {
      skipBlurCommit = true;
      draftTemp = String(pick(hot.target_temp, 0));
      setEditingTemp(false);
    }

    var cardCls = ['hotend-card'];
    cardCls.push('hotend-card-' + String(op).toLowerCase());
    if (isSelected) cardCls.push('selected');
    if (!slot.enabled) cardCls.push('disabled');
    if (isTaken) cardCls.push('taken');

    var indexCls = ['hotend-card-index'];
    if (isSelected) indexCls.push('selected');

    var menuButton = el('button', {
      class: 'hotend-card-menu',
      type: 'button',
      'aria-label': 'hotend menu ' + slot.display_index,
      'aria-haspopup': 'menu',
      'aria-expanded': 'false',
      onClick: function (e) {
        e.stopPropagation();
        setMenuOpen(!menuOpen);
      },
    }, menuDots());

    var menuWrap = el('span', { class: 'hotend-card-menu-wrap' }, menuButton);

    function buildMenuPopover() {
      return el('div', { class: 'hotend-menu-popover', role: 'menu' }, [
        el('button', { type: 'button', role: 'menuitem', disabled: !canSetTemperature, text: t('Set Temperature'), onClick: function () { runMenuAction('adjust_temperature'); } }),
        el('button', { type: 'button', role: 'menuitem', disabled: !canTakePlace, text: hotendMoveLabel, onClick: function () { runMenuAction(hotendMoveAction); } }),
        el('button', { type: 'button', role: 'menuitem', disabled: !canRead, text: t('Refresh'), onClick: function () { runMenuAction('read_hotend'); } }),
        el('button', { type: 'button', role: 'menuitem', disabled: !canDetail, text: t('Details'), onClick: function () { runMenuAction('hotend_detail'); } }),
      ]);
    }

    var bodyEl = el('div', { class: 'hotend-card-body' });

    function renderBody() {
      BBL.dom.clear(bodyEl);
      tempInput = null;

      if (isStatusReplaced) {
        bodyEl.appendChild(statusReadout(op));
      } else if (isTaken) {
        bodyEl.appendChild(takenReadout());
      } else if (isNozzleTool) {
        bodyEl.appendChild(tempReadout());
      } else if (isEmpty) {
        bodyEl.appendChild(emptyReadout());
      } else {
        bodyEl.appendChild(el('div', { class: 'hotend-readout hotend-readout-empty', 'aria-hidden': 'true' }));
      }

      bodyEl.appendChild(toolIcon(toolKind, op, op === 'Error', isTaken));
      bodyEl.appendChild(toolLabel(toolKind, hot.nozzle_label, hot.nozzle_subtext, op, isTaken));
    }

    function tempReadout() {
      var current = hot.current_temp;
      var target = hot.target_temp;
      var cur = typeof current === 'number' ? current : null;
      var tgt = typeof target === 'number' ? target : 0;
      var cls = ['hotend-readout', 'temp'];
      if (op === 'Heating') cls.push('heating');
      if (op === 'Cooling') cls.push('cooling');

      var firstLine = el('div', { class: 'hotend-readout-line' }, [
        el('span', { class: 'hotend-temp-current', text: cur === null ? '--' : String(cur) }),
        op === 'Heating' ? arrowIcon('up') : null,
        op === 'Cooling' ? arrowIcon('down') : null,
      ]);

      var secondLine = el('div', { class: 'hotend-readout-line' });
      if (editingTemp) {
        tempInput = el('input', {
          inputmode: 'numeric',
          pattern: '[0-9]*',
          'aria-label': 'hotend target temperature',
          onClick: function (event) { event.stopPropagation(); },
          onInput: function (event) {
            var cleaned = event.target.value.replace(/[^\d]/g, '');
            draftTemp = cleaned;
            if (event.target.value !== cleaned) event.target.value = cleaned;
          },
          onBlur: function () {
            if (skipBlurCommit) {
              skipBlurCommit = false;
              return;
            }
            submitTemperature();
          },
          onKeyDown: function (event) {
            if (event.key === 'Enter') {
              event.preventDefault();
              skipBlurCommit = true;
              submitTemperature();
            } else if (event.key === 'Escape') {
              event.preventDefault();
              cancelTemperatureEdit();
            }
          },
        });
        tempInput.value = draftTemp;
        secondLine.appendChild(el('span', { class: 'hotend-temp-target-edit' }, ['/', tempInput, '\u2103']));
      } else {
        secondLine.appendChild(el('button', {
          type: 'button',
          class: 'hotend-temp-target-button' + (canSetTemperature ? '' : ' disabled'),
          disabled: !canSetTemperature,
          text: '/' + tgt + '\u2103',
          onClick: function (event) {
            event.stopPropagation();
            if (canSetTemperature) setEditingTemp(true);
          },
        }));
      }

      return el('div', { class: cls.join(' ') }, [firstLine, secondLine]);
    }

    var card = el('div', {
      class: cardCls.join(' '),
      'data-testid': 'ams-hotend-hotend-card-' + slot.display_index,
      'data-op-state': op,
      onClick: function () { if (slot.enabled) onAction('select_hotend', slot); },
    }, [
      el('div', { class: 'hotend-card-head' }, [
        el('span', {
          class: indexCls.join(' '),
          'data-testid': 'ams-hotend-hotend-index-' + slot.display_index,
          text: String(slot.display_index),
        }),
        menuWrap,
      ]),
      bodyEl,
    ]);

    renderBody();
    return card;
  }

  // Empty rack position (tool_kind === 'empty'): grey "Empty" text where the
  // temperature normally is, per Figma 1882:112071.
  function emptyReadout() {
    return el('div', { class: 'hotend-readout status status-empty', 'data-testid': 'ams-hotend-empty' },
      el('span', { class: 'hotend-readout-text', text: t('Empty') }));
  }

  // Rack slot whose nozzle is currently mounted on the extruder (Figma Taken UX).
  function takenReadout() {
    return el('div', { class: 'hotend-readout status status-taken', 'data-testid': 'ams-hotend-taken' },
      el('span', { class: 'hotend-readout-text', text: t('Taken') }));
  }

  function statusReadout(op) {
    var text =
      op === 'Reading' ? t('Reading') :
      op === 'Taking' ? t('Taking') :
      op === 'Placing' ? t('Placing') :
      op === 'Removing' ? t('Removing') :
      op === 'Error' ? t('Error') :
      op === 'Unknown' ? t('Unknown') : '';
    return el('div', { class: 'hotend-readout status status-' + String(op).toLowerCase() },
      el('span', { class: 'hotend-readout-text', text: text }));
  }

  function menuDots() {
    return el('svg', { width: 16, height: 16, viewBox: '0 0 16 16', 'aria-hidden': 'true' }, [
      el('circle', { cx: 3, cy: 8, r: 1.2, fill: 'var(--ams-text-card-menu-dot)' }),
      el('circle', { cx: 8, cy: 8, r: 1.2, fill: 'var(--ams-text-card-menu-dot)' }),
      el('circle', { cx: 13, cy: 8, r: 1.2, fill: 'var(--ams-text-card-menu-dot)' }),
    ]);
  }

  function arrowIcon(dir) {
    return el('svg', {
      class: 'hotend-temp-arrow hotend-temp-arrow-' + dir,
      width: 12,
      height: 16,
      viewBox: '0 0 16 12',
      fill: 'none',
      'aria-hidden': 'true',
    }, el('path', {
      d: 'M13.8018 6.50384C13.9277 6.37403 14 6.19228 14 6.00189C14 5.81149 13.9277 5.63263 13.8018 5.49993L9.0875 0.653472C8.83036 0.388071 8.42321 0.39961 8.17946 0.67655C7.93571 0.953491 7.94375 1.39198 8.20089 1.6545L11.7527 5.30954H2.64286C2.28661 5.30954 2 5.61821 2 6.00189C2 6.38557 2.28661 6.69424 2.64286 6.69424H11.7527L8.19821 10.3464C7.94107 10.6118 7.93304 11.0474 8.17679 11.3243C8.42054 11.6013 8.82768 11.6099 9.08482 11.3474L13.7991 6.50096L13.8018 6.50384Z',
      fill: 'currentColor',
    }));
  }

  function toolIcon(kind, op, withError, taken) {
    if (op === 'Reading') {
      return el('div', { class: 'hotend-tool-icon-wrap' },
        el('img', { class: 'hotend-icon-loading', src: './assets/hotend_reading_arc.png', width: 28, height: 28, alt: 'reading' }));
    }
    // Taken rack slot: upload/out-of-tray icon instead of the nozzle body.
    if (taken) {
      return el('div', { class: 'hotend-tool-icon-wrap' },
        el('img', { class: 'hotend-icon-taken', src: './assets/hotend_taken.svg', width: 20, height: 20, alt: 'taken' }));
    }
    if (op === 'Unknown') {
      return el('div', { class: 'hotend-tool-icon-wrap hotend-icon-nozzle-wrap' },
        el('img', { class: 'hotend-icon-nozzle', src: './assets/hotend_nozzle_unknow.png', width: 36, height: 42, alt: 'unknown' }));
    }
    if (withError) {
      return el('div', { class: 'hotend-tool-icon-wrap hotend-icon-nozzle-wrap' },
        el('img', { class: 'hotend-icon-nozzle', src: './assets/hotend_nozzle_err.png', width: 36, height: 42, alt: 'error' }));
    }
    if (op === 'Lost') {
      return el('div', { class: 'hotend-tool-icon-wrap hotend-icon-nozzle-wrap' },
        el('img', { class: 'hotend-icon-nozzle', src: './assets/hotend_nozzle_lost.png', width: 36, height: 42, alt: 'lost' }));
    }
    if (kind === 'empty') {
      return el('div', { class: 'hotend-tool-icon-wrap' },
        el('img', { class: 'hotend-icon-forbidden', src: './assets/hotend_forbidden.svg', width: 20, height: 20, alt: 'empty' }));
    }
    if (kind === 'cutter' || kind === 'pen') {
      return el('div', { class: 'hotend-tool-icon-wrap' },
        el('img', { class: 'hotend-icon-nozzle', src: './assets/hotend_nozzle_disable.png', width: 36, height: 42, alt: kind }));
    }
    if (kind === 'laser' || kind === 'fan' || kind === 'unknown') {
      return el('div', { class: 'hotend-tool-icon-wrap' },
        el('div', {
          class: 'hotend-tool-icon-generic hotend-tool-icon-' + kind,
          'aria-hidden': 'true',
          text: kind === 'laser' ? 'L' : kind === 'fan' ? 'F' : '?',
        }));
    }
    return el('div', { class: 'hotend-tool-icon-wrap hotend-icon-nozzle-wrap' }, [
      el('img', { class: 'hotend-icon-nozzle', src: './assets/hotend_nozzle.png', width: 36, height: 42, alt: 'nozzle' }),
      (op === 'Placing' || op === 'Taking')
        ? el('img', { class: 'hotend-placing-arrow', src: './assets/hotend_placing_arrow.svg', width: 9, height: 32, alt: 'placing' })
        : null,
    ]);
  }

  function toolLabel(kind, label, subtext, op, taken) {
    var isMuted = taken || kind === 'empty' || kind === 'unknown' || op === 'Reading' || op === 'Unknown';
    // Taken / Unknown: no reliable label - always show "--/--".
    var primary = (taken || op === 'Unknown') ? '--' : pick(label, isMuted ? '--' : '');
    var secondary = (taken || op === 'Unknown') ? '--' : pick(subtext, isMuted ? '--' : '');
    return el('div', { class: 'hotend-tool-label' + (isMuted ? ' muted' : '') }, [
      el('div', { class: 'hotend-tool-name', text: primary || '\u00A0' }),
      el('div', { class: 'hotend-tool-sub', text: secondary || '\u00A0' }),
    ]);
  }

  // ==========================================================================
  // HotendRackProgressPanel.tsx / HotendRackProgressVisual.tsx
  // ==========================================================================

  function progressTitle(status) {
    var rawIndex = status.target_display_index || status.display_index;
    var total = status.total || 0;
    var n = rawIndex > 0 && (total <= 0 || rawIndex <= total) ? rawIndex : 0;
    if (status.operation === 'read_all') {
      return status.message || t('Reading ({{current}} / {{total}})', { current: status.current, total: status.total });
    }
    if (status.operation === 'take' || status.operation === 'exchange') {
      return n ? t('Hotend {{n}} taking', { n: n }) : t('Taking hotend');
    }
    if (status.operation === 'place') {
      return n ? t('Hotend {{n}} placing', { n: n }) : t('Placing hotend');
    }
    if (status.operation === 'read_single') {
      return status.message || (n ? t('Reading hotend {{n}}', { n: n }) : t('Reading hotend'));
    }
    return status.message || t('Hotend rack is busy');
  }

  function rackStepLabel(step) {
    switch (step.id) {
      case 1: return t('Homing PrintTool Rack');
      case 2: return t('Homing Toolhead');
      case 3: return t('Calibrating Hotend Rack');
      case 4: return t('Waiting for hotend to heat');
      case 5: return t('Cutting Filament');
      case 6: return t('Unlocking Hotend');
      case 7: return t('Lifting Hotend Rack');
      case 8: return t('Nesting Tool');
      case 9: return t('Fetching Tool');
      case 10: return t('Locking Tool');
      case 11: return t('Moving PrintTool Rack');
      case 12: return t('Refresh Tool Info');
      default: return step.label ? t(step.label) : t('Step {{n}}', { n: step.id });
    }
  }

  function renderHotendRackProgressVisual(operation) {
    var shouldShowPreview = operation === 'take'
      || operation === 'place'
      || operation === 'exchange'
      || operation === 'read_all'
      || operation === 'read_single';
    if (!shouldShowPreview) return null;

    return el('div', { class: 'hotend-rack-progress-visual-card', 'aria-hidden': 'true' },
      el('img', { class: 'rack-visual-hotend-job-preview', src: './assets/rack_hotend_job_preview.svg', alt: '' }));
  }

  function renderHotendRackProgressPanel(status, onAction) {
    if (!status || !status.active) return null;

    var steps = status.steps || [];

    var actions = el('div', { class: 'hotend-rack-progress-actions' });
    if (status.can_retry) {
      actions.appendChild(el('button', {
        type: 'button',
        class: 'primary',
        text: t('Retry'),
        onClick: function () { if (onAction) onAction('retry_hotend_operation'); },
      }));
    }
    actions.appendChild(el('button', {
      type: 'button',
      disabled: !status.can_stop,
      text: t('Stop'),
      onClick: function () { if (onAction) onAction('stop_hotend_operation'); },
    }));

    return el('section', { class: 'hotend-rack-progress', 'data-testid': 'ams-hotend-rack-progress' }, [
      el('div', { class: 'hotend-rack-progress-bar', 'data-testid': 'ams-hotend-reading-status' }, [
        el('span', { class: 'hotend-rack-status-spinner', 'aria-hidden': 'true' }),
        el('span', { text: progressTitle(status) }),
      ]),

      el('div', { class: 'hotend-rack-progress-detail' }, [
        el('div', { class: 'hotend-rack-progress-steps' }, steps.map(function (step, index) {
          return el('div', { class: 'hotend-rack-progress-step ' + step.state }, [
            el('span', {
              class: 'hotend-rack-progress-mark',
              text: step.state === 'done' ? '\u2713' : step.state === 'failed' ? '\u00D7' : String(index + 1),
            }),
            el('span', { class: 'hotend-rack-progress-label', text: rackStepLabel(step) }),
          ]);
        })),
        el('div', { class: 'hotend-rack-progress-visual', 'aria-hidden': 'true' },
          renderHotendRackProgressVisual(status.operation)),
      ]),

      actions,
    ]);
  }

  // ==========================================================================
  // BatchProgressStrip.tsx
  // ==========================================================================

  // Horizontal step strip that sits between the slot stage and the per-step
  // progress panel during a multi-slot filament change run. Mirrors Figma
  // 1670:80943.
  //
  // Layout: pips and connectors are siblings of the strip flex container
  // (no per-cell wrapper). The connectors carry `flex: 1 1 0` with a
  // 5px..20px clamp so they auto-fill when the panel is wide and shrink
  // gracefully when many slots are queued. `justify-content: center` then
  // centers the whole group inside the panel.
  function renderBatchProgressStrip(items) {
    if (!items || !items.length) return null;
    var children = [];
    items.forEach(function (item, index) {
      var isLast = index === items.length - 1;
      children.push(el('span', {
        class: 'batch-progress-pip state-' + item.state,
        title: item.reason || t('Slot {{n}}', { n: item.display_index }),
        'data-testid': 'ams-batch-progress-pip-' + item.display_index,
        text: item.state === 'failed' ? '\u00D7' : String(item.display_index),
      }));
      if (!isLast) children.push(el('span', { class: 'batch-progress-connector', 'aria-hidden': 'true' }));
    });
    return el('div', {
      class: 'batch-progress-strip',
      role: 'progressbar',
      'aria-label': t('Batch progress'),
      'data-testid': 'ams-batch-progress-strip',
    }, children);
  }

  // ==========================================================================
  // FilamentChangeProgressPanel.tsx
  // ==========================================================================

  var STEP_IDLE = 0x00;
  var STEP_PAUSE = 0x01;
  var STEP_HEAT_NOZZLE = 0x02;
  var STEP_CUT_FILAMENT = 0x03;
  var STEP_PULL_CURR_FILAMENT = 0x04;
  var STEP_PUSH_NEW_FILAMENT = 0x05;
  var STEP_GRAB_NEW_FILAMENT = 0x06;
  var STEP_PURGE_OLD_FILAMENT = 0x07;
  var STEP_CHECK_POSITION = 0x08;
  var STEP_SWITCH_EXTRUDER = 0x09;
  var STEP_SWITCH_HOTEND = 0x0A;
  var STEP_AMS_FILA_COOLING = 0x0B;
  var STEP_PUSH_SWITCHER_FILA = 0x0C;
  var STEP_PULL_SWITCHER_FILA = 0x0D;
  var STEP_SWITCHER_SWITCH = 0x0E;
  var STEP_BATCH_PULL_FILA = 0x0F;

  var MANUAL_CARD_STEP_STYLES = { manual_load: 1, maunal_unload: 1 };

  // Literal t() calls so i18next-parser can extract keys. Copy matches
  // C++ filament_step_label() / gettext catalog.
  function filamentStepLabel(stepId) {
    switch (stepId) {
      case STEP_IDLE: return t('Idling...');
      case STEP_PAUSE: return t('Pause');
      case STEP_HEAT_NOZZLE: return t('Heat the nozzle');
      case STEP_CUT_FILAMENT: return t('Cut filament');
      case STEP_PULL_CURR_FILAMENT: return t('Pull back current filament');
      case STEP_PUSH_NEW_FILAMENT: return t('Push new filament into extruder');
      case STEP_GRAB_NEW_FILAMENT: return t('Grab new filament');
      case STEP_PURGE_OLD_FILAMENT: return t('Purge old filament');
      case STEP_CHECK_POSITION: return t('Check filament location');
      case STEP_SWITCH_EXTRUDER: return t('Switch extruder');
      case STEP_SWITCH_HOTEND: return t('Switch hotend');
      case STEP_AMS_FILA_COOLING: return t('Wait for AMS cooling');
      case STEP_PUSH_SWITCHER_FILA: return t('Switch current filament at Filament Track Switch');
      case STEP_PULL_SWITCHER_FILA: return t('Pull back current filament at Filament Track Switch');
      case STEP_SWITCHER_SWITCH: return t('Switch track at Filament Track Switch');
      case STEP_BATCH_PULL_FILA: return t('Batch pull back the filaments');
      default: return '';
    }
  }

  // Data layer uses unified `change` semantics; UI label is the same regardless.
  function operationText() {
    return t('Loading/Unloading Filament');
  }

  function displaySlotName(status) {
    var slot = status.running_slot;
    if (!slot) return '';
    return slot.ams_id && slot.ams_id !== '0'
      ? 'AMS-' + slot.ams_id + (Number(slot.slot_id) + 1)
      : t('Slot {{n}}', { n: slot.display_index });
  }

  function resolveSteps(status) {
    var firmwareSteps = status.steps || [];
    var sourceSteps = firmwareSteps.length
      ? firmwareSteps
      : (typeof status.current_step === 'number' ? [status.current_step] : []);

    return sourceSteps.map(function (step, index) {
      var stepId = typeof step === 'number' ? step : step.id;
      var stepLabel = typeof step === 'number' ? '' : pick(step.label, '');
      var stepStyle = typeof step === 'number' ? undefined : step.step_style;
      var stepStyleText = typeof step === 'number' ? undefined : step.step_style_text;
      var stepText = typeof step === 'number' ? undefined : step.step_text;
      return {
        id: stepId,
        label: filamentStepLabel(stepId)
          || (status.step_labels ? status.step_labels[index] : undefined)
          || stepLabel
          || (stepId === status.current_step ? status.current_step_name : '')
          || t('Step {{n}}', { n: stepId }),
        step_style: stepStyle,
        step_style_text: stepStyleText,
        step_text: stepText,
      };
    });
  }

  function resolveActiveIndex(status, steps) {
    if (typeof status.current_step === 'number') {
      var byStep = -1;
      steps.forEach(function (step, index) {
        if (byStep < 0 && step.id === status.current_step) byStep = index;
      });
      if (byStep >= 0) return byStep;
    }
    if (typeof status.current_step_index === 'number' && status.current_step_index > 0) {
      return Math.min(status.current_step_index - 1, steps.length - 1);
    }
    return 0;
  }

  function renderFilamentChangeProgressPanel(status, onAction) {
    if (!(status && status.active) && !(status && status.mode === 'batch_reserved')) return null;
    if (status.mode === 'batch_manual_pull') return null;

    if (status.mode === 'batch_reserved') {
      var items = (status.batch_reserved && status.batch_reserved.items) || [];
      var summary = status.batch_reserved;
      var header = el('div', { class: 'filament-change-header' }, [
        el('div', {
          class: 'filament-change-title',
          'data-testid': 'ams-filament-change-title',
          text: t('Batch {{op}} reserved', { op: operationText() }),
        }),
        (summary && typeof summary.current === 'number' && typeof summary.total === 'number')
          ? el('div', { class: 'filament-change-meta' }, [String(summary.current), ' / ', String(summary.total)])
          : null,
      ]);
      var reserved = el('section', {
        class: 'filament-change-panel filament-change-panel-reserved',
        'data-testid': 'ams-filament-change-panel',
      }, header);
      if (items.length) {
        reserved.appendChild(el('div', { class: 'filament-change-batch-list' }, items.map(function (item) {
          return el('span', {
            class: 'filament-change-batch-item state-' + item.state,
            text: t('Slot {{n}}', { n: item.display_index }),
          });
        })));
      }
      return reserved;
    }

    var slotName = displaySlotName(status);
    var op = operationText();
    // Batch-run mode reuses the per-step layout below for the currently-
    // active slot but prepends a horizontal step strip so the user can see
    // overall queue progress. C++ only emits `batch_running.items` when
    // >=2 slots are queued, so a 1-slot batch falls back to plain single.
    var batchItems = status.mode === 'batch_running'
      ? ((status.batch_running && status.batch_running.items) || [])
      : [];
    var showBatchStrip = batchItems.length >= 2;
    // Title reflects the run scope: in batch mode, the per-slot detail is
    // already conveyed by the strip + step list, so the bar collapses to a
    // generic "Batch ... in progress" message; single-slot keeps the
    // firmware-supplied message or the slot-specific fallback.
    var title = showBatchStrip
      ? t('Batch filament {{op}} in progress', { op: op })
      : status.message
        || (slotName
          ? t('{{slot}} filament {{op}} in progress', { slot: slotName, op: op })
          : t('Filament {{op}} in progress', { op: op }));
    var steps = resolveSteps(status);
    var activeIndex = resolveActiveIndex(status, steps);

    var section = el('section', { class: 'filament-change-panel', 'data-testid': 'ams-filament-change-panel' });
    if (showBatchStrip) {
      var strip = renderBatchProgressStrip(batchItems);
      if (strip) section.appendChild(strip);
    }

    section.appendChild(el('div', { class: 'filament-change-status-bar' },
      el('div', { class: 'filament-change-title', 'data-testid': 'ams-filament-change-title' }, [
        el('span', { class: 'filament-change-spinner' }),
        title,
      ])));

    section.appendChild(el('div', { class: 'filament-change-detail' }, [
      el('div', { class: 'filament-change-task-list', 'data-testid': 'ams-filament-change-step' },
        steps.map(function (step, index) {
          var state = index < activeIndex ? 'done' : (index === activeIndex ? 'active' : 'pending');
          return el('div', { class: 'filament-change-task-row ' + state }, [
            el('span', {
              class: 'filament-change-task-mark',
              text: state === 'done' ? '\u2713' : String(index + 1),
            }),
            el('span', { class: 'filament-change-task-label', text: step.label }),
            (step.step_style && MANUAL_CARD_STEP_STYLES[step.step_style])
              ? el('div', { class: 'filament-change-manual-card' }, [
                el('div', { class: 'filament-change-manual-title', text: t('Manual action required') }),
                el('div', {
                  class: 'filament-change-manual-text',
                  text: step.step_style_text || step.step_text || t("Insert the filament into the slot's loading port. Push gently until it stops or you hear a soft click."),
                }),
              ])
              : null,
          ]);
        })),

      el('div', { class: 'filament-change-hotend-visual', 'aria-hidden': 'true' },
        BBL.components.nozzle.renderNozzle({
          nozzleState: 'Active',
          displayIndex: (status.running_slot && status.running_slot.display_index) || 0,
        })),
    ]));

    var changeActions = el('div', { class: 'filament-change-actions' });
    if (status.can_retry) {
      changeActions.appendChild(el('button', {
        type: 'button',
        class: 'primary',
        text: t('Retry'),
        onClick: function () { if (onAction) onAction('retry_filament_change'); },
      }));
    }
    changeActions.appendChild(el('button', {
      type: 'button',
      disabled: status.can_stop === false,
      text: t('Stop'),
      onClick: function () { if (onAction) onAction('stop_filament_change'); },
    }));
    section.appendChild(changeActions);

    return section;
  }

  // ==========================================================================
  // ToolbarMoreMenu.tsx
  // ==========================================================================

  // The "More" (...) icon button plus its dropdown. Collects the actions that
  // used to sit inline on the toolbar / footer (Load / External Load / Unload /
  // Dry / Auto-refill) behind a single icon.
  function renderToolbarMoreMenu(actions, onAction) {
    var open = false;
    var menu = null;

    // Mirror the previous per-action gating: `Load` (auto load) follows the
    // show/hide flag; External / Unload disable when unavailable;
    // Auto-refill only appears when the AMS supports it.
    var showLoad = pick(actions.can_show_load_auto, true);
    var canLoadExternal = pick(actions.can_load_external, pick(actions.can_load, false));
    var showDry = pick(actions.can_dry, false);

    var onDocPointer = function (e) {
      if (root && !root.contains(e.target)) setOpen(false);
    };
    var onKey = function (e) {
      if (e.key === 'Escape') setOpen(false);
    };

    function run(action) {
      setOpen(false);
      onAction(action);
    }

    function buildMenu() {
      var children = [];
      if (showLoad) {
        children.push(el('button', {
          type: 'button',
          role: 'menuitem',
          'data-testid': 'ams-hotend-action-load-auto',
          text: t('Load'),
          onClick: function () { run('load_auto'); },
        }));
      }
      children.push(el('button', {
        type: 'button',
        role: 'menuitem',
        'data-testid': 'ams-hotend-action-load-external',
        disabled: !canLoadExternal,
        text: t('Manual Loading'),
        onClick: function () { run('load_external'); },
      }));
      children.push(el('button', {
        type: 'button',
        role: 'menuitem',
        'data-testid': 'ams-hotend-action-unload',
        disabled: !actions.can_unload,
        text: t('Unload'),
        onClick: function () { run('unload'); },
      }));
      if (showDry) {
        children.push(el('button', {
          type: 'button',
          role: 'menuitem',
          'data-testid': 'ams-hotend-action-dry',
          text: t('Dry'),
          onClick: function () { run('dry'); },
        }));
      }
      if (actions.show_auto_refill) {
        children.push(el('button', {
          type: 'button',
          role: 'menuitem',
          'data-testid': 'ams-hotend-action-auto-refill',
          text: t('Auto-refill'),
          onClick: function () { run('auto_refill'); },
        }));
      }
      return el('div', { class: 'toolbar-more-menu', role: 'menu', 'data-testid': 'ams-hotend-more-menu' }, children);
    }

    function setOpen(next) {
      if (open === next) return;
      open = next;
      BBL.dom.setAttr(trigger, 'aria-expanded', String(open));
      if (open) {
        menu = buildMenu();
        root.appendChild(menu);
        document.addEventListener('mousedown', onDocPointer);
        document.addEventListener('keydown', onKey);
      } else {
        document.removeEventListener('mousedown', onDocPointer);
        document.removeEventListener('keydown', onKey);
        if (menu && menu.parentNode === root) root.removeChild(menu);
        menu = null;
      }
    }

    var trigger = el('button', {
      type: 'button',
      class: 'toolbar-icon-btn',
      'data-testid': 'ams-hotend-action-more',
      'aria-haspopup': 'menu',
      'aria-expanded': 'false',
      title: t('More'),
      onClick: function () { setOpen(!open); },
    }, el('img', { src: './assets/toolbar_more.svg', alt: '', 'aria-hidden': 'true' }));

    var root = el('div', { class: 'toolbar-more' }, trigger);
    return root;
  }

  // ==========================================================================
  // CardContextMenu.tsx + useCardContextMenu.ts
  // ==========================================================================

  var VIEWPORT_MARGIN = 4;
  // Above .stage slot columns and batch dialogs so the menu is never clipped by siblings.
  var CONTEXT_MENU_Z_INDEX = 1100;

  /** Resolve menu item visibility from Schema menu_actions (with local fallbacks). */
  function resolveCardMenuFlags(slot) {
    var lineState = slot.filament_line.state;
    var cardState = slot.filament_card.state;
    var m = slot.menu_actions;
    return {
      canEdit: pick(m && m.can_edit, cardState !== 'Empty'),
      canLoad: pick(m && m.can_load, lineState === 'no_filament' && cardState !== 'Empty'),
      canUnload: pick(m && m.can_unload, lineState === 'loaded'),
      canRead: pick(m && m.can_read, cardState !== 'Reading' && cardState !== 'Empty'),
      canAmsOperation: pick(m && m.can_ams_operation, pick(slot.bind_ams_count, 0) > 0),
      readLabel: pick(m && m.read_label, 'Read'),
    };
  }

  function hasCardContextMenuItems(slot) {
    var flags = resolveCardMenuFlags(slot);
    return flags.canEdit || flags.canLoad || flags.canUnload || flags.canRead || flags.canAmsOperation;
  }

  /**
   * Floating context menu mounted on document.body at a fixed (x, y) anchor.
   * Owns the viewport-edge clamping so the popover never spills off-screen,
   * plus the outside-click / Escape / scroll dismissal that lived in
   * useCardContextMenu. Returns the node (with a `closeMenu()` method) or null
   * when the slot has no actionable items.
   */
  function renderCardContextMenu(slot, anchor, onAction, onClose) {
    var flags = resolveCardMenuFlags(slot);

    // Bail out when the slot has no actionable items so right-click on a
    // fully-empty slot doesn't pop a useless empty box.
    if (!flags.canEdit && !flags.canLoad && !flags.canUnload && !flags.canRead && !flags.canAmsOperation) {
      return null;
    }

    var closed = false;

    function close() {
      if (closed) return;
      closed = true;
      document.removeEventListener('pointerdown', onPointerDown);
      document.removeEventListener('keydown', onKeyDown);
      global.removeEventListener('scroll', onScroll, true);
      global.removeEventListener('resize', onScroll);
      if (node.parentNode) node.parentNode.removeChild(node);
      if (onClose) onClose();
    }

    function run(action) {
      close();
      onAction(action, slot);
    }

    var items = [];
    if (flags.canEdit) items.push(el('button', { type: 'button', role: 'menuitem', text: t('Edit'), onClick: function () { run('edit'); } }));
    if (flags.canLoad) items.push(el('button', { type: 'button', role: 'menuitem', text: t('Load'), onClick: function () { run('load'); } }));
    if (flags.canUnload) items.push(el('button', { type: 'button', role: 'menuitem', text: t('Unload'), onClick: function () { run('unload'); } }));
    if (flags.canRead) items.push(el('button', { type: 'button', role: 'menuitem', text: t(flags.readLabel), onClick: function () { run('read'); } }));
    if (flags.canAmsOperation) items.push(el('button', { type: 'button', role: 'menuitem', text: t('Connected Slots'), onClick: function () { run('ams_operation'); } }));

    var node = el('div', {
      class: 'card-context-menu',
      role: 'menu',
      'data-card-context-menu': 'true',
      'data-testid': 'ams-hotend-slot-' + slot.display_index + '-context-menu',
      // Stop the bubble: clicks inside the menu shouldn't trigger the card's
      // own click-to-select handler.
      onClick: function (e) { e.stopPropagation(); },
    }, items);
    node.style.position = 'fixed';
    node.style.left = anchor.x + 'px';
    node.style.top = anchor.y + 'px';
    node.style.zIndex = String(CONTEXT_MENU_Z_INDEX);

    // Close on any pointerdown outside the menu element itself (the menu
    // tags its root with data-card-context-menu="true" so we can identify it
    // without holding a ref).
    var onPointerDown = function (event) {
      var target = event.target;
      if (target && target.closest && target.closest('[data-card-context-menu]')) return;
      close();
    };
    var onKeyDown = function (event) {
      if (event.key === 'Escape') close();
    };
    var onScroll = function () { close(); };

    document.body.appendChild(node);

    // Clamp inside the viewport after the menu measures itself; flips to the
    // left / above the anchor when there isn't enough room to the right / below.
    var rect = node.getBoundingClientRect();
    var left = anchor.x;
    var top = anchor.y;
    var maxLeft = global.innerWidth - rect.width - VIEWPORT_MARGIN;
    var maxTop = global.innerHeight - rect.height - VIEWPORT_MARGIN;
    if (left > maxLeft) left = Math.max(VIEWPORT_MARGIN, anchor.x - rect.width);
    if (top > maxTop) top = Math.max(VIEWPORT_MARGIN, anchor.y - rect.height);
    node.style.left = left + 'px';
    node.style.top = top + 'px';

    document.addEventListener('pointerdown', onPointerDown);
    document.addEventListener('keydown', onKeyDown);
    global.addEventListener('scroll', onScroll, true);
    global.addEventListener('resize', onScroll);

    node.closeMenu = close;
    return node;
  }

  BBL.components = BBL.components || {};
  BBL.components.hotend = {
    findDryerRuns: findDryerRuns,
    renderDryerChamberDecoration: renderDryerChamberDecoration,
    renderHotendCard: renderHotendCard,
    renderHotendRackProgressPanel: renderHotendRackProgressPanel,
    renderFilamentChangeProgressPanel: renderFilamentChangeProgressPanel,
    renderBatchProgressStrip: renderBatchProgressStrip,
    renderToolbarMoreMenu: renderToolbarMoreMenu,
    renderCardContextMenu: renderCardContextMenu,
    hasCardContextMenuItems: hasCardContextMenuItems,
  };
})(window);
