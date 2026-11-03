import resetIcon from '../assets/reset.svg';

interface ResetButtonProps {
  onClick: () => void;
  label?: string;
}

// Top-right reset button. Wires through action/reset to invoke the
// MaterialItem reset callback on the C++ side.
export function ResetButton({ onClick, label = 'Reset' }: ResetButtonProps) {
  return (
    <button
      type="button"
      className="ams-map-hotend__reset-btn"
      data-testid="ams-mapping-hotend-reset"
      onClick={onClick}
    >
      <img className="ams-map-hotend__reset-btn__icon" src={resetIcon} alt="" />
      <span>{label}</span>
    </button>
  );
}
