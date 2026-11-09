// Visual alignment harness: renders the React build and the native HTML build
// from one identical ViewModel and reports where they differ.
//
// The repository has no visual regression baseline at all, so this is the only
// mechanical way to tell whether the HTML port actually matches. It serves both
// documents from a single static origin, installs a fake WebView2 host that
// answers the pages' bridge traffic with the shared fixture, then captures a
// screenshot plus a DOM class-usage census from each.
//
//   node tools/compare-pages.mjs            # capture + compare
//   node tools/compare-pages.mjs --open     # also leave the browser open
//
// Output lands in tools/out/.
import { createServer } from 'node:http';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { dirname, extname, join, resolve, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
import { mockAmsFilamentHotendState } from './fixtures/mockState.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const deviceWebDir = resolve(here, '../..');           // .../GUI/DeviceWeb
const outDir = resolve(here, 'out');

// Playwright is a devDependency of device_page, not of this directory, and ESM
// bare specifiers do not walk into a sibling tree. Resolve it explicitly rather
// than duplicating the dependency.
const requireFromDevicePage = createRequire(join(deviceWebDir, 'device_page', 'package.json'));
const { chromium } = requireFromDevicePage('@playwright/test');

const PORT = 13931;
const REACT_URL = `http://127.0.0.1:${PORT}/device_page/dist/index.html?lang=en#/device_page/ams_filament_hotend`;
const HTML_URL = `http://127.0.0.1:${PORT}/DeviceWebHtml/web/ams_hotend.html?lang=en#/device_page/ams_filament_hotend`;

// Matches the dialog's client area (wgtDeviceFilamentHotendHtmlDialog 900x700 DIP).
const VIEWPORT = { width: 900, height: 700 };

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.woff2': 'font/woff2',
  '.map': 'application/json; charset=utf-8',
};

function startServer() {
  const server = createServer(async (req, res) => {
    const url = new URL(req.url, `http://127.0.0.1:${PORT}`);
    const rel = decodeURIComponent(url.pathname).replace(/^\/+/, '');
    const filePath = normalize(join(deviceWebDir, rel));
    if (!filePath.startsWith(deviceWebDir) || !existsSync(filePath)) {
      res.writeHead(404).end('not found');
      return;
    }
    try {
      const body = await readFile(filePath);
      res.writeHead(200, { 'Content-Type': MIME[extname(filePath)] || 'application/octet-stream' });
      res.end(body);
    } catch (e) {
      res.writeHead(500).end(String(e));
    }
  });
  return new Promise((ok) => server.listen(PORT, '127.0.0.1', () => ok(server)));
}

// Fake C++ host. Answers every `<module>/state/get` with the fixture and ACKs
// everything else, mirroring DeviceWebBridge's response envelope.
function hostInitScript(state) {
  return `(() => {
    const STATE = ${JSON.stringify(state)};
    const reply = (head, body) => {
      setTimeout(() => {
        if (typeof window.__cppPush === 'function') {
          window.__cppPush({ event: 'device', head, body });
        } else {
          document.dispatchEvent(new CustomEvent('cpp:device', {
            detail: { event: 'device', head, body },
          }));
        }
      }, 0);
    };
    window.__harnessSeen = [];
    const handle = (raw) => {
      let pkt;
      try { pkt = JSON.parse(raw); } catch { return; }
      window.__harnessSeen.push(pkt.body);
      const b = pkt.body || {};
      const head = { version: '1.0', type: 'response', seq: pkt.head.seq, ts: Date.now() };
      if (b.module === 'device_host') return;                 // health / layout: no response
      if (b.submod === 'state' && b.action === 'get') {
        reply(head, { ...b, error_code: 0, payload: STATE });
        return;
      }
      reply(head, { ...b, error_code: 0, payload: STATE });
    };
    window.chrome = window.chrome || {};
    window.chrome.webview = { postMessage: handle, addEventListener() {}, removeEventListener() {} };
    // The React build reads the theme from the UserAgent; force light so both
    // pages are compared under one scheme.
    window.__internalBuild = false;
  })();`;
}

