import type { FilamentLoadState } from './types';

export const mockFilamentLoadState: FilamentLoadState = {
  display: {
    visible: true,
    expanded: true,
    title: 'Filament loading...',
    image_name: 'filament_load_x_series_ext0.png',
    steps: {
      kind: 'load',
      slot_text: 'Ams-1-1 Slot',
      items: [
        { text: 'Heat the nozzle', status: 'done' },
        { text: 'Cut filament', status: 'current' },
        { text: 'Pull back current filament', status: 'pending' },
        { text: 'Push new filament into extruder', status: 'pending' },
        { text: 'Purge old filament', status: 'pending' },
      ],
    },
    actions: {
      retry: { visible: true, enabled: true, text: 'Retry', disabled_reason: '' },
      stop: { visible: true, enabled: true, text: 'Stop', disabled_reason: '' },
    },
  },
};
