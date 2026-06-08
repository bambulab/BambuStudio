// STUDIO-17977 dev-only mock bridge.
//
// Only loaded when `import.meta.env.DEV` is truthy AND no webview host is
// detected. Production builds (`pnpm build`) drop this file via tree-shaking
// (the entry guard short-circuits on a build-time constant). Inside the
// device shell (Edge-WebView2 / WKWebView) the shim is bypassed because
// `window.chrome.webview` is already there.
//
// Goal: feed the React layer enough vendors / spools / candidates / AMS data
// to drive the AddEditDialog through the four follow-up requirements:
//   1. multi-hex preview row,
//   2. BBL official colour code surfacing,
//   3. type-aggregated fallback palette for non-official spools,
//   4. brand/material/series/color reactivity.
// The shape mirrors what FilamentManagerVM produces; nothing here is meant to
// match real product IDs.

import type {
  Spool,
  PresetOptions,
  AmsData,
  CandidateColor,
  CloudSyncState,
  CloudFilamentConfig,
} from '../features/filament-manager/types';

interface RequestPacket {
  head: { version: string; type: string; seq: number; ts: number };
  body: { module: string; submod: string; action: string; payload?: Record<string, unknown> };
}

const SDK_VERSION = '1.0';
const mockAmsFilamentHotendState: Record<string, unknown> = {};

// --- Mock catalogue ---------------------------------------------------------

const PRESETS: PresetOptions = {
  vendors: [
    {
      name: 'Bambu Lab',
      types: [
        {
          name: 'PLA Basic',
          series: ['Basic', 'Translucent'],
          items: [
            { series: 'Basic',       filament_id: 'GFL00', setting_id: 'BBL_PLA_BASIC' },
            { series: 'Translucent', filament_id: 'GFL01', setting_id: 'BBL_PLA_TRANSLUCENT' },
          ],
        },
        {
          name: 'PETG Basic',
          series: ['Basic'],
          items: [
            { series: 'Basic', filament_id: 'GFG00', setting_id: 'BBL_PETG_BASIC' },
          ],
        },
      ],
    },
    {
      name: 'Generic',
      types: [
        {
          name: 'PLA Basic',
          series: ['Basic'],
          items: [
            { series: 'Basic', filament_id: 'GFL10', setting_id: 'GEN_PLA_BASIC' },
          ],
        },
        {
          name: 'PETG Basic',
          series: ['Basic'],
          items: [
            { series: 'Basic', filament_id: 'GFG10', setting_id: 'GEN_PETG_BASIC' },
          ],
        },
      ],
    },
    {
      // Non-official brand: every "Acme3D" spool is non-official, so the
      // candidate panel should fall back to type-aggregated colours from
      // the two vendors above.
      name: 'Acme3D',
      types: [
        {
          name: 'PLA Basic',
          series: ['Pro'],
          items: [
            // Note: empty setting_id forces filaId resolver to fall back to
            // filament_id, then to '' if even that is missing - mirrors how
            // the device flags non-system vendors.
            { series: 'Pro', filament_id: '', setting_id: '', is_user: true },
          ],
        },
      ],
    },
  ],
};

