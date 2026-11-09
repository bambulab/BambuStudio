import { expect, test } from '@playwright/test';

test.describe('AMS mapping hotend popup', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/#/device_page/ams_mapping_hotend');
    await expect(page.getByTestId('ams-mapping-hotend-page')).toBeVisible();
  });

  test('renders the 5-slot hotend grid from the mock bridge', async ({ page }) => {
    // First slot is selectable + currently checked in mockData.
    const first = page.getByTestId('ams-mapping-hotend-slot-128-0');
    await expect(first).toBeVisible();
    await expect(first).toHaveAttribute('data-state', 'Normal');

    // Second slot is MaterialUnmatch — visible but visually dimmed
    // (PETG filament loaded while slicing requires PLA).
    await expect(page.getByTestId('ams-mapping-hotend-slot-128-1'))
      .toHaveAttribute('data-state', 'MaterialUnmatch');

    // Third slot is MaterialEmpty (rack nozzle present, no filament loaded;
    // still rendered, not selectable in non-all-ext mode).
    await expect(page.getByTestId('ams-mapping-hotend-slot-128-2'))
      .toHaveAttribute('data-state', 'MaterialEmpty');

    // External spool (ext=true) renders as the last card.
    await expect(page.getByTestId('ams-mapping-hotend-slot-254-0'))
      .toBeVisible();
  });

  test('cutter / pen tools render their own icon and are non-pickable', async ({ page }) => {
    // Cutter-equipped rack position: forced into NozzleUnmatch by the backend,
    // shows the "Cutter" label instead of dia / flow.
    const cutter = page.getByTestId('ams-mapping-hotend-slot-128-6');
    await expect(cutter).toBeVisible();
    await expect(cutter).toHaveAttribute('data-state', 'NozzleUnmatch');
    await expect(cutter.locator('.ams-map-hotend__nozzle-info__tool')).toHaveText('Cutter');
    await expect(cutter.locator('.ams-map-hotend__nozzle-info__icon--tool')).toHaveCount(1);

    // Pen / marker tool: same non-pickable treatment, "Pen" label.
    const pen = page.getByTestId('ams-mapping-hotend-slot-128-7');
    await expect(pen).toBeVisible();
    await expect(pen).toHaveAttribute('data-state', 'NozzleUnmatch');
    await expect(pen.locator('.ams-map-hotend__nozzle-info__tool')).toHaveText('Pen');
    await expect(pen.locator('.ams-map-hotend__nozzle-info__icon--tool')).toHaveCount(1);
  });

  test('reset button is wired to the bridge', async ({ page }) => {
    await expect(page.getByTestId('ams-mapping-hotend-reset')).toBeVisible();
    await page.getByTestId('ams-mapping-hotend-reset').click();
    // No throw / no error tip surfaced.
    await expect(page.locator('.ams-map-hotend__tips')).toHaveCount(0);
  });
});
