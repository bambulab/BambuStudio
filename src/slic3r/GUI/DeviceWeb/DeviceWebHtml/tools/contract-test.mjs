// Host-contract self test for the native HTML front-end.
//
// Runs bridge/health/layout/theme/store against a hand-rolled window stub and a
// fake C++ side, so the four host contracts can be verified without building
// Studio. Covers exactly the things whose failure modes are invisible in review:
// ready-handshake timing, content_size math, state normalisation and the
// business protocol envelope.
//
//   node src/slic3r/GUI/DeviceWeb/DeviceWebHtml/tools/contract-test.mjs
import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const here = dirname(fileURLToPath(import.meta.url));
const webDir = resolve(here, '../web');

let failures = 0;
function check(name, condition, detail) {
  if (condition) {
    console.log(`  ok   ${name}`);
  } else {
    failures += 1;
    console.log(`  FAIL ${name}${detail ? ` -- ${detail}` : ''}`);
  }
}

// ---- minimal DOM / window stub -------------------------------------------
function makeEnv({ userAgent = 'BBL-Slicer/v1 (dark) BBL-Language/en', href = 'file:///ams_hotend.html?lang=en' } = {}) {
  const sent = [];
  const docListeners = {};

  function node() {
    return {
      style: { setProperty() {} },
      hidden: false,
      classList: { contains: () => false, toggle() {}, remove() {} },
      appendChild() {}, removeChild() {}, setAttribute() {}, removeAttribute() {},
      getAttribute: () => null, addEventListener() {}, querySelectorAll: () => [],
    };
  }

  const document = {
    documentElement: { dataset: {} },
    head: node(),
    body: node(),
    createElement: node,
    createElementNS: node,
    getElementById: () => null,
    querySelectorAll: () => [],
    addEventListener(type, fn) { (docListeners[type] ||= []).push(fn); },
    removeEventListener(type, fn) {
      const list = docListeners[type] || [];
      const i = list.indexOf(fn);
      if (i >= 0) list.splice(i, 1);
    },
    dispatchEvent(event) {
      (docListeners[event.type] || []).slice().forEach((fn) => fn(event));
    },
  };

  const sandbox = {
    console,
    setTimeout, clearTimeout, setInterval, clearInterval, Promise,
    URLSearchParams, JSON, Date, Math, Number, String, Object, Array, Boolean, isFinite,
    document,
    navigator: { userAgent },
    location: { href, search: '?lang=en', hash: '' },
    requestAnimationFrame: (fn) => setTimeout(fn, 0),
    cancelAnimationFrame: clearTimeout,
    addEventListener() {}, removeEventListener() {},
    ResizeObserver: undefined,
    CustomEvent: class CustomEvent {
      constructor(type, init) { this.type = type; this.detail = init?.detail; }
    },
    // Pretend to be WebView2 so rawSend takes the postMessage path.
    chrome: { webview: { postMessage: (str) => sent.push(JSON.parse(str)) } },
    __bambuWebViewHealth: { pageInstanceId: 'page-instance-1' },
  };
  sandbox.window = sandbox;
  sandbox.global = sandbox;

  const context = vm.createContext(sandbox);
  for (const file of ['js/dom.js', 'js/bridge.js', 'js/health.js', 'js/layout.js',
                      'js/theme.js', 'js/defaults.js', 'js/store.js']) {
    vm.runInContext(readFileSync(resolve(webDir, file), 'utf8'), context, { filename: file });
  }

  // Deliver a packet the way DeviceWebBridge::InitBridge()'s __cppPush does.
  function push(pkt) {
    document.dispatchEvent(new sandbox.CustomEvent('cpp:device', { detail: pkt }));
  }

  return { sandbox, sent, push };
}

const tick = () => new Promise((r) => setTimeout(r, 0));