// Per-fila_id candidates returned by `filament.colors.query_for_id`.
// Use deliberately mixed cases (single / gradient / multicolor) to exercise
// every branch of the swatch / preview rendering.
const CANDIDATES_BY_FILA_ID: Record<string, CandidateColor[]> = {
  // BBL PLA Basic - 3 single + 1 multicolor.
  BBL_PLA_BASIC: [
    { color_code: '10100', name: '亮橙 Bright Orange', color_type: 2, colors: ['#FF911A'] },
    { color_code: '10101', name: '青柠 Lime',          color_type: 2, colors: ['#A4F84B'] },
    { color_code: '10102', name: '海军蓝 Navy',        color_type: 2, colors: ['#1B3F8B'] },
    { color_code: '13903', name: '蓝粉双色 Blue/Pink', color_type: 1, colors: ['#3F8BFF', '#FF5BB0'] },
  ],
  // BBL PLA Translucent - 2 single + 1 gradient.
  BBL_PLA_TRANSLUCENT: [
    { color_code: '10200', name: '雾蓝 Frost Blue',  color_type: 2, colors: ['#B7D9F2'] },
    { color_code: '10201', name: '雾粉 Frost Pink',  color_type: 2, colors: ['#F2C2D6'] },
    { color_code: '13700', name: '渐变珊瑚 Coral',   color_type: 0, colors: ['#FF7B5A', '#FFD18C'] },
  ],
  // BBL PETG Basic - 2 single.
  BBL_PETG_BASIC: [
    { color_code: '20100', name: '黑 Black', color_type: 2, colors: ['#1A1A1A'] },
    { color_code: '20101', name: '白 White', color_type: 2, colors: ['#F2F2F2'] },
  ],
  // Generic PLA Basic - 1 single.
  GEN_PLA_BASIC: [
    { color_code: 'GEN001', name: '通用红 Red', color_type: 2, colors: ['#D02A2A'] },
  ],
  // Generic PETG Basic - 1 single.
  GEN_PETG_BASIC: [
    { color_code: 'GEN002', name: '通用绿 Green', color_type: 2, colors: ['#2AA84F'] },
  ],
};

const FILA_TYPE_BY_FILA_ID: Record<string, string> = {
  BBL_PLA_BASIC: 'PLA Basic',
  BBL_PLA_TRANSLUCENT: 'PLA Basic',
  BBL_PETG_BASIC: 'PETG Basic',
  GEN_PLA_BASIC: 'PLA Basic',
  GEN_PETG_BASIC: 'PETG Basic',
};

// Five spools: covers single / gradient / multicolor / non-official / empty
// state, so SpoolTable / DetailDialog / AddEditDialog edit paths all have
// something to bind against.
let SPOOLS: Spool[] = [
  {
    spool_id: 'sp-1', brand: 'Bambu Lab', material_type: 'PLA Basic', series: 'Basic',
    color_code: '#FF911A', color_name: '亮橙 Bright Orange',
    colors: ['#FF911A'], color_type: 2,
    diameter: 1.75, initial_weight: 1000, net_weight: 850, spool_weight: 0,
    remain_percent: 85, status: 'active', favorite: true, entry_method: 'manual', note: '',
    setting_id: 'BBL_PLA_BASIC',
  },
  {
    spool_id: 'sp-2', brand: 'Bambu Lab', material_type: 'PLA Basic', series: 'Basic',
    color_code: '#3F8BFF', color_name: '蓝粉双色 Blue/Pink',
    colors: ['#3F8BFF', '#FF5BB0'], color_type: 1,
    diameter: 1.75, initial_weight: 1000, net_weight: 720, spool_weight: 0,
    remain_percent: 72, status: 'active', favorite: false, entry_method: 'manual', note: '',
    setting_id: 'BBL_PLA_BASIC',
  },
  {
    spool_id: 'sp-3', brand: 'Bambu Lab', material_type: 'PLA Basic', series: 'Translucent',
    color_code: '#FF7B5A', color_name: '渐变珊瑚 Coral',
    colors: ['#FF7B5A', '#FFD18C'], color_type: 0,
    diameter: 1.75, initial_weight: 1000, net_weight: 600, spool_weight: 0,
    remain_percent: 60, status: 'active', favorite: false, entry_method: 'manual', note: '',
    setting_id: 'BBL_PLA_TRANSLUCENT',
  },
  {
    spool_id: 'sp-4', brand: 'Generic', material_type: 'PLA Basic', series: 'Basic',
    color_code: '#D02A2A', color_name: '通用红 Red',
    colors: ['#D02A2A'], color_type: 2,
    diameter: 1.75, initial_weight: 1000, net_weight: 980, spool_weight: 0,
    remain_percent: 98, status: 'active', favorite: false, entry_method: 'manual', note: '',
    setting_id: 'GEN_PLA_BASIC',
  },
  {
    // Non-official brand spool: filaId resolves empty - drives the
    // type-aggregated fallback panel in AddEditDialog.
    spool_id: 'sp-5', brand: 'Acme3D', material_type: 'PLA Basic', series: 'Pro',
    color_code: '#888888', color_name: '',
    colors: [], color_type: 2,
    diameter: 1.75, initial_weight: 1000, net_weight: 500, spool_weight: 0,
    remain_percent: 50, status: 'active', favorite: false, entry_method: 'manual', note: '',
    setting_id: '',
  },
];

