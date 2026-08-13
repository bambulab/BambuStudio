import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const hotendPageSource = readFileSync(new URL('../../ViewModels/DevicePage/AmsFilamentHotend/PageHotend.cpp', import.meta.url), 'utf8');
const hotendDetailSource = readFileSync(new URL('../src/features/device-page/ams-filament-hotend/hotend-detail/HotendDetailPage.tsx', import.meta.url), 'utf8');
const typesSource = readFileSync(new URL('../src/features/device-page/ams-filament-hotend/types.ts', import.meta.url), 'utf8');

assert.match(
  typesSource,
  /used_time_hours\?:\s*string\s*\|\s*null;/,
  'Hotend detail used_time_hours should be a display text string or null',
);

assert.match(
  hotendPageSource,
  /format_nozzle_used_time\(int\s+print_time_seconds\)/,
  'Hotend ViewModel should format nozzle print time from seconds before sending it to Web',
);

assert.match(
  hotendPageSource,
  /safe_seconds\s*\/\s*60\)\s*\+\s*" min"/,
  'Hotend ViewModel should show exact minutes for print times below one hour',
);

assert.match(
  hotendPageSource,
  /std::fixed\s*<<\s*std::setprecision\(1\)\s*<<\s*hours/,
  'Hotend ViewModel should format one decimal hour text for print times of at least one hour',
);

assert.match(
  hotendDetailSource,
  /function\s+formatUsedTime\(usedTimeText\?:\s*string\s*\|\s*null\)/,
  'Hotend detail page should treat used_time_hours as preformatted text',
);

console.log('hotendUsedTimeSource tests passed');
