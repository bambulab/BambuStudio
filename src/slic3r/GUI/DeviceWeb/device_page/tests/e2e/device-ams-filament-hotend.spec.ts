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

  test('renders AMS unit humidity and temperature outside the dryer chamber', async ({ page }) => {
    const ambient = page.getByTestId('ams-hotend-ambient-info');
    await expect(ambient).toHaveCount(1);
    await expect(ambient).toContainText('24%');
    await expect(ambient).toContainText('24℃');

    // The readout anchors to the left of the unit's first column, so it must
    // stay clear of slot 2 (and the dryer chamber on the right half of the top tier).
    const ambientBox = await ambient.boundingBox();
    const slot2Box = await page.getByTestId('ams-hotend-slot-2').boundingBox();
    const chamberBox = await page.getByTestId('ams-hotend-dryer-chamber').boundingBox();
    expect(ambientBox!.x + ambientBox!.width).toBeLessThanOrEqual(slot2Box!.x);
    expect(ambientBox!.x + ambientBox!.width).toBeLessThanOrEqual(chamberBox!.x);

    // Even when slot 2 is fed by an external spool (`ams_id: "254"`), `zone_addrs`
    // keeps the ambient readout anchored to the left of slot 2 rather than shifting
    // into the gap between slot 2 and slot 4.
    await page.evaluate(() => {
      document.dispatchEvent(new CustomEvent('cpp:device', {
        detail: {
          head: { version: '1.0', type: 'report', seq: 9099, ts: Date.now() },
          body: {
            module: 'device_page_ams_filament_hotend',
            submod: 'state',
            action: 'changed',
            error_code: 0,
            message: '',
            payload: {
              filament_page: {
                actions: {},
                ams_dry_zones: [{
                  ams_id: '32',
                  zone_slots: [5, 7],
                  zone_addrs: [5, 7],
                  humidity_raw: 35,
                  dry_time: 0,
                  dry_status: 'Idle',
                  dry_status_text: '空闲',
                }],
                ams_env_units: [{
                  ams_id: '32',
                  zone_addrs: [0, 1, 2, 3, 4, 6, 8],
                  humidity_raw: 34,
                  temp: 25,
                }],
                hotend_slots: [
                  { addr_id: 0, display_index: 1, ams_id: '255', slot_id: '0', is_top_tier: false, is_external_spool: true, enabled: true, filament_card: { state: 'ReadyToUse', color: '#FFFFFF', text: 'PLA', name: 'PLA', type: 'PLA', status_text: '' }, filament_line: { state: 'loaded' }, filament_nozzle: { state: 'Normal' } },
                  { addr_id: 1, display_index: 2, ams_id: '254', slot_id: '0', is_top_tier: true, is_external_spool: true, enabled: true, filament_card: { state: 'ReadyToUse', color: '#FF0000', text: 'PLA', name: 'PLA', type: 'PLA', status_text: '' }, filament_line: { state: 'loaded' }, filament_nozzle: { state: 'Normal' } },
                  { addr_id: 2, display_index: 3, ams_id: '253', slot_id: '0', is_top_tier: false, is_external_spool: true, enabled: true, filament_card: { state: 'ReadyToUse', color: '#884400', text: 'PLA', name: 'PLA', type: 'PLA', status_text: '' }, filament_line: { state: 'loaded' }, filament_nozzle: { state: 'Normal' } },
                  { addr_id: 3, display_index: 4, ams_id: '32', slot_id: '3', is_top_tier: true, is_external_spool: false, enabled: true, filament_card: { state: 'ReadyToUse', color: '#0066FF', text: 'PLA', name: 'PLA', type: 'PLA', status_text: '' }, filament_line: { state: 'loaded' }, filament_nozzle: { state: 'Normal' } },
                  { addr_id: 4, display_index: 5, ams_id: '32', slot_id: '4', is_top_tier: false, is_external_spool: false, enabled: true, filament_card: { state: 'ReadyToUse', color: '#6600AA', text: 'PLA', name: 'PLA', type: 'PLA', status_text: '' }, filament_line: { state: 'loaded' }, filament_nozzle: { state: 'Normal' } },
                  { addr_id: 5, display_index: 6, ams_id: '32', slot_id: '5', is_top_tier: true, is_external_spool: false, enabled: true, filament_card: { state: 'ReadyToUse', color: '#0033AA', text: 'PLA', name: 'PLA', type: 'PLA', status_text: '' }, filament_line: { state: 'loaded' }, filament_nozzle: { state: 'Normal' } },
                  { addr_id: 6, display_index: 7, ams_id: '249', slot_id: '0', is_top_tier: false, is_external_spool: true, enabled: true, filament_card: { state: 'ReadyToLoad', color: '#DD1133', text: 'TPU', name: 'TPU', type: 'TPU', status_text: '' }, filament_line: { state: 'no_filament' }, filament_nozzle: { state: 'Normal' } },
                  { addr_id: 7, display_index: 8, ams_id: '248', slot_id: '0', is_top_tier: true, is_external_spool: true, enabled: true, filament_card: { state: 'ReadyToLoad', color: '#EEDD55', text: 'TPU', name: 'TPU', type: 'TPU', status_text: '' }, filament_line: { state: 'no_filament' }, filament_nozzle: { state: 'Normal' } },
                  { addr_id: 8, display_index: 9, ams_id: '32', slot_id: '8', is_top_tier: false, is_external_spool: false, enabled: true, filament_card: { state: 'ReadyToUse', color: '#DDDD00', text: 'PLA', name: 'PLA', type: 'PLA', status_text: '' }, filament_line: { state: 'loaded' }, filament_nozzle: { state: 'Normal' } },
                ],
              },
              hotend_page: { actions: {}, hotend_slots: [] },
            },
          },
        },
      }));
    });

    await expect(ambient).toContainText('34%');
    await expect(ambient).toContainText('25℃');
    const extAmbientBox = await ambient.boundingBox();
    const extSlot2Box = await page.getByTestId('ams-hotend-slot-2').boundingBox();
    expect(extAmbientBox!.x + extAmbientBox!.width).toBeLessThanOrEqual(extSlot2Box!.x);
  });

  test('hides AMS unit humidity and temperature when the payload disables it', async ({ page }) => {
    await expect(page.getByTestId('ams-hotend-ambient-info')).toHaveCount(1);

    await page.evaluate(() => {
      document.dispatchEvent(new CustomEvent('cpp:device', {
        detail: {
          head: { version: '1.0', type: 'report', seq: 9100, ts: Date.now() },
          body: {
            module: 'device_page_ams_filament_hotend',
            submod: 'state',
            action: 'changed',
            error_code: 0,
            message: '',
            payload: {
              filament_page: {
                mode: 'batch_ext_load',
                actions: {},
                show_ams_env_units: false,
              },
              hotend_page: { actions: {}, hotend_slots: [] },
            },
          },
        },
      }));
    });

    await expect(page.getByTestId('ams-hotend-ambient-info')).toHaveCount(0);
  });

  test('clicking the dryer chamber readout opens drying for that unit', async ({ page }) => {
    const dryRequests = async () => {
      const trace = await page.evaluate(() =>
        (window as unknown as {
          __deviceWebTrace?: Array<{ direction: string; body: { action: string; payload: Record<string, unknown> } }>;
        }).__deviceWebTrace ?? []
      );
      return [...trace].reverse().filter((e) => e.direction === 'web_request' && e.body?.action === 'dry');
    };

    await expect(page.getByTestId('ams-hotend-ambient-dry')).toHaveCount(0);
    const ambientBox = (await page.getByTestId('ams-hotend-ambient-info').boundingBox())!;
    await page.mouse.click(ambientBox.x + ambientBox.width / 2, ambientBox.y + ambientBox.height / 2);
    expect(await dryRequests()).toHaveLength(0);

    await page.getByTestId('ams-hotend-dryer-dry').click();
    const requests = await dryRequests();
    expect(requests.length).toBeGreaterThan(0);
    expect(requests[0]!.body.payload).toEqual({ ams_id: '0' });
  });

  test('renders the icon toolbar and More dropdown wired through mock bridge', async ({ page }) => {
    // New N1x toolbar: Read All (Rack-B only) + AMS Settings + More icons.
    await expect(page.getByTestId('ams-hotend-action-read-all')).toBeVisible();
    await expect(page.getByTestId('ams-hotend-action-settings')).toBeVisible();
    await expect(page.getByTestId('ams-hotend-action-more')).toBeVisible();

    // Open the More dropdown; the relocated actions live inside it.
    await page.getByTestId('ams-hotend-action-more').click();
    await expect(page.getByTestId('ams-hotend-more-menu')).toBeVisible();
    await expect(page.getByTestId('ams-hotend-action-unload')).toBeVisible();
    await expect(page.getByTestId('ams-hotend-action-dry')).toBeVisible();

    await page.getByTestId('ams-hotend-tab-hotend').click();
    await expect(page.getByTestId('ams-hotend-tab-hotend')).toHaveClass(/active/);
  });

  test('shows full-page progress while reading all hotends', async ({ page }) => {
    await page.getByTestId('ams-hotend-tab-hotend').click();
    await page.getByTestId('ams-hotend-action-read-all').click();

    await expect(page.getByTestId('ams-hotend-rack-progress')).toBeVisible();
    await expect(page.getByTestId('ams-hotend-reading-status')).toContainText('读取中（1 / 9）');
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
                  addr_id: 4,
                  display_index: 5,
                  ams_id: '',
                  slot_id: '',
                  bind_ams: [{ ams_id: '0', ams_type: 'ZMULTI_A', slot_count: 8 }],
                  bind_ams_count: 1,
                  is_top_tier: false,
                  enabled: true,
                  filament_card: {
                    state: 'Empty',
                    color: '#D9D9D9',
                    color_type: 'single',
                    text: '',
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
                  addr_id: 4,
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
    await expect(page.getByRole('menuitem', { name: /Connected Slots|查看已连接耗材/ })).toBeVisible();
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
                  addr_id: 0,
                  display_index: 1,
                  ams_id: '0',
                  slot_id: '0',
                  bind_ams: [],
                  bind_ams_count: 0,
                  is_top_tier: false,
                  enabled: true,
                  filament_card: {
                    state: 'Unknown',
                    color: '#D9D9D9',
                    color_type: 'single',
                    text: '',
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
                  addr_id: 0,
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

  test('shows manual card from filament step style', async ({ page }) => {
    await page.evaluate(() => {
      document.dispatchEvent(new CustomEvent('cpp:device', {
        detail: {
          head: { version: '1.0', type: 'report', seq: 9004, ts: Date.now() },
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
                  current_step: 99,
                  current_step_name: 'Manual load',
                  steps: [{ id: 99, label: 'Manual load', step_style: 'manual_load', step_style_text: 'Custom manual load text' }],
                  current_step_index: 1,
                  current_step_total: 1,
                  message: 'Slot 1 loading/unloading filament in progress',
                  tips: '',
                },
                hotend_slots: [],
              },
              hotend_page: {
                actions: {},
                hotend_slots: [],
              },
            },
          },
        },
      }));
    });

    await expect(page.getByTestId('ams-filament-change-step')).toContainText('Manual load');
    await expect(page.getByText('Manual action required')).toBeVisible();
    await expect(page.getByText('Custom manual load text')).toBeVisible();
  });

  test('shows manual card from unload step style', async ({ page }) => {
    await page.evaluate(() => {
      document.dispatchEvent(new CustomEvent('cpp:device', {
        detail: {
          head: { version: '1.0', type: 'report', seq: 9005, ts: Date.now() },
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
                  current_step: 98,
                  current_step_name: 'Manual unload',
                  steps: [{ id: 98, label: 'Manual unload', step_style: 'maunal_unload', step_style_text: 'Custom manual unload text' }],
                  current_step_index: 1,
                  current_step_total: 1,
                  message: 'Slot 1 loading/unloading filament in progress',
                  tips: '',
                },
                hotend_slots: [],
              },
              hotend_page: {
                actions: {},
                hotend_slots: [],
              },
            },
          },
        },
      }));
    });

    await expect(page.getByTestId('ams-filament-change-step')).toContainText('Manual unload');
    await expect(page.getByText('Manual action required')).toBeVisible();
    await expect(page.getByText('Custom manual unload text')).toBeVisible();
  });

  test('shows manual pull banner for batch pull step', async ({ page }) => {
    await page.evaluate(() => {
      document.dispatchEvent(new CustomEvent('cpp:device', {
        detail: {
          head: { version: '1.0', type: 'report', seq: 9006, ts: Date.now() },
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
                  mode: 'batch_manual_pull',
                  operation: 'change',
                  current_step: 15,
                  current_step_name: 'Pull back current filament',
                  steps: [],
                  current_step_index: 0,
                  current_step_total: 0,
                  message: '',
                  tips: '',
                  manual_pull: {
                    title: '有耗材未拔出',
                    can_exit: false,
                    can_close: true,
                    items: [{
                      ams_id: '0',
                      slot_id: '0',
                      display_index: 1,
                      filament_name: 'PLA Basic',
                      filament_type: 'PLA',
                      color: '#00CC44',
                      color_type: 'single',
                      state: 'pending',
                      reason: '',
                    }],
                  },
                },
                hotend_slots: [],
              },
              hotend_page: {
                actions: {},
                hotend_slots: [],
              },
            },
          },
        },
      }));
    });

    await expect(page.getByTestId('ams-filament-change-panel')).toBeHidden();
    await expect(page.getByTestId('ams-filament-manual-pull-banner')).toContainText('有耗材未拔出');
  });

  test('shows manual pull dialog route for batch pull step', async ({ page }) => {
    await page.goto('/#/device_page/filament_manual_pull');
    await expect(page.getByTestId('ams-filament-manual-pull-page')).toBeVisible();

    await page.evaluate(() => {
      document.dispatchEvent(new CustomEvent('cpp:device', {
        detail: {
          head: { version: '1.0', type: 'report', seq: 9007, ts: Date.now() },
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
                  mode: 'batch_manual_pull',
                  operation: 'change',
                  manual_pull: {
                    title: '有耗材未拔出',
                    can_exit: false,
                    can_close: true,
                    items: [{
                      ams_id: '0',
                      slot_id: '0',
                      display_index: 1,
                      filament_name: 'PLA Basic',
                      filament_type: 'PLA',
                      color: '#00CC44',
                      color_type: 'single',
                      state: 'pending',
                      reason: '',
                    }],
                  },
                },
                hotend_slots: [],
              },
              hotend_page: {
                actions: {},
                hotend_slots: [],
              },
            },
          },
        },
      }));
    });

    await expect(page.getByTestId('ams-filament-manual-pull-dialog')).toBeVisible();
    await expect(page.getByTestId('ams-filament-manual-pull-dialog')).toContainText('Please pull out all selected filaments for this unload.');
    await expect(page.getByTestId('ams-filament-manual-pull-dialog')).toContainText('Completed: 0/1');
    await expect(page.getByTestId('ams-filament-manual-pull-dialog')).toContainText('1');
    await expect(page.getByTestId('ams-filament-manual-pull-result-1')).toHaveAttribute('alt', 'Not removed');
  });

  test('shows prefeed color marker only outside batch mode', async ({ page }) => {
    const dispatchPrefeedState = async (mode: 'normal' | 'batch_ams_load') => {
      await page.evaluate((nextMode) => {
        document.dispatchEvent(new CustomEvent('cpp:device', {
          detail: {
            head: { version: '1.0', type: 'report', seq: 9003, ts: Date.now() },
            body: {
              module: 'device_page_ams_filament_hotend',
              submod: 'state',
              action: 'changed',
              error_code: 0,
              message: '',
              payload: {
                filament_page: {
                  mode: nextMode,
                  actions: {},
                  filament_change_status: { active: false, mode: 'idle', message: '', tips: '' },
                  hotend_slots: [{
                    addr_id: 0,
                    display_index: 1,
                    ams_id: '255',
                    slot_id: '0',
                    bind_ams: [],
                    bind_ams_count: 0,
                    is_top_tier: false,
                    enabled: true,
                    is_external_spool: true,
                    select_action: nextMode === 'batch_ams_load' ? { can_select: true, reason: '' } : undefined,
                    filament_card: {
                      state: 'ReadyToUse',
                      color: '#E02020',
                      color_type: 'single',
                      text: 'PLA Basic',
                      name: 'PLA Basic',
                      type: 'PLA',
                      remain: 100,
                      status_text: '',
                      prefeed_indicator: {
                        visible: true,
                        colors: ['#00CC44'],
                        color_type: 'single',
                      },
                    },
                    filament_line: { state: 'loading', has_filament: true, filament_color: '#E02020' },
                    filament_nozzle: { state: 'Active', has_filament: true, filament_color: '#E02020', on_extruder: true },
                  }],
                },
                hotend_page: {
                  actions: {},
                  hotend_slots: [{
                    addr_id: 0,
                    display_index: 1,
                    ams_id: '255',
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
      }, mode);
    };

    await dispatchPrefeedState('normal');
    await expect(page.getByTestId('ams-hotend-slot-1')).toHaveAttribute('data-line-state', 'loading');
    await expect(page.getByTestId('ams-hotend-prefeed-indicator-1')).toBeVisible();
    await expect(page.getByTestId('ams-hotend-prefeed-indicator-1')).toHaveAttribute('data-color-type', 'single');

    await dispatchPrefeedState('batch_ams_load');
    await expect(page.getByTestId('ams-hotend-prefeed-indicator-1')).toBeHidden();
  });

  test('shows slot progress and disables other hotend operations while one hotend is reading', async ({ page }) => {
    await page.getByTestId('ams-hotend-tab-hotend').click();
    await page.getByLabel('hotend menu 9').click();
    await page.getByRole('menuitem', { name: '刷新' }).click();

    await expect(page.getByTestId('ams-hotend-hotend-card-9')).toHaveAttribute('data-op-state', 'Reading');
    await expect(page.getByTestId('ams-hotend-reading-status')).toContainText('9号热端读取中');
    await expect(page.getByTestId('ams-hotend-action-read-all')).toBeDisabled();

    await page.getByLabel('hotend menu 1').click();
    await expect(page.getByRole('menuitem', { name: '刷新' })).toBeDisabled();
    await expect(page.getByRole('menuitem', { name: '取用' })).toBeDisabled();
    await expect(page.getByRole('menuitem', { name: '详情' })).toBeEnabled();
  });

});
