/**
 * @studio-18340 @studio-18341 @webview-only
 * JIRA: STUDIO-18340, STUDIO-18341
 *
 * Webview-only regression coverage for color-type mapping and same-RFID AMS
 * import behavior. The bridge is mocked, so no real inventory is modified.
 */
import type { Page } from '@playwright/test'
import { test, expect } from '../src/fixtures/filament'

interface BridgeRequest {
  module: string
  submod: string
  action: string
  payload?: Record<string, unknown>
}

interface FilamentStoreApi {
  getState: () => {
    filament: {
      setCloudSync: (value: unknown) => void
      setPresets: (value: unknown) => void
      setCloudConfig: (value: unknown) => void
      setMachines: (value: unknown) => void
      setAmsData: (value: unknown) => void
      setSelectedMachineDevId: (value: string) => void
      setSpools: (value: Array<Record<string, unknown>>) => void
    }
  }
}

const BASE_SYNC = {
  logged_in: true,
  is_syncing: false,
  is_pulling: false,
  last_synced_at: '',
  last_error: { code: 0, message: '' },
}

const PRESETS = {
  vendors: [
    {
      name: 'Third Party',
      types: [
        {
          name: 'PLA',
          series: ['Custom'],
          items: [
            {
              series: 'PLA Custom',
              filament_id: 'third-party-pla-custom',
              setting_id: 'third-party-pla-custom',
              name: 'PLA Custom',
              is_user: true,
            },
          ],
        },
      ],
    },
  ],
}

const MACHINES = [
  { dev_id: 'dev-1', dev_name: 'Mock Printer', is_online: true },
]

function amsData() {
  return {
    selected_dev_id: 'dev-1',
    ams_units: [
      {
        ams_id: '0',
        ams_type: 0,
        trays: [
          {
            slot_id: '1',
            is_exists: true,
            tag_uid: 'TAG-GRADIENT',
            setting_id: 'ams-gradient-setting',
            fila_type: 'PLA',
            sub_brands: 'Gradient',
            color: '#FF0000',
            colors: ['#FF0000', '#00FF00'],
            color_type: 0,
            weight: '1000',
            remain: 80,
            diameter: '1.75',
            is_bbl: true,
          },
          {
            slot_id: '2',
            is_exists: true,
            tag_uid: 'TAG-MULTI',
            setting_id: 'ams-multi-setting',
            fila_type: 'PLA',
            sub_brands: 'Multi',
            color: '#0047BB',
            colors: ['#0047BB', '#F7D959'],
            color_type: 1,
            weight: '1000',
            remain: 70,
            diameter: '1.75',
            is_bbl: true,
          },
          {
            slot_id: '3',
            is_exists: true,
            tag_uid: 'TAG-EXISTING',
            setting_id: 'ams-official-setting',
            fila_type: '',
            sub_brands: '',
            color: '#112233',
            colors: ['#112233', '#445566'],
            color_type: 1,
            weight: '1000',
            remain: 60,
            diameter: '1.75',
            is_bbl: false,
          },
        ],
      },
    ],
  }
}

function existingSpools() {
  return [
    {
      spool_id: 'spool-existing',
      brand: 'Bambu Lab',
      material_type: 'PLA',
      series: 'PLA Basic',
      color_code: '#112233',
      color_name: '',
      colors: ['#112233', '#445566'],
      color_type: 1,
      diameter: 1.75,
      initial_weight: 1000,
      net_weight: 600,
      spool_weight: 0,
      remain_percent: 60,
      status: 'active',
      favorite: false,
      entry_method: 'ams_sync',
      note: '',
      tag_uid: 'TAG-EXISTING',
      setting_id: 'old-bbl-setting',
      bound_ams_id: '0',
      bound_dev_id: 'dev-1',
    },
  ]
}

