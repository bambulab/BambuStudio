// /device_page/filament_manual_pull - standalone manual-pull dialog page.
// Ported from manual-pull/FilamentManualPullPage.tsx.
(function (global) {
  'use strict';

  var BBL = global.BBL || (global.BBL = {});
  var el = BBL.dom.el;
  var t = BBL.t;

  var MODULE = 'device_page_ams_filament_hotend';

  function send(action) {
    return BBL.bridge.request(
      { module: MODULE, submod: 'action', action: action, payload: {} },
      1000
    );
  }

  function mount(container) {
    // The host closes this dialog by watching for the mode leaving
    // batch_manual_pull, but only once the page has actually seen that mode -
    // otherwise an early poll would close the dialog before it opened.
    var hasSeenManualPull = false;
    var closeRequested = false;

    var page = el('main', {
      class: 'filament-manual-pull-page',
      'data-testid': 'ams-filament-manual-pull-page',
    });
    container.appendChild(page);

    var stopLayout = BBL.layout.observe(page);

    function renderStep(step, index) {
      var done = !!step.done;
      return el('li', {
        class: 'manual-pull-step' + (done ? ' done' : ' pending'),
      }, [
        el('img', {
          class: 'manual-pull-step-icon',
          src: done ? './assets/manual_pull_status_done.svg'
             : './assets/manual_pull_status_pending.svg',
          alt: '', 'aria-hidden': 'true',
        }),
        el('span', {
          class: 'manual-pull-step-label',
          text: step.label || step.text || String(index + 1),
        }),
      ]);
    }

    function render() {
      var state = BBL.store.getState();
      if (!state) return;

      var changeStatus = state.filament_page.filament_change_status || {};
      if (changeStatus.mode === 'batch_manual_pull') {
        hasSeenManualPull = true;
      } else if (hasSeenManualPull && !closeRequested) {
        closeRequested = true;
        send('manual_pull_close');
      }

      var status = changeStatus.manual_pull || {};

      BBL.dom.clear(page);
      var dialog = el('div', {
        class: 'filament-manual-pull-dialog embedded',
        'data-testid': 'ams-filament-manual-pull-dialog',
      });

      if (status.title) dialog.appendChild(el('h2', { class: 'manual-pull-title', text: status.title }));
      if (status.message) dialog.appendChild(el('p', { class: 'manual-pull-message', text: status.message }));

      if (Array.isArray(status.steps) && status.steps.length) {
        dialog.appendChild(el('ol', { class: 'manual-pull-steps' }, status.steps.map(renderStep)));
      }

      if (status.tips) dialog.appendChild(el('p', { class: 'manual-pull-tips', text: status.tips }));

      dialog.appendChild(el('div', { class: 'manual-pull-actions' }, [
        el('button', {
          type: 'button', class: 'manual-pull-exit', text: t('Exit'),
          onClick: function () { send('manual_pull_exit'); },
        }),
        el('button', {
          type: 'button', class: 'manual-pull-close', text: t('Close'),
          onClick: function () { send('manual_pull_close'); },
        }),
      ]));

      page.appendChild(dialog);
    }

    // The manual-pull flow is driven by device echoes that do not always arrive
    // as reports, so this page polls in addition to subscribing.
    var timer = global.setInterval(function () { BBL.store.refresh('normal'); }, 1000);
    var unsubscribe = BBL.store.subscribe(render);
    render();

    return function teardown() {
      global.clearInterval(timer);
      unsubscribe();
      stopLayout();
    };
  }

  BBL.pages = BBL.pages || {};
  BBL.pages.manualPull = { mount: mount };
})(window);