async function capture(browser, name, url, state, prepare) {
  const context = await browser.newContext({
    viewport: VIEWPORT,
    deviceScaleFactor: 2,
    // Mirrors WebView.cpp's UA so both pages resolve the same theme.
    userAgent: 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) '
      + 'Chrome/120.0.0.0 Safari/537.36 BBL-Slicer/v02.09.00.64 (light) BBL-Language/en',
  });
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  // A missing <image href> inside an SVG fails silently in the console, so watch
  // the network directly.
  page.on('requestfailed', (r) => errors.push(`requestfailed ${r.url()}`));
  page.on('response', (r) => { if (r.status() >= 400) errors.push(`HTTP ${r.status()} ${r.url()}`); });
  await page.addInitScript(hostInitScript(state));

  await page.goto(url, { waitUntil: 'load' });
  // Both builds mount into #root and settle after the first state.get round trip.
  await page.waitForSelector('.ams-filament-hotend-page', { timeout: 10_000 }).catch(() => {});
  await page.waitForTimeout(1200);
  if (prepare) {
    await prepare(page);
    await page.waitForTimeout(600);
  }

  // The spool loading / reading arcs rotate forever; without freezing them the
  // two captures differ purely by animation phase.
  await page.screenshot({
    path: join(outDir, `${name}.png`),
    fullPage: false,
    animations: 'disabled',
  });

  const census = await page.evaluate(() => {
    const counts = {};
    document.querySelectorAll('*').forEach((node) => {
      const cls = node.getAttribute('class');
      if (!cls) return;
      cls.split(/\s+/).filter(Boolean).forEach((c) => { counts[c] = (counts[c] || 0) + 1; });
    });
    const testids = [...document.querySelectorAll('[data-testid]')]
      .map((n) => n.getAttribute('data-testid')).sort();
    const root = document.querySelector('.ams-filament-hotend-page');

    // Pretty-print so the two sides diff line by line instead of as one blob.
    const format = (node) => {
      if (!node) return '';
      const walk = (n, depth) => {
        const pad = '  '.repeat(depth);
        if (n.nodeType === 3) {
          const text = n.textContent.trim();
          return text ? `${pad}"${text}"\n` : '';
        }
        if (n.nodeType !== 1) return '';
        const attrs = [...n.attributes]
          .map((a) => [a.name, a.value])
          // React generates unstable useId() values; the HTML port uses its own
          // counter. Neither is meaningful for structural comparison.
          .map(([k, v]) => [k, v.replace(/spool-material-[\w:-]+?-(\d+)/g, 'spool-material-$1')])
          .sort((a, b) => a[0].localeCompare(b[0]))
          .map(([k, v]) => `${k}="${v}"`)
          .join(' ');
        let out = `${pad}<${n.tagName.toLowerCase()}${attrs ? ' ' + attrs : ''}>\n`;
        n.childNodes.forEach((c) => { out += walk(c, depth + 1); });
        return out;
      };
      return walk(node, 0);
    };

    const sample = (selector) => format(document.querySelector(selector));

    return {
      counts,
      testids,
      rootHtmlLength: root ? root.innerHTML.length : 0,
      svgCount: document.querySelectorAll('svg').length,
      bodyText: document.body.innerText.replace(/\s+/g, ' ').trim().slice(0, 400),
      samples: {
        toolbar: sample('.page-toolbar'),
        // Slot 5 exercises the richest card: partial remain ring, AMS bind
        // badge and a bound-slot menu.
        slot5: sample('[data-testid="ams-hotend-slot-5"]'),
        slotColumn: sample('.slot-column'),
        stage: sample('.stage') && sample('.stage').split('\n').slice(0, 6).join('\n'),
        footer: sample('.page-footer'),
      },
    };
  });

  await context.close();
  return { census, errors };
}