const AMS: AmsData = {
  selected_dev_id: 'mock-dev-1',
  ams_units: [
    {
      ams_id: 'ams-1', ams_type: 0,
      trays: [
        { slot_id: '0', is_exists: true, tag_uid: 'RFID-A1',
          setting_id: 'BBL_PLA_BASIC', fila_type: 'PLA',
          color: '#FF911A', colors: ['#FF911A'], color_type: 2,
          weight: 850, remain: 85, diameter: 1.75, is_bbl: true,
          sub_brands: 'Bambu Lab' },
        { slot_id: '1', is_exists: true, tag_uid: 'RFID-A2',
          setting_id: 'BBL_PLA_BASIC', fila_type: 'PLA',
          color: '#3F8BFF', colors: ['#3F8BFF', '#FF5BB0'], color_type: 1,
          weight: 720, remain: 72, diameter: 1.75, is_bbl: true,
          sub_brands: 'Bambu Lab' },
        { slot_id: '2', is_exists: true, tag_uid: 'RFID-A3',
          setting_id: 'BBL_PLA_TRANSLUCENT', fila_type: 'PLA',
          color: '#FF7B5A', colors: ['#FF7B5A', '#FFD18C'], color_type: 0,
          weight: 600, remain: 60, diameter: 1.75, is_bbl: true,
          sub_brands: 'Bambu Lab' },
        { slot_id: '3', is_exists: false },
      ],
    },
  ],
};

const CLOUD_SYNC: CloudSyncState = {
  // Logged-in mock: avoids the "sign in to view your filament library"
  // empty state so the page actually exposes Add Filament + spools.
  logged_in: true,
  is_syncing: false,
  is_pulling: false,
  last_synced_at: '2026-05-08T08:00:00Z',
  last_error: { code: 0, message: '' },
};

const CLOUD_CONFIG: CloudFilamentConfig = {
  filamentSettings: [],
  vendors: [],
  types: [],
};

// --- Dispatcher -------------------------------------------------------------

function dispatchResponse(seq: number, body: Record<string, unknown>) {
  const pkt = {
    head: { version: SDK_VERSION, type: 'response', seq, ts: Date.now() },
    body,
  };
  document.dispatchEvent(new CustomEvent('cpp:device', { detail: pkt }));
}

function dispatchReport(body: Record<string, unknown>) {
  const pkt = {
    head: { version: SDK_VERSION, type: 'report', seq: 0, ts: Date.now() },
    body,
  };
  document.dispatchEvent(new CustomEvent('cpp:device', { detail: pkt }));
}

function makeOk(submod: string, action: string, payload: unknown): Record<string, unknown> {
  return {
    module: 'filament', submod, action,
    error_code: 0, message: '',
    payload: payload as Record<string, unknown>,
  };
}

function makeModuleOk(module: string, submod: string, action: string, payload: unknown): Record<string, unknown> {
  return {
    module, submod, action,
    error_code: 0, message: '',
    payload: payload as Record<string, unknown>,
  };
}

