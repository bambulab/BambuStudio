// Content size reporting: the hosting dialog sizes itself from this message.
//
// Algorithm mirrored from ams-filament-hotend/hostContentSize.ts. The host then
// clamps to 578x352 .. 1100x850 in wgtDeviceFilamentHotendHtmlDialog.
(function (global) {
  'use strict';

  var BBL = global.BBL || (global.BBL = {});

  var MODULE = 'device_host';
  var MIN_WIDTH = 560;
  var BASE_HEIGHT = 440;

  // Layout messages share the request channel but must not consume business
  // sequence numbers, so they start from a disjoint range.
  var sequence = 1000000;

  function resolveHostContentSize(root) {
    var width = Math.ceil(Math.max(root.scrollWidth, MIN_WIDTH));
    var rawHeight = root.scrollHeight || root.offsetHeight;
    var height = Math.ceil(Math.max(rawHeight, BASE_HEIGHT));
    return { width: width, height: height };
  }

  // Starts reporting for `root`. Returns a teardown function.
  function observe(root) {
    if (!root) return function () {};

    var rafId = 0;
    var lastSizeKey = '';

    function report() {
      rafId = 0;
      var size = resolveHostContentSize(root);
      if (size.width <= 0 || size.height <= 0) return;

      var sizeKey = size.width + 'x' + size.height;
      if (sizeKey === lastSizeKey) return;
      lastSizeKey = sizeKey;

      BBL.bridge.sendRaw(
        { version: BBL.bridge.SDK_VERSION, type: 'request', seq: sequence++, ts: Date.now() },
        { module: MODULE, submod: 'layout', action: 'content_size', payload: size }
      );
    }

    function schedule() {
      if (rafId) return;
      rafId = global.requestAnimationFrame(report);
    }

    schedule();
    global.addEventListener('resize', schedule);

    var observer = typeof ResizeObserver === 'function' ? new ResizeObserver(schedule) : null;
    if (observer) observer.observe(root);

    return function teardown() {
      if (rafId) global.cancelAnimationFrame(rafId);
      if (observer) observer.disconnect();
      global.removeEventListener('resize', schedule);
    };
  }

  BBL.layout = {
    observe: observe,
    resolveHostContentSize: resolveHostContentSize,
    MIN_WIDTH: MIN_WIDTH,
    BASE_HEIGHT: BASE_HEIGHT,
  };
})(window);