function diffCensus(a, b) {
  const keys = [...new Set([...Object.keys(a.counts), ...Object.keys(b.counts)])].sort();
  const missing = [];
  const extra = [];
  const mismatched = [];
  for (const k of keys) {
    const ca = a.counts[k] || 0;
    const cb = b.counts[k] || 0;
    if (ca && !cb) missing.push(`${k} x${ca}`);
    else if (!ca && cb) extra.push(`${k} x${cb}`);
    else if (ca !== cb) mismatched.push(`${k}: react=${ca} html=${cb}`);
  }
  return { missing, extra, mismatched };
}

// Pixel diff without pulling in an image library: decode both PNGs on a canvas
// inside the browser we already have running.
async function pixelDiff(browser, aPath, bPath, outPath) {
  const toDataUrl = async (p) => `data:image/png;base64,${(await readFile(p)).toString('base64')}`;
  const page = await browser.newPage();
  const result = await page.evaluate(async ([aSrc, bSrc]) => {
    const load = (src) => new Promise((ok, err) => {
      const img = new Image();
      img.onload = () => ok(img);
      img.onerror = err;
      img.src = src;
    });
    const [a, b] = await Promise.all([load(aSrc), load(bSrc)]);
    const w = Math.max(a.width, b.width);
    const h = Math.max(a.height, b.height);
    const read = (img) => {
      const c = Object.assign(document.createElement('canvas'), { width: w, height: h });
      const ctx = c.getContext('2d', { willReadFrequently: true });
      ctx.drawImage(img, 0, 0);
      return ctx.getImageData(0, 0, w, h).data;
    };
    const da = read(a);
    const db = read(b);

    const out = Object.assign(document.createElement('canvas'), { width: w, height: h });
    const octx = out.getContext('2d');
    const odata = octx.createImageData(w, h);
    let differing = 0;
    // 12/255 tolerates antialiasing without hiding a real colour change.
    const TOL = 12;
    for (let i = 0; i < da.length; i += 4) {
      const d = Math.max(
        Math.abs(da[i] - db[i]), Math.abs(da[i + 1] - db[i + 1]),
        Math.abs(da[i + 2] - db[i + 2]), Math.abs(da[i + 3] - db[i + 3]),
      );
      if (d > TOL) {
        differing++;
        odata.data[i] = 255; odata.data[i + 1] = 0; odata.data[i + 2] = 0; odata.data[i + 3] = 255;
      } else {
        const grey = (da[i] + da[i + 1] + da[i + 2]) / 3;
        const faded = 255 - (255 - grey) * 0.15;
        odata.data[i] = faded; odata.data[i + 1] = faded; odata.data[i + 2] = faded;
        odata.data[i + 3] = 255;
      }
    }
    octx.putImageData(odata, 0, 0);
    return {
      width: w, height: h, differing,
      ratio: differing / (w * h),
      png: out.toDataURL('image/png').split(',')[1],
    };
  }, [await toDataUrl(aPath), await toDataUrl(bPath)]);
  await page.close();
  await writeFile(outPath, Buffer.from(result.png, 'base64'));
  return result;
}

const server = await startServer();
await mkdir(outDir, { recursive: true });
const browser = await chromium.launch();

// Each variant is captured from both front-ends and diffed.
const VARIANTS = [
  { name: 'filament', prepare: null },
  {
    name: 'hotend',
    prepare: (page) => page.click('[data-testid="ams-hotend-tab-hotend"]'),
  },
];

