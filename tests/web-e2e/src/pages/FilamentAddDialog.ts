/**
 * Page object for the +Add Filament dialog.  Backed by the same React
 * component as the Edit dialog (`AddEditDialog.tsx`) but distinguished
 * by `data-testid="add-dialog"` on the outer wrapper.
 *
 * The class exposes three coherent surfaces:
 *
 *   - tab switcher (manual entry vs. read from AMS)
 *   - manual entry form (brand / material / color)
 *   - preview bar (color name, official BBL code, hex list)
 *
 * Each piece is reachable by a typed accessor; specs assemble flows out
 * of these primitives.
 */
import type { Locator, Page } from '@playwright/test'
import { BasePage } from './BasePage'

export type DialogTab = 'manual' | 'ams'
export type ColorType = 0 | 1 | 2 // gradient / multicolor / single

export interface PreviewSnapshot {
  /** Display name (official "Bright Orange" / 自定义 hex when no match). */
  name: string
  /** BBL official filament code (e.g. "13903"); empty when unofficial. */
  filaCode: string
  /** Hex list rendered next to the name, e.g. ["#3F8BFF","#FF5BB0"]. */
  hexes: string[]
}

export interface CandidateSnapshot {
  colorCode: string
  colors: string[]
  colorType: ColorType
  name: string
}

export class FilamentAddDialog extends BasePage {
  constructor(page: Page) {
    super(page)
  }

  protected get root(): Locator {
    return this.page.getByTestId('add-dialog')
  }

  async waitForReady(): Promise<void> {
    await this.root.waitFor({ state: 'visible', timeout: 10_000 })
  }

  // --------------------------------------------------------------- tabs
  async switchTab(tab: DialogTab): Promise<void> {
    await this.byTestId(`dialog-tab-${tab}`).click()
  }

  async currentTab(): Promise<DialogTab> {
    const isManual = await this.byTestId('dialog-tab-manual')
      .getAttribute('data-active')
      .catch(() => null)
    return isManual === 'true' ? 'manual' : 'ams'
  }

  // --------------------------------------------------------------- manual form
  /** brand / material selectors (HTML <select> rendered by AddEditDialog). */
  get brandSelect(): Locator {
    return this.byTestId('filament-brand')
  }
  get materialSelect(): Locator {
    return this.byTestId('filament-material')
  }
  get pickCustomColorButton(): Locator {
    return this.byTestId('pick-custom-color-button')
  }

  async selectBrand(brand: string): Promise<void> {
    await this.brandSelect.selectOption({ label: brand })
  }

  async selectMaterial(material: string): Promise<void> {
    await this.materialSelect.selectOption({ label: material })
  }

  /**
   * Read the visible options from a <select>, skipping the "please choose"
   * placeholder (value === '').  Returns labels in DOM order.  Specs that
   * pick by index get a stable snapshot and never depend on a specific
   * product name like "PLA Basic" being present in the user's library.
   */
  private async readSelectLabels(select: Locator): Promise<string[]> {
    return select.evaluate((el: HTMLSelectElement) =>
      Array.from(el.options)
        .filter((o) => o.value !== '' && !o.disabled)
        .map((o) => o.label || o.textContent || ''),
    )
  }

  async listBrands(): Promise<string[]> {
    return this.readSelectLabels(this.brandSelect)
  }

  async listMaterials(): Promise<string[]> {
    return this.readSelectLabels(this.materialSelect)
  }

  // --------------------------------------------------------------- candidates
  get candidatePanel(): Locator {
    return this.byTestId('color-candidate-panel')
  }

  candidateByCode(colorCode: string): Locator {
    return this.byTestId(`color-candidate-${colorCode}`)
  }

  /** Read the visible candidate buttons into a structured snapshot. */
  async listCandidates(): Promise<CandidateSnapshot[]> {
    const buttons = await this.candidatePanel.locator('[data-testid^="color-candidate-"]').all()
    const out: CandidateSnapshot[] = []
    for (const btn of buttons) {
      const colorCode = (await btn.getAttribute('data-color-code'))?.trim() ?? ''
      const name = (await btn.getAttribute('data-color-name'))?.trim() ?? ''
      const type = Number(await btn.getAttribute('data-color-type')) as ColorType
      const hexAttr = (await btn.getAttribute('data-colors'))?.trim() ?? ''
      const colors = hexAttr ? hexAttr.split(',').map((c) => c.trim()).filter(Boolean) : []
      out.push({ colorCode, colors, colorType: type, name })
    }
    return out
  }

  async pickCandidate(colorCode: string): Promise<void> {
    await this.candidateByCode(colorCode).click()
  }

  // --------------------------------------------------------------- AMS panel
  get amsGrid(): Locator {
    return this.byTestId('ams-grid')
  }

  amsSlot(unit: number, tray: number): Locator {
    return this.byTestId(`ams-slot-${unit}-${tray}`)
  }

  async pickAmsSlot(unit: number, tray: number): Promise<void> {
    await this.amsSlot(unit, tray).click()
  }

  // --------------------------------------------------------------- preview bar
  get previewBar(): Locator {
    return this.byTestId('color-preview-bar')
  }

  async readPreview(): Promise<PreviewSnapshot> {
    // Allow the React commit to flush before we sample DOM.
    await this.previewBar.waitFor({ state: 'visible' })
    // STUDIO-17977: preview-color-name only renders when colorName is
    // truthy (spool with empty color_name shows hex-only). The other
    // two slots are also conditional on data. Read directly from DOM
    // via evaluate so missing elements yield empty strings instead of
    // making the locator wait the full actionTimeout.
    const snap = await this.previewBar.evaluate((bar) => ({
      name: bar.querySelector('[data-testid="preview-color-name"]')?.textContent?.trim() ?? '',
      filaCode: (bar.querySelector('[data-testid="preview-fila-code"]')?.textContent?.replace(/[·\s]/g, '').trim()) ?? '',
      hexLabel: bar.querySelector('[data-testid="preview-hex-list"]')?.textContent?.trim() ?? '',
    }))
    const { name, filaCode, hexLabel } = snap
    const hexes = hexLabel
      .split('/')
      .map((h) => h.trim())
      .filter((h) => /^#?[0-9a-f]{6}$/i.test(h))
      .map((h) => (h.startsWith('#') ? h : `#${h}`))
    return { name, filaCode, hexes }
  }

  // --------------------------------------------------------------- footer
  get cancelButton(): Locator {
    return this.byTestId('dialog-cancel')
  }
  get confirmButton(): Locator {
    return this.byTestId('dialog-confirm')
  }

  async cancel(): Promise<void> {
    await this.cancelButton.click()
    await this.root.waitFor({ state: 'detached', timeout: 5_000 }).catch(() => undefined)
  }

  async confirm(): Promise<void> {
    await this.confirmButton.click()
    await this.root.waitFor({ state: 'detached', timeout: 7_000 }).catch(() => undefined)
  }
}
