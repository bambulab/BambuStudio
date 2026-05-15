// deviceSlice.ts
import type { StateCreator } from 'zustand';
import type { AmsState } from './AmsSlice';
import type { RootState } from './AppStore';

export interface DeviceState {
  id: string;
  value: number;
  AmsList: AmsState[];
}
export interface DeviceActions {
  addAms: (id: string) => void;
  removeAms: (id: string) => void;
}
export interface DeviceSlice {
  device: DeviceState & DeviceActions;
}

export const createDeviceSlice: StateCreator<
  RootState,
  [['zustand/immer', never]],
  [],
  DeviceSlice
> = (set) => ({
  device: {
    id: '',
    value: 0,
    AmsList: [],
    addAms: (id) =>
      set((state) => {
        state.device.AmsList.push({ id, value: 0, SlotList: [] });
      }),
    removeAms: (id) =>
      set((state) => {
        state.device.AmsList = state.device.AmsList.filter((ams) => ams.id !== id);
      }),
  },
});
