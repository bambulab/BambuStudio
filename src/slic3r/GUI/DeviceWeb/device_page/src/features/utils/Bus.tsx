import { useRef, useEffect, useState } from "react";
import Bar from "./Bar.tsx";

const colors = ["#ff7e7e", "#b4ffbe", "#7e1919", "#6666ff"];
type Point = { x: number; y: number };
const isPoint = (point: Point | null): point is Point => point !== null;

const ConnectorExample = () => {
  // 创建 ref 数组
  const barRefs = useRef<(HTMLDivElement | null)[]>([]);
  const containerRef = useRef<HTMLDivElement | null>(null);
  const [barPoints, setBarPoints] = useState<Point[]>([]);

  useEffect(() => {
    // 获取每个 bar 的底部中点
    const points = barRefs.current.map((ref) => {
      if (ref && containerRef.current) {
        const rect = ref.getBoundingClientRect();
        const parentRect = containerRef.current.getBoundingClientRect();
        return {
          x: rect.left + rect.width / 2 - parentRect.left,
          y: rect.top + rect.height - parentRect.top,
        };
      }
      return null;
    });
    setBarPoints(points.filter(isPoint));
  }, [colors.length]);

  // 底部横条参数
  const barBottom = 120;
  const barWidth = 56;
  const barHeight = 18;
  const containerWidth = 420;
  const containerHeight = 140;

  return (
    <div
      ref={containerRef}
      className="relative bg-gray-100"
      style={{
        width: containerWidth,
        height: containerHeight,
        borderRadius: 12,
        overflow: "visible",
      }}
    >
      {/* 子组件 */}
      <div className="flex flex-row justify-center items-start pt-6 pb-16">
        {colors.map((color, idx) => (
          <Bar
            color={color}
            key={idx}
            ref={(el) => { barRefs.current[idx] = el; }}
          />
        ))}
      </div>

      {/* SVG 画线 */}
      <svg
        className="absolute left-0 top-0 pointer-events-none"
        width={containerWidth}
        height={containerHeight}
      >
        {barPoints.map((p, idx) => (
          <line
            key={idx}
            x1={p.x}
            y1={p.y}
            x2={containerWidth / 2}
            y2={barBottom}
            stroke="#bbb"
            strokeWidth="2"
          />
        ))}
        {/* 底部横条 */}
        <rect
          x={containerWidth / 2 - barWidth / 2}
          y={barBottom}
          width={barWidth}
          height={barHeight}
          fill="#bbb"
          rx={4}
        />
      </svg>
    </div>
  );
};

export default ConnectorExample;
