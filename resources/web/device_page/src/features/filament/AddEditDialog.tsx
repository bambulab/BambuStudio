import { useState, useEffect, useMemo, useRef, useCallback } from 'react';
import { useTranslation } from 'react-i18next';
import type { Spool, PresetVendor, MachineItem, AmsData, AmsUnit, AmsTray } from './types';
import { BAMBU_COLORS } from './constants';
import { SpoolSvg } from './SpoolSvg';
import useStore from '../../store/AppStore';

interface Props {
  open: boolean;
  editingSpool: Spool | null;        // null = add mode
  prefilledSpool: Partial<Spool> | null; // addSimilar prefill
  presets: PresetVendor[];
  onClose: () => void;
  onSubmitAdd: (data: Partial<Spool>, quantity: number) => void;
  onSubmitUpdate: (data: Partial<Spool>) => void;
  onFetchMachines: () => Promise<MachineItem[]>;
  onFetchAmsData: (devId: string) => Promise<AmsData | null>;
}

export function AddEditDialog({
  open, editingSpool, prefilledSpool, presets, onClose,
  onSubmitAdd, onSubmitUpdate, onFetchMachines, onFetchAmsData,
}: Props) {
  const { t } = useTranslation();
  const isEdit = !!editingSpool;
  const initSpool = editingSpool || prefilledSpool;

  // Pull cloud-side filament config so the brand dropdown includes vendors
  // the user has configured in the cloud but not yet locally.
  const cloudConfig = useStore((s) => s.filament.cloudConfig);
  // F4.7: mirror of Studio's global selected machine. Used to default the
  // AMS-tab printer and to follow external changes made via
  // DeviceManager::OnSelectedMachineChanged.
  const globalSelectedDev = useStore((s) => s.filament.selectedMachineDevId);
  const mergedVendorNames = useMemo(() => {
    const set = new Set<string>();
    presets.forEach((v) => { if (v.name) set.add(v.name); });
    const cloudVendors = cloudConfig?.vendors;
    if (Array.isArray(cloudVendors)) {
      cloudVendors.forEach((n) => { if (typeof n === 'string' && n) set.add(n); });
    }
    return [...set].sort();
  }, [presets, cloudConfig]);

  // Dialog mode
  const [mode, setMode] = useState<'manual' | 'ams'>('manual');

  // Form fields
  const [brand, setBrand] = useState('');
  const [materialType, setMaterialType] = useState('');
  const [series, setSeries] = useState('');
  const [colorCode, setColorCode] = useState('');
  const [colorName, setColorName] = useState('');
  const [totalWeight, setTotalWeight] = useState(1000);
  const [spoolWeight, setSpoolWeight] = useState(250);
  const [note, setNote] = useState('');
  const [quantity, setQuantity] = useState(1);

  // AMS state
  const [machines, setMachines] = useState<MachineItem[]>([]);
  const [amsData, setAmsData] = useState<AmsData | null>(null);
  const [selectedUnit, setSelectedUnit] = useState<string | null>(null);
  const [selectedSlot, setSelectedSlot] = useState<{ ams_id: string; slot_id: string; tray: AmsTray } | null>(null);
  const [amsLoading, setAmsLoading] = useState(false);
  const [amsError, setAmsError] = useState('');
  const [amsReadonly, setAmsReadonly] = useState(false);

  // Reset form when dialog opens
  useEffect(() => {
    if (!open) return;
    setMode('manual');
    setQuantity(1);
    setSelectedSlot(null);
    setSelectedUnit(null);
    setAmsData(null);
    setAmsReadonly(false);
    setAmsError('');
    if (initSpool) {
      setBrand(initSpool.brand || '');
      setMaterialType(initSpool.material_type || '');
      setSeries(initSpool.series || '');
      setColorCode(initSpool.color_code || '');
      setColorName(initSpool.color_name || '');
      setTotalWeight(initSpool.initial_weight || 1000);
      setSpoolWeight(initSpool.spool_weight || 250);
      setNote(initSpool.note || '');
    } else {
      setBrand(''); setMaterialType(''); setSeries('');
      setColorCode(''); setColorName('');
      setTotalWeight(1000); setSpoolWeight(250);
      setNote('');
    }
  }, [open]);

  // UX decision (per F4.3 feedback, revised):
  //   收起 Type / Series 两个独立字段，合并为单个「耗材类型」<select>，
  //   选项直接显示完整名如 "PLA Basic" / "PLA Matte"；只允许从下拉里选，
  //   不允许手填（用户反馈："耗材类型不允许编辑，参考品牌使用下拉列表"）。
  //   内部仍拆成 material_type + series 写入 Spool（后端 / 归档 / detail 语义不变）。
  const typeSeriesOptions = useMemo<string[]>(() => {
    const set = new Set<string>();
    const vendors = brand ? presets.filter((v) => v.name === brand) : presets;
    vendors.forEach((v) => {
      v.types.forEach((tp) => {
        const series = Array.isArray(tp.series) ? tp.series.filter(Boolean) : [];
        if (series.length === 0) {
          if (tp.name) set.add(tp.name);
        } else {
          series.forEach((s) => set.add(`${tp.name} ${s}`.trim()));
        }
      });
    });
    return [...set].sort();
  }, [brand, presets]);

  // Split a combined "PLA Basic" string back into (type, series) using the
  // vendor's known types as anchors (longest-first to tolerate types with
  // spaces). Falls back to first token as type / rest as series.
  const splitTypeSeries = (full: string): { type: string; series: string } => {
    const s = (full || '').trim();
    if (!s) return { type: '', series: '' };
    const allTypes = new Set<string>();
    const vendors = brand ? presets.filter((v) => v.name === brand) : presets;
    vendors.forEach((v) => v.types.forEach((tp) => { if (tp.name) allTypes.add(tp.name); }));
    const sortedTypes = [...allTypes].sort((a, b) => b.length - a.length);
    for (const tp of sortedTypes) {
      if (s === tp) return { type: tp, series: '' };
      if (s.startsWith(tp + ' ')) return { type: tp, series: s.substring(tp.length + 1).trim() };
    }
    const first = s.split(/\s+/)[0];
    return { type: first, series: s.substring(first.length).trim() };
  };

  // Current combined value shown in the single combobox.
  const typeSeriesFull = series ? `${materialType} ${series}`.trim() : materialType;

  const matchedPresetItem = useMemo(() => {
    for (const vendor of presets) {
      if (vendor.name !== brand) continue;
      for (const tp of vendor.types) {
        if (tp.name !== materialType) continue;
        const item = tp.items?.find((entry) => (entry.series || '') === (series || ''));
        if (item) return item;
      }
    }
    return null;
  }, [presets, brand, materialType, series]);

  const handleTypeSeriesChange = (full: string) => {
    const { type, series: sr } = splitTypeSeries(full);
    setMaterialType(type);
    setSeries(sr);
  };

  const netWeight = Math.max(0, totalWeight - spoolWeight);
  // F4.5: validation no longer depends on `series` — the combined type field
  // covers both, and plenty of materials legitimately have no series (e.g. ABS).
  const isValid = !!(brand && materialType && colorCode && totalWeight > 0);

  // F4.4: whether the currently picked colour is a user-defined one (not in
  // the preset BAMBU_COLORS palette). Used to render a custom-colour preview.
  const isCustomColor = !!colorCode && !BAMBU_COLORS.some(
    (c) => c.toUpperCase() === colorCode.toUpperCase(),
  );

  const handleSubmit = () => {
    const data: Partial<Spool> = {
      brand, material_type: materialType, series,
      color_code: colorCode, color_name: colorName,
      initial_weight: totalWeight, spool_weight: spoolWeight,
      net_weight: netWeight,
      note,
      setting_id: matchedPresetItem?.filament_id || initSpool?.setting_id || '',
    };

    if (isEdit) {
      data.spool_id = editingSpool!.spool_id;
      onSubmitUpdate(data);
    } else {
      if (mode === 'ams' && selectedSlot) {
        // F4.8: the spool is imported from a live AMS slot — propagate every
        // authoritative field the tray gives us (tag_uid / setting_id /
        // remain / diameter) so the local record matches what the printer
        // actually has in the slot, not the manual-mode defaults.
        const tray = selectedSlot.tray;
        const trayNetInit = parseInt(String(tray.weight ?? '0'), 10) || 0; // initial net (g)
        const remain = typeof tray.remain === 'number' ? tray.remain : 0;
        data.entry_method = 'ams_sync';
        data.tag_uid = tray.tag_uid || '';
        data.setting_id = tray.setting_id || data.setting_id || '';
        data.bound_ams_id = selectedSlot.ams_id;
        data.bound_dev_id = amsData?.selected_dev_id || '';
        data.remain_percent = remain;
        // When a remain% is reported, store the *current* net weight (not
        // the full-spool value), so SpoolTable.getDisplayedRemainWeight
        // renders the live AMS reading instead of a stale initial value.
        if (trayNetInit > 0 && remain > 0) {
          data.net_weight = Math.round(trayNetInit * remain / 100);
        }
        // `tray.diameter` is a string in the MQTT payload; accept both.
        const diaRaw = tray.diameter;
        const diaNum = typeof diaRaw === 'number' ? diaRaw : parseFloat(String(diaRaw ?? '0'));
        if (diaNum > 0) data.diameter = diaNum;
      } else {
        data.entry_method = 'manual';
      }
      onSubmitAdd(data, quantity);
    }
    onClose();
  };

  // AMS mode switch
  const switchToAms = async () => {
    setMode('ams');
    setSelectedSlot(null);
    setAmsLoading(true);
    setAmsError('');
    try {
      const list = await onFetchMachines();
      setMachines(list);
      if (list.length === 0) {
        setAmsData(null);
        setSelectedUnit(null);
        setSelectedSlot(null);
        setAmsError('');
        setAmsLoading(false);
        return;
      }
      // F4.7: default to whichever printer Studio is globally pointing at
      // (DeviceManager::get_selected_machine), falling back to the first
      // online / first machine only when the global selection is not in
      // this printer list.
      const defaultDev =
        list.find((m) => globalSelectedDev && m.dev_id === globalSelectedDev) ||
        list.find((m) => m.is_online) ||
        list[0];
      const data = await onFetchAmsData(defaultDev.dev_id);
      setAmsData(data);
      if (data && data.ams_units.length > 0) {
        setSelectedUnit(data.ams_units[0].ams_id);
      } else {
        setAmsError(t('No AMS detected on this device'));
      }
    } catch {
      setAmsError(t('Getting device list failed, please retry'));
    }
    setAmsLoading(false);
  };

  const handleDeviceChange = useCallback(async (devId: string) => {
    setSelectedUnit(null);
    setSelectedSlot(null);
    setAmsLoading(true);
    setAmsError('');
    try {
      const data = await onFetchAmsData(devId);
      setAmsData(data);
      if (data && data.ams_units.length > 0) {
        setSelectedUnit(data.ams_units[0].ams_id);
      } else {
        setAmsError(t('No AMS detected on this device'));
      }
    } catch {
      setAmsError(t('Getting AMS data failed'));
    }
    setAmsLoading(false);
  }, [onFetchAmsData, t]);

  // F4.7: follow Studio-wide machine switches while the dialog is open on
  // the AMS tab. If the user picks a different printer elsewhere in
  // Studio, mirror that selection here instead of staying on whichever
  // printer was active when the dialog opened.
  useEffect(() => {
    if (!open || mode !== 'ams' || amsLoading) return;
    if (!globalSelectedDev) return;
    const currentDev = amsData?.selected_dev_id || '';
    if (currentDev === globalSelectedDev) return;
    // Only follow if the new machine is actually in the dropdown list we
    // currently show; otherwise leave the user in their chosen state.
    if (!machines.some((m) => m.dev_id === globalSelectedDev)) return;
    void handleDeviceChange(globalSelectedDev);
  }, [open, mode, amsLoading, globalSelectedDev, amsData, machines, handleDeviceChange]);

  // Live-refresh AMS data while the "Read from AMS" tab is open. Rationale:
  // once the user picks a printer, the C++ side keeps parsing fresh MQTT
  // push packets (MachineObject::parse_json) that mutate tray weight /
  // color / RFID / remain% in place. Without a live pull, the dialog keeps
  // showing the snapshot captured when the tab was opened, so a freshly
  // inserted / swapped spool never appears and `tray.weight` shown in the
  // form is stale. A lightweight 1.5 s poll is enough — the RPC payload is
  // a few KB and we keep the existing selection + form fields intact so
  // there is no visible flicker.
  const selectedSlotRef = useRef(selectedSlot);
  selectedSlotRef.current = selectedSlot;
  useEffect(() => {
    if (!open || mode !== 'ams' || amsLoading) return;
    const devId = amsData?.selected_dev_id || '';
    if (!devId) return;

    let cancelled = false;
    const tick = async () => {
      try {
        const data = await onFetchAmsData(devId);
        if (cancelled || !data) return;
        setAmsData(data);

        // Keep the currently highlighted slot pointing at the latest tray
        // object so downstream renderers (form, palette) reflect the new
        // MQTT snapshot. If the tray disappeared (user pulled the spool),
        // clear the selection so stale form values can't be submitted.
        // Compare the tray payload before calling setState so we don't
        // force a full re-render on every 1.5 s tick when nothing changed.
        const cur = selectedSlotRef.current;
        if (cur) {
          const unit = data.ams_units.find((u) => u.ams_id === cur.ams_id);
          const tray = unit?.trays.find((tr) => tr.slot_id === cur.slot_id);
          if (!tray) {
            setSelectedSlot(null);
          } else if (JSON.stringify(tray) !== JSON.stringify(cur.tray)) {
            setSelectedSlot({ ams_id: cur.ams_id, slot_id: cur.slot_id, tray });
          }
        }
      } catch {
        // Poll errors are non-fatal: the next tick will try again. We
        // deliberately do NOT surface `amsError` here because that would
        // nuke the user's current view on a transient hiccup.
      }
    };

    const timer = window.setInterval(tick, 1500);
    return () => {
      cancelled = true;
      window.clearInterval(timer);
    };
  }, [open, mode, amsLoading, amsData?.selected_dev_id, onFetchAmsData]);

  const selectAmsSlot = (unit: AmsUnit, tray: AmsTray) => {
    setSelectedSlot({ ams_id: unit.ams_id, slot_id: tray.slot_id, tray });
    const hasUid = !!(tray.tag_uid && tray.tag_uid.length > 0);
    setAmsReadonly(hasUid);

    // F4.8: drive the whole form from the AMS tray. Previous implementation
    // only pushed sub_brands / fila_type / color / weight and left the form
    // defaults (spool=250g, initial=1000g) untouched, which meant the
    // weights shown in the form did not reflect the actual slot.
    //
    // Strategy (preferred order):
    //   1. Resolve (brand, material_type, series) via `setting_id`
    //      (reverse-lookup through presets). This is the most authoritative
    //      mapping because it comes straight from the Bambu preset used to
    //      print the spool.
    //   2. Fall back to sub_brands + fila_type string matching for trays
    //      that do not carry a setting_id (3rd-party / old firmware).

    let brandName  = '';
    let typeName   = '';
    let seriesName = '';

    const settingId = tray.setting_id || '';
    if (settingId) {
      outer: for (const vendor of presets) {
        for (const tp of vendor.types) {
          const hit = tp.items?.find((it) => it.filament_id === settingId);
          if (hit) {
            brandName  = vendor.name;
            typeName   = tp.name;
            seriesName = hit.series || '';
            break outer;
          }
        }
      }
    }

    if (!brandName) brandName = tray.sub_brands || '';
    if (!typeName) {
      const fullType = tray.fila_type || '';
      const typeCandidates = new Set<string>();
      const vendorsForMatch = brandName ? presets.filter((v) => v.name === brandName) : presets;
      vendorsForMatch.forEach((v) => v.types.forEach((tp) => { if (tp.name) typeCandidates.add(tp.name); }));
      let matchedType = '';
      let seriesRemainder = '';
      for (const cand of typeCandidates) {
        if (fullType === cand) { matchedType = cand; seriesRemainder = ''; break; }
        if (fullType.startsWith(cand + ' ') && cand.length > matchedType.length) {
          matchedType = cand;
          seriesRemainder = fullType.substring(cand.length + 1).trim();
        } else if (fullType.startsWith(cand) && cand.length > matchedType.length) {
          matchedType = cand;
          seriesRemainder = fullType.substring(cand.length).trim();
        }
      }
      typeName = matchedType;
      if (!seriesName) seriesName = seriesRemainder;
    }

    setBrand(brandName);
    setMaterialType(typeName);
    setSeries(seriesName);

    // F4.8: color may arrive as #RRGGBBAA (BBL firmware appends alpha for
    // transparent filaments). Strip alpha so the palette match and the hex
    // label stay canonical 6-digit HEX.
    const rawColor = tray.color || '';
    const sanitizedColor = /^#[0-9a-fA-F]{8}$/.test(rawColor)
      ? rawColor.substring(0, 7)
      : rawColor;
    if (sanitizedColor) setColorCode(sanitizedColor);

    // F4.8: `tray.weight` is the spool's *initial net* weight in grams
    // (MQTT `tray_weight`, e.g. "1000"). Derive the form numbers so that
    // Total = Spool + Net(tray.weight) and everything downstream is
    // consistent with the printer's own view of the cartridge.
    const trayNetWeight = parseInt(String(tray.weight ?? '0'), 10) || 0;
    const defaultSpoolWeight = 250;
    setSpoolWeight(defaultSpoolWeight);
    setTotalWeight(trayNetWeight > 0 ? trayNetWeight + defaultSpoolWeight : 1000);
  };

  // F4.10: dialog drag support. Users expect to be able to drag the dialog
  // around by its header so it doesn't cover underlying context.
  const [dragOffset, setDragOffset] = useState<{ x: number; y: number }>({ x: 0, y: 0 });
  const dragState = useRef<{ startX: number; startY: number; baseX: number; baseY: number } | null>(null);

  // Reset drag offset whenever dialog is re-opened.
  useEffect(() => { if (open) setDragOffset({ x: 0, y: 0 }); }, [open]);

  const handleDragMove = useCallback((e: MouseEvent) => {
    const st = dragState.current;
    if (!st) return;
    setDragOffset({ x: st.baseX + (e.clientX - st.startX), y: st.baseY + (e.clientY - st.startY) });
  }, []);
  const handleDragEnd = useCallback(() => {
    dragState.current = null;
    window.removeEventListener('mousemove', handleDragMove);
    window.removeEventListener('mouseup', handleDragEnd);
    document.body.style.userSelect = '';
  }, [handleDragMove]);
  const handleDragStart = useCallback((e: React.MouseEvent) => {
    // Ignore drags that start on interactive controls (e.g. close button).
    const target = e.target as HTMLElement;
    if (target.closest('button, input, select, textarea, a')) return;
    dragState.current = { startX: e.clientX, startY: e.clientY, baseX: dragOffset.x, baseY: dragOffset.y };
    window.addEventListener('mousemove', handleDragMove);
    window.addEventListener('mouseup', handleDragEnd);
    document.body.style.userSelect = 'none';
    e.preventDefault();
  }, [dragOffset, handleDragMove, handleDragEnd]);
  const resetDragOffset = useCallback(() => setDragOffset({ x: 0, y: 0 }), []);

  if (!open) return null;

  const currentUnit = amsData?.ams_units.find((u) => u.ams_id === selectedUnit);
  const slotLabels = ['A1', 'A2', 'A3', 'A4'];

  return (
    <div className="fixed inset-0 bg-black/50 flex items-start justify-center pt-10 z-[1000]" onClick={onClose}>
      <div
        className="w-[644px] max-h-[calc(100vh-80px)] bg-[#242424] [html[data-theme=light]_&]:bg-fm-sidebar rounded-lg shadow-[0px_8px_24px_0px_rgba(0,0,0,0.12)] flex flex-col overflow-hidden fm-native-form"
        style={{ transform: `translate(${dragOffset.x}px, ${dragOffset.y}px)` }}
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header — drag handle (F4.10). Double-click resets position. */}
        <div
          className="flex items-center justify-between h-[40px] pl-[24px] pr-[8px] shrink-0 relative cursor-move select-none"
          onMouseDown={handleDragStart}
          onDoubleClick={resetDragOffset}
          title={t('Drag to move')}
        >
          <h3 className="text-[14px] font-bold leading-[22px] text-fm-text-strong truncate">{isEdit ? t('Edit Filament') : t('Add Filament')}</h3>
          <button className="size-[24px] rounded-[6px] flex items-center justify-center bg-transparent border-none text-fm-text-detail cursor-pointer hover:text-fm-text-strong hover:bg-fm-hover" onClick={onClose}>
            <svg width="16" height="16" viewBox="0 0 16 16" fill="none"><path d="M4 4l8 8M12 4l-8 8" stroke="currentColor" strokeWidth="1.2"/></svg>
          </button>
          <div className="absolute bottom-0 left-0 right-0 h-px bg-fm-border" />
        </div>

        {/* Scrollable content */}
        <div className="flex-1 overflow-y-auto">
        <div className="flex flex-col gap-[24px] p-[24px]">

        {/* Mode tabs — only in add mode */}
        {!isEdit && (
          <div className="flex gap-[16px] items-start w-full">
            <div
              className={`flex-1 flex items-center justify-center py-[4px] px-[16px] cursor-pointer rounded-[8px] border transition-all duration-150 ${mode === 'manual' ? 'border-fm-brand text-[#50e81d]' : 'border-fm-border-focus text-fm-text-primary hover:bg-fm-hover'}`}
              onClick={() => setMode('manual')}
            ><span className="py-[5px] text-[14px] leading-[22px]">{t('Manual Add')}</span></div>
            <div
              className={`flex-1 flex items-center justify-center py-[4px] px-[8px] cursor-pointer rounded-[8px] border transition-all duration-150 ${mode === 'ams' ? 'border-fm-brand text-[#50e81d]' : 'border-fm-border-focus text-fm-text-primary hover:bg-fm-hover'}`}
              onClick={switchToAms}
            ><span className="py-[5px] text-[14px] leading-[22px]">{t('Read from AMS')}</span></div>
          </div>
        )}

        {/* AMS Section — always show a printer picker row (F4.7); errors are inline, not a blank page */}
        {mode === 'ams' && (
          <div className="flex flex-col gap-[16px]">
            <div className="flex items-center gap-[6px]">
              <div className="w-[2px] h-[16px] rounded-[10px] bg-[#50e81d]" />
              <span className="text-[14px] leading-[22px] text-fm-text-primary">{t('Select Device')}</span>
            </div>

            {amsLoading && (
              <div className="text-center text-fm-text-detail text-xs py-4">{t('Fetching device info...')}</div>
            )}

            {!amsLoading && (
              <div className="flex flex-col gap-[10px] rounded-[8px] border border-fm-border-focus bg-fm-inner p-3">
                <label className="text-[11px] leading-[16px] text-fm-text-secondary">{t('Printer')}</label>
                <select
                  className="w-full min-h-[36px] rounded-[6px] bg-fm-inner2 px-[8px] text-fm-text-strong text-xs outline-none focus:shadow-[0_0_0_1px_var(--color-fm-brand)] fm-select-arrow cursor-pointer disabled:cursor-not-allowed disabled:opacity-60"
                  value={machines.length ? (amsData?.selected_dev_id || machines[0]?.dev_id || '') : ''}
                  disabled={machines.length === 0}
                  onChange={(e) => { if (e.target.value) void handleDeviceChange(e.target.value); }}
                >
                  {machines.length === 0 ? (
                    <option value="">{t('No printers — sign in and bind a device')}</option>
                  ) : (
                    machines.map((m) => (
                      <option key={m.dev_id} value={m.dev_id}>
                        {m.dev_name || m.dev_id}{m.is_online ? ` (${t('Online')})` : ''}
                      </option>
                    ))
                  )}
                </select>
                {machines.length === 0 && (
                  <p className="text-[11px] leading-[16px] text-fm-text-detail m-0">{t('No printer found, please ensure logged in and device bound')}</p>
                )}
              </div>
            )}

            {!!amsError && !amsLoading && (
              <div className="text-xs text-fm-warning leading-[19px] px-1">{amsError}</div>
            )}

            {!amsLoading && machines.length > 0 && (
              <div className="flex flex-col gap-[4px]">
                <div className="flex items-center gap-[12px]">
                  <div className="flex flex-1 gap-[12px] items-center justify-end min-w-0">
                    {amsData?.ams_units.map((u) => (
                      <button
                        key={u.ams_id}
                        type="button"
                        className={`rounded-[4px] border bg-transparent cursor-pointer flex items-center p-[4px] transition-colors duration-150 hover:border-fm-text-secondary ${u.ams_id === selectedUnit ? 'border-fm-brand' : 'border-fm-border-focus'}`}
                        title={`AMS ${parseInt(u.ams_id, 10) + 1}`}
                        onClick={() => { setSelectedUnit(u.ams_id); setSelectedSlot(null); }}
                      >
                        <AmsUnitIcon unit={u} isActive={u.ams_id === selectedUnit} />
                      </button>
                    ))}
                  </div>
                </div>
                {currentUnit && (
                  <div className="flex gap-[8px] py-[8px]">
                    {currentUnit.trays.map((tray, i) => {
                      const label = slotLabels[i] || `A${i + 1}`;
                      const isSelected = selectedSlot?.slot_id === tray.slot_id && selectedSlot?.ams_id === currentUnit.ams_id;
                      if (!tray.is_exists) {
                        return (
                          <div key={label} className="flex-1 flex flex-col rounded-[6px] border border-fm-border-focus cursor-default opacity-30 transition-all duration-150 overflow-hidden">
                            <div className="fm-slot-header-bg text-center py-[2px] text-[11px] leading-[16px] text-fm-text-strong bg-[#424242] rounded-t-[6px]">{label}</div>
                            <div className="flex gap-[4px] items-center p-[4px]">
                              <div className="size-[40px] shrink-0 [&>svg]:size-[40px]">
                                <SpoolSvg color="#555" size={32} />
                              </div>
                              <div className="flex-1 flex flex-col gap-[4px] min-w-0">
                                <span className="text-[12px] leading-[19px] text-fm-text-primary truncate">{t('Empty')}</span>
                              </div>
                            </div>
                          </div>
                        );
                      }
                      return (
                        <div
                          key={label}
                          className={`flex-1 flex flex-col rounded-[6px] border cursor-pointer transition-all duration-150 overflow-hidden hover:border-fm-text-secondary ${isSelected ? 'border-fm-brand' : 'border-fm-border-focus'}`}
                          onClick={() => selectAmsSlot(currentUnit, tray)}
                        >
                          <div className={`fm-slot-header-bg text-center py-[2px] text-[11px] leading-[16px] text-fm-text-strong bg-[#424242] rounded-t-[6px] ${isSelected ? '!bg-fm-brand !text-white' : ''}`}>{label}</div>
                          <div className="flex gap-[4px] items-center p-[4px]">
                            <div className="size-[40px] shrink-0 [&>svg]:size-[40px]">
                              <SpoolSvg color={tray.color} size={32} />
                            </div>
                            <div className="flex-1 flex flex-col gap-[4px] min-w-0">
                              <div className="text-[12px] leading-[19px] text-fm-text-primary flex items-center gap-[4px] truncate">
                                <span className="inline-block size-[12px] rounded-sm shrink-0" style={{ background: tray.color || '#888' }} />
                                {tray.fila_type || '—'}
                              </div>
                              <div className="text-[12px] leading-[19px] text-fm-text-secondary">{tray.weight ? `${tray.weight}g` : '—'}</div>
                            </div>
                          </div>
                        </div>
                      );
                    })}
                  </div>
                )}
              </div>
            )}
          </div>
        )}

        {/* Separator between AMS and form */}
        {mode === 'ams' && selectedSlot && <div className="h-0 w-full border-t border-fm-border" />}

        {/* Form body — visible in manual mode, or in AMS when slot is selected */}
        {(mode === 'manual' || selectedSlot) && (
          <div className="flex flex-col gap-[12px]">
            {/* Section title: 耗材信息 */}
            <div className="flex flex-col gap-[16px]">
              <div className="flex items-center gap-[6px]">
                <div className="w-[2px] h-[16px] rounded-[10px] bg-[#50e81d]" />
                <span className="text-[14px] leading-[22px] text-fm-text-strong">{t('Filament Info')}</span>
              </div>

              {/* Brand / Material Type — 2 columns.
                  Series has been merged into Material Type per F4.3 feedback;
                  options look like "PLA Basic" / "PLA Matte" and we split the
                  combined string back into material_type + series internally. */}
              <div className="flex gap-[12px]">
                <div className="flex flex-col gap-[4px] flex-1 pb-[24px]">
                  <label className="text-[12px] leading-[19px] text-fm-text-secondary"><span className="text-[#ff2b00]">*</span> {t('Brand')}</label>
                  <select className="bg-fm-inner2 border-none rounded-[6px] h-[32px] pl-[8px] pr-[4px] text-fm-text-strong text-[12px] leading-[19px] outline-none w-full focus:shadow-[0_0_0_1px_var(--color-fm-brand)] fm-select-arrow cursor-pointer" value={brand} onChange={(e) => { setBrand(e.target.value); setMaterialType(''); setSeries(''); }} disabled={amsReadonly}>
                    <option value="">{t('Select Brand')}</option>
                    {mergedVendorNames.map((n) => <option key={n} value={n}>{n}</option>)}
                  </select>
                </div>
                <div className="flex flex-col gap-[4px] flex-1 pb-[24px]">
                  <label className="text-[12px] leading-[19px] text-fm-text-secondary"><span className="text-[#ff2b00]">*</span> {t('Material Type')}</label>
                  {/* F4.3: Must pick a brand first.  When brand is empty the
                      options list would be the union of all vendors' types
                      and that confuses users into thinking they can pick
                      before a brand is set — disable the control instead and
                      show a hint placeholder. */}
                  <select
                    className="bg-fm-inner2 border-none rounded-[6px] h-[32px] pl-[8px] pr-[4px] text-fm-text-strong text-[12px] leading-[19px] outline-none w-full focus:shadow-[0_0_0_1px_var(--color-fm-brand)] fm-select-arrow cursor-pointer disabled:cursor-not-allowed disabled:opacity-60"
                    value={typeSeriesFull}
                    onChange={(e) => handleTypeSeriesChange(e.target.value)}
                    disabled={amsReadonly || !brand}
                  >
                    <option value="">{!brand ? t('Select Brand First') : t('Select Type')}</option>
                    {typeSeriesOptions.map((n) => <option key={n} value={n}>{n}</option>)}
                  </select>
                </div>
              </div>

              {/* Color palette. F4.4 feedback: 自定义颜色需要"可保存 / 能看到已选"。
                  实现上保留原生 <input type="color"> 作为取色器，但在取完色后把
                  自定义色显示为一个可视色块（带选中高亮 + hex 标签 + 重新选择），
                  这样用户无需单独"保存"按钮即可确认颜色已入选。 */}
              <div className="flex flex-col gap-[8px]">
                <label className="text-[12px] leading-[19px] text-fm-text-secondary"><span className="text-[#ff2b00]">*</span> {t('Color')}</label>
                <div className={`flex flex-wrap gap-[6px] items-center${amsReadonly ? ' opacity-50 pointer-events-none' : ''}`}>
                  {/* Custom-color picker: 未选时显示"+"占位；已选自定义色时显示色块 */}
                  <div
                    className={`size-[24px] rounded-[4px] cursor-pointer border relative overflow-hidden flex items-center justify-center text-fm-text-detail text-sm ${
                      isCustomColor ? 'border-fm-brand border-2' : 'border-dashed border-white/40 bg-fm-inner'
                    }`}
                    style={isCustomColor ? { background: colorCode } : undefined}
                    title={isCustomColor ? t('Custom Color') : t('Pick Custom Color')}
                  >
                    {!isCustomColor && (
                      <svg width="14" height="14" viewBox="0 0 14 14" fill="none"><path d="M7 2v10M2 7h10" stroke="currentColor" strokeWidth="1.2"/></svg>
                    )}
                    <input
                      className="absolute inset-0 opacity-0 cursor-pointer"
                      type="color"
                      value={colorCode || '#000000'}
                      onChange={(e) => setColorCode(e.target.value)}
                    />
                  </div>
                  {BAMBU_COLORS.map((c) => (
                    <div
                      key={c}
                      className={`size-[24px] rounded-[4px] cursor-pointer border transition-colors duration-150 hover:border-white/40 ${c.toUpperCase() === colorCode.toUpperCase() ? 'border-fm-brand border-2' : 'border-white/16'}`}
                      style={{ background: c }}
                      onClick={() => !amsReadonly && setColorCode(c)}
                    />
                  ))}
                </div>
                {/* Selected-color preview bar — always visible once a color is picked. */}
                {colorCode && (
                  <div className="flex items-center gap-[8px] bg-fm-inner rounded-[6px] px-[8px] py-[6px]">
                    <span className="size-[16px] rounded-[3px] border border-white/20 shrink-0" style={{ background: colorCode }} />
                    <span className="text-[11px] leading-[16px] text-fm-text-secondary shrink-0">
                      {isCustomColor ? t('Custom Color') : t('Preset Color')}
                    </span>
                    <span className="text-[11px] leading-[16px] text-fm-text-primary font-mono tracking-wider">{colorCode.toUpperCase()}</span>
                  </div>
                )}
              </div>

              {/* Weight */}
              <div className="flex flex-col gap-[4px]">
                <label className="text-[12px] leading-[19px] text-fm-text-secondary"><span className="text-[#ff2b00]">*</span> {t('Weight')}</label>
                <div className="bg-fm-inner rounded-[6px] p-[8px]">
                  <div className="flex gap-[8px] items-center">
                    <div className="flex flex-col gap-[4px] flex-1">
                      <span className="text-[11px] leading-[16px] text-fm-text-secondary">{t('Total Weight')}</span>
                      <input className="bg-fm-inner2 border-none rounded-[6px] h-[32px] pl-[8px] pr-[4px] text-fm-text-strong text-[12px] leading-[19px] outline-none w-full focus:shadow-[0_0_0_1px_var(--color-fm-brand)]" type="number" placeholder={t('Input Total Weight')} value={totalWeight} onChange={(e) => setTotalWeight(Number(e.target.value) || 0)} disabled={amsReadonly} />
                    </div>
                    <div className="shrink-0 w-[7px] h-[54px] flex flex-col justify-end">
                      <span className="h-[32px] flex items-center justify-center text-[14px] text-fm-text-primary">-</span>
                    </div>
                    <div className="flex flex-col gap-[4px] flex-1">
                      <span className="text-[11px] leading-[16px] text-fm-text-secondary">{t('Spool Weight')}</span>
                      <input className="bg-fm-inner2 border-none rounded-[6px] h-[32px] pl-[8px] pr-[4px] text-fm-text-strong text-[12px] leading-[19px] outline-none w-full focus:shadow-[0_0_0_1px_var(--color-fm-brand)]" type="number" placeholder={t('Input Spool Weight')} value={spoolWeight} onChange={(e) => setSpoolWeight(Number(e.target.value) || 0)} disabled={amsReadonly} />
                    </div>
                    <div className="shrink-0 w-[7px] h-[54px] flex flex-col justify-end">
                      <span className="h-[32px] flex items-center justify-center text-[14px] text-fm-text-primary">=</span>
                    </div>
                    <div className="flex flex-col gap-[4px] flex-1">
                      <span className="text-[11px] leading-[16px] text-fm-text-secondary">{t('Net Weight')}</span>
                      <span className="h-[32px] flex items-center text-[14px] font-medium text-fm-text-primary">{netWeight}g</span>
                    </div>
                  </div>
                </div>
              </div>
            </div>

            {/* 备注 — 唯一与云端同步的扩展字段，直接显示，不再折叠在高级设置里 */}
            <div className="flex flex-col gap-[4px]">
              <label className="text-[12px] leading-[19px] text-fm-text-secondary">{t('Note')}</label>
              <div className="relative">
                <textarea
                  className="bg-fm-inner2 border-none rounded-[6px] h-[110px] px-[12px] pt-[8px] pb-[4px] text-fm-text-strong text-[12px] leading-[19px] outline-none w-full focus:shadow-[0_0_0_1px_var(--color-fm-brand)] resize-none"
                  maxLength={50}
                  placeholder={t('Input Note')}
                  value={note}
                  onChange={(e) => setNote(e.target.value)}
                />
                <span className="absolute right-[12px] bottom-[4px] text-[12px] leading-[19px] text-fm-text-gray">{note.length}/50</span>
              </div>
            </div>
          </div>
        )}

        </div>{/* end p-[24px] content */}
        </div>{/* end scrollable wrapper */}

        {/* Footer */}
        <div className="flex items-center justify-between h-[60px] px-[16px] shrink-0">
          {!isEdit && (
            <div className="flex items-center gap-[4px] w-[106px]">
              <div className="flex-1 flex items-center gap-[4px] bg-fm-inner2 rounded-[6px] h-[24px] pl-[8px] pr-[4px]">
                <span className="text-[12px] leading-[19px] text-fm-text-strong flex-1">{quantity}</span>
                <span className="text-[11px] leading-[16px] text-fm-text-detail">{t('roll')}</span>
              </div>
              <div className="flex flex-col shrink-0 w-[18px] h-[24px]">
                <button className="flex-1 rounded-t-[6px] bg-fm-inner2 border-none cursor-pointer flex items-center justify-center text-fm-text-primary hover:bg-fm-hover" onClick={() => setQuantity(Math.min(99, quantity + 1))}>
                  <svg width="10" height="10" viewBox="0 0 10 10" fill="none"><path d="M2.5 6.5l2.5-3 2.5 3" stroke="currentColor" strokeWidth="1"/></svg>
                </button>
                <button className="flex-1 rounded-b-[6px] bg-fm-inner2 border-none cursor-pointer flex items-center justify-center text-fm-text-primary hover:bg-fm-hover" onClick={() => setQuantity(Math.max(1, quantity - 1))}>
                  <svg width="10" height="10" viewBox="0 0 10 10" fill="none"><path d="M2.5 3.5l2.5 3 2.5-3" stroke="currentColor" strokeWidth="1"/></svg>
                </button>
              </div>
            </div>
          )}
          {isEdit && <div />}
          <div className="flex gap-[12px] items-center">
            <button className="h-[30px] px-[32px] rounded-[8px] cursor-pointer text-[12px] leading-[19px] whitespace-nowrap transition-colors duration-150 bg-fm-input text-fm-text-primary border-none hover:bg-fm-hover" onClick={onClose}>{t('Cancel')}</button>
            <button className="h-[30px] px-[32px] rounded-[8px] border-none cursor-pointer text-[12px] leading-[19px] font-medium whitespace-nowrap transition-colors duration-150 bg-fm-brand text-white hover:bg-fm-brand-hover disabled:opacity-40 disabled:cursor-default" disabled={!isValid} onClick={handleSubmit}>
              {isEdit ? t('Save') : t('Add')}
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}

/* AMS unit icon — 2×2 colored grid */
function AmsUnitIcon({ unit, isActive }: { unit: AmsUnit; isActive: boolean }) {
  const trays = unit.trays || [];
  const colors = [0, 1, 2, 3].map((i) => {
    const t = trays[i];
    return t && t.is_exists && t.color ? t.color : 'rgba(255,255,255,0.1)';
  });
  return (
    <svg width="20" height="20" viewBox="0 0 20 20" fill="none">
      <rect x="1" y="1" width="18" height="18" rx="3"
        stroke={isActive ? 'var(--color-fm-brand)' : 'currentColor'} strokeWidth="1.2" fill="none" />
      <rect x="3" y="3" width="6" height="6" rx="1" fill={colors[0]} />
      <rect x="11" y="3" width="6" height="6" rx="1" fill={colors[1]} />
      <rect x="3" y="11" width="6" height="6" rx="1" fill={colors[2]} />
      <rect x="11" y="11" width="6" height="6" rx="1" fill={colors[3]} />
    </svg>
  );
}
