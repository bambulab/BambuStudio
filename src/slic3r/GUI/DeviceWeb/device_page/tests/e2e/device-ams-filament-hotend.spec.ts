import { expect, test } from '@playwright/test';

test.describe('AMS filament hotend page', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/#/device_page/ams_filament_hotend');
    await expect(page.getByTestId('ams-filament-hotend-page')).toBeVisible();
  });

  test('renders AMS hotend slot grid with mock bridge state', async ({ page }) => {
    await expect(page.getByTestId('ams-hotend-preview')).toBeVisible();
    await expect(page.getByTestId('ams-hotend-slot-1')).toHaveAttribute('data-card-state', 'Loading');
    await expect(page.getByTestId('ams-hotend-slot-name-1')).toContainText('--');
    await expect(page.getByTestId('ams-hotend-slot-status-1')).toContainText('进/退料中');
    await expect(page.getByTestId('ams-filament-change-panel')).toBeVisible();
    await expect(page.getByTestId('ams-filament-change-title')).toContainText('1号进/退料中');
    await expect(page.getByTestId('ams-filament-change-step')).toContainText('咬入耗材丝');

    await expect(page.getByTestId('ams-hotend-slot-2')).toHaveAttribute('data-card-state', 'Reading');
    await expect(page.getByTestId('ams-hotend-slot-status-2')).toContainText('读取中');

    await expect(page.getByTestId('ams-hotend-slot-8')).toHaveAttribute('data-card-state', 'ReadyToUse');
    await expect(page.getByTestId('ams-hotend-nozzle-8')).toHaveAttribute('data-nozzle-state', 'Active');
  });

  test('keeps primary actions wired through mock bridge', async ({ page }) => {
    await expect(page.getByTestId('ams-hotend-action-load-auto')).toBeEnabled();
    await expect(page.getByTestId('ams-hotend-action-load-external')).toBeEnabled();
    await expect(page.getByTestId('ams-hotend-action-unload')).toBeEnabled();
    await expect(page.getByTestId('ams-hotend-action-dry')).toBeEnabled();

    await page.getByTestId('ams-hotend-action-load-auto').click();
    await expect(page.getByTestId('ams-hotend-slot-5')).toHaveAttribute('data-card-state', 'ReadyToLoad');

    await page.getByTestId('ams-hotend-tab-hotend').click();
    await expect(page.getByTestId('ams-hotend-tab-hotend')).toHaveClass(/active/);
  });

  test('shows full-page progress while reading all hotends', async ({ page }) => {
    await page.getByTestId('ams-hotend-tab-hotend').click();
    await page.getByTestId('ams-hotend-action-read-all').click();

    await expect(page.getByTestId('ams-hotend-read-all-progress')).toBeVisible();
    await expect(page.getByTestId('ams-hotend-read-all-progress')).toContainText('读取中（1 / 9）');
    await expect(page.getByTestId('ams-hotend-read-all-tips')).toContainText('请稍后...');
    await expect(page.getByTestId('ams-hotend-action-read-all')).toBeDisabled();
    const pageBox = await page.getByTestId('ams-filament-hotend-page').boundingBox();
    expect(pageBox?.height).toBeGreaterThanOrEqual(260);
  });

  test('allows AMS operation from an empty slot with bound external AMS', async ({ page }) => {
    await page.evaluate(() => {
      document.dispatchEvent(new CustomEvent('cpp:device', {
        detail: {
          head: { version: '1.0', type: 'report', seq: 9001, ts: Date.now() },
          body: {
            module: 'device_page_ams_filament_hotend',
            submod: 'state',
            action: 'changed',
            error_code: 0,
            message: '',
            payload: {
              filament_page: {
                actions: {},
                filament_change_status: { active: false, mode: 'idle', message: '', tips: '' },
                hotend_slots: [{
                  nozzle_pos: 20,
                  display_index: 5,
                  ams_id: '',
                  slot_id: '',
                  bind_ams: [{ ams_id: '0', ams_type: 'ZMULTI_A', slot_count: 8 }],
                  bind_ams_slot_count: 8,
                  is_top_tier: false,
                  enabled: true,
                  filament_card: {
                    state: 'Empty',
                    color: '#D9D9D9',
                    color_type: 'single',
                    name: '',
                    type: '',
                    remain: 100,
                    status_text: '--',
                  },
                  filament_line: { state: 'no_filament', has_filament: false, filament_color: '' },
                  filament_nozzle: { state: 'Normal', has_filament: false, filament_color: '', on_extruder: false },
                }],
              },
              hotend_page: {
                actions: {},
                hotend_slots: [{
                  nozzle_pos: 20,
                  display_index: 5,
                  ams_id: '',
                  slot_id: '',
                  enabled: true,
                  taken: false,
                  hotend: { current_temp: null, target_temp: 0, op_state: 'Idle', tool_kind: 'empty' },
                }],
              },
            },
          },
        },
      }));
    });

    await page.getByTestId('ams-hotend-slot-5').getByLabel('slot menu').click();
    await expect(page.getByRole('menuitem', { name: 'AMS操作' })).toBeVisible();
  });

  test('renders unknown filament independently from loading state', async ({ page }) => {
    await page.evaluate(() => {
      document.dispatchEvent(new CustomEvent('cpp:device', {
        detail: {
          head: { version: '1.0', type: 'report', seq: 9002, ts: Date.now() },
          body: {
            module: 'device_page_ams_filament_hotend',
            submod: 'state',
            action: 'changed',
            error_code: 0,
            message: '',
            payload: {
              filament_page: {
                actions: {},
                filament_change_status: {
                  active: true,
                  mode: 'single',
                  operation: 'change',
                  running_slot: { ams_id: '0', slot_id: '0', display_index: 1 },
                  current_step: 6,
                  current_step_name: 'Grab new filament',
                  steps: [6],
                  step_labels: ['Grab new filament'],
                  current_step_index: 1,
                  current_step_total: 1,
                  message: 'Slot 1 loading/unloading filament in progress',
                  tips: '',
                },
                hotend_slots: [{
                  nozzle_pos: 16,
                  display_index: 1,
                  ams_id: '0',
                  slot_id: '0',
                  bind_ams: [],
                  bind_ams_slot_count: 0,
                  is_top_tier: false,
                  enabled: true,
                  filament_card: {
                    state: 'Unknown',
                    color: '#D9D9D9',
                    color_type: 'single',
                    name: '',
                    type: '',
                    remain: 100,
                    status_text: 'Unknown filament',
                  },
                  filament_line: { state: 'loading', has_filament: true, filament_color: '' },
                  filament_nozzle: { state: 'Active', has_filament: true, filament_color: '', on_extruder: true },
                }],
              },
              hotend_page: {
                actions: {},
                hotend_slots: [{
                  nozzle_pos: 16,
                  display_index: 1,
                  ams_id: '0',
                  slot_id: '0',
                  enabled: true,
                  taken: true,
                  hotend: { current_temp: 220, target_temp: 0, op_state: 'Idle', tool_kind: 'nozzle' },
                }],
              },
            },
          },
        },
      }));
    });

    await expect(page.getByTestId('ams-hotend-slot-1')).toHaveAttribute('data-card-state', 'Unknown');
    await expect(page.getByTestId('ams-hotend-slot-1')).toHaveAttribute('data-line-state', 'loading');
    await expect(page.getByTestId('ams-hotend-slot-name-1')).toContainText('?');
    await expect(page.getByTestId('ams-hotend-card-loading')).toBeVisible();
  });

  test('shows slot progress and disables other hotend operations while one hotend is reading', async ({ page }) => {
    await page.getByTestId('ams-hotend-tab-hotend').click();
    await page.getByLabel('hotend menu 9').click();
    await page.getByRole('menuitem', { name: '读取' }).click();

    await expect(page.getByTestId('ams-hotend-hotend-card-9')).toHaveAttribute('data-op-state', 'Reading');
    await expect(page.getByTestId('ams-hotend-reading-status')).toContainText('9号热端读取中');
    await expect(page.getByTestId('ams-hotend-action-read-all')).toBeDisabled();

    await page.getByLabel('hotend menu 1').click();
    await expect(page.getByRole('menuitem', { name: '读取' })).toBeDisabled();
    await expect(page.getByRole('menuitem', { name: '取用' })).toBeDisabled();
    await expect(page.getByRole('menuitem', { name: '详情' })).toBeEnabled();
  });

});
