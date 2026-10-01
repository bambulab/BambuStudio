import { create } from "zustand";
import { immer } from "zustand/middleware/immer";

/* ----------- 类型定义 ----------- */
export type PartStatus = "ok" | "error";

export interface Part {
  id: string;
  name: string;
  status: PartStatus;
}

export interface Device {
  id: string;
  name: string;
  parts: Record<string, Part>;          //每台设备内部再用字典保存零件
}

export interface LogEntry {
  ts: number;                           // 时间戳
  deviceId: string;
  msg: string;
}

/* ----------- Zustand Store ----------- */
interface DeviceState {
  devices: Record<string, Device>;
  logs: LogEntry[];
  count: number;
  /* actions */
  addDevice: (d: Device) => void;
  togglePartStatus: (deviceId: string, partId: string) => void;
  addLog: (entry: LogEntry) => void;
  setCount: (count: number) => void;
}

export const useDeviceStore = create<DeviceState>()(
  immer((set) => ({
    devices: {},
    logs: [],
    count: 0,
    addDevice: (d) =>
      set((draft) => {
        draft.devices[d.id] = d;
      }),

    togglePartStatus: (deviceId, partId) =>
      set((draft) => {
        const part = draft.devices[deviceId]?.parts?.[partId];
        if (!part) return;
        part.status = part.status === "ok" ? "error" : "ok";
        draft.logs.push({
          ts: Date.now(),
          deviceId,
          msg: `${part.name} status toggled to ${part.status}`,
        });
        /* 只保留最近 100 条日志 */
        if (draft.logs.length > 100) draft.logs = draft.logs.slice(-100);
      }),

    addLog: (entry) =>
      set((draft) => {
        draft.logs.push(entry);
        if (draft.logs.length > 100) draft.logs = draft.logs.slice(-100);
      }),

    setCount: (count: number) =>
      set((draft) => {
        draft.count = count;
      }),
  })
)
);
