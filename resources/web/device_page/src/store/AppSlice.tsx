// appSlice.ts
import type { StateCreator } from 'zustand';
import type { DeviceState } from './DeviceSlice';
import type { RootState } from './AppStore'; // only import type, avoid circular dependency

export interface AppState {
  devices: DeviceState[];
}
export interface AppActions {
  addDevice: (id: string) => void;
  removeDevice: (id: string) => void;
}

export type AppSlice = AppState & AppActions;


export const createAppSlice: StateCreator<
  RootState,
  [['zustand/immer', never]],
  [],
  AppSlice
> = (set) => ({
  devices: [],
  addDevice: (id) =>
    set((state) => {
      state.devices.push({ id, value: 0, AmsList: [] });
    }),
  removeDevice: (id) =>
    set((state) => {
      state.devices = state.devices.filter((d) => d.id !== id);
    }),
});
