import { expect, test } from '@playwright/test';

test.describe('device hotend rack page', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/#/device_page/hotend_rack');
    await expect(page.getByTestId('hotend-rack-page')).toBeVisible();
  });

  test('renders toolhead, raised row, hotend slots, and footer actions', async ({ page }) => {
    await expect(page.getByTestId('hotend-rack-toolhead')).toBeVisible();
    await expect(page.locator('.hr-position-column')).toContainText('Raised');
    await expect(page.locator('.hr-position-column')).toContainText('Row A');
    await expect(page.locator('.hr-position-column')).toContainText('Row B');
    await expect(page.getByTestId('hotend-rack-slot-1')).toContainText('0.4mm');
    await expect(page.getByTestId('hotend-rack-slot-1')).toContainText('Standard');
    await expect(page.getByTestId('hotend-rack-hotends-info')).toBeVisible();
    await expect(page.getByTestId('hotend-rack-read-all')).toBeVisible();
  });

  test('reorders rows when row B is raised from a changed bridge state', async ({ page }) => {
    await page.evaluate(() => {
      document.dispatchEvent(new CustomEvent('cpp:device', {
        detail: {
          head: { version: '1.0', type: 'report', seq: 9301, ts: Date.now() },
          body: {
            module: 'device_page_hotend_rack',
            submod: 'state',
            action: 'changed',
            error_code: 0,
            message: '',
            payload: {
              display: {
                visible: true,
                rack: {
                  position: 'b_top',
                  rows: [
                    { id: 'B', raised: true, slots: [], move_control: { enabled: false, disabled_reason: '' } },
                    { id: 'A', raised: false, slots: [], move_control: { enabled: true, disabled_reason: '' } },
                  ],
                },
              },
            },
          },
        },
      }));
    });

    const positionColumn = page.locator('.hr-position-column');
    await expect(positionColumn).toContainText('Raised');
    await expect(positionColumn).toContainText('Row B');
    await expect(positionColumn).toContainText('Row A');
  });

  test('sends semantic action payloads through the bridge', async ({ page }) => {
    await page.getByTestId('hotend-rack-read-all').click();
    await page.getByTestId('hotend-rack-hotends-info').click();

    const trace = await page.evaluate(() =>
      (window as Window & {
        __deviceWebTrace?: Array<{ direction: string; body: { module: string; action: string } }>;
      }).__deviceWebTrace ?? []
    );
    const requests = trace.filter((entry) => entry.direction === 'web_request' && entry.body.module === 'device_page_hotend_rack');
    expect(requests.some((entry) => entry.body.action === 'read_all')).toBeTruthy();
    expect(requests.some((entry) => entry.body.action === 'open_hotends_info')).toBeTruthy();
  });
});
