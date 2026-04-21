import type { StateCreator } from 'zustand';
import type { RootState } from './AppStore';

export interface SlotState {
  id: string;
  value: number;
  status?: string;
}
export interface SlotActions {
  setValue: (id: string, value: number) => void;
  setStatus: (id: string, status: string) => void;
}
export interface SlotSlice {
  slot: SlotState & SlotActions;
}

export const createSlotSlice: StateCreator<
  RootState,
  [['zustand/immer', never]],
  [],
  SlotSlice
> = (set) => ({
  slot: {
    id: '',
    value: 0,
    status: '',
    setValue: (id, value) =>
      set((s) => {
        s.slot.id = id;
        s.slot.value = value;
      }),
    setStatus: (id, status) =>
      set((s) => {
        s.slot.id = id;
        s.slot.status = status;
      }),
  },
});
