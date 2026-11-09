import { expect, test, type Page } from '@playwright/test';

// Mock E2E of the Select Filament dialog. A fake WebView2 host answers the
// real bridge packets, so the test covers the C++ <-> Web contract
// (state/get, action/confirm, action/close, state reports) end to end on the
// Web side. Data is synthetic: no real spool / device identifiers.

const MODULE = 'device_page_filament_select';

type Row = {
  row_key: string;
  chip_key: string;
  search_key: string;
  name: string;
  weight_g: number;
  swatch: { color_type: 0 | 1 | 2; colors: string[]; color_code: string };
  show_bbl_logo: boolean;
  tail: { kind: 'slot' | 'recent' | 'none'; slot_name: string };
  selectable: boolean;
};

function row(i: number, chipKey: string, name: string, extra: Partial<Row> = {}): Row {
  return {
    row_key: `r${i}`,
    chip_key: chipKey,
    search_key: `${chipKey.replace('brand:', '')} ${name}`.toLowerCase(),
    name,
    weight_g: 1000 - i,
    swatch: { color_type: 2, colors: [], color_code: '#00AE42' },
    show_bbl_logo: chipKey === 'brand:Bambu Lab',
    tail: { kind: 'none', slot_name: '' },
    selectable: true,
    ...extra,
  };
}

const BRANDS = ['Bambu Lab', 'Generic', 'Polymaker', 'Elegoo', 'eSUN', 'Overture', 'SUNLU', 'Kingroon', 'Jayo'];

function makeState() {
  return {
    theme: 'light',
    manager: {
      chips: [
        { key: 'all', kind: 'all', label: '' },
        ...BRANDS.map((b) => ({ key: `brand:${b}`, kind: 'brand', label: b })),
        { key: 'other', kind: 'other', label: '' },
        { key: 'unsupported', kind: 'unsupported', label: '' },
      ],
      rows: [
        row(0, 'brand:Bambu Lab', 'PLA Basic', { tail: { kind: 'recent', slot_name: '' } }),
        row(1, 'brand:Generic', 'Generic PETG Black'),
        row(2, 'brand:Polymaker', 'PolyTerra PLA Grey'),
        row(3, 'other', 'PLA Orange'),
        row(4, 'brand:Bambu Lab', 'PETG HF', { tail: { kind: 'slot', slot_name: 'A1' }, selectable: false }),
        row(5, 'unsupported', 'Acme PA-CF Black', { selectable: false }),
      ],
    },
    presets: {
      brands: [
        { key: 'brand:Bambu Lab', kind: 'brand', label: 'Bambu Lab' },
        { key: 'brand:Generic', kind: 'brand', label: 'Generic' },
        { key: 'other', kind: 'other', label: '' },
      ],
      aliases: {
        'brand:Bambu Lab': ['Bambu PLA Basic', 'Bambu PETG HF'],
        'brand:Generic': ['Generic PLA', 'Generic PETG'],
        other: ['Custom PLA'],
      },
      initial_brand: 'brand:Generic',
      checked_alias: 'Generic PETG',
    },
  };
}

type Sent = { module: string; submod: string; action: string; payload: Record<string, unknown> };

