import { expect, test } from '@playwright/test';

test.describe('device control panel page', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/#/device_page/control_panel');
    await expect(page.getByTestId('control-panel-page')).toBeVisible();
  });

  test('renders temperature, motion, extruder, and misc areas', async ({ page }) => {
    const root = page.getByTestId('control-panel-page');
    await expect(page.getByTestId('control-panel-extruder-temp-main')).toBeVisible();
    await expect(page.getByTestId('control-panel-bed-temp')).toBeVisible();
    await expect(page.getByTestId('control-panel-chamber-temp')).toBeVisible();
    await expect(page.getByTestId('control-panel-xy')).toBeVisible();
    await expect(page.getByTestId('control-panel-z')).toBeVisible();
    await expect(page.getByTestId('control-panel-rack')).toBeVisible();
    await expect(page.getByTestId('control-panel-extruder')).toBeVisible();
    await expect(page.getByTestId('control-panel-speed')).toContainText('100%');
    await expect(page.getByTestId('control-panel-lamp')).toBeVisible();
    await expect(page.getByTestId('control-panel-fan')).toContainText('风扇');
    await expect(root).not.toContainText('idle');
    await expect(root).not.toContainText('cooling');
    await expect(root).not.toContainText('loading');
  });

  test('sends semantic action payloads through the bridge', async ({ page }) => {
    await page.getByTestId('control-panel-speed').click();
    await page.getByTestId('control-panel-lamp').click();
    await page.getByTestId('control-panel-xy').getByRole('button', { name: 'Home' }).click();

    const trace = await page.evaluate(() =>
      (window as Window & {
        __deviceWebTrace?: Array<{ direction: string; body: { module: string; action: string; payload: Record<string, unknown> } }>;
      }).__deviceWebTrace ?? []
    );
    const requests = trace.filter((entry) => entry.direction === 'web_request' && entry.body.module === 'device_page_control_panel');
    expect(requests.some((entry) => entry.body.action === 'set_speed' && entry.body.payload.level === 3)).toBeTruthy();
    expect(requests.some((entry) => entry.body.action === 'toggle_lamp' && entry.body.payload.on === false)).toBeTruthy();
    expect(requests.some((entry) => entry.body.action === 'axis_move' && entry.body.payload.op === 'xy_home')).toBeTruthy();
  });

  test('renders Debug button only when __debugBuild is enabled without shifting layout', async ({ page }) => {
    await expect(page.getByTestId('control-panel-debug')).toHaveCount(0);

    await page.addInitScript(() => {
      (window as Window & { __debugBuild?: boolean }).__debugBuild = true;
    });
    await page.reload();
    const debugBtn = page.getByTestId('control-panel-debug');
    await expect(debugBtn).toBeVisible();
    await debugBtn.click();

    const trace = await page.evaluate(() =>
      (window as Window & {
        __deviceWebTrace?: Array<{ direction: string; body: { module: string; submod: string; action: string } }>;
      }).__deviceWebTrace ?? []
    );
    expect(
      trace.some(
        (entry) =>
          entry.direction === 'web_request' &&
          entry.body.module === 'device_page_control_panel' &&
          entry.body.submod === 'debug' &&
          entry.body.action === 'open_bridge_trace',
      ),
    ).toBeTruthy();
  });
});
