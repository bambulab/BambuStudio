import { useEffect, useMemo } from 'react';
import { useAmsFilamentHotendBridge } from '../useAmsFilamentHotendBridge';
import { useDeviceWebTheme } from '../../../../hooks/useDeviceWebTheme';
import type { AmsFilamentHotendSlotViewModel } from '../types';
import { BatchExternalFilamentDialog } from '../components/BatchExternalFilamentDialog';
import { selectionKey } from '../components/Stage';

type SlotQuery = {
  ams_id: string;
  slot_id: string;
  display_index: number;
};

function getQuery() {
  const hash = window.location.hash;
  const query = hash.includes('?') ? hash.slice(hash.indexOf('?') + 1) : '';
  const params = new URLSearchParams(query);
  return {
    intent: (params.get('intent') === 'run' ? 'run' : 'edit') as 'edit' | 'run',
    slots: parseSlots(params.get('slots') ?? ''),
  };
}

function parseSlots(raw: string): SlotQuery[] {
  if (!raw) return [];
  return raw.split(',').map((part) => {
    const [ams_id, slot_id, display] = part.split('|');
    return {
      ams_id: ams_id ?? '',
      slot_id: slot_id ?? '',
      display_index: Number(display ?? 0),
    };
  }).filter((item) => item.ams_id && item.slot_id && item.display_index > 0);
}

export function AmsExtFilamentEditPage() {
  const query = useMemo(getQuery, []);
  const { state, refresh, runSlotAction, runBatchAction } = useAmsFilamentHotendBridge();
  useDeviceWebTheme();

  useEffect(() => {
    void refresh('batch_ext_load');
  }, [refresh]);

  // The native filament-settings dialog is opened by C++ after the Web
  // `edit` action has already returned, so a refresh in the Promise `then`
  // can easily run before the user saves. Keep this embedded dialog lightly
  // refreshed while it is open; it mirrors the manual refresh behaviour the
  // user already verified, without requiring another click.
  useEffect(() => {
    let refreshing = false;
    const refreshExtRows = () => {
      if (refreshing) return;
      refreshing = true;
      void refresh('batch_ext_load').finally(() => {
        refreshing = false;
      });
    };
    window.addEventListener('focus', refreshExtRows);
    document.addEventListener('visibilitychange', refreshExtRows);
    const timer = window.setInterval(refreshExtRows, 1000);
    return () => {
      window.removeEventListener('focus', refreshExtRows);
      document.removeEventListener('visibilitychange', refreshExtRows);
      window.clearInterval(timer);
    };
  }, [refresh]);

  const queryKeys = useMemo(
    () => new Set(query.slots.map((slot) => `${slot.ams_id}::${slot.slot_id}`)),
    [query.slots],
  );

  const slots = useMemo(() => {
    const all = state.filament_page.hotend_slots;
    const selected = all.filter((slot) => queryKeys.has(selectionKey(slot)));
    if (selected.length) return selected;
    return query.slots.map((slot): AmsFilamentHotendSlotViewModel => ({
      addr_id: 0,
      display_index: slot.display_index,
      ams_id: slot.ams_id,
      slot_id: slot.slot_id,
      is_top_tier: false,
      enabled: true,
      is_external_spool: true,
      filament_card: {
        state: 'Unknown',
        color: '#D9D9D9',
        color_type: 'single',
        text: '',
        name: '',
        type: '',
        remain: 100,
        status_text: '',
      },
      filament_line: {
        state: 'no_filament',
        has_filament: false,
        filament_color: '',
      },
      filament_nozzle: {
        state: 'Normal',
        has_filament: false,
        filament_color: '',
        on_extruder: false,
      },
      select_action: {
        can_select: true,
        reason: '',
        ext_label: true,
      },
    }));
  }, [query.slots, queryKeys, state.filament_page.hotend_slots]);

  const closeDialog = () => {
    void runSlotAction('close_dialog');
  };

  const editSlot = (slot: AmsFilamentHotendSlotViewModel) => {
    void runSlotAction('edit', slot).then(() => {
      // Immediate refresh keeps non-modal edit implementations responsive;
      // delayed refreshes cover the native modal path where the first refresh
      // fires before the user has saved the dialog.
      void refresh('batch_ext_load');
      window.setTimeout(() => void refresh('batch_ext_load'), 500);
      window.setTimeout(() => void refresh('batch_ext_load'), 1500);
    });
  };

  const confirmRun = () => {
    if (slots.some((slot) => slot.filament_card.state === 'Unknown')) return;
    void runBatchAction('batch_ext_load', slots);
    closeDialog();
  };

  return (
    <div className="batch-ext-edit-page">
      <BatchExternalFilamentDialog
        open
        embedded
        intent={query.intent}
        slots={slots}
        onClose={closeDialog}
        onEditSlot={editSlot}
        onConfirmRun={confirmRun}
      />
    </div>
  );
}
