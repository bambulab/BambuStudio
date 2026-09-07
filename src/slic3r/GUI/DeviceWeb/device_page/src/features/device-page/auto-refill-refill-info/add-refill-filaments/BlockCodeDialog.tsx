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

  const title = interaction.title ?? interaction.tooltip ?? '';
  // Design row 13 substitutes the hotend number into both strings.
  const fill = (text: string) =>
    interaction.details === 'hotend_index'
      ? text.replace('%1', String(slot.display_index))
      : text;

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
        aria-label={t(title)}
        data-testid="add-refill-block-dialog"
        data-block-code={interaction.block_code}
      >
        <div className="refill-block-dialog__body">
          {title && <div className="refill-block-dialog__title">{t(fill(title))}</div>}
          {details.map((detail) => (
            <div className="refill-block-dialog__detail" key={detail.label}>
              <span>{t(detail.label)}</span>
              <span>{detail.value}</span>
            </div>
          ))}
          {interaction.message && (
            <div className="refill-block-dialog__message">{t(fill(interaction.message))}</div>
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
          <button type="button" className="add-refill-page__button" onClick={onCancel}>
            {t('Cancel')}
          </button>
          {interaction.confirm_label && (
            <button
              type="button"
              className="add-refill-page__button add-refill-page__button--primary"
              onClick={() => onConfirm(dontShowAgain)}
            >
              {t(interaction.confirm_label)}
            </button>
          )}
        </footer>
      </section>
    </div>
  );
}
