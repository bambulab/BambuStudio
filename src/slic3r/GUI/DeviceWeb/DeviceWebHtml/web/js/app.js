// Bootstrap: wire the routes and bring the page up in the order the host expects.
//
//   theme  -> before first paint, so the dialog never flashes the wrong scheme
//   i18n   -> before any page renders, so no untranslated frame is shown
//   store  -> issues state.get, whose successful response releases the ready
//             handshake via bridge.js -> health.reportReady()
//   router -> mounts the route the host put in the URL fragment
(function (global) {
  'use strict';

  var BBL = global.BBL;

  BBL.router.register('/device_page/ams_filament_hotend', BBL.pages.main.mount);
  BBL.router.register('/device_page/ams_ext_operation', BBL.pages.extOperation.mount);
  BBL.router.register('/device_page/ams_ext_filament_edit', BBL.pages.extFilamentEdit.mount);
  BBL.router.register('/device_page/filament_manual_pull', BBL.pages.manualPull.mount);
  BBL.router.register('/device_page/hotend_detail', BBL.pages.hotendDetail.mount);

  function start() {
    BBL.theme.init();
    BBL.i18n.init().then(function () {
      // Mount before the first state.get resolves: the page renders the empty
      // skeleton, then re-renders on the store's first notify. Mounting after
      // the request would leave the WebView blank for a whole round trip.
      BBL.router.start('/device_page/ams_filament_hotend');
      BBL.store.init();
    });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', start, { once: true });
  } else {
    start();
  }
})(window);
