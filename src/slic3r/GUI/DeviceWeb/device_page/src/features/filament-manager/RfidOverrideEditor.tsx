import { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useDeviceBridge } from '../../hooks/Bridge';
import type { BridgeResponseBody, Spool } from './types';

interface Options {
  profiles: string[];
  printer: string;
  selected: string;
  color: string;
  active: boolean;
  mixed_nozzles: boolean;
}

export function RfidOverrideEditor({ spool, onUpdated }: { spool: Spool; onUpdated: () => void }) {
  const { t } = useTranslation();
  const request = useDeviceBridge();
  const [options, setOptions] = useState<Options | null>(null);
  const [profile, setProfile] = useState('');
  const [color, setColor] = useState('#808080');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [editing, setEditing] = useState(false);
  const body = (action: string, extra = {}) => ({ module: 'filament', submod: 'spool', action, payload: { spool_id: spool.spool_id, ...extra } });

  useEffect(() => {
    let live = true;
    setOptions(null);
    setError('');
    setEditing(false);
    void request<ReturnType<typeof body>, BridgeResponseBody>(body('rfid_options')).then((res) => {
      if (!live) return;
      if (!res.ok || res.value.error_code !== 0) {
        setError(res.ok ? res.value.message || t('Cannot load RFID assignment.') : t('Cannot load RFID assignment.'));
        return;
      }
      const next = res.value.payload as unknown as Options;
      setOptions(next);
      setProfile(next.selected);
      const hex = next.color.startsWith('#') ? next.color : '#' + next.color;
      setColor(/^#[0-9a-f]{6,8}$/i.test(hex) ? hex.slice(0, 7) : '#808080');
    });
    return () => { live = false; };
  }, [spool.spool_id, request, t]);

  const save = async (remove: boolean) => {
    if (!options || busy) return;
    setBusy(true);
    setError('');
    try {
      const res = await request<ReturnType<typeof body>, BridgeResponseBody>(body(remove ? 'rfid_remove' : 'rfid_assign', { profile, color, printer: options.printer }));
      if (!res.ok || res.value.error_code !== 0) {
        setError(res.ok ? res.value.message || t('Could not save assignment.') : t('Could not save assignment.'));
        return;
      }
      setOptions({ ...options, active: !remove, selected: remove ? '' : profile, color: remove ? spool.color_code : color });
      if (remove) setProfile('');
      setEditing(false);
      onUpdated();
    } finally { setBusy(false); }
  };
  return <section className="mt-4 text-sm text-fm-text-primary" data-testid="rfid-override">
    <div className="fm-section-bar mb-2">{t('Studio RFID assignment')}</div>
    <label className="block mb-2">{t('Spool RFID ID')}
      <input className="block w-full rounded bg-fm-input px-2 py-1 font-mono text-xs text-fm-text-primary"
        readOnly value={spool.tag_uid || ''} onFocus={(event) => event.currentTarget.select()} />
    </label>
    <p>{t('Original filament')}: {spool.brand} {spool.series || spool.material_type}, {spool.color_name ? spool.color_name + ' ' : ''}({spool.color_code})</p>
    <p>{options?.active ? t('User override') : t('No override')}</p>
    {options?.active && <p>{options.selected || t('No assigned profile for this printer')}</p>}
    <p className="text-xs text-fm-text-secondary">{t('Saved on this computer. Use Sync in Prepare to apply. The printer still reads the original tag; remaining weight is unchanged.')}</p>
    {error && <p role="alert">{error}</p>}
    {options && !editing && <div className="flex gap-3 mt-2">
      <button className="rounded px-3 py-1 bg-fm-input text-fm-text-primary disabled:opacity-40" disabled={busy || options.mixed_nozzles} onClick={() => setEditing(true)}>{options.active ? t('Change assignment...') : t('Assign actual filament...')}</button>
      {options.active && <button className="rounded px-3 py-1 bg-fm-input text-fm-text-primary disabled:opacity-40" disabled={busy} onClick={() => void save(true)}>{t('Remove override')}</button>}
    </div>}
    {options?.mixed_nozzles && <p>{t('RFID overrides are not yet supported in mixed nozzle mode.')}</p>}
    {options && editing && <div className="flex flex-col gap-2 mt-2">
      <label>{t('Profile for')} {options.printer}
        <select className="w-full bg-fm-input" value={profile} disabled={busy} onChange={(e) => setProfile(e.target.value)}>
          <option value="">{t('Choose a filament profile')}</option>
          {options.profiles.map((name) => <option key={name} value={name}>{name}</option>)}
        </select>
      </label>
      <p className="text-xs">{t('This prototype supports the same base material as the tag.')}</p>
      <label>{t('Actual color')} <input type="color" value={color} disabled={busy} onChange={(e) => setColor(e.target.value)} /></label>
      <div className="flex gap-3">
        <button className="rounded px-3 py-1 bg-fm-input text-fm-text-primary disabled:opacity-40" disabled={busy || !profile} onClick={() => void save(false)}>{t('Save assignment')}</button>
        <button className="rounded px-3 py-1 bg-fm-input text-fm-text-primary disabled:opacity-40" disabled={busy} onClick={() => { setProfile(options.selected); const hex = options.color.startsWith('#') ? options.color : '#' + options.color; setColor(/^#[0-9a-f]{6,8}$/i.test(hex) ? hex.slice(0, 7) : '#808080'); setEditing(false); }}>{t('Cancel')}</button>
      </div>
    </div>}
  </section>;
}
