import { useState, useEffect, useMemo, useRef, useCallback } from 'react';
import { useTranslation } from 'react-i18next';
import type { Spool, PresetVendor, MachineItem, AmsData, AmsUnit, AmsTray } from './types';
import { BAMBU_COLORS, formatTypeSeries } from './constants';
import { SpoolSvg } from './SpoolSvg';
import useStore from '../../store/AppStore';

function normalizeColorCode(value?: string): string {
  const raw = (value || '').trim();
  if (!raw) return '';
  const hex = raw.startsWith('#') ? raw : `#${raw}`;
  return hex.slice(0, 7).toUpperCase();
}

function isPresetColor(value: string): boolean {
  return BAMBU_COLORS.some((c) => c.toUpperCase() === value.toUpperCase());
}

// Derive the AMS tray's *current* net weight in grams from the MQTT payload.
// `tray.weight` is the spool's initial net (e.g. "1000"), and `tray.remain`
// is the AMS-reported remaining percentage. Falls back to the initial net
// when remain% is missing so brand-new or non-RFID spools still show a
// sensible value instead of "—".
function getTrayCurrentNetWeight(tray: AmsTray): number {
  const init = parseInt(String(tray.weight ?? '0'), 10) || 0;
  const remain = typeof tray.remain === 'number' ? tray.remain : 0;
  if (init <= 0) return 0;
  if (remain <= 0) return init;
  return Math.round(init * remain / 100);
}

interface Props {
  open: boolean;
  editingSpool: Spool | null;        // null = add mode
  prefilledSpool: Partial<Spool> | null; // addSimilar prefill
  presets: PresetVendor[];
  onClose: () => void;
  onSubmitAdd: (data: Partial<Spool>, quantity: number) => void;
  onSubmitUpdate: (data: Partial<Spool>) => void;
  onFetchMachines: () => Promise<MachineItem[]>;
  // Ask the currently-selected (or specified) printer to resend its full
  // state package. Bound to the refresh button next to the Printer
  // dropdown. Read-only with respect to the globally selected machine.
  onRequestPushall: (devId?: string) => Promise<boolean>;
  // `switchSelected=true` signals a user-initiated machine switch from
  // this dialog; the C++ side will update Studio's global selected
  // machine. Poll ticks and read-only snapshot refreshes must omit this
  // flag so C++ does not repeatedly call set_selected_machine.
  onFetchAmsData: (devId?: string, switchSelected?: boolean) => Promise<AmsData | null>;
}