async function seedMockFilamentBridge(page: Page, spools = [] as Array<Record<string, unknown>>) {
  const hasStore = await page.evaluate(() => {
    const store = (window as unknown as { __filamentStore?: { getState?: () => unknown } }).__filamentStore
    return typeof store?.getState === 'function'
  })
  test.skip(!hasStore, 'This regression spec needs the dev/test __filamentStore hook.')

  await page.evaluate(
    ({ presets, machines, ams, seedSpools, sync }) => {
      type Packet = {
        head: { version: string; type: string; seq: number; ts: number }
        body: BridgeRequest
      }
      const win = window as unknown as {
        __filamentStore: FilamentStoreApi
        __filamentE2ERequests: Array<Packet['body']>
        chrome?: { webview?: { postMessage?: (message: string) => void } }
      }
      const store = win.__filamentStore.getState().filament
      store.setCloudSync(sync)
      store.setPresets(presets)
      store.setCloudConfig({ vendors: ['Third Party'] })
      store.setMachines(machines)
      store.setAmsData(ams)
      store.setSelectedMachineDevId('dev-1')
      store.setSpools(seedSpools)

      const requests: Array<Packet['body']> = []
      win.__filamentE2ERequests = requests
      let currentSpools = [...seedSpools]

      const respond = (pkt: Packet, payload: unknown) => {
        document.dispatchEvent(new CustomEvent('cpp:device', {
          detail: {
            head: { ...pkt.head, type: 'response', ts: Date.now() },
            body: {
              module: 'filament',
              submod: pkt.body.submod,
              action: pkt.body.action,
              error_code: 0,
              message: '',
              payload,
            },
          },
        }))
      }

      const postMessage = (message: string) => {
        const pkt = JSON.parse(message) as Packet
        const body = pkt.body
        requests.push(body)
        const payload = body.payload ?? {}
        if (body.submod === 'machine' && body.action === 'list') {
          respond(pkt, { machines, selected_dev_id: 'dev-1' })
          return
        }
        if (body.submod === 'ams' && body.action === 'data') {
          respond(pkt, ams)
          return
        }
        if (body.submod === 'preset' && body.action === 'list') {
          respond(pkt, presets)
          return
        }
        if (body.submod === 'colors' && body.action === 'query_for_id') {
          respond(pkt, { fila_id: String(payload.fila_id ?? ''), candidates: [] })
          return
        }
        if (body.submod === 'spool' && body.action === 'update') {
          const next = payload
          currentSpools = currentSpools.map((sp) =>
            sp.spool_id === next.spool_id ? { ...sp, ...next } : sp,
          )
          store.setSpools(currentSpools)
          respond(pkt, currentSpools)
          return
        }
        if (body.submod === 'spool' && body.action === 'add') {
          const next = { spool_id: 'mock-added-spool', ...payload }
          currentSpools = [...currentSpools, next]
          store.setSpools(currentSpools)
          respond(pkt, currentSpools)
          return
        }
        respond(pkt, {})
      }

      win.chrome = { ...(win.chrome ?? {}), webview: { postMessage } }
    },
    { presets: PRESETS, machines: MACHINES, ams: amsData(), seedSpools: spools, sync: BASE_SYNC },
  )
}

async function bridgeRequests(page: Page) {
  return page.evaluate<BridgeRequest[]>(() =>
    ((window as unknown as { __filamentE2ERequests?: BridgeRequest[] }).__filamentE2ERequests ?? []),
  )
}

test.describe('STUDIO-18340/STUDIO-18341 filament colour type regressions @studio-18340 @studio-18341 @webview-only', () => {
  test('STUDIO-18341 AMS gradient and multicolor slots keep canonical color_type semantics', async ({
    page,
    filamentList,
  }) => {
    await seedMockFilamentBridge(page)

    const dialog = await filamentList.openAddDialog()
    await dialog.switchTab('ams')

    const gradientSlot = await dialog.readAmsSlot(0, 1)
    const multiSlot = await dialog.readAmsSlot(0, 2)

    expect(gradientSlot.colorType, 'gradient slot data-color-type').toBe('0')
    expect(gradientSlot.colors).toEqual(['#FF0000', '#00FF00'])
    expect(multiSlot.colorType, 'multicolor slot data-color-type').toBe('1')
    expect(multiSlot.colors).toEqual(['#0047BB', '#F7D959'])

    await dialog.pickAmsSlot(0, 1)
    expect((await dialog.readPreviewBackground()).toLowerCase()).toContain('linear-gradient')
    expect((await dialog.readPreview()).hexes).toEqual(['#FF0000', '#00FF00'])

    await dialog.pickAmsSlot(0, 2)
    const multiBackground = (await dialog.readPreviewBackground()).toLowerCase()
    expect(multiBackground).toContain('linear-gradient')
    expect(multiBackground, 'multicolor preview should use hard stops').toContain('0%')
    expect((await dialog.readPreview()).hexes).toEqual(['#0047BB', '#F7D959'])

    await dialog.cancel()
  })

  test('STUDIO-18340 AMS import updates the existing RFID spool and keeps user-selected third-party setting_id', async ({
    page,
    filamentList,
  }) => {
    await seedMockFilamentBridge(page, existingSpools())

    const dialog = await filamentList.openAddDialog()
    await dialog.switchTab('ams')
    await dialog.pickAmsSlot(0, 3)

    await dialog.selectBrand('Third Party')
    await dialog.selectMaterial('PLA Custom')
    await dialog.confirm()

    await page.waitForFunction(() => {
      const reqs = (window as unknown as { __filamentE2ERequests?: Array<{ submod: string; action: string }> })
        .__filamentE2ERequests ?? []
      return reqs.some((r) => r.submod === 'spool' && r.action === 'update')
    })

    const updateRequest = (await bridgeRequests(page)).find((r) =>
      r.submod === 'spool' && r.action === 'update',
    )

    expect(updateRequest, 'expected existing RFID spool to be updated, not added').toBeTruthy()
    expect(updateRequest?.payload?.spool_id).toBe('spool-existing')
    expect(updateRequest?.payload?.setting_id).toBe('third-party-pla-custom')
    expect(updateRequest?.payload?.setting_id).not.toBe('ams-official-setting')

    const addRequest = (await bridgeRequests(page)).find((r) =>
      r.submod === 'spool' && r.action === 'add',
    )
    expect(addRequest, 'same RFID import must not create a duplicate spool').toBeFalsy()
  })
})