async function openPage(page: Page, state: unknown) {
  await page.addInitScript((initialState) => {
    const w = window as unknown as {
      chrome: { webview: { postMessage: (s: string) => void } };
      __fsSent: unknown[];
      __fsReport: (payload: unknown) => void;
    };
    w.__fsSent = [];
    const reply = (head: { seq: number }, body: Record<string, unknown>, payload: unknown) => {
      const detail = {
        head: { version: '1.0', type: 'response', seq: head.seq, ts: Date.now() },
        body: { ...body, error_code: 0, message: '', payload },
      };
      setTimeout(() => document.dispatchEvent(new CustomEvent('cpp:device', { detail })), 0);
    };
    w.chrome = {
      webview: {
        postMessage(str: string) {
          const pkt = JSON.parse(str);
          const body = pkt.body ?? {};
          if (body.module !== 'device_page_filament_select') return;
          w.__fsSent.push(body);
          if (body.submod === 'state' && body.action === 'get') reply(pkt.head, body, initialState);
          else reply(pkt.head, body, {});
        },
      },
    };
    w.__fsReport = (payload: unknown) => {
      const detail = {
        head: { version: '1.0', type: 'report', seq: 0, ts: Date.now() },
        body: { module: 'device_page_filament_select', submod: 'state', action: 'theme_changed', error_code: 0, message: '', payload },
      };
      document.dispatchEvent(new CustomEvent('cpp:device', { detail }));
    };
  }, state);
  await page.setViewportSize({ width: 477, height: 517 });
  await page.goto('/#/device_page/filament_select');
  await expect(page.getByTestId('filament-select-page')).toBeVisible();
}

async function sent(page: Page): Promise<Sent[]> {
  return page.evaluate(() => (window as unknown as { __fsSent: Sent[] }).__fsSent);
}

async function actions(page: Page): Promise<Sent[]> {
  return (await sent(page)).filter((b) => b.submod === 'action');
}

async function visibleRowKeys(page: Page): Promise<string[]> {
  return page.locator('.fs-row').evaluateAll((els) =>
    els.map((el) => (el.getAttribute('data-testid') ?? '').replace('filament-select-row-', '')));
}

