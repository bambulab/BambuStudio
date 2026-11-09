// Ready handshake with WebViewWatcher.
//
// The watchdog (kDefaultReadyTimeoutMs = 10s, RecoveryLimit = 1) reloads once and
// then shows the diagnostic fallback panel. Failing to send 'ready' therefore does
// not merely lose telemetry - it makes the host tear the page down.
//
// DeviceWebHealth.cpp already injected its own 'error' listener and published
// window.__bambuWebViewHealth. This unit must not overwrite window.onerror or the
// js_error channel goes dark.
(function (global) {
  'use strict';

  var BBL = global.BBL || (global.BBL = {});

  var MODULE = 'device_host';
  var SUBMOD = 'health';
  var MAX_URL_LEN = 2048;

  var reportedPageInstanceId;
  var sequence = 0;

  function pageInstanceId() {
    var health = global.__bambuWebViewHealth;
    return health && health.pageInstanceId;
  }

  function send(action, instanceId) {
    BBL.bridge.sendRaw(
      { version: BBL.bridge.SDK_VERSION, type: 'request', seq: sequence++, ts: Date.now() },
      {
        module: MODULE,
        submod: SUBMOD,
        action: action,
        payload: {
          page_instance_id: instanceId,
          document_url: String(global.location.href).slice(0, MAX_URL_LEN),
        },
      }
    );
  }

  // Idempotent per page instance: the C++ side warns about 'stale_ready' when the
  // instance id does not match the one it recorded at boot.
  function reportReady() {
    var instanceId = pageInstanceId();
    if (!instanceId || reportedPageInstanceId === instanceId) return;

    try {
      send('boot', instanceId);
      send('ready', instanceId);
      reportedPageInstanceId = instanceId;
    } catch (e) {
      // Health reporting must never break the business flow that triggered it.
    }
  }

  BBL.health = { reportReady: reportReady };
})(window);
