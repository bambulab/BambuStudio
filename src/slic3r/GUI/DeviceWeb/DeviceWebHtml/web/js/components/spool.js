// Spool graphics and card chrome shared by the AMS filament / hotend pages.
//
// Ported from SpoolGraphic.tsx, SpoolDisc.tsx, IndexPill.tsx, CardText.tsx,
// LoadingArcIcon.tsx, PrefeedIndicator.tsx and BatchSlotCheckbox.tsx. The DOM
// shape, class names, data-testid values and SVG geometry are kept identical
// because ams-filament-hotend.css is reused verbatim from the React build.
(function (global) {
  'use strict';

  var BBL = global.BBL || (global.BBL = {});
  var el = BBL.dom.el;
  var t = BBL.t;

  var FILAMENT_TAG_URL = BBL.inlineAssets.SPOOL_FILAMENT_TAG;
  var UNKNOWN_SPOOL_URL = BBL.inlineAssets.SPOOL_UNKNOWN;
  var PREFEED_UNKNOWN_URL = BBL.inlineAssets.PREFEED_UNKNOWN;

  var SPOOL_SIZE = 52;
  var SPOOL_OUTER = 26;
  var SPOOL_RING = 23;
  var SPOOL_HOLE = 9;

  // Stands in for React's useId(): every rendered spool needs its own mask /
  // gradient / pattern ids, otherwise a duplicate id makes all but the first
  // spool reuse the wrong mask.
  var uidCounter = 0;
  function nextUid() {
    uidCounter += 1;
    return 'r' + uidCounter;
  }

  // ---- colour helpers ----

  function normalizeColors(colors) {
    return colors && colors.length ? colors.map(normalizeColor) : ['#D9D9D9'];
  }

  function normalizeColor(raw) {
    if (!raw) return '#D9D9D9';
    if (raw === 'transparent') return raw;
    return raw[0] === '#' ? raw : '#' + raw;
  }

  function alphaFromHex(color) {
    if (!/^#[0-9a-fA-F]{8}$/.test(color)) return null;
    return Number.parseInt(color.slice(7, 9), 16);
  }

  function isTransparentColor(color) {
    if (color === 'transparent') return true;
    var alpha = alphaFromHex(color);
    return alpha !== null && alpha < 255;
  }

  function opaqueColor(color) {
    if (color === 'transparent') return '#9ed3ae';
    if (/^#[0-9a-fA-F]{8}$/.test(color)) return color.slice(0, 7);
    return color;
  }

  function colorOpacity(color) {
    var alpha = alphaFromHex(color);
    return alpha === null ? 1 : Math.max(0.2, alpha / 255);
  }

  // Firmware reports the colour list from the inside of the spool outwards, so
  // colors[0] must land on the inner edge of the visible ring (r = SPOOL_HOLE)
  // and colors[N-1] on its outer edge (r = SPOOL_RING). Spreading the stops over
  // the full 0..SPOOL_RING radius instead buries the first colour(s) under the
  // centre hole.
  var RING_INNER_OFFSET = SPOOL_HOLE / SPOOL_RING;

  function gradientStopOffset(index, total) {
    if (total <= 1) return 100;
    return (RING_INNER_OFFSET + (1 - RING_INNER_OFFSET) * (index / (total - 1))) * 100;
  }

  // ---- SpoolGraphic.tsx ----

  function renderRemainMask(id, center, pct) {
    var ringRadius = (SPOOL_RING + SPOOL_HOLE) / 2;
    var stroke = SPOOL_RING - SPOOL_HOLE;
    var circumference = 2 * Math.PI * ringRadius;
    var filled = (pct / 100) * circumference;
    var gap = circumference - filled;
    return el('mask', { id: id, maskUnits: 'userSpaceOnUse' }, [
      el('rect', { x: 0, y: 0, width: SPOOL_SIZE, height: SPOOL_SIZE, fill: 'black' }),
      pct >= 100
        ? el('circle', { cx: center, cy: center, r: SPOOL_RING, fill: 'white' })
        : el('g', { transform: 'rotate(-90 ' + center + ' ' + center + ')' },
            el('circle', {
              cx: center,
              cy: center,
              r: ringRadius,
              fill: 'none',
              stroke: 'white',
              'stroke-width': stroke,
              'stroke-dasharray': filled + ' ' + gap,
            })),
    ]);
  }

  function renderCheckerPattern(id) {
    var tile = 6;
    return el('pattern', { id: id, width: tile, height: tile, patternUnits: 'userSpaceOnUse' }, [
      el('rect', { width: tile, height: tile, fill: '#dfeee4' }),
      el('rect', { width: tile / 2, height: tile / 2, fill: '#9ed3ae' }),
      el('rect', { x: tile / 2, y: tile / 2, width: tile / 2, height: tile / 2, fill: '#9ed3ae' }),
    ]);
  }

  // colors[0] is the innermost band, colors[N-1] the outermost one. The discs are
  // painted from the outside in so each smaller one covers the previous.
  function renderConcentricRings(center, colors) {
    var band = (SPOOL_RING - SPOOL_HOLE) / colors.length;
    return colors.map(function (_, index) {
      var color = colors[colors.length - 1 - index];
      return el('circle', {
        cx: center,
        cy: center,
        r: SPOOL_RING - band * index,
        fill: opaqueColor(color),
        'fill-opacity': colorOpacity(color),
      });
    });
  }

  function renderMaterialFill(center, colors, colorType, gradientId) {
    if (colorType === 'gradient') {
      return el('circle', { cx: center, cy: center, r: SPOOL_RING, fill: 'url(#' + gradientId + ')' });
    }
    if (colors.length > 1) {
      return renderConcentricRings(center, colors);
    }
    return el('circle', {
      cx: center,
      cy: center,
      r: SPOOL_RING,
      fill: opaqueColor(colors[0]),
      'fill-opacity': colorOpacity(colors[0]),
    });
  }

  // Returns the fragment SpoolGraphic / SpoolDisc splice into their <svg>.
  function renderMaterialDonut(center, colors, colorType, pct, idPrefix) {
    var maskId = idPrefix + '-remain';
    var gradientId = idPrefix + '-gradient';
    var checkerId = idPrefix + '-checker';
    var transparent = colors.some(isTransparentColor);
    var safePct = Math.max(0, Math.min(100, pct));

    var defsChildren = [renderRemainMask(maskId, center, safePct)];
    if (colorType === 'gradient') {
      var stops = [
        // Flat lead-in across the hidden centre so the inner edge of the
        // visible ring starts exactly on colors[0].
        el('stop', {
          offset: '0%',
          'stop-color': opaqueColor(colors[0]),
          'stop-opacity': colorOpacity(colors[0]),
        }),
      ];
      colors.forEach(function (c, i) {
        stops.push(el('stop', {
          offset: gradientStopOffset(i, colors.length) + '%',
          'stop-color': opaqueColor(c),
          'stop-opacity': colorOpacity(c),
        }));
      });
      defsChildren.push(el('radialGradient', {
        id: gradientId,
        cx: center,
        cy: center,
        r: SPOOL_RING,
        fx: center,
        fy: center,
        gradientUnits: 'userSpaceOnUse',
      }, stops));
    }
    if (transparent) defsChildren.push(renderCheckerPattern(checkerId));

    var maskedGroup = el('g', { mask: 'url(#' + maskId + ')' }, [
      transparent ? el('circle', { cx: center, cy: center, r: SPOOL_RING, fill: 'url(#' + checkerId + ')' }) : null,
      renderMaterialFill(center, colors, colorType, gradientId),
    ]);

    return [
      el('defs', null, defsChildren),
      el('circle', { cx: center, cy: center, r: SPOOL_RING, fill: 'var(--ams-overlay-bg)' }),
      maskedGroup,
      el('circle', { cx: center, cy: center, r: SPOOL_RING, fill: 'none', stroke: 'var(--color-fm-chip-ring)', 'stroke-width': 1 }),
      el('circle', { cx: center, cy: center, r: SPOOL_HOLE, fill: 'var(--ams-spool-hole-bg)' }),
      el('circle', { cx: center, cy: center, r: SPOOL_HOLE, fill: 'none', stroke: 'var(--ams-border)', 'stroke-width': 1 }),
    ];
  }

  // Standalone filled spool: outer body ring + colored donut + glossy tag. Driven
  // only by colors / colorType / remain so any feature can render a spool without
  // synthesizing a full slot view-model.
  function renderSpoolGraphic(options) {
    var opts = options || {};
    var size = opts.size === undefined ? SPOOL_SIZE : opts.size;
    var remain = opts.remain === undefined ? 100 : opts.remain;
    var showTag = opts.showTag === undefined ? true : opts.showTag;
    var center = SPOOL_SIZE / 2;
    var norm = normalizeColors(opts.colors);
    var materialId = 'spool-material-' + nextUid();

    return el('svg', {
      class: opts.className ? 'spool ' + opts.className : 'spool',
      width: size,
      height: size,
      viewBox: '0 0 ' + SPOOL_SIZE + ' ' + SPOOL_SIZE,
      role: 'img',
    }, [
      el('circle', { cx: center, cy: center, r: SPOOL_OUTER, fill: 'var(--ams-overlay-bg)' }),
      el('circle', { cx: center, cy: center, r: SPOOL_OUTER - 0.5, fill: 'none', stroke: 'var(--ams-border)', 'stroke-width': 1 }),
      renderMaterialDonut(center, norm, opts.colorType, remain, materialId),
      showTag && remain > 0
        ? el('image', {
            href: FILAMENT_TAG_URL,
            x: 0,
            y: 0,
            width: SPOOL_SIZE,
            height: SPOOL_SIZE,
            preserveAspectRatio: 'none',
          })
        : null,
    ]);
  }

  // ---- SpoolDisc.tsx ----

  // Universal "no entry" mark used by the batch-mode card when the slot is not
  // eligible for the current mode. Drawn at the bottom-right quadrant of the
  // spool so the colour ring stays visible underneath.
  function renderNotAllowedOverlay(center) {
    var cx = center + 14;
    var cy = center + 14;
    var r = 9;
    return el('g', { class: 'spool-not-allowed' }, [
      el('circle', { cx: cx, cy: cy, r: r, fill: 'var(--ams-card-text-bg)', stroke: 'var(--ams-text-muted)', 'stroke-width': 1.2 }),
      el('line', {
        x1: cx - r * 0.55,
        y1: cy - r * 0.55,
        x2: cx + r * 0.55,
        y2: cy + r * 0.55,
        stroke: 'var(--ams-text-muted)',
        'stroke-width': 1.6,
        'stroke-linecap': 'round',
      }),
    ]);
  }

  function renderReadingArc(center) {
    var r = SPOOL_RING - 3;
    var dashLen = 2 * Math.PI * r * 0.28;
    return el('g', {
      class: 'spool-loading-rotor',
      style: { transformOrigin: center + 'px ' + center + 'px' },
    }, el('circle', {
      class: 'spool-loading-mark',
      cx: center,
      cy: center,
      r: r,
      fill: 'none',
      stroke: 'var(--ams-brand)',
      'stroke-width': 3,
      'stroke-linecap': 'round',
      'stroke-dasharray': dashLen + ' 999',
    }));
  }

  // notAllowed dims the spool body and overlays a no-entry mark 鈥?used by the
  // batch-mode card whose select_action.can_select is false.
  function renderSpoolDisc(slot, notAllowed) {
    var card = slot.filament_card;
    var state = card.state;
    var displayIndex = slot.display_index;
    var cardColors = card.colors;
    var colors = normalizeColors(cardColors && cardColors.length ? cardColors : [card.color]);
    // Loading/Unloading slots still show the spool body in its loaded shape
    // (filled donut + tag); the rotating progress mark moves to the card-head
    // top-right slot instead of obscuring the disc centre.
    var isTransporting = state === 'Loading' || state === 'Unloading';
    var isFilled = state === 'ReadyToLoad' || state === 'ReadyToUse' || isTransporting;
    var isError = state === 'Error';
    var showFilamentTag = isFilled;
    var center = SPOOL_SIZE / 2;

    var remain = typeof card.remain === 'number' ? card.remain : 100;
    var materialId = 'spool-material-' + nextUid() + '-' + displayIndex;

    return el('svg', {
      class: 'spool spool-' + state.toLowerCase() + (notAllowed ? ' not-allowed' : ''),
      viewBox: '0 0 ' + SPOOL_SIZE + ' ' + SPOOL_SIZE,
      role: 'img',
      'aria-label': 'spool-' + displayIndex,
    }, [
      el('circle', { cx: center, cy: center, r: SPOOL_OUTER, fill: 'var(--ams-overlay-bg)' }),
      el('circle', { cx: center, cy: center, r: SPOOL_OUTER - 0.5, fill: 'none', stroke: 'var(--ams-border)', 'stroke-width': 1 }),

      isFilled ? renderMaterialDonut(center, colors, card.color_type, remain, materialId) : null,

      isError ? [
        el('circle', { cx: center, cy: center, r: SPOOL_RING, fill: 'var(--ams-danger)' }),
        el('circle', { cx: center, cy: center, r: SPOOL_HOLE, fill: 'var(--ams-spool-hole-bg)' }),
        el('g', { stroke: 'var(--ams-text-on-brand)', 'stroke-width': 2.5, 'stroke-linecap': 'round' }, [
          el('line', { x1: center - 5, y1: center - 5, x2: center + 5, y2: center + 5 }),
          el('line', { x1: center - 5, y1: center + 5, x2: center + 5, y2: center - 5 }),
        ]),
      ] : null,

      showFilamentTag
        ? el('image', { href: FILAMENT_TAG_URL, x: 0, y: 0, width: SPOOL_SIZE, height: SPOOL_SIZE, preserveAspectRatio: 'none' })
        : null,

      state === 'Reading' ? renderReadingArc(center) : null,

      state === 'Empty'
        ? el('text', {
            x: center + 0.5,
            y: center + 1,
            'font-family': 'HarmonyOS Sans SC, system-ui, sans-serif',
            'font-size': 14,
            fill: 'var(--ams-text-disabled)',
            'text-anchor': 'middle',
            'dominant-baseline': 'central',
          }, '/')
        : null,

      state === 'Unknown'
        ? el('image', { href: UNKNOWN_SPOOL_URL, x: 0, y: 0, width: SPOOL_SIZE, height: SPOOL_SIZE, preserveAspectRatio: 'none' })
        : null,

      notAllowed ? renderNotAllowedOverlay(center) : null,
    ]);
  }

  // ---- IndexPill.tsx ----

  function renderAmsBindIcon() {
    return el('svg', { width: 12, height: 10, viewBox: '0 0 12 10', 'aria-hidden': 'true' }, [
      el('rect', { x: 1, y: 2, width: 10, height: 6, rx: 1, fill: 'none', stroke: 'currentColor', 'stroke-width': 1 }),
      el('rect', { x: 3, y: 0.75, width: 1.2, height: 2.2, rx: 0.4, fill: 'currentColor' }),
      el('rect', { x: 5.4, y: 0.75, width: 1.2, height: 2.2, rx: 0.4, fill: 'currentColor' }),
      el('rect', { x: 7.8, y: 0.75, width: 1.2, height: 2.2, rx: 0.4, fill: 'currentColor' }),
      el('rect', { x: 3, y: 4.25, width: 1.1, height: 1.1, rx: 0.2, fill: 'currentColor' }),
      el('rect', { x: 5.45, y: 4.25, width: 1.1, height: 1.1, rx: 0.2, fill: 'currentColor' }),
      el('rect', { x: 7.9, y: 4.25, width: 1.1, height: 1.1, rx: 0.2, fill: 'currentColor' }),
    ]);
  }

  function renderIndexPill(slot) {
    var lineState = slot.filament_line.state;
    var isTransporting = lineState === 'loading' || lineState === 'unloading';
    var cls = ['card-index'];
    // `selected` covers both the user's manual UI selection and the firmware-
    // reported transport states so the pill matches the green highlight while
    // filament is in motion.
    if (slot.selected || isTransporting) cls.push('selected');
    if (slot.is_running) cls.push('running');
    if (!slot.enabled) cls.push('disabled');
    var bindCount = slot.bind_ams_count === undefined || slot.bind_ams_count === null
      ? 0
      : slot.bind_ams_count;

    return el('span', { class: 'card-index-row' }, [
      el('span', {
        class: cls.join(' '),
        'data-testid': 'ams-hotend-slot-index-' + slot.display_index,
        text: slot.display_index,
      }),
      slot.is_external_spool
        ? el('span', {
            class: 'card-index-ext-tag',
            'data-testid': 'ams-hotend-slot-ext-' + slot.display_index,
            title: t('External spool'),
            text: 'Ext',
          })
        : null,
      bindCount > 0
        ? el('span', {
            class: 'ams-bind-badge',
            title: t('Associated AMS: {{count}}', { count: bindCount }),
          }, [
            renderAmsBindIcon(),
            el('span', { text: '+' + bindCount }),
          ])
        : null,
    ]);
  }

  // ---- CardText.tsx ----

  function renderCardText(slot) {
    var card = slot.filament_card;
    var isEmpty = card.state === 'Empty';
    var isUnknown = card.state === 'Unknown';

    // Loading / Unloading slots intentionally fall through to the default
    // branch: the tray still holds known filament during a change, and the user
    // expects to see the C++ controlled card text (e.g. "PLA Basic") under the
    // spool 鈥?the spinning indicator on the card head already conveys transit.
    var primary = isEmpty ? 'Empty' : isUnknown ? '?' : (card.text || card.name || card.type || '');

    return el('div', { class: 'card-text', 'data-card-state': card.state },
      el('div', { class: 'card-name-row' },
        el('span', {
          class: 'card-name' + (isEmpty ? ' placeholder' : '') + (isUnknown ? ' card-name-unknown' : ''),
          'data-testid': 'ams-hotend-slot-name-' + slot.display_index,
          title: card.name || primary,
          text: primary,
        })));
  }

  // ---- LoadingArcIcon.tsx ----

  // Small rotating brand-coloured arc shown in the card-head right slot while a
  // slot is in `Loading` / `Unloading`. Sits in the same 16x16 slot the batch
  // slot checkbox occupies in batch mode.
  function renderLoadingArcIcon() {
    return el('span', {
      class: 'card-loading-arc',
      role: 'img',
      'aria-label': 'loading',
      'data-testid': 'ams-hotend-card-loading',
    }, el('svg', { width: 16, height: 16, viewBox: '0 0 16 16', 'aria-hidden': 'true' },
      el('g', { class: 'spool-loading-rotor', style: { transformOrigin: '8px 8px' } },
        el('circle', {
          class: 'spool-loading-mark',
          cx: 8,
          cy: 8,
          r: 6,
          fill: 'none',
          stroke: 'var(--ams-brand)',
          'stroke-width': 2,
          'stroke-linecap': 'round',
          // ~75% of the circumference filled: large enough to read as
          // "progress", short enough to leave a visible gap that telegraphs
          // the rotation when animated.
          'stroke-dasharray': (2 * Math.PI * 6 * 0.75) + ' 999',
        }))));
  }

  // ---- PrefeedIndicator.tsx ----

  function colorBackground(colors, colorType) {
    if (colors.length === 1) return colors[0];
    if (colorType === 'gradient') {
      return 'linear-gradient(135deg, ' + colors.join(', ') + ')';
    }

    var step = 100 / colors.length;
    var stops = [];
    colors.forEach(function (color, index) {
      stops.push(color + ' ' + Math.round(index * step) + '%');
      stops.push(color + ' ' + Math.round((index + 1) * step) + '%');
    });
    return 'conic-gradient(' + stops.join(', ') + ')';
  }

  function renderPrefeedIndicator(options) {
    var opts = options || {};
    var displayIndex = opts.displayIndex;
    // Empty / missing filaType means unknown prefeed filament.
    var isUnknown = !opts.filaType;
    var normalizedColors = (opts.colors || []).filter(Boolean);
    if (!isUnknown && normalizedColors.length === 0) return null;

    if (isUnknown) {
      return el('img', {
        class: 'prefeed-indicator prefeed-indicator--unknown',
        'data-testid': 'ams-hotend-prefeed-indicator-' + displayIndex,
        'data-unknown': 'true',
        src: PREFEED_UNKNOWN_URL,
        width: 14,
        height: 14,
        alt: 'AMS pre-feed unknown',
        title: 'AMS pre-feed',
        draggable: 'false',
      });
    }

    return el('span', {
      class: 'prefeed-indicator',
      'data-testid': 'ams-hotend-prefeed-indicator-' + displayIndex,
      'data-color-type': opts.colorType || 'single',
      style: { background: colorBackground(normalizedColors, opts.colorType) },
      title: 'AMS pre-feed',
      'aria-label': 'ams-prefeed-' + displayIndex,
    });
  }

  // ---- BatchSlotCheckbox.tsx ----

  // Card-head checkbox used in batch_* page modes. Eligibility is fully
  // authoritative on the C++ side (slot.select_action.can_select) 鈥?Web does
  // not re-derive it; it just renders + bubbles the toggle event up.
  function renderBatchSlotCheckbox(options) {
    var opts = options || {};
    var checked = !!opts.checked;
    var canSelect = !!opts.canSelect;
    var displayIndex = opts.displayIndex;

    return el('button', {
      type: 'button',
      role: 'checkbox',
      'aria-checked': checked ? 'true' : 'false',
      'aria-label': canSelect ? 'Select slot ' + displayIndex : (opts.reason || 'Not selectable'),
      title: !canSelect ? opts.reason : undefined,
      'data-testid': 'ams-hotend-slot-checkbox-' + displayIndex,
      class: 'batch-slot-checkbox' + (checked ? ' checked' : '') + (!canSelect ? ' disabled' : ''),
      disabled: !canSelect,
      onClick: function (event) {
        event.stopPropagation();
        if (!canSelect) return;
        if (opts.onToggle) opts.onToggle();
      },
    }, checked
      ? el('svg', { width: 12, height: 12, viewBox: '0 0 12 12', 'aria-hidden': 'true' },
          el('path', {
            d: 'M2.5 6.2 5 8.7 9.5 3.6',
            fill: 'none',
            stroke: 'currentColor',
            'stroke-width': 1.6,
            'stroke-linecap': 'round',
            'stroke-linejoin': 'round',
          }))
      : null);
  }

  BBL.components = BBL.components || {};
  BBL.components.spool = {
    SPOOL_SIZE: SPOOL_SIZE,
    SPOOL_OUTER: SPOOL_OUTER,
    SPOOL_RING: SPOOL_RING,
    SPOOL_HOLE: SPOOL_HOLE,
    normalizeColors: normalizeColors,
    renderSpoolGraphic: renderSpoolGraphic,
    renderMaterialDonut: renderMaterialDonut,
    renderSpoolDisc: renderSpoolDisc,
    renderIndexPill: renderIndexPill,
    renderCardText: renderCardText,
    renderLoadingArcIcon: renderLoadingArcIcon,
    renderPrefeedIndicator: renderPrefeedIndicator,
    renderBatchSlotCheckbox: renderBatchSlotCheckbox,
  };
})(window);
