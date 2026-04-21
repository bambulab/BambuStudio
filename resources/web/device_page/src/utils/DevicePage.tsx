import { useEffect } from "react";
import { useDeviceStore } from "./DeviceStore";

/**
 * 一个示例页面：
 * - 首次挂载时注入一台演示设备
 * - 展示所有设备、零件状态
 * - 点击按钮切换零件状态（ok / error）
 * - 下方滚动框显示最近日志
 */
export default function DevicePage() {
  const devices = useDeviceStore((s) => s.devices);
  const logs = useDeviceStore((s) => s.logs);
  const addDevice = useDeviceStore((s) => s.addDevice);
  const togglePartStatus = useDeviceStore((s) => s.togglePartStatus);

  /* ---------- 填充示例数据，仅在首轮渲染时执行 ---------- */
  useEffect(() => {
    if (Object.keys(devices).length === 0) {
      addDevice({
        id: "printer-1",
        name: "3D Printer #1",
        parts: {
          extruder: { id: "extruder", name: "Extruder", status: "ok" },
          bed: { id: "bed", name: "Heated Bed", status: "ok" },
        },
      });
    }
  }, [addDevice, devices]);

  /* ---------- UI ---------- */
  return (
    <div className="p-8 space-y-8">
      <h1 className="text-3xl font-bold">Device Dashboard</h1>

      {/* 设备 / 零件列表 */}
      {Object.values(devices).map((dev) => (
        <section
          key={dev.id}
          className="border rounded-lg p-4 shadow-sm space-y-4"
        >
          <h2 className="text-xl font-semibold">{dev.name}</h2>

          <ul className="space-y-2">
            {Object.values(dev.parts).map((part) => (
              <li
                key={part.id}
                className="flex items-center justify-between border-b pb-1"
              >
                <span>{part.name}</span>

                <span
                  className={
                    part.status === "ok" ? "text-green-600" : "text-red-600"
                  }
                >
                  {part.status}
                </span>

                <button
                  onClick={() => togglePartStatus(dev.id, part.id)}
                  className="ml-2 text-sm px-3 py-1 rounded bg-gray-200 hover:bg-gray-300"
                >
                  Toggle
                </button>
              </li>
            ))}
          </ul>
        </section>
      ))}

      {/* 日志面板 */}
      <section>
        <h2 className="text-xl font-semibold mb-2">Recent Logs</h2>
        <ul className="border rounded-lg max-h-48 overflow-y-auto text-sm p-3 space-y-1">
          {logs
            .slice()         // 拷贝一份再 reverse，避免直接改 store
            .reverse()
            .map((log, i) => (
              <li key={i}>
                {new Date(log.ts).toLocaleTimeString()} — {log.msg}
              </li>
            ))}
        </ul>
      </section>
    </div>
  );
}
