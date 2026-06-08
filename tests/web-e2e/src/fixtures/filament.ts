/**
 * Composes domain-specific POM instances onto the studio fixtures so specs
 * can write `test('...', async ({ filamentList }) => { ... })` instead of
 * threading a Page everywhere.
 *
 * One layered fixture per persistent surface in the manager:
 *   - filamentList   : the main list page (the entry point)
 *   - filamentAdd    : the +Add Filament dialog (lazy-opened on demand)
 *   - filamentEdit   : the Edit dialog launched from a list row
 *
 * "Lazy" fixtures still construct the POM eagerly; opening/closing the
 * underlying dialog is the spec's responsibility.  This separation keeps
 * specs explicit about UI transitions instead of hiding them behind setup.
 */
import { test as base, expect } from './studio'
import { FilamentListPage } from '../pages/FilamentListPage'
import { FilamentAddDialog } from '../pages/FilamentAddDialog'
import { FilamentEditDialog } from '../pages/FilamentEditDialog'
import { FilamentDetailDialog } from '../pages/FilamentDetailDialog'
import { ConfirmDialog } from '../pages/ConfirmDialog'

export interface FilamentFixtures {
  filamentList: FilamentListPage
  filamentAdd: FilamentAddDialog
  filamentEdit: FilamentEditDialog
  filamentDetail: FilamentDetailDialog
  filamentConfirm: ConfirmDialog
}

export const test = base.extend<FilamentFixtures>({
  filamentList: async ({ page }, use) => {
    const list = new FilamentListPage(page)
    await list.waitForReady()
    await use(list)
  },

  filamentAdd: async ({ page }, use) => {
    await use(new FilamentAddDialog(page))
  },

  filamentEdit: async ({ page }, use) => {
    await use(new FilamentEditDialog(page))
  },

  filamentDetail: async ({ page }, use) => {
    await use(new FilamentDetailDialog(page))
  },

  filamentConfirm: async ({ page }, use) => {
    await use(new ConfirmDialog(page))
  },
})

export { expect }