function handleRequest(pkt: RequestPacket) {
  const { module, submod, action, payload = {} } = pkt.body;
  // Per-call audit log so we can replay request order in the console.
  console.debug('[mockBridge] req', module, submod, action, payload);
  if (module === 'device_page_ams_filament_hotend') {
    if (submod === 'state' && (action === 'get' || action === 'init')) {
      dispatchResponse(pkt.head.seq, makeModuleOk(module, submod, action, mockAmsFilamentHotendState));
      return;
    }
    if (submod === 'action') {
      dispatchResponse(pkt.head.seq, makeModuleOk(module, submod, action, mockAmsFilamentHotendState));
      dispatchReport(makeModuleOk(module, 'state', 'changed', mockAmsFilamentHotendState));
      return;
    }
  }
  if (module !== 'filament') {
    dispatchResponse(pkt.head.seq, {
      module, submod, action, error_code: -1,
      message: `mock: unsupported module ${module}`, payload: {},
    });
    return;
  }

  if (submod === 'init' && action === 'init') {
    dispatchResponse(pkt.head.seq, makeOk('init', 'init', {
      theme: 'dark',
      spools: SPOOLS,
      presets: PRESETS,
      cloud_sync: CLOUD_SYNC,
      debug_enabled: true,
    }));
    return;
  }

  if (submod === 'preset' && action === 'list') {
    dispatchResponse(pkt.head.seq, makeOk('preset', 'list', PRESETS));
    return;
  }

  if (submod === 'spool' && action === 'list') {
    dispatchResponse(pkt.head.seq, makeOk('spool', 'list', SPOOLS));
    return;
  }

  if (submod === 'spool' && (action === 'add' || action === 'batch_add')) {
    const sp = (action === 'add' ? payload : (payload.spool ?? {})) as Partial<Spool>;
    const qty = action === 'batch_add' ? Number(payload.quantity ?? 1) : 1;
    for (let i = 0; i < qty; i++) {
      const id = `sp-${Date.now()}-${i}`;
      SPOOLS = [...SPOOLS, { ...(sp as Spool), spool_id: id }];
    }
    dispatchResponse(pkt.head.seq, makeOk('spool', action, SPOOLS));
    dispatchReport({ module: 'filament', submod: 'spool', action: 'list', payload: SPOOLS });
    return;
  }

  // STUDIO-18344: AMS multi-select fast path. `creates[]` get appended with
  // fresh ids, `updates[]` are merged into the existing record by spool_id
  // (mirroring the C++ batch_create dispatcher behaviour). Either bucket
  // may be empty so an "all updates" or "all creates" save still works.
  if (submod === 'spool' && action === 'batch_create') {
    const creates = (payload.creates as Partial<Spool>[] | undefined) ?? [];
    const updates = (payload.updates as Partial<Spool>[] | undefined) ?? [];
    creates.forEach((sp, i) => {
      const id = `sp-${Date.now()}-c${i}`;
      SPOOLS = [...SPOOLS, { ...(sp as Spool), spool_id: id }];
    });
    updates.forEach((sp) => {
      const id = String(sp.spool_id ?? '');
      if (!id) return;
      SPOOLS = SPOOLS.map((s) => s.spool_id === id ? { ...s, ...sp } as Spool : s);
    });
    dispatchResponse(pkt.head.seq, makeOk('spool', action, SPOOLS));
    dispatchReport({ module: 'filament', submod: 'spool', action: 'list', payload: SPOOLS });
    return;
  }

  if (submod === 'spool' && action === 'update') {
    const sp = payload as Partial<Spool>;
    SPOOLS = SPOOLS.map((s) => s.spool_id === sp.spool_id ? { ...s, ...sp } as Spool : s);
    dispatchResponse(pkt.head.seq, makeOk('spool', action, SPOOLS));
    dispatchReport({ module: 'filament', submod: 'spool', action: 'list', payload: SPOOLS });
    return;
  }

  if (submod === 'spool' && (action === 'remove' || action === 'archive' || action === 'mark_empty' || action === 'toggle_favorite')) {
    const id = String(payload.spool_id ?? '');
    if (action === 'remove') {
      SPOOLS = SPOOLS.filter((s) => s.spool_id !== id);
    } else if (action === 'archive') {
      SPOOLS = SPOOLS.map((s) => s.spool_id === id ? { ...s, status: 'archived' } : s);
    } else if (action === 'mark_empty') {
      SPOOLS = SPOOLS.map((s) => s.spool_id === id ? { ...s, status: 'empty', remain_percent: 0, net_weight: 0 } : s);
    } else if (action === 'toggle_favorite') {
      SPOOLS = SPOOLS.map((s) => s.spool_id === id ? { ...s, favorite: !s.favorite } : s);
    }
    dispatchResponse(pkt.head.seq, makeOk('spool', action, SPOOLS));
    return;
  }

  if (submod === 'spool' && action === 'batch_remove') {
    const ids = (payload.spool_ids as string[] | undefined) ?? [];
    SPOOLS = SPOOLS.filter((s) => !ids.includes(s.spool_id));
    dispatchResponse(pkt.head.seq, makeOk('spool', action, SPOOLS));
    return;
  }

  if (submod === 'machine' && action === 'list') {
    dispatchResponse(pkt.head.seq, makeOk('machine', 'list', {
      machines: [{ dev_id: 'mock-dev-1', dev_name: 'MockX1C', is_online: true, is_lan: false }],
      selected_dev_id: 'mock-dev-1',
    }));
    return;
  }

  if (submod === 'machine' && action === 'request_pushall') {
    dispatchResponse(pkt.head.seq, makeOk('machine', 'request_pushall', {}));
    return;
  }

  if (submod === 'ams' && action === 'list') {
    dispatchResponse(pkt.head.seq, makeOk('ams', 'list', AMS));
    return;
  }

  if (submod === 'colors' && action === 'query_for_id') {
    const id = String(payload.fila_id ?? '');
    const candidates = CANDIDATES_BY_FILA_ID[id] ?? [];
    const fila_type = FILA_TYPE_BY_FILA_ID[id] ?? '';
    dispatchResponse(pkt.head.seq, makeOk('colors', 'query_for_id', {
      fila_id: id, fila_type, candidates,
    }));
    return;
  }

  if (submod === 'sync' && action === 'status') {
    dispatchResponse(pkt.head.seq, makeOk('sync', 'status', CLOUD_SYNC));
    return;
  }

  if (submod === 'sync' && (action === 'pull' || action === 'push_all_now')) {
    // Both endpoints reply with the new sync state; pull additionally fires
    // a `pull_done` report. Returning a complete CloudSyncState (with
    // last_error) avoids CloudBadge crashing on `last_error.code`.
    dispatchResponse(pkt.head.seq, makeOk('sync', action, CLOUD_SYNC));
    if (action === 'pull') {
      setTimeout(() => {
        dispatchReport({
          module: 'filament', submod: 'sync', action: 'pull_done',
          payload: { spools: SPOOLS, state: CLOUD_SYNC, added: 0, updated: 0 },
        });
      }, 30);
    }
    return;
  }

  if (submod === 'config' && action === 'fetch') {
    dispatchResponse(pkt.head.seq, makeOk('config', 'fetch', CLOUD_CONFIG));
    setTimeout(() => {
      dispatchReport({ module: 'filament', submod: 'config', action: 'fetched', payload: CLOUD_CONFIG });
    }, 50);
    return;
  }

  // Default: empty success so unhandled actions don't time out.
  dispatchResponse(pkt.head.seq, {
    module, submod, action, error_code: 0,
    message: `mock: no-op for ${submod}/${action}`, payload: {},
  });
}

// Install only when no real webview host is present. Safe to import in
// production - the entry guard short-circuits via DCE.
export function installMockBridge() {
  if (typeof window === 'undefined') return;
  const w = window as unknown as {
    chrome?: { webview?: { postMessage?: (s: string) => void } };
    __mockBridgeInstalled?: boolean;
  };
  if (w.__mockBridgeInstalled) return;
  if (w.chrome?.webview?.postMessage) return;

  w.chrome = {
    webview: {
      postMessage: (raw: string) => {
        try {
          const pkt = JSON.parse(raw) as RequestPacket;
          // Defer one tick so React's setState during render flushes first.
          queueMicrotask(() => handleRequest(pkt));
        } catch (err) {
          console.error('[mockBridge] failed to parse request', err, raw);
        }
      },
    },
  };
  w.__mockBridgeInstalled = true;
  console.info('[mockBridge] dev-only bridge installed');
}
