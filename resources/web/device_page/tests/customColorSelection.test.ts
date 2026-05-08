import assert from 'node:assert/strict';
import {
  commitCustomColorSelection,
  isPresetColor,
  normalizeColorCode,
} from '../src/features/filament/customColorSelection.ts';

const presetColors = ['#000000', '#FFFFFF'];

assert.equal(normalizeColorCode('abc123'), '#ABC123');
assert.equal(isPresetColor('#ffffff', presetColors), true);

const existingColors = ['#123456'];
assert.deepEqual(existingColors, ['#123456']);

const committed = commitCustomColorSelection('#abcdef', existingColors, presetColors);

assert.equal(committed.colorCode, '#ABCDEF');
assert.deepEqual(committed.customColors, ['#123456', '#ABCDEF']);
assert.deepEqual(existingColors, ['#123456'], 'commit must not mutate previous custom colors');

const duplicate = commitCustomColorSelection('#abcdef', committed.customColors, presetColors);

assert.deepEqual(duplicate.customColors, ['#123456', '#ABCDEF']);

const preset = commitCustomColorSelection('#ffffff', duplicate.customColors, presetColors);

assert.equal(preset.colorCode, '#FFFFFF');
assert.deepEqual(preset.customColors, ['#123456', '#ABCDEF']);
