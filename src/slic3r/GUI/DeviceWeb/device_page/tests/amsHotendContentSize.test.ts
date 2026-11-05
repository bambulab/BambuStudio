import assert from 'node:assert/strict';
import { resolveHostContentSize } from '../src/features/device-page/ams-filament-hotend/hostContentSize.ts';

assert.deepEqual(
  resolveHostContentSize({ scrollWidth: 545, scrollHeight: 260, offsetHeight: 260 }),
  { width: 560, height: 440 },
);

assert.deepEqual(
  resolveHostContentSize({ scrollWidth: 545, scrollHeight: 520, offsetHeight: 520 }),
  { width: 560, height: 520 },
);

assert.deepEqual(
  resolveHostContentSize({ scrollWidth: 620, scrollHeight: 439, offsetHeight: 439 }),
  { width: 620, height: 440 },
);

console.log('amsHotendContentSize tests passed');