// ---- 1. transport envelope + ready handshake ------------------------------
console.log('transport + ready handshake');
{
  const { sandbox, sent, push } = makeEnv();
  sandbox.BBL.theme.init();
  const done = sandbox.BBL.store.init();
  await tick();

  const first = sent[0];
  check('state.get is the first packet',
    first?.body?.module === 'device_page_ams_filament_hotend' &&
    first?.body?.submod === 'state' && first?.body?.action === 'get',
    JSON.stringify(first?.body));
  check('envelope carries version 1.0 / type request',
    first?.head?.version === '1.0' && first?.head?.type === 'request');
  check('state.get carries the page mode', first?.body?.payload?.mode === 'normal');
  check('no ready before the response arrives',
    !sent.some((p) => p.body?.action === 'ready'));

  push({
    head: { version: '1.0', type: 'response', seq: first.head.seq, ts: Date.now() },
    body: { module: 'device_page_ams_filament_hotend', submod: 'state', action: 'get',
            error_code: 0, payload: { theme: 'light', filament_page: {}, hotend_page: {} } },
  });
  await done;
  await tick();

  const health = sent.filter((p) => p.body?.module === 'device_host' && p.body?.submod === 'health');
  check('boot then ready are sent after a successful state.get',
    health.length === 2 && health[0].body.action === 'boot' && health[1].body.action === 'ready',
    health.map((p) => p.body.action).join(','));
  check('health payload carries page_instance_id',
    health[1]?.body?.payload?.page_instance_id === 'page-instance-1');
  check('health payload carries document_url',
    typeof health[1]?.body?.payload?.document_url === 'string' &&
    health[1].body.payload.document_url.length > 0);
  check('theme report from the payload is applied',
    sandbox.BBL.theme.get() === 'light', sandbox.BBL.theme.get());

  // A second successful state.get must not re-announce ready.
  sandbox.BBL.store.refresh('normal');
  await tick();
  const second = sent[sent.length - 1];
  push({
    head: { version: '1.0', type: 'response', seq: second.head.seq, ts: Date.now() },
    body: { module: 'device_page_ams_filament_hotend', error_code: 0, payload: {} },
  });
  await tick();
  const health2 = sent.filter((p) => p.body?.submod === 'health');
  check('ready is idempotent per page instance', health2.length === 2, `${health2.length} health packets`);
}

// ---- 2. failure paths -----------------------------------------------------
console.log('failure paths');
{
  const { sandbox, sent, push } = makeEnv();
  sandbox.BBL.theme.init();
  sandbox.BBL.store.init();
  await tick();
  const first = sent[0];

  push({
    head: { version: '1.0', type: 'response', seq: first.head.seq, ts: Date.now() },
    body: { module: 'device_page_ams_filament_hotend', error_code: 7, message: 'device offline' },
  });
  await tick();
  check('error_code != 0 surfaces the message',
    sandbox.BBL.store.getError() === 'device offline', sandbox.BBL.store.getError());
  check('a failed state.get does NOT release ready',
    !sent.some((p) => p.body?.action === 'ready'));

  // Version mismatch must be dropped rather than mis-paired.
  push({ head: { version: '9.9', type: 'response', seq: first.head.seq, ts: Date.now() }, body: {} });
  await tick();
  check('packets with a foreign SDK version are ignored',
    sandbox.BBL.store.getError() === 'device offline');
}

// ---- 3. state normalisation ----------------------------------------------
console.log('state normalisation');
{
  const { sandbox, sent, push } = makeEnv();
  sandbox.BBL.theme.init();
  sandbox.BBL.store.init();
  await tick();
  push({
    head: { version: '1.0', type: 'response', seq: sent[0].head.seq, ts: Date.now() },
    body: { module: 'device_page_ams_filament_hotend', error_code: 0,
            payload: { filament_page: { mode: 'batch_unload' } } },
  });
  await tick();

  const state = sandbox.BBL.store.getState();
  check('omitted hotend_page is filled from defaults', !!state.hotend_page?.actions);
  check('omitted filament_change_status is filled',
    state.filament_page.filament_change_status?.active === false);
  check('omitted hotend_slots becomes an array',
    Array.isArray(state.filament_page.hotend_slots));
  check('omitted ams_dry_zones becomes an array',
    Array.isArray(state.filament_page.ams_dry_zones));
  check('supplied fields win over defaults',
    state.filament_page.mode === 'batch_unload');

  // De-duplication: an identical report must not notify subscribers again.
  let notifications = 0;
  sandbox.BBL.store.subscribe(() => { notifications += 1; });
  push({
    head: { version: '1.0', type: 'report', ts: Date.now(), seq: 1 },
    body: { module: 'device_page_ams_filament_hotend',
            payload: { filament_page: { mode: 'batch_unload' } } },
  });
  await tick();
  check('an unchanged report does not re-render', notifications === 0, `${notifications} notifications`);

  push({
    head: { version: '1.0', type: 'report', ts: Date.now(), seq: 2 },
    body: { module: 'device_page_ams_filament_hotend',
            payload: { filament_page: { mode: 'normal' } } },
  });
  await tick();
  check('a changed report does re-render', notifications > 0);
}