try {
  let worstRatio = 0;
  for (const variant of VARIANTS) {
    const r = await capture(browser, `react.${variant.name}`, REACT_URL,
      mockAmsFilamentHotendState, variant.prepare);
    const h = await capture(browser, `html.${variant.name}`, HTML_URL,
      mockAmsFilamentHotendState, variant.prepare);
    const d = diffCensus(r.census, h.census);
    const px = await pixelDiff(browser,
      join(outDir, `react.${variant.name}.png`), join(outDir, `html.${variant.name}.png`),
      join(outDir, `diff.${variant.name}.png`));
    worstRatio = Math.max(worstRatio, px.ratio);
    const missingIds = r.census.testids.filter((id) => !h.census.testids.includes(id));
    console.log(`[${variant.name}] svg ${r.census.svgCount}/${h.census.svgCount} | `
      + `class missing ${d.missing.length} extra ${d.extra.length} mismatch ${d.mismatched.length} | `
      + `testid missing ${missingIds.length} | `
      + `pixels ${px.differing} (${(px.ratio * 100).toFixed(3)}%) | `
      + `errors ${r.errors.length}/${h.errors.length}`);
    if (d.missing.length) console.log('   missing: ' + d.missing.slice(0, 20).join(', '));
    if (d.extra.length) console.log('   extra  : ' + d.extra.slice(0, 20).join(', '));
    if (d.mismatched.length) console.log('   count  : ' + d.mismatched.slice(0, 20).join(', '));
    if (missingIds.length) console.log('   testids: ' + missingIds.slice(0, 20).join(', '));
    if (h.errors.length) console.log('   html errors: ' + h.errors.slice(0, 5).join(' | '));
  }
  console.log(`\nworst pixel delta across variants: ${(worstRatio * 100).toFixed(3)}%`);

  const react = await capture(browser, 'react', REACT_URL, mockAmsFilamentHotendState);
  const html = await capture(browser, 'html', HTML_URL, mockAmsFilamentHotendState);

  const diff = diffCensus(react.census, html.census);

  const report = {
    generated: new Date().toISOString(),
    react: {
      svgCount: react.census.svgCount,
      rootHtmlLength: react.census.rootHtmlLength,
      testids: react.census.testids.length,
      errors: react.errors,
    },
    html: {
      svgCount: html.census.svgCount,
      rootHtmlLength: html.census.rootHtmlLength,
      testids: html.census.testids.length,
      errors: html.errors,
    },
    classesMissingInHtml: diff.missing,
    classesOnlyInHtml: diff.extra,
    classCountMismatch: diff.mismatched,
    testidsMissingInHtml: react.census.testids.filter((id) => !html.census.testids.includes(id)),
    testidsOnlyInHtml: html.census.testids.filter((id) => !react.census.testids.includes(id)),
  };
  await writeFile(join(outDir, 'report.json'), JSON.stringify(report, null, 2), 'utf8');

  // Side-by-side DOM dumps: the fastest way to see exactly which element,
  // attribute or nesting level the port got wrong.
  for (const [key, value] of Object.entries(react.census.samples)) {
    await writeFile(join(outDir, `react.${key}.txt`), value || '(not found)', 'utf8');
  }
  for (const [key, value] of Object.entries(html.census.samples)) {
    await writeFile(join(outDir, `html.${key}.txt`), value || '(not found)', 'utf8');
  }

  const line = (label, arr) => `${label}: ${arr.length}${arr.length ? '\n  ' + arr.slice(0, 40).join('\n  ') : ''}`;
  console.log(`react: ${react.census.svgCount} svg, ${react.census.rootHtmlLength} chars, ${react.errors.length} errors`);
  console.log(`html : ${html.census.svgCount} svg, ${html.census.rootHtmlLength} chars, ${html.errors.length} errors`);
  if (react.errors.length) console.log('react errors:\n  ' + react.errors.slice(0, 5).join('\n  '));
  if (html.errors.length) console.log('html errors:\n  ' + html.errors.slice(0, 5).join('\n  '));
  console.log('');
  console.log(line('classes missing in html', diff.missing));
  console.log(line('classes only in html', diff.extra));
  console.log(line('class count mismatch', diff.mismatched));
  console.log(line('testids missing in html', report.testidsMissingInHtml));
  console.log('');
  const px = await pixelDiff(browser, join(outDir, 'react.png'), join(outDir, 'html.png'),
    join(outDir, 'diff.png'));
  console.log(`pixel diff: ${px.differing} / ${px.width * px.height} `
    + `(${(px.ratio * 100).toFixed(3)}%) -> ${join(outDir, 'diff.png')}`);
  console.log(`screenshots -> ${join(outDir, 'react.png')} / ${join(outDir, 'html.png')}`);
} finally {
  await browser.close();
  server.close();
}
