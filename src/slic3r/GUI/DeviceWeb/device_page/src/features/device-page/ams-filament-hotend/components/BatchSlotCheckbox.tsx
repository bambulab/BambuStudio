// Card-head checkbox used in batch_* page modes. Replaces the right-click /
// long-press menu surface on each slot card. The eligibility is fully
// authoritative on the C++ side (slot.select_action.can_select) — Web does
// not re-derive it; it just renders + bubbles the toggle event up.

export function BatchSlotCheckbox({
  checked,
  canSelect,
  reason,
  displayIndex,
  onToggle,
}: {
  checked: boolean;
  canSelect: boolean;
  reason?: string;
  displayIndex: number;
  onToggle: () => void;
}) {
  return (
    <button
      type="button"
      role="checkbox"
      aria-checked={checked}
      aria-label={canSelect ? `Select slot ${displayIndex}` : reason || 'Not selectable'}
      title={!canSelect ? reason : undefined}
      data-testid={`ams-hotend-slot-checkbox-${displayIndex}`}
      className={`batch-slot-checkbox${checked ? ' checked' : ''}${!canSelect ? ' disabled' : ''}`}
      disabled={!canSelect}
      onClick={(event) => {
        event.stopPropagation();
        if (!canSelect) return;
        onToggle();
      }}
    >
      {checked && (
        <svg width={12} height={12} viewBox="0 0 12 12" aria-hidden="true">
          <path
            d="M2.5 6.2 5 8.7 9.5 3.6"
            fill="none"
            stroke="currentColor"
            strokeWidth={1.6}
            strokeLinecap="round"
            strokeLinejoin="round"
          />
        </svg>
      )}
    </button>
  );
}
