import type { ConnectedSlotsViewModel } from './types';
import mockJson from './mock.json';

// Schema sample: src/slic3r/GUI/DeviceWeb/device_page/src/features/device-page/connected-slots/mock.json
// Mirrors ConnectedSlotsSchema::format::State.
export const mockConnectedSlotsState = mockJson as ConnectedSlotsViewModel;
