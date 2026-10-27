import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import type {
  AutoRefillAddRefillFilamentsSlot,
  RefillBlockInteraction,
  RefillGroupReference,
} from './types';

// Dialog of one `block` code. All copy comes from C++
// (BlockCodeUtil::Create), so the wording of a code changes there and not
// here; this component only decides which live values fill its detail lines.
export function BlockCodeDialog({
  interaction,
  slot,
  groupReference,
  onCancel,
  onConfirm,
}: {
  interaction: RefillBlockInteraction;
  slot: AutoRefillAddRefillFilamentsSlot;
  groupReference: RefillGroupReference;
  onCancel: () => void;
  // `dontShowAgain` is only ever true for a code that allows it.
  onConfirm: (dontShowAgain: boolean) => void;
}) {
  const { t } = useTranslation();
  const [dontShowAgain, setDontShowAgain] = useState(false);
  const confirmLabel = interaction.confirm_label;
  const hasConfirmAction = Boolean(confirmLabel);

  const title = interaction.title ?? interaction.tooltip ?? '';
  // Design row 13 stores the English source text as the i18n key, so the
  // hotend index has to be filled after translation instead of before lookup.
  const fill = (text: string) =>
    interaction.details === 'hotend_index'
      ? text.replace('%1', String(slot.display_index))
      : text;
  const translate = (text: string) => fill(t(text));

  const currentFilament = slot.filament_name || slot.filament_type;
  const currentNozzle = [slot.nozzle.label, slot.nozzle.subtext].filter(Boolean).join(' ');

  const details: { label: string; value: string }[] = [];
  if (interaction.details === 'filament_comparison') {
    details.push({ label: 'Refill group filament:', value: groupReference.filament_name || groupReference.filament_type });
    details.push({ label: 'Current filament:', value: currentFilament });
  } else if (interaction.details === 'hotend_comparison') {
    details.push({ label: 'Refill group hotend:', value: groupReference.nozzle });
    details.push({ label: 'Current hotend:', value: currentNozzle });
  }

  return (
    <div className="refill-block-dialog__scrim" role="presentation">
      <section
        className="refill-block-dialog"
        role="dialog"
        aria-modal="true"
        aria-label={translate(title)}
        data-testid="add-refill-block-dialog"
        data-block-code={interaction.block_code}
      >
        <div className="refill-block-dialog__body">
          {title && <div className="refill-block-dialog__title">{translate(title)}</div>}
          {details.map((detail) => (
            <div className="refill-block-dialog__detail" key={detail.label}>
              <span>{t(detail.label)}</span>
              <span>{detail.value}</span>
            </div>
          ))}
          {interaction.message && (
            <div className="refill-block-dialog__message">{translate(interaction.message)}</div>
          )}
          {interaction.allow_dont_show_again && (
            <label className="refill-block-dialog__dsa">
              <input
                type="checkbox"
                checked={dontShowAgain}
                onChange={(event) => setDontShowAgain(event.target.checked)}
              />
              <span>{t("Don't show again")}</span>
            </label>
          )}
        </div>

        <footer className="refill-block-dialog__footer">
          <button
            type="button"
            className={`add-refill-page__button${hasConfirmAction ? '' : ' add-refill-page__button--primary'}`}
            onClick={onCancel}
          >
            {t(hasConfirmAction ? 'Cancel' : 'OK')}
          </button>
          {hasConfirmAction && (
            <button
              type="button"
              className="add-refill-page__button add-refill-page__button--primary"
              onClick={() => onConfirm(dontShowAgain)}
            >
              {t(confirmLabel!)}
            </button>
          )}
        </footer>
      </section>
    </div>
  );
}
