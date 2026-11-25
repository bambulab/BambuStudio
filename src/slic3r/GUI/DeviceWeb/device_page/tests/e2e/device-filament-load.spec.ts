import { expect, test, type Locator, type Page } from '@playwright/test';

// Geometry targets come from the wx panel at 100% scaling:
// StatusBasePanel::create_filament_group (StatusPanel.cpp) and
// FilamentStepIndicator::doRender (StepCtrl.cpp).

async function box(locator: Locator) {
  const b = await locator.boundingBox();
  expect(b).not.toBeNull();
  return b!;
}

async function relativeTo(page: Page, target: Locator) {
  const body = await box(page.getByTestId('filament-load-body'));
  const b = await box(target);
  return { x: b.x - body.x, y: b.y - body.y, width: b.width, height: b.height };
}

// Same report the C++ FilamentLoad VM sends on theme switch.
async function reportTheme(page: Page, theme: 'dark' | 'light') {
  await page.evaluate((next) => {
    document.dispatchEvent(new CustomEvent('cpp:device', {
      detail: {
        head: { type: 'report' },
        body: { module: 'device_page_filament_load', submod: 'init', action: 'theme_changed', payload: { theme: next } },
      },
    }));
  }, theme);
  await expect(page.locator('html')).toHaveAttribute('data-theme', theme);
}

test.describe('device filament load page', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/?lang=zh_CN#/device_page/filament_load');
    await expect(page.getByTestId('filament-load-body')).toBeVisible();
    await reportTheme(page, 'light');
  });

  test('header matches the wx scale panel', async ({ page }) => {
    const header = page.locator('.fl-header');
    expect((await box(header)).height).toBe(40);
    const icon = await box(page.locator('.fl-fold-icon'));
    expect(icon.width).toBe(24);
    expect(icon.height).toBe(24);
    await expect(header).toHaveCSS('font-size', '14.67px');
    await expect(header).toHaveCSS('color', 'rgb(27, 136, 68)');
  });

  test('body, steps, image and buttons follow the wx layout', async ({ page }) => {
    const body = await box(page.getByTestId('filament-load-body'));
    expect(body.width).toBe(586);
    expect(Math.abs(body.height - 256)).toBeLessThanOrEqual(1);

    await expect(page.locator('.fl-step-heading')).toHaveText('进料');

    // Step 0 is done (icon), step 1 is current: an 18px green dot centred at (29, 100).
    const steps = page.getByTestId('filament-load-step');
    await expect(steps).toHaveCount(5);
    const dot1 = await relativeTo(page, steps.nth(1).locator('.fl-step-dot'));
    expect(dot1.width).toBe(18);
    expect(dot1.height).toBe(18);
    expect(Math.abs(dot1.x + dot1.width / 2 - 29)).toBeLessThanOrEqual(1);
    expect(Math.abs(dot1.y + dot1.height / 2 - 100)).toBeLessThanOrEqual(1);
    await expect(steps.nth(1).locator('.fl-step-dot')).toHaveCSS('background-color', 'rgb(0, 174, 66)');

    const dot2 = await relativeTo(page, steps.nth(2).locator('.fl-step-dot'));
    expect(Math.abs(dot2.y - dot1.y - 27)).toBeLessThanOrEqual(0.5);

    await expect(steps.nth(0)).toHaveCSS('color', 'rgb(172, 172, 172)');
    await expect(steps.nth(1)).toHaveCSS('color', 'rgb(50, 58, 61)');
    await expect(steps.nth(1)).toHaveCSS('font-weight', '700');
    await expect(steps.nth(2)).toHaveCSS('color', 'rgb(107, 107, 107)');
    await expect(steps.nth(2)).toHaveCSS('font-size', '12px');

    const image = await relativeTo(page, page.locator('.fl-load-image'));
    expect(image).toMatchObject({ x: 300, y: 5, width: 215, height: 215 });

    const buttons = page.locator('.fl-actions .fl-action-btn');
    const retry = await relativeTo(page, buttons.nth(0));
    const stop = await relativeTo(page, buttons.nth(1));
    expect(retry.x).toBe(28);
    expect(retry.y).toBe(220);
    expect(retry.height).toBe(31);
    expect(retry.width).toBeGreaterThanOrEqual(80);
    expect(Math.round(stop.x - (retry.x + retry.width))).toBe(14);
    await expect(buttons.nth(0)).toHaveCSS('border-radius', '8px');

    await expect(page.locator('.fl-error')).toHaveCount(0);
  });

  test('follows the host dark theme report', async ({ page }) => {
    await reportTheme(page, 'dark');
    const body = page.getByTestId('filament-load-body');
    await expect(body).toHaveCSS('background-color', 'rgb(45, 45, 49)');
    await expect(body).toHaveCSS('box-shadow', /rgb\(76, 76, 85\)/);
    const steps = page.getByTestId('filament-load-step');
    await expect(steps.nth(1).locator('.fl-step-dot')).toHaveCSS('background-color', 'rgb(33, 164, 82)');
    await expect(steps.nth(1)).toHaveCSS('color', 'rgb(229, 229, 228)');
    await expect(page.locator('.fl-actions .fl-action-btn').first()).toHaveCSS('border-color', 'rgb(239, 239, 240)');
  });
});
