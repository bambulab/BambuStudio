import React from "react";
import type { CSSProperties, ReactNode } from "react";

interface ScrollViewProps {
  children: ReactNode;
  direction?: "vertical" | "horizontal";
  height?: number | string;
  width?: number | string;
  className?: string;
  style?: CSSProperties;
  smooth?: boolean;
}

const ScrollView: React.FC<ScrollViewProps> = ({
  children,
  direction = "vertical",
  height = "100%",
  width = "100%",
  className = "",
  style = {},
  smooth = false,
}) => {
  const scrollStyles: CSSProperties = {
    height,
    width,
    overflowY: direction === "vertical" ? "auto" : "hidden",
    overflowX: direction === "horizontal" ? "auto" : "hidden",
    scrollBehavior: smooth ? "smooth" : "auto",
    ...style,
  };

  const tailwindClass = `
    ${direction === "vertical" ? "overflow-y-auto" : "overflow-x-auto whitespace-nowrap"}
    ${smooth ? "scroll-smooth" : ""}
    ${className}
  `;

  return (
    <div style={scrollStyles} className={tailwindClass}>
      {children}
    </div>
  );
};

export default ScrollView;
