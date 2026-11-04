// Theme synchronisation, two sources:
//   1. first paint - parsed from the UserAgent that WebView.cpp sets
//      ("BBL-Slicer/v... (dark|light) ..."). Must run before the first paint or
//      the dialog flashes white.
//   2. runtime      - 'init'/'theme_changed' reports from the host.
(function (global) {
  'use strict';

  var BBL = global.BBL || (global.BBL = {});

  // Modules that report runtime theme changes (mirrors useDeviceWebTheme.ts).
  var THEME_SOURCE_MODULES = {
    device_page_ams_filament_hotend: 1,
    device_page_connected_slots: 1,
    device_page_auto_refill_refill_info: 1,
  };

  var current = 'dark';
  var subscribers = [];

  function isThemeName(value) {
    return value === 'dark' || value === 'light';
  }

  function detectFromUserAgent() {
    return /\(\s*light\s*\)/i.test(global.navigator.userAgent) ? 'light' : 'dark';
  }

  function apply(theme) {
    if (!isThemeName(theme) || theme === current) return;
    current = theme;
    document.documentElement.dataset.theme = theme;
    var bg = theme === 'dark' ? '#2d2d31' : '#ffffff';
    if (document.documentElement && document.documentElement.style) {
      document.documentElement.style.backgroundColor = bg;
    }
    subscribers.slice().forEach(function (fn) {
      try { fn(theme); } catch (e) { console.error('[theme] subscriber failed', e); }
    });
  }

  function init() {
    current = detectFromUserAgent();
    document.documentElement.dataset.theme = current;
    var bg = current === 'dark' ? '#2d2d31' : '#ffffff';
    if (document.documentElement && document.documentElement.style) {
      document.documentElement.style.backgroundColor = bg;
    }

    BBL.bridge.onReport(function (body) {
      if (!body || !THEME_SOURCE_MODULES[body.module]) return;
      if (body.submod !== 'init' || body.action !== 'theme_changed') return;
      var next = body.payload && body.payload.theme;
      if (isThemeName(next)) apply(next);
    });
  }

  BBL.theme = {
    init: init,
    apply: apply,
    get: function () { return current; },
    subscribe: function (fn) {
      subscribers.push(fn);
      return function () {
        var i = subscribers.indexOf(fn);
        if (i >= 0) subscribers.splice(i, 1);
      };
    },
  };
})(window);
