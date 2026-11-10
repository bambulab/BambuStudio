import { expect, test, type Page } from '@playwright/test';

async function injectState(page: Page, display: Record<string, unknown>) {
  await page.evaluate((payloadDisplay) => {
    document.dispatchEvent(new CustomEvent('cpp:device', {
      detail: {
        head: { version: '1.0', type: 'report', seq: 9400, ts: Date.now() },
        body: {
          module: 'device_page_hotend_rack',
          submod: 'state',
          action: 'changed',
          error_code: 0,
          message: '',
          payload: { display: { visible: true, ...payloadDisplay } },
        },
      },
    }));
  }, display);
}

const control = (enabled: boolean) => ({ enabled, disabled_reason: '' });
const slot = (id: number, status: string, diameter: string, flowType = '') => ({
  id,
  display_id: String(id + 1),
  status,
  diameter,
  flow_type: flowType,
  filament_color: '',
  has_error_detail: status === 'error',
});

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

  test('replaces rows with an animated reading page and native progress text', async ({ page }) => {
    await injectState(page, {
      rack: { position: 'a_top', busy: true, rows: [] },
      reading: { active: true, index: 2, count: 6 },
    });

    const reading = page.getByTestId('hotend-rack-reading');
    await expect(reading).toContainText('Reading (2/6) ...');
    await expect(reading).toContainText('Please wait');
    await expect(page.getByTestId('hotend-rack-read-all')).toHaveCount(0);

    const icon = page.getByTestId('hotend-rack-reading-icon');
    const firstFrame = await icon.getAttribute('src');
    await expect.poll(async () => icon.getAttribute('src'), { timeout: 2000 }).not.toBe(firstFrame);
  });

  test('marks abnormal hotends in red and only the error icon opens details', async ({ page }) => {
    await injectState(page, {
      rack: {
        position: 'a_top',
        busy: false,
        rows: [
          { id: 'A', raised: true, slots: [slot(0, 'error', 'Error'), slot(2, 'normal', '0.4mm', 'Standard'), slot(4, 'empty', 'Empty')], move_control: control(false) },
          { id: 'B', raised: false, slots: [slot(1, 'unknown', 'Unknown'), slot(3, 'empty', 'Empty'), slot(5, 'empty', 'Empty')], move_control: control(true) },
        ],
      },
      actions: { home: control(true), read_all: control(true), hotends_info: control(true) },
    });

    const errorCard = page.getByTestId('hotend-rack-slot-1');
    await expect(errorCard).toHaveJSProperty('tagName', 'DIV');
    const dangerColor = await page.evaluate(() => {
      const probe = document.createElement('span');
      probe.style.color = 'var(--hr-danger)';
      document.querySelector('.hotend-rack-page')?.appendChild(probe);
      const color = getComputedStyle(probe).color;
      probe.remove();
      return color;
    });
    expect(['rgb(208, 27, 27)', 'rgb(187, 42, 58)']).toContain(dangerColor);
    await expect(errorCard.locator('.hr-slot-line.error')).toHaveCSS('color', dangerColor);

    await page.getByTestId('hotend-rack-slot-error-1').click();
    const trace = await page.evaluate(() =>
      (window as Window & {
        __deviceWebTrace?: Array<{ direction: string; body: { module: string; action: string; payload?: { slot_id?: number } } }>;
      }).__deviceWebTrace ?? []
    );
    const detail = trace.find((entry) => entry.direction === 'web_request'
      && entry.body.module === 'device_page_hotend_rack'
      && entry.body.action === 'show_error_detail');
    expect(detail?.body.payload?.slot_id).toBe(0);
  });

  test('shows Empty under an empty toolhead', async ({ page }) => {
    await injectState(page, {
      toolhead: { status: 'empty', diameter: 'Empty', flow_type: '', filament_color: '' },
    });
    await expect(page.getByTestId('hotend-rack-toolhead')).toContainText('Empty');
  });
});

test('hotend rack debug page loads the snapshot from the hotend rack module', async ({ page }) => {
  await page.goto('/#/device_page/hotend_rack_debug');
  await expect(page.getByRole('heading', { name: 'Hotend Rack Web Bridge Debug' })).toBeVisible();
  await expect(page.locator('section').filter({ hasText: 'display' }).first()).toContainText('Induction Hotend Rack');

  const trace = await page.evaluate(() =>
    (window as Window & {
      __deviceWebTrace?: Array<{ direction: string; body: { module: string; submod: string; action: string } }>;
    }).__deviceWebTrace ?? []
  );
  expect(trace.some((entry) => entry.direction === 'web_request'
    && entry.body.module === 'device_page_hotend_rack'
    && entry.body.submod === 'debug'
    && entry.body.action === 'snapshot')).toBeTruthy();
});
