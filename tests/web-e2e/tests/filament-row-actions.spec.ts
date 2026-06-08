/**
 * @filament-baseline @webview-only
 *
 * Each spool row exposes three action buttons (detail / add-similar /
 * delete).  This spec verifies that each button OPENS the right
 * downstream dialog and that the dialog can be cancelled cleanly
 * without mutating the library.
 *
 * Crucially: the delete path stops at the confirm dialog and CANCELS
 * it.  Specs in this baseline suite must never confirm a destructive
 * action against the developer's real library.
 */
import { test, expect } from '../src/fixtures/filament'

test.describe('filament row action entry points @filament-baseline @webview-only', () => {
  test.beforeEach(async ({ filamentList }) => {
    const rows = await filamentList.readRowAttributes()
    test.skip(rows.length === 0, 'No rows to exercise row actions against.')
  })

  test('detail button opens detail dialog and closes cleanly', async ({ filamentList, filamentDetail }) => {
    const [first] = await filamentList.readRowAttributes()
    if (!first) return

    await filamentList.openDetailFor(first.id)
    await filamentDetail.waitForReady()
    expect(await filamentDetail.getSpoolId()).toBe(first.id)
    await expect(filamentDetail.editButton).toBeVisible()

    await filamentDetail.close()
    await expect(filamentDetail.editButton).toHaveCount(0)
  })

  test('add-similar button opens add dialog prefilled and cancels cleanly', async ({
    filamentList,
    filamentAdd,
  }) => {
    const [first] = await filamentList.readRowAttributes()
    if (!first) return

    const dialog = await filamentList.openAddSimilarFor(first.id)
    await expect(dialog.brandSelect).toBeVisible()
    // The add dialog opened from "add similar" carries a prefilled brand
    // value; we don't assert the exact label (depends on user's library)
    // but we DO assert the dialog truly opened in manual entry mode.
    expect(await filamentAdd.currentTab()).toBe('manual')

    await dialog.cancel()
  })

  test('delete button opens confirm dialog and CANCELS without mutating', async ({
    filamentList,
    filamentConfirm,
  }) => {
    const before = await filamentList.readRowAttributes()
    const [first] = before
    if (!first) return

    await filamentList.clickDeleteFor(first.id)
    await filamentConfirm.waitForReady()
    expect(await filamentConfirm.isDanger()).toBe(true)

    await filamentConfirm.cancel()

    const after = await filamentList.readRowAttributes()
    expect(after.length, 'cancelling delete must not change row count').toBe(before.length)
    expect(
      after.some((r) => r.id === first.id),
      'cancelled delete must keep the row',
    ).toBe(true)
  })
})