test.describe('Select Filament dialog (Web)', () => {
  test('requests state and renders rows in the C++ order', async ({ page }) => {
    await openPage(page, makeState());
    await expect.poll(async () => (await sent(page)).some((b) => b.submod === 'state' && b.action === 'get')).toBe(true);
    await expect.poll(() => visibleRowKeys(page)).toEqual(['r0', 'r1', 'r2', 'r3', 'r4', 'r5']);

    const recent = page.getByTestId('filament-select-row-r0');
    await expect(recent.getByTestId('filament-select-row-recent')).toHaveText('Recently used');
    await expect(recent.locator('.fs-row__weight')).toHaveText('1000g');
    await expect(recent.locator('.fs-row__logo')).toHaveCount(1);

    const inPrinter = page.getByTestId('filament-select-row-r4');
    await expect(inPrinter).toHaveClass(/fs-row--dimmed/);
    await expect(inPrinter.getByTestId('filament-select-row-slot')).toHaveText('A1 slot');
    await expect(page.getByTestId('filament-select-row-r5')).toHaveClass(/fs-row--dimmed/);
    await expect(page.getByTestId('filament-select-row-r1').locator('.fs-row__logo')).toHaveCount(0);
  });

  test('chip filter, All pinned, arrow paging', async ({ page }) => {
    await openPage(page, makeState());
    const all = page.getByTestId('filament-select-chip-all');
    await expect(all).toHaveClass(/fs-chip--active/);
    // Paged out at this width: still on the track, but outside the window.
    await expect(page.getByTestId('filament-select-chip-other')).toHaveAttribute('data-paged-out', 'true');

    await page.getByTestId('filament-select-chip-brand:Bambu Lab').click();
    await expect.poll(() => visibleRowKeys(page)).toEqual(['r0', 'r4']);

    const left = page.getByTestId('filament-select-chip-left');
    const right = page.getByTestId('filament-select-chip-right');
    await expect(left).toBeDisabled();
    await expect(right).toBeEnabled();

    await right.click();
    await expect(left).toBeEnabled();
    await expect(page.getByTestId('filament-select-chip-brand:Bambu Lab')).toHaveAttribute('data-paged-out', 'true');
    await expect(all).toBeVisible();
    await expect(all).not.toHaveAttribute('data-paged-out', 'true');
    // The active chip keeps filtering even when paged out (native behaviour).
    await expect.poll(() => visibleRowKeys(page)).toEqual(['r0', 'r4']);

    while (await right.isEnabled()) await right.click();
    await expect(page.getByTestId('filament-select-chip-unsupported')).not.toHaveAttribute('data-paged-out', 'true');
    await page.getByTestId('filament-select-chip-unsupported').click();
    await expect.poll(() => visibleRowKeys(page)).toEqual(['r5']);
  });

  test('arrow click slides the chip track and keeps only whole chips in the window', async ({ page }) => {
    await openPage(page, makeState());
    const row = page.locator('.fs-chip-row');
    const track = page.getByTestId('filament-select-chip-track');
    await expect(track).toHaveAttribute('style', /translateX\(0px\)/);
    await expect(row).not.toHaveClass(/fs-chip-row--sliding/);

    await page.getByTestId('filament-select-chip-right').click();
    await expect(row).toHaveClass(/fs-chip-row--sliding/);
    await expect(track).toHaveCSS('transition-duration', '0.22s');
    await expect(track).toHaveAttribute('style', /translateX\(-\d+(\.\d+)?px\)/);
    // The slide ends and transitions switch off again.
    await expect(row).not.toHaveClass(/fs-chip-row--sliding/);

    const wholeChipsOnly = await page.evaluate(() => {
      const vp = document.querySelector('[data-testid="filament-select-chip-viewport"]')!.getBoundingClientRect();
      return Array.from(document.querySelectorAll('.fs-chip-track .fs-chip')).every((el) => {
        const r = el.getBoundingClientRect();
        const inside = r.left >= vp.left - 0.5 && r.right <= vp.right + 0.5;
        const outside = r.right <= vp.left + 0.5 || r.left >= vp.right - 0.5;
        return el.hasAttribute('data-paged-out') ? outside : inside;
      });
    });
    expect(wholeChipsOnly).toBe(true);

    // Paged-out chips are out of the tab order.
    await expect(page.getByTestId('filament-select-chip-brand:Bambu Lab')).toHaveAttribute('tabindex', '-1');
  });

  test('search re-layout does not animate', async ({ page }) => {
    await openPage(page, makeState());
    await page.getByTestId('filament-select-search').fill('pla');
    await expect(page.locator('.fs-chip-row')).not.toHaveClass(/fs-chip-row--sliding/);
    await expect(page.getByTestId('filament-select-chip-track')).toHaveCSS('transition-duration', '0s');
  });

  test('reduced motion: arrows page without a transition', async ({ page }) => {
    await page.emulateMedia({ reducedMotion: 'reduce' });
    await openPage(page, makeState());
    await page.getByTestId('filament-select-chip-right').click();
    await expect(page.getByTestId('filament-select-chip-track')).toHaveCSS('transition-duration', '0s');
    await expect(page.getByTestId('filament-select-chip-brand:Bambu Lab')).toHaveAttribute('data-paged-out', 'true');
    // The fallback timer still ends the slide state.
    await expect(page.locator('.fs-chip-row')).not.toHaveClass(/fs-chip-row--sliding/);
  });

  test('search filters rows, hides chips without hits and resets paging', async ({ page }) => {
    await openPage(page, makeState());
    const right = page.getByTestId('filament-select-chip-right');
    await right.click();
    await expect(page.getByTestId('filament-select-chip-left')).toBeEnabled();

    await page.getByTestId('filament-select-search').fill('  PLA ');
    await expect.poll(() => visibleRowKeys(page)).toEqual(['r0', 'r2', 'r3']);
    await expect(page.getByTestId('filament-select-chip-left')).toBeDisabled();
    await expect(page.getByTestId('filament-select-chip-brand:Generic')).toHaveCount(0);
    for (const key of ['brand:Bambu Lab', 'brand:Polymaker', 'other']) {
      await expect(page.getByTestId(`filament-select-chip-${key}`)).not.toHaveAttribute('data-paged-out', 'true');
    }
    await expect(right).toBeDisabled();

    await page.getByTestId('filament-select-search').fill('no-such-filament');
    await expect.poll(() => visibleRowKeys(page)).toEqual([]);
    await expect(page.getByTestId('filament-select-chip-all')).toBeVisible();
  });

  test('dimmed rows cannot be selected; confirm sends the selected row key', async ({ page }) => {
    await openPage(page, makeState());
    // aria-disabled: force the click to prove it is ignored.
    await page.getByTestId('filament-select-row-r4').click({ force: true });
    await expect(page.locator('.fs-row--selected')).toHaveCount(0);

    await page.getByTestId('filament-select-row-r1').click();
    await expect(page.getByTestId('filament-select-row-r1')).toHaveClass(/fs-row--selected/);
    await page.getByTestId('filament-select-row-r2').click();
    await expect(page.locator('.fs-row--selected')).toHaveCount(1);

    await page.getByTestId('filament-select-confirm').click();
    await expect.poll(() => actions(page)).toEqual([
      { module: MODULE, submod: 'action', action: 'confirm', payload: { tab: 'manager', row_key: 'r2' } },
    ]);
  });

  test('confirm without a selection is still sent for C++ to reject', async ({ page }) => {
    await openPage(page, makeState());
    await page.getByTestId('filament-select-confirm').click();
    await expect.poll(() => actions(page)).toEqual([
      { module: MODULE, submod: 'action', action: 'confirm', payload: { tab: 'manager', row_key: '' } },
    ]);
  });

  test('system presets: preselection, brand switch, confirm alias', async ({ page }) => {
    await openPage(page, makeState());
    await page.getByTestId('filament-select-tab-presets').click();
    await expect(page.getByTestId('filament-select-tab-presets')).toHaveClass(/fs-tab--active/);
    await expect(page.getByTestId('filament-select-brand-brand:Generic')).toHaveClass(/fs-brand-row--selected/);
    await expect(page.getByTestId('filament-select-brand-other')).toHaveText('Other');
    await expect(page.locator('.fs-alias-row--checked')).toHaveText('Generic PETG');

    await page.getByTestId('filament-select-brand-brand:Bambu Lab').click();
    await expect(page.getByTestId('filament-select-alias')).toHaveText(['Bambu PLA Basic', 'Bambu PETG HF']);
    await expect(page.locator('.fs-alias-row--checked')).toHaveCount(0);

    await page.getByTestId('filament-select-alias').filter({ hasText: 'Bambu PETG HF' }).click();
    await page.getByTestId('filament-select-confirm').click();
    await expect.poll(() => actions(page)).toEqual([
      { module: MODULE, submod: 'action', action: 'confirm', payload: { tab: 'presets', alias: 'Bambu PETG HF' } },
    ]);
  });

  test('Close button and Escape send close', async ({ page }) => {
    await openPage(page, makeState());
    await page.getByTestId('filament-select-close').click();
    await page.keyboard.press('Escape');
    await expect.poll(async () => (await actions(page)).map((a) => a.action)).toEqual(['close', 'close']);
  });

  test('empty store shows "No filaments"', async ({ page }) => {
    const state = makeState();
    state.manager.rows = [];
    await openPage(page, state);
    await expect(page.getByTestId('filament-select-empty')).toHaveText('No filaments');
  });

  test('theme report switches theme without resetting the selection', async ({ page }) => {
    const state = makeState();
    await openPage(page, state);
    await page.getByTestId('filament-select-row-r1').click();
    await page.evaluate((s) => (window as unknown as { __fsReport: (p: unknown) => void }).__fsReport(s),
      { ...state, theme: 'dark' });
    await expect(page.getByTestId('filament-select-page')).toHaveAttribute('data-theme', 'dark');
    await expect(page.getByTestId('filament-select-row-r1')).toHaveClass(/fs-row--selected/);
  });
});
