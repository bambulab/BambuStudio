// Hash router over the six device_page routes this front-end serves.
//
// The C++ host appends the route as a fragment (DeviceWebHost::BuildUrl), e.g.
// ams_hotend.html?lang=zh_CN#/device_page/ams_filament_hotend, and navigates by
// rewriting that fragment. Routes are matched by path only; query-ish suffixes
// after '?' inside the hash are parsed into params.
(function (global) {
  'use strict';

  var BBL = global.BBL || (global.BBL = {});

  var routes = {};
  var fallbackPath = null;
  var currentPath = null;
  var teardown = null;

  function parseHash() {
    var raw = global.location.hash.replace(/^#/, '');
    if (!raw) return { path: '', params: {} };
    var qIndex = raw.indexOf('?');
    if (qIndex < 0) return { path: raw, params: {} };
    var params = {};
    new URLSearchParams(raw.slice(qIndex + 1)).forEach(function (value, key) {
      params[key] = value;
    });
    return { path: raw.slice(0, qIndex), params: params };
  }

  function register(path, mount) {
    routes[path] = mount;
  }

  // mount(container, params) may return a teardown function.
  function resolve() {
    var parsed = parseHash();
    var path = routes[parsed.path] ? parsed.path : fallbackPath;
    if (!path) return;

    var container = document.getElementById('root');
    if (!container) return;

    if (currentPath === path && parsed.path === currentPath) return;

    if (typeof teardown === 'function') {
      try { teardown(); } catch (e) { console.error('[router] teardown failed', e); }
    }
    teardown = null;
    BBL.dom.clear(container);
    container.__bblKeyed = null;
    currentPath = path;

    var result = routes[path](container, parsed.params);
    if (typeof result === 'function') teardown = result;
  }

  function start(defaultPath) {
    fallbackPath = defaultPath;
    global.addEventListener('hashchange', resolve);
    resolve();
  }

  BBL.router = {
    register: register,
    start: start,
    resolve: resolve,
    get current() { return currentPath; },
  };
})(window);
