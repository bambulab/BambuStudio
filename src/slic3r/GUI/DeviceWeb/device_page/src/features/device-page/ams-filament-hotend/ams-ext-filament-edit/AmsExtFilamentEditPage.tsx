import { useEffect, useMemo } from 'react';
import { useAmsFilamentHotendBridge } from '../useAmsFilamentHotendBridge';
import { useDeviceWebTheme } from '../../../../hooks/useDeviceWebTheme';
import { useFitHostWidth } from '../../../../hooks/useFitHostWidth';
import type { AmsFilamentHotendSlotViewModel, FilamentPageMode } from '../types';
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
    mode: parseMode(params.get('mode')),
    slots: parseSlots(params.get('slots') ?? ''),
    runSlots: parseSlots(params.get('run_slots') ?? ''),
  };
}

function parseMode(raw: string | null): FilamentPageMode {
  return raw === 'batch_ams_load' ? 'batch_ams_load' : 'batch_ext_load';
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

// ExtFilamentEditDialog is a fixed FromDIP(620) wide host.
const HOST_WIDTH_DIP = 620;

export function AmsExtFilamentEditPage() {
  const query = useMemo(getQuery, []);
  const { state, refresh, runSlotAction, runBatchAction } = useAmsFilamentHotendBridge();
  useDeviceWebTheme();
  useFitHostWidth(HOST_WIDTH_DIP);

  useEffect(() => {
    void refresh(query.mode);
  }, [query.mode, refresh]);

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
      void refresh(query.mode).finally(() => {
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
  }, [query.mode, refresh]);

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
      void refresh(query.mode);
      window.setTimeout(() => void refresh(query.mode), 500);
      window.setTimeout(() => void refresh(query.mode), 1500);
    });
  };

  const confirmRun = () => {
    if (slots.some((slot) => slot.filament_card.state === 'Unknown')) return;
    // Slots were just edited here, so tell C++ to run without re-checking the
    // (still-lagging) device tray identity: issue the load and exit batch mode.
    void runBatchAction(query.mode, query.runSlots.length ? query.runSlots : slots, true);
    // Close with the "run committed" signal so the main N1x AMS page is asked
    // to leave batch selection and return to Idle (see close_dialog_after_run).
    void runSlotAction('close_dialog_after_run');
  };

  return (
    <div className="batch-ext-edit-page">
      <BatchExternalFilamentDialog
        open
        embedded
        intent={query.intent}
        mode={query.mode}
        slots={slots}
        onClose={closeDialog}
        onEditSlot={editSlot}
        onConfirmRun={confirmRun}
      />
    </div>
  );
}
