import { useRef, useEffect, useState } from "react";

export default function SvgCanvasDemo() {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const [svgColor, setSvgColor] = useState("#4f8cff");
  const [progress, setProgress] = useState(0);

  // Canvas 动画：画一个进度圆圈
  useEffect(() => {
    let running = true;
    let start = Date.now();
    const draw = () => {
      if (!canvasRef.current) return;
      const ctx = canvasRef.current.getContext("2d");
      if (!ctx) return;
      const size = 100;
      ctx.clearRect(0, 0, size, size);

      // 背景环
      ctx.beginPath();
      ctx.arc(size / 2, size / 2, 40, 0, 2 * Math.PI);
      ctx.strokeStyle = "#eee";
      ctx.lineWidth = 10;
      ctx.stroke();

      // 动态进度
      const now = Date.now();
      const percent = ((now - start) % 2000) / 2000;
      setProgress(percent);

      ctx.beginPath();
      ctx.arc(
        size / 2,
        size / 2,
        40,
        -Math.PI / 2,
        -Math.PI / 2 + 2 * Math.PI * percent
      );
      ctx.strokeStyle = "#ff7f50";
      ctx.lineWidth = 10;
      ctx.stroke();

      if (running) requestAnimationFrame(draw);
    };
    draw();
    return () => {
      running = false;
    };
  }, []);

  // SVG 圆环点击更换颜色
  function handleSvgClick() {
    setSvgColor(svgColor === "#4f8cff" ? "#52c41a" : "#4f8cff");
  }

  return (
    <div style={{ display: "flex", gap: 40, alignItems: "center", margin: 32 }}>
      {/* SVG 示例 */}
      <div>
        <h3>SVG 彩色圆环</h3>
        <svg
          width={100}
          height={100}
          style={{ cursor: "pointer" }}
          onClick={handleSvgClick}
        >
          {/* 背景圆环 */}
          <circle
            cx={50}
            cy={50}
            r={40}
            fill="none"
            stroke="#eee"
            strokeWidth={10}
          />
          {/* 彩色进度环 */}
          <circle
            cx={50}
            cy={50}
            r={40}
            fill="none"
            stroke={svgColor}
            strokeWidth={10}
            strokeDasharray={2 * Math.PI * 40}
            strokeDashoffset={2 * Math.PI * 40 * (1 - progress)}
            strokeLinecap="round"
            style={{ transition: "stroke 0.3s" }}
          />
          <text
            x={50}
            y={55}
            textAnchor="middle"
            fontSize={20}
            fill="#222"
          >{`${Math.round(progress * 100)}%`}</text>
        </svg>
        <div style={{ fontSize: 14, color: "#888", marginTop: 8 }}>
          点击圆环切换颜色
        </div>
      </div>
      {/* Canvas 示例 */}
      <div>
        <h3>Canvas 动画圆环</h3>
        <canvas ref={canvasRef} width={100} height={100} style={{ display: "block" }} />
        <div style={{ fontSize: 14, color: "#888", marginTop: 8 }}>
          动态进度动画
        </div>
      </div>
    </div>
  );
}
