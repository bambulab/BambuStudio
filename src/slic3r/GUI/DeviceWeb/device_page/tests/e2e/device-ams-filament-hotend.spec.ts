import { expect, test } from '@playwright/test';

test.describe('AMS filament hotend page', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/#/device_page/ams_filament_hotend');
    await expect(page.getByTestId('ams-filament-hotend-page')).toBeVisible();
  });

  test('renders AMS hotend slot grid with mock bridge state', async ({ page }) => {
    await expect(page.getByTestId('ams-hotend-preview')).toBeVisible();
    await expect(page.getByTestId('ams-hotend-slot-1')).toHaveAttribute('data-card-state', 'ReadyToLoad');
    await expect(page.getByTestId('ams-hotend-slot-name-1')).toContainText('PLA Basic');
    await expect(page.getByTestId('ams-hotend-slot-status-1')).toContainText('待进料');

    await expect(page.getByTestId('ams-hotend-slot-4')).toHaveAttribute('data-card-state', 'Reading');
    await expect(page.getByTestId('ams-hotend-slot-status-4')).toContainText('读取中');

    await expect(page.getByTestId('ams-hotend-slot-8')).toHaveAttribute('data-card-state', 'ReadyToUse');
    await expect(page.getByTestId('ams-hotend-nozzle-8')).toHaveAttribute('data-nozzle-state', 'Active');
  });

  test('keeps primary actions wired through mock bridge', async ({ page }) => {
    await expect(page.getByTestId('ams-hotend-action-load')).toBeEnabled();
    await expect(page.getByTestId('ams-hotend-action-unload')).toBeEnabled();
    await expect(page.getByTestId('ams-hotend-action-dry')).toBeEnabled();

    await page.getByTestId('ams-hotend-action-load').click();
    await expect(page.getByTestId('ams-hotend-slot-5')).toHaveAttribute('data-card-state', 'ReadyToLoad');

    await page.getByTestId('ams-hotend-tab-hotend').click();
    await expect(page.getByTestId('ams-hotend-tab-hotend')).toHaveClass(/active/);
  });
});
