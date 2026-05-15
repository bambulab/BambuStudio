// store.ts
import { create } from 'zustand';
import { immer } from 'zustand/middleware/immer';
import { devtools } from 'zustand/middleware';

import { createAppSlice } from './AppSlice';
import { createDeviceSlice } from './DeviceSlice';
import { createAmsSlice } from './AmsSlice';
import { createSlotSlice } from './SlotSlice';
import { createFilamentSlice } from './FilamentSlice';

import type { SlotSlice } from './SlotSlice';
import type { AmsSlice } from './AmsSlice';
import type { DeviceSlice } from './DeviceSlice';
import type { AppSlice } from './AppSlice';
import type { FilamentSlice } from './FilamentSlice';

export type RootState = AppSlice & DeviceSlice & AmsSlice & SlotSlice & FilamentSlice;

export const useStore = create<RootState>()(
  devtools(
    immer<RootState>((set, get, api) => ({
      ...createAppSlice(set, get, api),
      ...createDeviceSlice(set, get, api),
      ...createAmsSlice(set, get, api),
      ...createSlotSlice(set, get, api),
      ...createFilamentSlice(set, get, api),
    })),
    { name: 'app-store' }
  )
);

export default useStore;

// STUDIO-17977 dev/test hook: expose the store in non-production builds
// so the standalone Playwright probe can drive setColorCandidates() to
// reproduce the late-empty-response race against the live React tree.
// This branch is dropped from production bundles by Vite's dead-code
// elimination on `import.meta.env.PROD`.
if (typeof window !== 'undefined' && !import.meta.env.PROD) {
  (window as unknown as { __filamentStore?: unknown }).__filamentStore = useStore;
}

// route: module-id/device-id/ams-id
//  className="ml-[0.5rem] grid h-[2.25rem] w-[2.25rem] place-items-center rounded-[0.75rem] border border-black/10 bg-white text-black/70 shadow-[0_0.25rem_1rem_rgba(0,0,0,0.06)] transition-all duration-200 hover:bg-black/5 hover:shadow-[0_0.5rem_1.25rem_rgba(0,0,0,0.08)] active:translate-y-[0.0625rem] active:shadow-[0_0.125rem_0.5rem_rgba(0,0,0,0.05)]"
