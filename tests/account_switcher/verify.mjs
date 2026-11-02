import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { createReadStream, mkdirSync, statSync, writeFileSync } from "node:fs";
import { createServer } from "node:http";
import { dirname, extname, join, normalize, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright";

const testDirectory = dirname(fileURLToPath(import.meta.url));
const repositoryRoot = resolve(testDirectory, "../..");
const cacheDirectory = join(repositoryRoot, ".cache");
const outputPath = join(cacheDirectory, "account-switcher-browser-results.json");
const edgePath = "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe";
const port = 41739;
const pageUrl = `http://127.0.0.1:${port}/resources/web/homepage3/left.html`;

const mimeTypes = new Map([
  [".css", "text/css; charset=utf-8"],
  [".html", "text/html; charset=utf-8"],
  [".js", "application/javascript; charset=utf-8"],
  [".json", "application/json; charset=utf-8"],
  [".png", "image/png"],
  [".svg", "image/svg+xml"],
  [".woff", "font/woff"],
  [".woff2", "font/woff2"]
]);

const observations = {
  checks: [],
  missingResources: [],
  remaining: [
    "原生消息发送、账号添加、账号切换、登录取消、网络插件状态变化和完成结果时序需要在真实 GUI 中验证。"
  ]
};

function record(name, details) {
  observations.checks.push({ name, details });
}

function staticServer() {
  return createServer((request, response) => {
    const url = new URL(request.url ?? "/", "http://127.0.0.1");
    const requestedPath = decodeURIComponent(url.pathname);
    const candidate = normalize(join(repositoryRoot, requestedPath));
    const insideRepository = relative(repositoryRoot, candidate);
    if (insideRepository.startsWith("..") || insideRepository.includes(`..${sep}`)) {
      response.writeHead(403);
      response.end("Forbidden");
      return;
    }
    try {
      const fileStat = statSync(candidate);
      if (!fileStat.isFile()) {
        observations.missingResources.push(requestedPath);
        response.writeHead(404);
        response.end("Not found");
        return;
      }
      response.writeHead(200, {
        "Cache-Control": "no-store",
        "Content-Type": mimeTypes.get(extname(candidate).toLowerCase()) ?? "application/octet-stream"
      });
      createReadStream(candidate).pipe(response);
    } catch {
      observations.missingResources.push(requestedPath);
      response.writeHead(404);
      response.end("Not found");
    }
  });
}

async function inspectLayout(page, width) {
  await page.setViewportSize({ width, height: 720 });
  return page.evaluate(() => {
    const rect = (selector) => {
      const box = document.querySelector(selector).getBoundingClientRect();
      return {
        left: box.left,
        right: box.right,
        top: box.top,
        bottom: box.bottom,
        width: box.width,
        height: box.height
      };
    };
    return {
      viewport: { width: innerWidth, height: innerHeight },
      bodyScrollWidth: document.body.scrollWidth,
      board: rect("#LeftBoard"),
      switcher: rect("#AccountSwitcher"),
      list: rect("#SavedAccountList"),
      buttonArea: rect("#BtnArea"),
      titleDisplay: getComputedStyle(document.querySelector("#AccountSwitcherTitle")).display,
      statusDisplay: getComputedStyle(document.querySelector("#AccountSwitcherStatus")).display,
      addDisplay: getComputedStyle(document.querySelector("#AddAccountButton")).display
    };
  });
}

async function inspectCssRules(page) {
  return page.evaluate(() => {
    const output = [];
    const visit = (sheet, media = "") => {
      for (const rule of sheet.cssRules) {
        if (rule.type === CSSRule.MEDIA_RULE) {
          visit(rule, rule.conditionText || rule.media?.mediaText || "");
        } else if (rule.selectorText) {
          output.push({
            media,
            selector: rule.selectorText,
            css: rule.style.cssText
          });
        }
      }
    };
    for (const sheet of document.styleSheets) {
      try {
        visit(sheet);
      } catch {
        // 本地页面样式表允许直接读取；其他来源的样式表无需参与本次检查。
      }
    }
    return output;
  });
}

let server;
let context;
try {
  mkdirSync(cacheDirectory, { recursive: true });

  execFileSync(process.execPath, [
    "--check",
    join(repositoryRoot, "resources/web/homepage3/js/account-switcher.js")
  ]);
  execFileSync(process.execPath, [
    "--check",
    join(repositoryRoot, "resources/web/homepage3/js/left.js")
  ]);
  record("JavaScript syntax", [
    "resources/web/homepage3/js/account-switcher.js",
    "resources/web/homepage3/js/left.js"
  ]);

  server = staticServer();
  await new Promise((resolveReady, rejectReady) => {
    server.once("error", rejectReady);
    server.listen(port, "127.0.0.1", resolveReady);
  });

  context = await chromium.launchPersistentContext(
    join(cacheDirectory, `edge-account-switcher-clean-${Date.now()}`),
    {
      executablePath: edgePath,
      headless: true,
      userAgent:
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 dark",
      viewport: { width: 220, height: 720 }
    }
  );
  const page = await context.newPage();
  const pageErrors = [];
  const consoleErrors = [];
  page.on("pageerror", (error) => pageErrors.push(error.message));
  page.on("console", (message) => {
    if (message.type() === "error")
      consoleErrors.push(message.text());
  });

  await page.goto(pageUrl, { waitUntil: "load" });
  await page.waitForFunction(
    () => document.querySelector("#AccountSwitcherTitle")?.textContent === "Saved accounts"
  );

  assert.equal(await page.title(), "homepage");
  const initialDom = await page.evaluate(() => {
    const section = document.querySelector("#AccountSwitcher");
    const list = document.querySelector("#SavedAccountList");
    const status = document.querySelector("#AccountSwitcherStatus");
    const addButton = document.querySelector("#AddAccountButton");
    return {
      sectionCount: document.querySelectorAll("#AccountSwitcher").length,
      sectionLabel: section.getAttribute("aria-labelledby"),
      title: document.querySelector("#AccountSwitcherTitle").textContent,
      addText: addButton.textContent,
      addDisabled: addButton.disabled,
      listRole: list.getAttribute("role"),
      listLabel: list.getAttribute("aria-label"),
      listChildren: list.children.length,
      statusRole: status.getAttribute("role"),
      statusLive: status.getAttribute("aria-live"),
      statusText: status.textContent
    };
  });
  assert.deepEqual(initialDom, {
    sectionCount: 1,
    sectionLabel: "AccountSwitcherTitle",
    title: "Saved accounts",
    addText: "Add account",
    addDisabled: false,
    listRole: "group",
    listLabel: "Switch account",
    listChildren: 0,
    statusRole: "status",
    statusLive: "polite",
    statusText: ""
  });
  const accessibilitySnapshot = await page.locator("#AccountSwitcher").ariaSnapshot();
  assert.ok(accessibilitySnapshot.includes("Saved accounts"));
  assert.ok(accessibilitySnapshot.includes("Add account"));
  record("Loaded HTML and accessibility", {
    ...initialDom,
    accessibilitySnapshot
  });

  await page.keyboard.press("Tab");
  assert.equal(
    await page.evaluate(() => document.activeElement?.id),
    "AddAccountButton"
  );
  assert.equal(pageErrors.length, 0, `页面脚本错误: ${pageErrors.join(" | ")}`);
  record("Keyboard focus", {
    firstTabTarget: "AddAccountButton",
    nativeBackendActivationDeferredToGui: true
  });

  const darkStyles = await page.evaluate(() => ({
    darkLinkCount: document.querySelectorAll('link[href$="css/dark.css"]').length,
    bodyBackground: getComputedStyle(document.body).backgroundColor,
    switcherColor: getComputedStyle(document.querySelector("#AccountSwitcher")).color,
    addColor: getComputedStyle(document.querySelector("#AddAccountButton")).color
  }));
  assert.equal(darkStyles.darkLinkCount, 1);
  assert.equal(darkStyles.bodyBackground, "rgb(36, 36, 40)");
  assert.equal(darkStyles.switcherColor, "rgb(239, 239, 240)");
  assert.equal(darkStyles.addColor, "rgb(0, 174, 66)");

  const cssRules = await inspectCssRules(page);
  const cssFindings = {
    accountRowFlex: cssRules.some(
      (rule) => rule.selector.includes(".SavedAccountRow") && rule.css.includes("display: flex")
    ),
    activeBackground: cssRules.some(
      (rule) =>
        rule.selector.includes(".SavedAccountActive") &&
        rule.css.includes("background:")
    ),
    accountButtonLayout: cssRules.some(
      (rule) =>
        rule.selector.includes(".SavedAccountButton") &&
        rule.css.includes("flex-direction: column")
    ),
    accountNameEllipsis: cssRules.some(
      (rule) =>
        rule.selector.includes(".SavedAccountName") &&
        rule.css.includes("text-overflow: ellipsis")
    ),
    focusVisible: cssRules.some(
      (rule) =>
        rule.selector.includes(":focus-visible") &&
        rule.css.includes("outline")
    )
  };
  record("Dark mode and CSS rules", {
    computed: darkStyles,
    cssFindings
  });
  assert.equal(Object.values(cssFindings).every(Boolean), true);

  const regularLayout = await inspectLayout(page, 220);
  assert.ok(regularLayout.switcher.width > 0);
  assert.ok(regularLayout.switcher.right <= regularLayout.viewport.width + 1);
  assert.ok(regularLayout.switcher.bottom <= regularLayout.buttonArea.top + 1);
  assert.ok(regularLayout.bodyScrollWidth <= regularLayout.viewport.width);
  assert.notEqual(regularLayout.titleDisplay, "none");
  assert.notEqual(regularLayout.statusDisplay, "none");

  const miniLayout = await inspectLayout(page, 167);
  assert.ok(miniLayout.switcher.width > 0);
  assert.ok(miniLayout.switcher.right <= miniLayout.viewport.width + 1);
  assert.ok(miniLayout.switcher.bottom <= miniLayout.buttonArea.top + 1);
  assert.ok(miniLayout.bodyScrollWidth <= miniLayout.viewport.width);
  assert.equal(miniLayout.titleDisplay, "none");
  assert.equal(miniLayout.statusDisplay, "none");
  assert.notEqual(miniLayout.addDisplay, "none");

  const narrowLayout = await inspectLayout(page, 64);
  assert.ok(narrowLayout.switcher.width > 0);
  assert.ok(narrowLayout.switcher.right <= narrowLayout.viewport.width + 1);
  assert.ok(narrowLayout.switcher.bottom <= narrowLayout.buttonArea.top + 1);
  assert.ok(narrowLayout.bodyScrollWidth <= narrowLayout.viewport.width);
  assert.equal(narrowLayout.titleDisplay, "none");
  assert.equal(narrowLayout.statusDisplay, "none");
  assert.notEqual(narrowLayout.addDisplay, "none");
  record("Responsive layout", {
    regularLayout,
    miniLayout,
    narrowLayout
  });

  assert.deepEqual(pageErrors, []);
  record("Browser health", {
    pageErrors,
    consoleErrors
  });

  writeFileSync(
    outputPath,
    JSON.stringify({ status: "passed", ...observations }, null, 2),
    "utf8"
  );
} catch (error) {
  writeFileSync(
    outputPath,
    JSON.stringify(
      {
        status: "failed",
        error: error instanceof Error ? error.stack : String(error),
        ...observations
      },
      null,
      2
    ),
    "utf8"
  );
  throw error;
} finally {
  if (context)
    await context.close();
  if (server)
    await new Promise((resolveClosed) => server.close(resolveClosed));
}
