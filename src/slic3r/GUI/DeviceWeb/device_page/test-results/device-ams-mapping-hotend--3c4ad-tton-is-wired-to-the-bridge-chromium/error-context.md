# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: device-ams-mapping-hotend.spec.ts >> AMS mapping hotend popup >> reset button is wired to the bridge
- Location: tests\e2e\device-ams-mapping-hotend.spec.ts:47:3

# Error details

```
Error: expect(locator).toBeVisible() failed

Locator: getByTestId('ams-mapping-hotend-reset')
Expected: visible
Timeout: 5000ms
Error: element(s) not found

Call log:
  - Expect "toBeVisible" with timeout 5000ms
  - waiting for getByTestId('ams-mapping-hotend-reset')

```

```yaml
- button "2 PETG Edit filament 0.4 mm Standard":
  - text: "2"
  - img
  - img
  - text: PETG
  - button "Edit filament":
    - img
  - text: 0.4 mm Standard
- button "4 PLA 0.4 mm Standard":
  - text: "4"
  - img
  - text: PLA 0.4 mm Standard
- button "6 PLA 0.4 mm Standard":
  - text: "6"
  - img
  - text: PLA 0.4 mm Standard
- button "8 PLA Pen":
  - text: "8"
  - img
  - text: PLA Pen
- button "1 PLA 0.4 mm Standard":
  - text: "1"
  - img
  - text: PLA 0.4 mm Standard
- button "3 Empty 0.4 mm Standard":
  - text: "3"
  - img: /
  - text: Empty 0.4 mm Standard
- button "5 PLA 0.4 mm Standard":
  - text: "5"
  - img
  - text: PLA 0.4 mm Standard
- button "7 PLA Cutter":
  - text: "7"
  - img
  - text: PLA Cutter
- button "9 Ext PLA Edit filament 0.4 mm Standard":
  - text: 9 Ext
  - img
  - text: PLA
  - button "Edit filament":
    - img
  - text: 0.4 mm Standard
```

# Test source

```ts
  1  | import { expect, test } from '@playwright/test';
  2  | 
  3  | test.describe('AMS mapping hotend popup', () => {
  4  |   test.beforeEach(async ({ page }) => {
  5  |     await page.goto('/#/device_page/ams_mapping_hotend');
  6  |     await expect(page.getByTestId('ams-mapping-hotend-page')).toBeVisible();
  7  |   });
  8  | 
  9  |   test('renders the 5-slot hotend grid from the mock bridge', async ({ page }) => {
  10 |     // First slot is selectable + currently checked in mockData.
  11 |     const first = page.getByTestId('ams-mapping-hotend-slot-128-0');
  12 |     await expect(first).toBeVisible();
  13 |     await expect(first).toHaveAttribute('data-state', 'Normal');
  14 | 
  15 |     // Second slot is MaterialUnmatch — visible but visually dimmed
  16 |     // (PETG filament loaded while slicing requires PLA).
  17 |     await expect(page.getByTestId('ams-mapping-hotend-slot-128-1'))
  18 |       .toHaveAttribute('data-state', 'MaterialUnmatch');
  19 | 
  20 |     // Third slot is MaterialEmpty (rack nozzle present, no filament loaded;
  21 |     // still rendered, not selectable in non-all-ext mode).
  22 |     await expect(page.getByTestId('ams-mapping-hotend-slot-128-2'))
  23 |       .toHaveAttribute('data-state', 'MaterialEmpty');
  24 | 
  25 |     // External spool (ext=true) renders as the last card.
  26 |     await expect(page.getByTestId('ams-mapping-hotend-slot-254-0'))
  27 |       .toBeVisible();
  28 |   });
  29 | 
  30 |   test('cutter / pen tools render their own icon and are non-pickable', async ({ page }) => {
  31 |     // Cutter-equipped rack position: forced into NozzleUnmatch by the backend,
  32 |     // shows the "Cutter" label instead of dia / flow.
  33 |     const cutter = page.getByTestId('ams-mapping-hotend-slot-128-6');
  34 |     await expect(cutter).toBeVisible();
  35 |     await expect(cutter).toHaveAttribute('data-state', 'NozzleUnmatch');
  36 |     await expect(cutter.locator('.ams-map-hotend__nozzle-info__tool')).toHaveText('Cutter');
  37 |     await expect(cutter.locator('.ams-map-hotend__nozzle-info__icon--tool')).toHaveCount(1);
  38 | 
  39 |     // Pen / marker tool: same non-pickable treatment, "Pen" label.
  40 |     const pen = page.getByTestId('ams-mapping-hotend-slot-128-7');
  41 |     await expect(pen).toBeVisible();
  42 |     await expect(pen).toHaveAttribute('data-state', 'NozzleUnmatch');
  43 |     await expect(pen.locator('.ams-map-hotend__nozzle-info__tool')).toHaveText('Pen');
  44 |     await expect(pen.locator('.ams-map-hotend__nozzle-info__icon--tool')).toHaveCount(1);
  45 |   });
  46 | 
  47 |   test('reset button is wired to the bridge', async ({ page }) => {
> 48 |     await expect(page.getByTestId('ams-mapping-hotend-reset')).toBeVisible();
     |                                                                ^ Error: expect(locator).toBeVisible() failed
  49 |     await page.getByTestId('ams-mapping-hotend-reset').click();
  50 |     // No throw / no error tip surfaced.
  51 |     await expect(page.locator('.ams-map-hotend__tips')).toHaveCount(0);
  52 |   });
  53 | });
  54 | 
```