// ---- 4. content_size math -------------------------------------------------
console.log('content_size');
{
  const { sandbox } = makeEnv();
  const r = sandbox.BBL.layout.resolveHostContentSize;
  check('width floors at 560', r({ scrollWidth: 100, scrollHeight: 900, offsetHeight: 0 }).width === 560);
  check('height floors at 440', r({ scrollWidth: 900, scrollHeight: 10, offsetHeight: 0 }).height === 440);
  check('offsetHeight is the fallback when scrollHeight is 0',
    r({ scrollWidth: 900, scrollHeight: 0, offsetHeight: 700 }).height === 700);
  check('larger values pass through',
    r({ scrollWidth: 1000, scrollHeight: 800, offsetHeight: 0 }).width === 1000);
  check('fractional sizes are ceiled',
    r({ scrollWidth: 900.2, scrollHeight: 700.1, offsetHeight: 0 }).width === 901);
}

// ---- 5. theme from UserAgent ---------------------------------------------
console.log('theme');
{
  const light = makeEnv({ userAgent: 'BBL-Slicer/v1 (light) BBL-Language/zh_CN' });
  light.sandbox.BBL.theme.init();
  check('(light) UA selects light', light.sandbox.BBL.theme.get() === 'light');
  check('data-theme is written to <html>',
    light.sandbox.document.documentElement.dataset.theme === 'light');

  const dark = makeEnv({ userAgent: 'BBL-Slicer/v1 (dark) BBL-Language/en' });
  dark.sandbox.BBL.theme.init();
  check('(dark) UA selects dark', dark.sandbox.BBL.theme.get() === 'dark');

  const none = makeEnv({ userAgent: 'Mozilla/5.0 no marker' });
  none.sandbox.BBL.theme.init();
  check('missing marker defaults to dark', none.sandbox.BBL.theme.get() === 'dark');

  // Runtime theme_changed report.
  dark.sandbox.BBL.theme.init();
  dark.push({
    head: { version: '1.0', type: 'report', seq: 1, ts: Date.now() },
    body: { module: 'device_page_ams_filament_hotend', submod: 'init',
            action: 'theme_changed', payload: { theme: 'light' } },
  });
  check('theme_changed report is applied', dark.sandbox.BBL.theme.get() === 'light');
}

// ---- 6. business protocol envelopes --------------------------------------
console.log('business protocol');
{
  const { sandbox, sent, push } = makeEnv();
  sandbox.BBL.theme.init();
  sandbox.BBL.store.init();
  await tick();
  push({
    head: { version: '1.0', type: 'response', seq: sent[0].head.seq, ts: Date.now() },
    body: { module: 'device_page_ams_filament_hotend', error_code: 0, payload: {} },
  });
  await tick();
  sent.length = 0;

  sandbox.BBL.store.runSlotAction('load', {
    addr_id: 3, display_index: 4, ams_id: '0', slot_id: '3', bind_ams_count: 2,
  });
  await tick();
  const slotPkt = sent[sent.length - 1];
  check('slot action uses submod=action',
    slotPkt.body.submod === 'action' && slotPkt.body.action === 'load');
  check('slot action payload carries the slot identity',
    slotPkt.body.payload.ams_id === '0' && slotPkt.body.payload.slot_id === '3' &&
    slotPkt.body.payload.display_index === 4 && slotPkt.body.payload.bind_ams_count === 2);

  sent.length = 0;
  sandbox.BBL.store.runBatchAction('batch_unload',
    [{ ams_id: '0', slot_id: '1', display_index: 2 }], true);
  await tick();
  const batchPkt = sent[sent.length - 1];
  check('batch action is action/batch_run', batchPkt.body.action === 'batch_run');
  check('batch payload carries mode + committed + slots',
    batchPkt.body.payload.mode === 'batch_unload' &&
    batchPkt.body.payload.committed === true &&
    batchPkt.body.payload.slots.length === 1 &&
    batchPkt.body.payload.slots[0].slot_id === '1');
  check('batch slots are trimmed to the three identity fields',
    Object.keys(batchPkt.body.payload.slots[0]).sort().join(',') === 'ams_id,display_index,slot_id');

  // Host-channel packets must never occupy business sequence numbers.
  check('layout sequence range is disjoint from business requests',
    sandbox.BBL.layout.MIN_WIDTH === 560 && sandbox.BBL.layout.BASE_HEIGHT === 440);
}

// ---- 7. request timeout ---------------------------------------------------
console.log('request timeout');
{
  const { sandbox } = makeEnv();
  const resp = await sandbox.BBL.bridge.request({ module: 'x', submod: 'y', action: 'z' }, 10);
  check('an unanswered request resolves as Request Timeout',
    resp.ok === false && resp.error === 'Request Timeout', JSON.stringify(resp));
}

console.log('');
if (failures) {
  console.log(`${failures} check(s) FAILED`);
  process.exit(1);
}
console.log('all contract checks passed');
