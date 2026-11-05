import { normalizeState } from '../src/features/device-page/control-panel/normalize.ts';
import type { ControlPanelState } from '../src/features/device-page/control-panel/types.ts';

function assertEqual(actual: unknown, expected: unknown) {
  if (actual !== expected) {
    throw new Error(`Expected ${String(expected)}, got ${String(actual)}`);
  }
}

assertEqual(
  normalizeState({
    display: {
      extruder: {
        type: 'single_rack',
        single_rack_extruder: {
          rack_toolhead_visual_state: 'picking',
        },
      },
    },
  } as unknown as Partial<ControlPanelState>).display.extruder.single_rack_extruder.rack_toolhead_visual_state,
  'picking',
);

assertEqual(
  normalizeState({
    display: {
      extruder: {
        type: 'single_rack',
        single_rack_extruder: {
          rack_toolhead_visual_state: 'cutter',
        },
      },
    },
  } as unknown as Partial<ControlPanelState>).display.extruder.single_rack_extruder.rack_toolhead_visual_state,
  'cutter',
);

assertEqual(
  normalizeState({
    display: {
      extruder: {
        type: 'single_rack',
        single_rack_extruder: {
          rack_toolhead_visual_state: 'unknown',
        },
      },
    },
  } as unknown as Partial<ControlPanelState>).display.extruder.single_rack_extruder.rack_toolhead_visual_state,
  'empty',
);

console.log('controlPanelNormalize tests passed');
