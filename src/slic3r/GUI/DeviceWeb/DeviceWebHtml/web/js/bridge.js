// JSON-RPC transport to the C++ DeviceWebBridge.
//
// Contract mirrored from device_page/src/hooks/Bridge.tsx - keep the two in sync.
//
//   Web -> C++ : JSON string via chrome.webview (WebView2) or
//                webkit.messageHandlers.wx (WKWebView), iframe app:// as fallback.
//   C++ -> Web : window.__cppPush(pkt) -> document 'cpp:<pkt.event>' CustomEvent.
//                The device page listens on 'cpp:device'.
//
//   packet = { head: { version, type, seq, ts }, body: { module, submod, action, ... } }
(function (global) {
  'use strict';

  var BBL = global.BBL || (global.BBL = {});

  var SDK_VERSION = '1.0';
  var MAX_TRACE_ENTRIES = 120;

  var sequence = 0;
  var pending = {};   // seq -> { resolve, timer, marksReady }
  var listeners = []; // report subscribers

  function cloneForTrace(value) {
    try {
      return JSON.parse(JSON.stringify(value));
    } catch (e) {
      return { unserializable: true, error: String(e) };
    }
  }

  function appendTrace(direction, head, body) {
    var trace = global.__deviceWebTrace || (global.__deviceWebTrace = []);
    trace.push({
      direction: direction,
      head: { version: head.version, type: head.type, seq: head.seq, ts: head.ts },
      body: cloneForTrace(body),
      recorded_ts: Date.now(),
    });
    if (trace.length > MAX_TRACE_ENTRIES) trace.splice(0, trace.length - MAX_TRACE_ENTRIES);
  }

  function rawSend(pkt) {
    var str;
    try {
      str = typeof pkt === 'string' ? pkt : JSON.stringify(pkt);
    } catch (e) {
      console.error('[DeviceWebBridge] Failed to serialize packet', e, pkt);
      throw e;
    }

    // Edge-WebView2 (Windows)
    if (global.chrome && global.chrome.webview && typeof global.chrome.webview.postMessage === 'function') {
      global.chrome.webview.postMessage(str);
      return;
    }

    // WKWebView (macOS) - handler registered as "wx" via AddScriptMessageHandler.
    if (global.webkit && global.webkit.messageHandlers && global.webkit.messageHandlers.wx &&
        typeof global.webkit.messageHandlers.wx.postMessage === 'function') {
      global.webkit.messageHandlers.wx.postMessage(str);
      return;
    }

    // Fallback: trigger wxEVT_WEBVIEW_NAVIGATING via iframe navigation.
    // fetch('app://...') does NOT trigger navigation events in WKWebView, so the
    // iframe is required to reach decidePolicyForNavigationAction.
    var frame = document.createElement('iframe');
    frame.style.display = 'none';
    frame.src = 'app://' + encodeURIComponent(str);
    document.body.appendChild(frame);
    setTimeout(function () {
      try { document.body.removeChild(frame); } catch (e) { /* already detached */ }
    }, 200);
  }

  function makeHead(seq) {
    return { version: SDK_VERSION, type: 'request', seq: seq, ts: Date.now() };
  }

  // Fire-and-forget send with a caller-supplied head. Used by the health and
  // layout channels, which must not occupy business sequence numbers.
  function sendRaw(head, body) {
    rawSend({ head: head, body: body });
  }

  // Same rule as Bridge.tsx marksHealthReady(): only a successful business
  // init / state.get proves the page is really alive, so only those responses
  // release the ready handshake.
  function marksHealthReady(body) {
    return body.module !== 'device_host' &&
      (body.action === 'init' || (body.submod === 'state' && body.action === 'get'));
  }

  // Returns a Promise<{ ok: true, value } | { ok: false, error }>. Never rejects.
  function request(body, timeoutMs) {
    var seq = sequence++;
    var head = makeHead(seq);
    var reqBody = body || {};
    appendTrace('web_request', head, reqBody);
    sendRaw(head, reqBody);

    return new Promise(function (resolve) {
      var timer = setTimeout(function () {
        delete pending[seq];
        resolve({ ok: false, error: 'Request Timeout' });
      }, typeof timeoutMs === 'number' ? timeoutMs : 5000);

      pending[seq] = { resolve: resolve, timer: timer, marksReady: marksHealthReady(reqBody) };
    });
  }

  function onReport(handler) {
    listeners.push(handler);
    return function off() {
      var index = listeners.indexOf(handler);
      if (index >= 0) listeners.splice(index, 1);
    };
  }

  function handlePacket(pkt) {
    if (!pkt || !pkt.head || pkt.head.version !== SDK_VERSION) return;

    if (pkt.head.type === 'response' || pkt.head.type === 'report') {
      appendTrace(pkt.head.type === 'response' ? 'cpp_response' : 'cpp_report', pkt.head, pkt.body);
    }

    if (pkt.head.type === 'response') {
      var entry = pending[pkt.head.seq];
      if (!entry) return;
      clearTimeout(entry.timer);
      delete pending[pkt.head.seq];
      var body = pkt.body || {};
      entry.resolve({ ok: true, value: body });
      if (entry.marksReady && Number(body.error_code || 0) === 0) {
        // Microtask, so the business handler observes the response first.
        Promise.resolve().then(function () {
          if (BBL.health) BBL.health.reportReady();
        });
      }
      return;
    }

    if (pkt.head.type === 'report') {
      listeners.slice().forEach(function (handler) {
        try {
          handler(pkt.body || {}, pkt.head);
        } catch (e) {
          console.error('[DeviceWebBridge] report handler failed', e);
        }
      });
    }
  }

  document.addEventListener('cpp:device', function (event) {
    handlePacket(event.detail);
  });

  BBL.bridge = {
    SDK_VERSION: SDK_VERSION,
    request: request,
    sendRaw: sendRaw,
    makeHead: makeHead,
    onReport: onReport,
    getTrace: function () { return (global.__deviceWebTrace || []).slice(); },
  };
})(window);
