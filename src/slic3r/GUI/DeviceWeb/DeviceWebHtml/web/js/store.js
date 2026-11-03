// Page state container for device_page_ams_filament_hotend.
//
// Owns the three things useAmsFilamentHotendBridge.ts owns on the React side:
// the business protocol, state normalisation against defaults, and change
// de-duplication. The host re-pushes a full ViewModel several times per second,
// so re-rendering on every push would be wasteful and visibly janky.
(function (global) {
  'use strict';

  var BBL = global.BBL || (global.BBL = {});

  var MODULE = 'device_page_ams_filament_hotend';

  var state = null;
  var stateKey = '';
  var error = '';
  var subscribers = [];

  function makeBody(submod, action, payload) {
    return { module: MODULE, submod: submod, action: action, payload: payload || {} };
  }

  function isOwnBody(body) {
    return !!body && typeof body === 'object' && body.module === MODULE;
  }

  function pickTheme(payload) {
    if (!payload || typeof payload !== 'object') return null;
    var value = payload.theme;
    return value === 'dark' || value === 'light' ? value : null;
  }

  function assign(target) {
    for (var i = 1; i < arguments.length; i++) {
      var source = arguments[i];
      if (!source) continue;
      Object.keys(source).forEach(function (key) { target[key] = source[key]; });
    }
    return target;
  }

  // Mirrors normalizeState() in useAmsFilamentHotendBridge.ts. The host omits
  // fields it has nothing to say about, so every consumer would otherwise need
  // its own null checks - and an abnormal device state would throw mid-render.
  function normalizeState(payload) {
    var defaults = BBL.defaults.amsFilamentHotendState;
    var incoming = payload || {};
    var incomingFp = incoming.filament_page || {};
    var incomingHp = incoming.hotend_page || {};

    var defaultRackStatus = defaults.hotend_page.hotend_rack_status || {
      active: false, mode: 'idle', display_index: 0, current: 0, total: 0, message: '', tips: '',
    };
    var defaultFilamentChangeStatus = { active: false, mode: 'idle' };

    var filamentPage = assign({}, defaults.filament_page, incomingFp);
    filamentPage.actions = assign({}, defaults.filament_page.actions, incomingFp.actions);
    filamentPage.filament_change_status =
      assign({}, defaultFilamentChangeStatus, incomingFp.filament_change_status);
    filamentPage.ams_dry_zones = Array.isArray(incomingFp.ams_dry_zones)
      ? incomingFp.ams_dry_zones
      : (defaults.filament_page.ams_dry_zones || []);
    filamentPage.hotend_slots = Array.isArray(incomingFp.hotend_slots)
      ? incomingFp.hotend_slots
      : defaults.filament_page.hotend_slots;

    var hotendPage = assign({}, defaults.hotend_page, incomingHp);
    hotendPage.actions = assign({}, defaults.hotend_page.actions, incomingHp.actions);
    hotendPage.hotend_rack_status =
      assign({}, defaultRackStatus, incomingHp.hotend_rack_status);
    hotendPage.hotend_slots = Array.isArray(incomingHp.hotend_slots)
      ? incomingHp.hotend_slots
      : defaults.hotend_page.hotend_slots;

    return { filament_page: filamentPage, hotend_page: hotendPage };
  }

  function notify() {
    subscribers.slice().forEach(function (fn) {
      try { fn(state, error); } catch (e) { console.error('[store] subscriber failed', e); }
    });
  }

  function applyState(payload, force) {
    var next = normalizeState(payload);
    var nextKey = JSON.stringify(next);
    if (!force && nextKey === stateKey) return false;
    stateKey = nextKey;
    state = next;
    notify();
    return true;
  }

  function setError(message) {
    if (error === message) return;
    error = message;
    notify();
  }

  // `state.get` carries the page mode; C++ remembers it and reuses it for
  // subsequent ReportState() pushes until the web side changes it again.
  function refresh(mode) {
    return BBL.bridge.request(makeBody('state', 'get', { mode: mode || 'normal' }), 5000)
      .then(function (resp) {
        if (!resp.ok) { setError(resp.error); return; }
        if (resp.value.error_code !== 0) {
          setError(resp.value.message || 'AMS filament hotend state failed');
          return;
        }
        if (resp.value.payload) {
          var theme = pickTheme(resp.value.payload);
          if (theme) BBL.theme.apply(theme);
          applyState(resp.value.payload, true);
          setError('');
        }
      });
  }

  function runSlotAction(action, slot, extraPayload) {
    var payload;
    if (slot) {
      payload = assign({
        addr_id: slot.addr_id,
        display_index: slot.display_index,
        ams_id: slot.ams_id,
        slot_id: slot.slot_id,
        // Present only on filament-tab slots.
        bind_ams_count: 'bind_ams_count' in slot ? (slot.bind_ams_count || 0) : 0,
      }, extraPayload);
    } else {
      payload = assign({}, extraPayload);
    }
    return BBL.bridge.request(makeBody('action', action, payload), 8000)
      .then(function (resp) {
        if (resp.ok && resp.value.payload) applyState(resp.value.payload, false);
        return resp;
      });
  }

  // `committed` = the caller already resolved every unknown slot, so C++ skips
  // its own unknown re-check (which lags device RFID round-trips) and runs the
  // load directly. C++ flips the page back to `normal` in its response.
  function runBatchAction(mode, slots, committed) {
    var payload = {
      mode: mode,
      committed: !!committed,
      slots: (slots || []).map(function (s) {
        return { ams_id: s.ams_id, slot_id: s.slot_id, display_index: s.display_index };
      }),
    };
    return BBL.bridge.request(makeBody('action', 'batch_run', payload), 8000)
      .then(function (resp) {
        if (resp.ok && resp.value.payload) applyState(resp.value.payload, false);
        return resp;
      });
  }

  function openBridgeDebugDialog() {
    return BBL.bridge.request(makeBody('debug', 'open_bridge_trace', {
      web_state: state,
      web_trace: BBL.bridge.getTrace(),
    }), 8000).then(function (resp) {
      if (!resp.ok) { setError('Debug request failed: ' + resp.error); return false; }
      if (resp.value.error_code !== 0) {
        setError('Debug request failed: ' + (resp.value.message || 'unknown error'));
        return false;
      }
      setError('');
      return true;
    });
  }

  function init() {
    state = normalizeState(BBL.defaults.amsFilamentHotendState);
    stateKey = JSON.stringify(state);

    BBL.bridge.onReport(function (body) {
      if (!isOwnBody(body)) return;
      if (body.payload) {
        applyState(body.payload, false);
        setError('');
      }
    });

    return refresh('normal');
  }

  BBL.store = {
    MODULE: MODULE,
    init: init,
    refresh: refresh,
    runSlotAction: runSlotAction,
    runBatchAction: runBatchAction,
    openBridgeDebugDialog: openBridgeDebugDialog,
    setError: setError,
    getState: function () { return state; },
    getError: function () { return error; },
    subscribe: function (fn) {
      subscribers.push(fn);
      return function () {
        var i = subscribers.indexOf(fn);
        if (i >= 0) subscribers.splice(i, 1);
      };
    },
  };
})(window);
