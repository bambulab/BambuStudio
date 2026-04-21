import { forwardRef } from "react";

const Bar = forwardRef(({ color }, ref) => (
  <div
    ref={ref}
    className="w-[1.25rem] h-[0.375rem] rounded bg-opacity-70"
    style={{ backgroundColor: color, margin: "0 24px" }}
  />
));

export default Bar;