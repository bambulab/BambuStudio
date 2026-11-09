interface TipsProps {
  text: string;
}

// Top-of-popup tips/info bar. Currently used for nozzle-mismatch hints.
export function Tips({ text }: TipsProps) {
  if (!text) return null;
  return (
    <div className="ams-map-hotend__tips">
      <span>{text}</span>
    </div>
  );
}
