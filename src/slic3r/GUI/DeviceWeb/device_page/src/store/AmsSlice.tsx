// amsSlice.ts
import type { StateCreator } from 'zustand';
import type { SlotState } from './SlotSlice';
import type { RootState } from './AppStore';

export interface AmsState {
    id: string;
    value: number;
    SlotList: SlotState[];
}
export interface AmsActions {
    setValue: (id: string, value: number) => void;
    addSlot: (id: string) => void;
    removeSlot: (id: string) => void;
}
export interface AmsSlice {
    ams: AmsState & AmsActions;
}

export const createAmsSlice: StateCreator<
    RootState,
    [['zustand/immer', never]],
    [],
    AmsSlice
> = (set) => ({
    ams: {
        id: '',
        value: 0,
        SlotList: [],
        setValue: (id, value) =>
            set((state) => {
                state.ams.id = id;
                state.ams.value = value;
            }),
        addSlot: (id) =>
            set((state) => {
                state.ams.SlotList.push({ id, value: 0 });
            }),
        removeSlot: (id) =>
            set((state) => {
                state.ams.SlotList = state.ams.SlotList.filter((slot) => slot.id !== id);
            }),
    },
});
