import { useTranslation } from 'react-i18next';
import type { AmsFilamentHotendSlotViewModel } from '../types';

export function IndexPill({ slot }: { slot: AmsFilamentHotendSlotViewModel }) {
  const { t } = useTranslation();
  const lineState = slot.filament_line.state;
  const isTransporting = lineState === 'loading' || lineState === 'unloading';
  const cls = ['card-index'];
  // `selected` covers both the user's manual UI selection and the firmware-
  // reported transport states so the pill matches the green-highlight in
  // Figma 1498:84068 while filament is in motion.
  if (slot.selected || isTransporting) cls.push('selected');
  if (slot.is_running) cls.push('running');
  if (!slot.enabled) cls.push('disabled');
  const bindCount = slot.bind_ams_count ?? 0;
  return (
    <span className="card-index-row">
      <span className={cls.join(' ')} data-testid={`ams-hotend-slot-index-${slot.display_index}`}>
        {slot.display_index}
      </span>
      {slot.is_external_spool && (
        <span
          className="card-index-ext-tag"
          data-testid={`ams-hotend-slot-ext-${slot.display_index}`}
          title={t('External spool')}
        >
          {'Ext'}
        </span>
      )}
      {bindCount > 0 && (
        <span className="ams-bind-badge" title={t('Associated AMS: {{count}}', { count: bindCount })}>
          <AmsBindIcon />
          <span>+{bindCount}</span>
        </span>
      )}
    </span>
  );
}

function AmsBindIcon() {
  return (
    <svg width={12} height={10} viewBox="0 0 12 10" aria-hidden="true">
      <rect x={1} y={2} width={10} height={6} rx={1} fill="none" stroke="currentColor" strokeWidth={1} />
      <rect x={3} y={0.75} width={1.2} height={2.2} rx={0.4} fill="currentColor" />
      <rect x={5.4} y={0.75} width={1.2} height={2.2} rx={0.4} fill="currentColor" />
      <rect x={7.8} y={0.75} width={1.2} height={2.2} rx={0.4} fill="currentColor" />
      <rect x={3} y={4.25} width={1.1} height={1.1} rx={0.2} fill="currentColor" />
      <rect x={5.45} y={4.25} width={1.1} height={1.1} rx={0.2} fill="currentColor" />
      <rect x={7.9} y={4.25} width={1.1} height={1.1} rx={0.2} fill="currentColor" />
    </svg>
  );
}