export function AddEditDialog({
  open, editingSpool, prefilledSpool, presets, onClose,
  onSubmitAdd, onSubmitUpdate, onFetchMachines, onRequestPushall, onFetchAmsData,
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
  const [customColors, setCustomColors] = useState<string[]>([]);
  const [colorName, setColorName] = useState('');
  // STUDIO-17991: align the weight form with the cloud schema
  // (design-user.api CreateFilamentV2Req: netWeight + totalNetWeight only).
  // The legacy "Total - Spool = Net" three-column UI leaked spool_weight
  // even though the cloud never round-trips it, which let pull-push cycles
  // strip 料盘 and leave the Remain column denominator drifting to
  // 1250 / 1013 / 995. Keep the form in the cloud's net-weight domain
  // end to end: 当前净重 ↔ net_weight, 总净重 ↔ initial_weight.
  const [totalNetWeight, setTotalNetWeight] = useState(1000);
  const [currentNetWeight, setCurrentNetWeight] = useState(1000);
  const [note, setNote] = useState('');
  const [quantity, setQuantity] = useState(1);

  // AMS state
  const [machines, setMachines] = useState<MachineItem[]>([]);
  const [amsData, setAmsData] = useState<AmsData | null>(null);
  const [selectedUnit, setSelectedUnit] = useState<string | null>(null);
  const [selectedSlot, setSelectedSlot] = useState<{ ams_id: string; slot_id: string; tray: AmsTray } | null>(null);
  const [amsLoading, setAmsLoading] = useState(false);
  const [amsError, setAmsError] = useState('');

  // Reset form when dialog opens
  useEffect(() => {
    if (!open) return;
    setMode('manual');
    setQuantity(1);
    setSelectedSlot(null);
    setSelectedUnit(null);
    setAmsData(null);
    setAmsError('');
    if (initSpool) {
      setBrand(initSpool.brand || '');
      setMaterialType(initSpool.material_type || '');
      setSeries(initSpool.series || '');
      const initialColor = normalizeColorCode(initSpool.color_code);
      setColorCode(initialColor);
      setCustomColors(initialColor && !isPresetColor(initialColor) ? [initialColor] : []);
      setColorName(initSpool.color_name || '');
      // Transparent migration from the legacy 毛重/料盘/净重 shape:
      // old rows stored initial_weight as 毛重 with spool_weight > 0,
      // new rows store initial_weight as the pure 整卷净重 with
      // spool_weight == 0. Collapsing "initial - spool" yields the
      // correct 整卷净重 in either case, and on save the row is rewritten
      // in the normalised shape so the next edit is already clean.
      const legacySpool = initSpool.spool_weight || 0;
      const netInit = Math.max(0, (initSpool.initial_weight || 0) - legacySpool);
      setTotalNetWeight(netInit > 0 ? netInit : 1000);
      const pct = initSpool.remain_percent || 0;
      const curNet = (typeof initSpool.net_weight === 'number' && initSpool.net_weight > 0)
        ? Math.round(initSpool.net_weight)
        : Math.round(netInit * pct / 100);
      setCurrentNetWeight(curNet > 0 ? curNet : (netInit > 0 ? netInit : 1000));
      setNote(initSpool.note || '');
    } else {
      setBrand(''); setMaterialType(''); setSeries('');
      setColorCode(''); setCustomColors([]); setColorName('');
      setTotalNetWeight(1000); setCurrentNetWeight(1000);
      setNote('');
    }
  }, [open]);

  // UX decision (per F4.3 feedback, revised):
  //   收起 Type / Series 两个独立字段，合并为单个「耗材类型」<select>，
  //   选项直接显示云端 `get_filament_config` 返回的 filamentName 完整名
  //   （例如 "PLA Basic" / "PETG Translucent" / "PLA-S Support For PLA/PETG"）。
  //   这样下拉值与云端创建/更新耗材接口的 filamentName 字段语义一致，不再
  //   依赖本地 preset 的 "type + series[]" 拼接。本地 preset 仅作为云端
  //   config 尚未拉回来时的 fallback 兜底。
  const typeSeriesOptions = useMemo<string[]>(() => {
    const set = new Set<string>();

    const settings = cloudConfig?.filamentSettings;
    if (Array.isArray(settings)) {
      settings.forEach((it) => {
        if (!it) return;
        if (brand && it.filamentVendor !== brand) return;
        const name = (it.filamentName || '').trim();
        if (name) set.add(name);
      });
    }

    if (set.size === 0) {
      const vendors = brand ? presets.filter((v) => v.name === brand) : presets;
      vendors.forEach((v) => {
        v.types.forEach((tp) => {
          const series = Array.isArray(tp.series) ? tp.series.filter(Boolean) : [];
          if (series.length === 0) {
            if (tp.name) set.add(tp.name);
          } else {
            series.forEach((s) => set.add(formatTypeSeries(tp.name, s)));
          }
        });
      });
    }
    return [...set].sort();
  }, [brand, cloudConfig, presets]);

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

  // Material Type 下拉当前值 = 云端 filamentName（本地 series）。不再做 type+series
  // 前缀拼接；对于云端 filamentName = "Support For PA/PET" 且 filamentType = "PA"
  // 这类"series 不含 type 前缀"的情况，拼接会错渲染成 "PA Support For PA/PET"。
  // series 为空时退回到 material_type（覆盖仅有 type 无 series 的简单场景）。
  const typeSeriesFull = (series || '').trim() || (materialType || '').trim();

  // 云端 filamentSettings 里和当前 brand + series（filamentName）对上的那条记录，
  // 用于取权威的 filamentId 作为本地 setting_id（与云端 POST/PUT 的 filamentId 字段
  // 对齐，也便于下次 pull 回来时 preset 关联一致）。
  const matchedCloudFilamentId = useMemo(() => {
    const settings = cloudConfig?.filamentSettings;
    if (!Array.isArray(settings)) return '';
    const name = (series || '').trim();
    if (!name || !brand) return '';
    const hit = settings.find(
      (it) => it && it.filamentVendor === brand && (it.filamentName || '').trim() === name,
    );
    return (hit?.filamentId || '').trim();
  }, [cloudConfig, brand, series]);

  // 语义统一：本地 series 直接对齐云端 filamentName 的完整名（如 "PLA Basic"）。
  // preset 里 items[].series 历史上是短名（"Basic"），因此比对时同时接受
  // 短名和完整名两种形态，保证新老数据都能匹配到 preset。
  const matchedPresetItem = useMemo(() => {
    for (const vendor of presets) {
      if (vendor.name !== brand) continue;
      for (const tp of vendor.types) {
        if (tp.name !== materialType) continue;
        const item = tp.items?.find((entry) => {
          const presetSeries = entry.series || '';
          return presetSeries === (series || '')
              || formatTypeSeries(tp.name, presetSeries) === (series || '')
              || presetSeries === formatTypeSeries(tp.name, series);
        });
        if (item) return item;
      }
    }
    return null;
  }, [presets, brand, materialType, series]);

  // 下拉切换时：
  //  - series 直接存下拉选中的完整 filamentName（例如 "PLA Basic"）。这与
  //    云端 PUT/POST 的 filamentName 字段语义一致，避免本地短名上云后丢类型前缀。
  //  - material_type 优先从云端 filamentSettings 查该 filamentName 对应的
  //    filamentType（云端是权威来源，能覆盖形如 "PLA-S Support For PLA/PETG"
  //    这种本地 anchor 匹配会误切的复杂名称）。云端没命中再退回本地 preset
  //    的前缀 anchor 匹配。
  const handleTypeSeriesChange = (full: string) => {
    const name = (full || '').trim();
    let type = '';
    const settings = cloudConfig?.filamentSettings;
    if (Array.isArray(settings)) {
      const hit = settings.find(
        (it) => it && it.filamentVendor === brand && (it.filamentName || '').trim() === name,
      );
      if (hit) type = (hit.filamentType || '').trim();
    }
    if (!type) {
      const res = splitTypeSeries(name);
      type = res.type;
    }
    setMaterialType(type);
    setSeries(name);
  };

  // F4.5: validation no longer depends on `series` — the combined type field
  // covers both, and plenty of materials legitimately have no series (e.g. ABS).
  // Weight rule: both values must be positive and 当前 ≤ 总 so the derived
  // remain_percent never goes negative / > 100.
  const isValid = !!(
    brand && materialType && colorCode &&
    totalNetWeight > 0 && currentNetWeight >= 0 &&
    currentNetWeight <= totalNetWeight
  );

  // F4.4: whether the currently picked colour is a user-defined one (not in
  // the preset BAMBU_COLORS palette). Used to render a custom-colour preview.
  const isCustomColor = !!colorCode && !BAMBU_COLORS.some(
    (c) => c.toUpperCase() === colorCode.toUpperCase(),
  );

  const handleCustomColorChange = useCallback((value: string) => {
    const nextColor = normalizeColorCode(value);
    setColorCode(nextColor);
    if (!nextColor || isPresetColor(nextColor)) return;
    setCustomColors((prev) => (
      prev.some((c) => c.toUpperCase() === nextColor.toUpperCase())
        ? prev
        : [...prev, nextColor]
    ));
  }, []);

  const handleSubmit = () => {
    // New weight model: initial_weight is now 整卷净重 (= swagger
    // totalNetWeight) and spool_weight is always persisted as 0 so no
    // code path can re-introduce 毛重 via cloud round-trips. remain_percent
    // is recomputed from 当前净重 / 总净重 so the three weight-related
    // fields (net_weight, initial_weight, remain_percent) are always
    // consistent on write.
    const remainPct = totalNetWeight > 0
      ? Math.min(100, Math.max(0, Math.round(currentNetWeight * 100 / totalNetWeight)))
      : 0;
    const data: Partial<Spool> = {
      brand, material_type: materialType, series,
      color_code: colorCode, color_name: colorName,
      initial_weight: totalNetWeight,
      spool_weight: 0,
      net_weight: currentNetWeight,
      remain_percent: remainPct,
      note,
      setting_id: matchedCloudFilamentId || matchedPresetItem?.filament_id || initSpool?.setting_id || '',
    };

    if (isEdit) {
      // STUDIO-17964: 编辑保存时只提交用户此次真正改过的字段。
      // - 空 patch 提交到 C++ 侧会被安全地忽略（不发空 PUT）。
      // - 与 initSpool 对齐未变的字段一律跳过；数字字段用 Object.is 比较避免
      //   NaN / +0 / -0 的歧义；字符串字段用 normalizeStr 把 null/undefined 视为
      //   空串，避免"本来就没有值"被误报成"改成了空"。
      const orig = editingSpool!;
      const normalizeStr = (v: unknown) =>
        (v === undefined || v === null) ? '' : String(v);
      const patch: Partial<Spool> = { spool_id: orig.spool_id };
      (Object.keys(data) as (keyof Spool)[]).forEach((k) => {
        const nv = (data as any)[k];
        const ov = (orig as any)[k];
        let changed: boolean;
        if (typeof nv === 'string' || typeof ov === 'string') {
          changed = normalizeStr(nv) !== normalizeStr(ov);
        } else {
          changed = !Object.is(nv, ov);
        }
        if (changed) (patch as any)[k] = nv;
      });
      onSubmitUpdate(patch);
    } else {
      if (mode === 'ams' && selectedSlot) {
        // The spool is imported from a live AMS slot. Propagate the
        // authoritative AMS-only fields (tag_uid / setting_id / diameter
        // / remain / binding) so the local record knows which physical
        // slot it came from. Weights come from the form state the user
        // confirmed (already baked into `data` above) but we override
        // `initial_weight` with the tray's reported 整卷净重 and
        // `remain_percent` with the authoritative device value so the
        // record lines up with what the printer reports on MQTT.
        const tray = selectedSlot.tray;
        const trayNetInit = parseInt(String(tray.weight ?? '0'), 10) || 0;
        const remain = typeof tray.remain === 'number' ? tray.remain : 0;
        data.entry_method = 'ams_sync';
        data.tag_uid = tray.tag_uid || '';
        data.setting_id = tray.setting_id || data.setting_id || '';
        data.bound_ams_id = selectedSlot.ams_id;
        data.bound_dev_id = amsData?.selected_dev_id || '';
        data.remain_percent = remain;
        if (trayNetInit > 0) {
          // tray.weight is already the spool's 整卷净重 (MQTT `tray_weight`,
          // see DevFilaSystem::weight); store it verbatim — no料盘 fudge.
          data.initial_weight = trayNetInit;
        }
        // `tray.diameter` is a string in the MQTT payload; accept both.
        const diaRaw = tray.diameter;
        const diaNum = typeof diaRaw === 'number' ? diaRaw : parseFloat(String(diaRaw ?? '0'));
        if (diaNum > 0) data.diameter = diaNum;
      } else {
        data.entry_method = 'manual';
      }
      onSubmitAdd(data, mode === 'ams' ? 1 : quantity);
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
      // If the default machine already matches Studio's globally selected
      // machine, skip the set and just read that machine's AMS snapshot.
      // Only when we fall back to "first online / first in the list" do
      // we ask C++ to switch once so the tray list is not empty. This is
      // a one-shot soft switch at dialog open, not a recurring trigger.
      const needSwitch = !globalSelectedDev || defaultDev.dev_id !== globalSelectedDev;
      const data = await onFetchAmsData(defaultDev.dev_id, needSwitch);
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

  // switchSelected=true : user picked a different printer in the dropdown.
  //   This is the only path that should make C++ call
  //   DeviceManager::set_selected_machine.
  // switchSelected=false : mirror of an external Studio-wide machine
  //   switch. C++ has already been switched by OnSelectedMachineChanged,
  //   so we just reset the local selection state and re-read the latest
  //   AMS snapshot without asking C++ to switch again.
  const handleDeviceChange = useCallback(async (
    devId: string,
    switchSelected: boolean = true,
  ) => {
    setSelectedUnit(null);
    setSelectedSlot(null);
    setAmsLoading(true);
    setAmsError('');
    try {
      const data = await onFetchAmsData(switchSelected ? devId : undefined, switchSelected);
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

  // Refresh button next to the Printer dropdown. Asks the selected
  // printer to resend its full state (get_version + pushall) so the AMS
  // tray snapshot in this dialog converges immediately instead of
  // waiting for the next spontaneous push from the device. Throttled for
  // ~2s to avoid hammering the printer on rapid double-clicks; the
  // button stays disabled while the throttle window is active.
  const [refreshBusy, setRefreshBusy] = useState(false);
  const handleRequestPushall = useCallback(async () => {
    if (refreshBusy) return;
    const devId = amsData?.selected_dev_id
      || machines.find((m) => m.is_online)?.dev_id
      || machines[0]?.dev_id
      || '';
    if (!devId) return;
    setRefreshBusy(true);
    try {
      await onRequestPushall(devId);
    } catch {
      // non-fatal: the next AMS poll tick will still pick up whatever
      // the printer eventually pushes back on its own.
    }
    window.setTimeout(() => setRefreshBusy(false), 2000);
  }, [refreshBusy, amsData?.selected_dev_id, machines, onRequestPushall]);

  // F4.7: follow Studio-wide machine switches while the dialog is open on
  // the AMS tab. If the user picks a different printer elsewhere in
  // Studio, mirror that selection here instead of staying on whichever
  // printer was active when the dialog opened.
  useEffect(() => {
    if (!open || mode !== 'ams' || amsLoading) return;
    if (!globalSelectedDev) return;
    const currentDev = amsData?.selected_dev_id || '';
    // Skip the mirror while we don't have a known snapshot yet. The
    // 1.5s poll can momentarily return an empty selected_dev_id when
    // DeviceManager::get_selected_machine is not ready; without this
    // guard the mirror would keep firing handleDeviceChange, which
    // toggles amsLoading and makes the whole AMS pane flicker.
    if (!currentDev) return;
    if (currentDev === globalSelectedDev) return;
    // Only follow if the new machine is actually in the dropdown list we
    // currently show; otherwise leave the user in their chosen state.
    if (!machines.some((m) => m.dev_id === globalSelectedDev)) return;
    // Mirror-only: the external switch has already called
    // set_selected_machine on the C++ side, so we just sync the UI and
    // re-read the AMS snapshot instead of triggering another set.
    void handleDeviceChange(globalSelectedDev, false);
    // Depend on the scalar `selected_dev_id` instead of the whole
    // amsData object — the 1.5s poll creates a new object reference on
    // every tick even when selected_dev_id doesn't actually change, and
    // re-running this effect on every tick is what caused the flicker.
  }, [open, mode, amsLoading, globalSelectedDev, amsData?.selected_dev_id, machines, handleDeviceChange]);

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
  // Mirror amsData into a ref so the poll tick can diff the incoming
  // snapshot against the latest value without having to list amsData in
  // the effect's dep array (which would reset the interval timer).
  const amsDataRef = useRef<AmsData | null>(amsData);
  amsDataRef.current = amsData;
  // STUDIO-17989: also mirror selectedUnit / amsError so the poll tick
  // can self-heal the "first snapshot after machine switch was empty"
  // case without adding selectedUnit / amsError to the effect deps
  // (which would restart the 1.5s interval on every selection change
  // and flicker the dialog).
  const selectedUnitRef = useRef<string | null>(selectedUnit);
  selectedUnitRef.current = selectedUnit;
  const amsErrorRef = useRef<string>(amsError);
  amsErrorRef.current = amsError;
  useEffect(() => {
    if (!open || mode !== 'ams' || amsLoading) return;
    const devId = amsData?.selected_dev_id || '';
    if (!devId) return;

    let cancelled = false;
    const tick = async () => {
      try {
        // Read-only poll: omit both dev_id and switch_selected so C++
        // returns the AMS snapshot of the currently selected machine
        // without calling set_selected_machine. The previous version
        // passed dev_id here and produced a "set current printer" log
        // entry on every 1.5s tick.
        const data = await onFetchAmsData();
        if (cancelled || !data) return;

        // Coalesce identical snapshots. C++ re-builds ams_units from the
        // latest MachineObject state every poll, so we get a fresh object
        // reference on every tick even when nothing actually changed.
        // Pushing that through setAmsData would rerender the dialog and
        // restart every effect that depends on amsData, which is what
        // caused the visible flicker of the AMS pane. Only update state
        // when the serialized payload actually differs.
        const prev = amsDataRef.current;
        const same = prev && JSON.stringify(prev) === JSON.stringify(data);
        if (same) return;

        setAmsData(data);

        // STUDIO-17989: self-heal the "switched to an AMS-equipped printer
        // but the first snapshot landed with empty ams_units" case. In
        // switchToAms / handleDeviceChange we optimistically set
        //   amsError = "No AMS detected on this device"
        //   selectedUnit = null
        // whenever the initial fetch returns zero units, but the device
        // MQTT push often arrives one tick later. Once a subsequent poll
        // actually sees AMS units, drop the stale banner and auto-select
        // the first unit so the slot list renders without the user
        // having to click the AMS icon manually.
        if (data.ams_units.length > 0) {
          if (amsErrorRef.current) setAmsError('');
          if (selectedUnitRef.current == null) {
            setSelectedUnit(data.ams_units[0].ams_id);
          }
        }

        // Keep the currently highlighted slot pointing at the latest tray
        // object so downstream renderers (form, palette) reflect the new
        // MQTT snapshot. If the tray disappeared (user pulled the spool),
        // clear the selection so stale form values can't be submitted.
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

  // Live-refresh the Printer dropdown while the AMS tab is open so
  // devices that come online / go offline / get (un)bound appear without
  // the user having to close and reopen the dialog. Runs at a lower rate
  // than the AMS tray poll because online state changes far less often
  // than tray RFID / weight / remain. Like the AMS tick this diffs
  // against the last payload and skips setState when nothing changed,
  // otherwise every tick would rerender the dialog for no reason.
  const machinesRef = useRef<MachineItem[]>(machines);
  machinesRef.current = machines;
  useEffect(() => {
    if (!open || mode !== 'ams' || amsLoading) return;
    let cancelled = false;
    const tick = async () => {
      try {
        const list = await onFetchMachines();
        if (cancelled || !Array.isArray(list)) return;
        const prev = machinesRef.current;
        if (JSON.stringify(prev) === JSON.stringify(list)) return;
        setMachines(list);
      } catch {
        // Poll errors are non-fatal; the next tick will retry.
      }
    };
    const timer = window.setInterval(tick, 3000);
    return () => {
      cancelled = true;
      window.clearInterval(timer);
    };
  }, [open, mode, amsLoading, onFetchMachines]);

  const selectAmsSlot = (unit: AmsUnit, tray: AmsTray) => {
    setSelectedSlot({ ams_id: unit.ams_id, slot_id: tray.slot_id, tray });
    // Historically we locked the form down when the tray reported an RFID
    // tag_uid (BBL original spool) so users could not accidentally change
    // authoritative fields. Product direction has since changed: the
    // Add-from-AMS flow must let the user correct every field before
    // saving (e.g. weigh the spool on a scale to override tray remain).
    // tag_uid is still captured in handleSubmit, only the read-only UI
    // gate has been removed.

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
    // series 统一存完整 filamentName（含 type 前缀），与云端 filamentName
    // 语义一致。这里拼接时走 formatTypeSeries 去重，避免 "ABS ABS" / "PLA PLA Basic"。
    setSeries(formatTypeSeries(typeName, seriesName));

    // F4.8: color may arrive as #RRGGBBAA (BBL firmware appends alpha for
    // transparent filaments). Strip alpha so the palette match and the hex
    // label stay canonical 6-digit HEX.
    const rawColor = tray.color || '';
    const sanitizedColor = /^#[0-9a-fA-F]{8}$/.test(rawColor)
      ? rawColor.substring(0, 7)
      : rawColor;
    if (sanitizedColor) setColorCode(sanitizedColor);

    // Seed both weight inputs from the AMS tray report. `tray.weight` is
    // the spool's original 整卷净重 (MQTT `tray_weight`, always net); the
    // current net is `tray.weight * tray.remain%` via getTrayCurrentNetWeight.
    // No more 料盘 bookkeeping on this path — the cloud schema never stored
    // it anyway.
    const trayNetInit = parseInt(String(tray.weight ?? '0'), 10) || 0;
    const currentNet = getTrayCurrentNetWeight(tray);
    setTotalNetWeight(trayNetInit > 0 ? trayNetInit : 1000);
    setCurrentNetWeight(currentNet > 0 ? currentNet : (trayNetInit > 0 ? trayNetInit : 1000));
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
                <div className="flex items-center gap-[8px]">
                  <select
                    className="flex-1 min-h-[36px] rounded-[6px] bg-fm-inner2 px-[8px] text-fm-text-strong text-xs outline-none focus:shadow-[0_0_0_1px_var(--color-fm-brand)] fm-select-arrow cursor-pointer disabled:cursor-not-allowed disabled:opacity-60"
                    value={machines.length ? (amsData?.selected_dev_id || machines[0]?.dev_id || '') : ''}
                    disabled={machines.length === 0}
                    onChange={(e) => { if (e.target.value) void handleDeviceChange(e.target.value); }}
                  >
                    {machines.length === 0 ? (
                      <option value="">{t('No printers — sign in and bind a device')}</option>
                    ) : (
                      machines.map((m) => (
                        // Label follows SelectMachineDialog's "<dev_name>(LAN)"
                        // convention; online/offline state is conveyed by the
                        // AMS snapshot state in the pane below, not by the
                        // dropdown text.
                        <option key={m.dev_id} value={m.dev_id}>
                          {m.dev_name || m.dev_id}{m.is_lan ? '(LAN)' : ''}
                        </option>
                      ))
                    )}
                  </select>
                  <button
                    type="button"
                    className="shrink-0 w-[32px] h-[32px] rounded-[6px] border border-fm-border-focus bg-fm-inner2 text-fm-text-secondary flex items-center justify-center hover:text-fm-text-strong hover:border-fm-text-secondary disabled:cursor-not-allowed disabled:opacity-50 transition-colors"
                    title={t('Refresh printer status')}
                    aria-label={t('Refresh printer status')}
                    disabled={machines.length === 0 || refreshBusy}
                    onClick={() => { void handleRequestPushall(); }}
                  >
                    <svg width="14" height="14" viewBox="0 0 14 14" fill="none" className={refreshBusy ? 'animate-spin' : ''}>
                      <path d="M11.5 3.5A5 5 0 1 0 12 8" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" fill="none"/>
                      <path d="M12 2v3h-3" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" strokeLinejoin="round" fill="none"/>
                    </svg>
                  </button>
                </div>
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
                              {/* Show the *remaining* net weight in each
                                  slot card (initial_net * remain%) so the
                                  user sees the live AMS reading, not the
                                  spool's brand-new / factory value. */}
                              <div className="text-[12px] leading-[19px] text-fm-text-secondary">
                                {(() => {
                                  const cur = getTrayCurrentNetWeight(tray);
                                  return cur > 0 ? `${cur}g` : '—';
                                })()}
                              </div>
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
                  <select className="bg-fm-inner2 border-none rounded-[6px] h-[32px] pl-[8px] pr-[4px] text-fm-text-strong text-[12px] leading-[19px] outline-none w-full focus:shadow-[0_0_0_1px_var(--color-fm-brand)] fm-select-arrow cursor-pointer" value={brand} onChange={(e) => { setBrand(e.target.value); setMaterialType(''); setSeries(''); }}>
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
                    disabled={!brand}
                  >
                    <option value="">{!brand ? t('Select Brand First') : t('Select Type')}</option>
                    {/* Edit 场景兜底：本地 spool 的 material_type/series 组合可能来源于
                        AMS 同步、自定义添加、或更早版本的 presets 数据，不一定出现在当前
                        brand 的 typeSeriesOptions 里。没有这个 fallback option 时 <select>
                        找不到匹配项会回落到 placeholder "Select Type"，造成编辑时看上去
                        "耗材类型未展示"。把当前值单独补一条，保证可回显、可修改。*/}
                    {typeSeriesFull && !typeSeriesOptions.includes(typeSeriesFull) && (
                      <option value={typeSeriesFull}>{typeSeriesFull}</option>
                    )}
                    {typeSeriesOptions.map((n) => <option key={n} value={n}>{n}</option>)}
                  </select>
                </div>
              </div>

              {/* Color palette. F4.4 feedback: 自定义颜色需要"可保存 / 能看到已选"。
                  "+" 始终保留为取色入口；新取的自定义色追加到预设色之后。 */}
              <div className="flex flex-col gap-[8px]">
                <label className="text-[12px] leading-[19px] text-fm-text-secondary"><span className="text-[#ff2b00]">*</span> {t('Color')}</label>
                <div className="flex flex-wrap gap-[6px] items-center">
                  {/* Custom-color picker: always keep "+" as the entry point. */}
                  <div
                    className="size-[24px] rounded-[4px] cursor-pointer border border-dashed border-fm-border-focus bg-fm-inner relative overflow-hidden flex items-center justify-center text-fm-text-detail text-sm hover:border-fm-text-secondary"
                    title={t('Pick Custom Color')}
                  >
                    <svg width="14" height="14" viewBox="0 0 14 14" fill="none"><path d="M7 2v10M2 7h10" stroke="currentColor" strokeWidth="1.2"/></svg>
                    <input
                      className="absolute inset-0 opacity-0 cursor-pointer"
                      type="color"
                      value={colorCode || '#000000'}
                      onChange={(e) => handleCustomColorChange(e.target.value)}
                    />
                  </div>
                  {BAMBU_COLORS.map((c) => (
                    <div
                      key={c}
                      className={`size-[24px] rounded-[4px] cursor-pointer border transition-colors duration-150 hover:border-fm-text-secondary ${c.toUpperCase() === colorCode.toUpperCase() ? 'border-fm-brand border-2' : 'border-fm-border-focus'}`}
                      style={{ background: c }}
                      onClick={() => setColorCode(c)}
                    />
                  ))}
                  {customColors.map((c) => (
                    <div
                      key={`custom-${c}`}
                      className={`size-[24px] rounded-[4px] cursor-pointer border transition-colors duration-150 hover:border-fm-text-secondary ${c.toUpperCase() === colorCode.toUpperCase() ? 'border-fm-brand border-2' : 'border-fm-border-focus'}`}
                      style={{ background: c }}
                      title={t('Custom Color')}
                      onClick={() => setColorCode(c)}
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

              {/* Weight — swagger-aligned two-field model
                  (design-user.api: netWeight + totalNetWeight). The old
                  三列 "Total - Spool = Net" UX was dropped because the
                  cloud never round-trips spool_weight, so it only ever
                  confused the Remain column. */}
              <div className="flex flex-col gap-[4px]">
                <label className="text-[12px] leading-[19px] text-fm-text-secondary"><span className="text-[#ff2b00]">*</span> {t('Weight')}</label>
                <div className="bg-fm-inner rounded-[6px] p-[8px]">
                  <div className="flex gap-[8px] items-center">
                    <div className="flex flex-col gap-[4px] flex-1">
                      <span className="text-[11px] leading-[16px] text-fm-text-secondary">{t('Current Net Weight')}</span>
                      {/* type=number inputs bound to a number state in
                          React keep showing "0" after the user deletes
                          the last digit (Number("") -> 0), so the next
                          keystroke appends "1" and you get "01". Select
                          all text on focus so typing always replaces the
                          previous value, matching the usual UX for
                          quantity-style fields. */}
                      <input className="bg-fm-inner2 border-none rounded-[6px] h-[32px] pl-[8px] pr-[4px] text-fm-text-strong text-[12px] leading-[19px] outline-none w-full focus:shadow-[0_0_0_1px_var(--color-fm-brand)]" type="number" placeholder={t('Input Current Net Weight')} value={currentNetWeight} onFocus={(e) => e.target.select()} onChange={(e) => setCurrentNetWeight(Number(e.target.value) || 0)} />
                    </div>
                    <div className="flex flex-col gap-[4px] flex-1">
                      <span className="text-[11px] leading-[16px] text-fm-text-secondary">{t('Total Net Weight')}</span>
                      {/* Total Net Weight is locked on edit: it is the
                          spool's factory full-weight and must not drift
                          after a row exists — only Current Net Weight
                          tracks consumption over time. */}
                      <input className={`bg-fm-inner2 border-none rounded-[6px] h-[32px] pl-[8px] pr-[4px] text-fm-text-strong text-[12px] leading-[19px] outline-none w-full focus:shadow-[0_0_0_1px_var(--color-fm-brand)] ${isEdit ? 'opacity-60 cursor-not-allowed' : ''}`} type="number" placeholder={t('Input Total Net Weight')} value={totalNetWeight} readOnly={isEdit} disabled={isEdit} onFocus={(e) => e.target.select()} onChange={(e) => setTotalNetWeight(Number(e.target.value) || 0)} />
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
          {!isEdit && mode === 'manual' && (
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
          {(isEdit || mode !== 'manual') && <div />}
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
