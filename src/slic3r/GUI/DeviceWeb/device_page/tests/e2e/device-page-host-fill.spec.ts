import { expect, test, type Page } from '@playwright/test';

// Pages hosted in a fixed-size wx dialog fill it with `100%` instead of
// 100vh, because vh is zoomed a second time under useFitHostWidth. `100%`
// only works while html/body/#root and every wrapper around the route stay at
// full height; a wrapper without a height silently collapses the page to its
// content. These tests pin the page to the viewport at 100% and at 125%
// Windows text scaling (the host renders 1 CSS px as 1.25 DIP, so the viewport
// is the host size / 1.25 and the page zooms itself back by 0.8).

const TEXT_SCALES = [1, 1.25];

async function openAtScale(page: Page, route: string, host: { width: number; height: number }, scale: number) {
  await page.setViewportSize({
    width: Math.round(host.width / scale),
    height: Math.round(host.height / scale),
  });
  await page.goto(route);
}

async function bottomGap(page: Page, selector: string) {
  return page.evaluate((sel) => {
    const bottom = document.querySelector(sel)?.getBoundingClientRect().bottom ?? 0;
    return Math.abs(window.innerHeight - bottom);
  }, selector);
}

// A page that falls back to its content height can still land within a pixel
// of the host by chance, so also grow the viewport and check the page follows.
async function expectFillsViewport(page: Page, selector: string) {
  expect(await bottomGap(page, selector)).toBeLessThanOrEqual(1);

  const viewport = page.viewportSize()!;
  await page.setViewportSize({ width: viewport.width, height: viewport.height + 120 });
  await expect.poll(() => bottomGap(page, selector)).toBeLessThanOrEqual(1);
}

async function expectNoHorizontalOverflow(page: Page) {
  const { scrollWidth, clientWidth } = await page.evaluate(() => ({
    scrollWidth: document.documentElement.scrollWidth,
    clientWidth: document.documentElement.clientWidth,
  }));
  expect(scrollWidth).toBeLessThanOrEqual(clientWidth);
}

test.describe('device pages fill their fixed-size host', () => {
  for (const scale of TEXT_SCALES) {
    test(`connected slots at ${scale * 100}% text`, async ({ page }) => {
      await openAtScale(page, '/#/device_page/connected_slots?addr_id=0', { width: 560, height: 360 }, scale);
      await expect(page.getByTestId('connected-slots-page')).toBeVisible();
      // Debug-only controls would make the content taller than the host.
      await page.addStyleTag({ content: '.cs-page__actions, .cs-page__error { display: none !important; }' });

      await expectFillsViewport(page, '.cs-page');
      await expectNoHorizontalOverflow(page);
    });

    test(`filament manual pull at ${scale * 100}% text`, async ({ page }) => {
      // The mock bridge has no manual-pull status; add one to every state it sends.
      await page.addInitScript(() => {
        const status = {
          title: 'Filament not removed',
          can_exit: true,
          can_close: true,
          items: [{ ams_id: '0', slot_id: '0', display_index: 1, filament_type: 'PLA', color: '#00AE42FF', state: 'failed' }],
        };
        const patch = (node: unknown) => {
          if (!node || typeof node !== 'object') return;
          const record = node as Record<string, unknown>;
          const filamentPage = record.filament_page as Record<string, unknown> | undefined;
          if (filamentPage) {
            filamentPage.filament_change_status = {
              ...(filamentPage.filament_change_status as object | undefined),
              active: true,
              mode: 'batch_manual_pull',
              manual_pull: status,
            };
          }
          Object.values(record).forEach(patch);
        };
        document.addEventListener('cpp:device', (event) => patch((event as CustomEvent).detail), true);
      });
      await openAtScale(page, '/#/device_page/filament_manual_pull', { width: 636, height: 358 }, scale);
      await expect(page.getByTestId('ams-filament-manual-pull-dialog')).toBeVisible();

      await expectFillsViewport(page, '.filament-manual-pull-dialog');
      await expectNoHorizontalOverflow(page);
    });
  }
});
