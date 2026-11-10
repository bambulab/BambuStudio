# Manual regression: Clone number dialog (macOS)

This is a manual procedure, not an automated test. The Catch2 suites under `tests/` exercise non-GUI code, and `tests/web-e2e` cannot drive this native dialog. Do not treat a green unit-test run as coverage for this bug.

Issue: [bambulab/BambuStudio#12285](https://github.com/bambulab/BambuStudio/issues/12285)

The unpatched crash is in `wxGetNumberFromUser` / `wxNumberEntryDialog` during the initial AppKit layout, before `Selection::clone` runs. On macOS, `Plater::clone_selection()` now uses `wxTextEntryDialog` instead. Windows and Linux still call `wxGetNumberFromUser`. `Plater::set_number_of_copies()` is out of scope and still uses the numeric dialog.

If the text dialog crashes on open the same way, stop. Record the stack, and do not claim this workaround fixed the crash. Do not turn that failure into a wxWidgets upgrade or edits to other numeric-dialog callers as part of this check.

## Platform and build identifiers

| Field | Value |
| --- | --- |
| Application | BambuStudio |
| `SLIC3R_VERSION` (`version.inc`) | `02.08.03.66` (reported crash build 2.8.3.66) |
| Issue | https://github.com/bambulab/BambuStudio/issues/12285 |
| Patched function | `Plater::clone_selection()` in `src/slic3r/GUI/Plater.cpp` |
| wxWidgets | `deps/wxWidgets/wxWidgets.cmake` builds `https://github.com/bambulab/wxWidgets` tag `master`; the app `find_package`s wxWidgets 3.1 |
| macOS deployment target default | `10.15` (`CMAKE_OSX_DEPLOYMENT_TARGET` in the top-level `CMakeLists.txt`) |
| macOS build | Repository config in `doc/How to build - Mac OS.md` or `./BuildMac.sh`. Do not invent a new CMake setup. |
| Intended crash OS | macOS 27 |
| Host inspected while writing this procedure | macOS 27.2 (build `26B5086k`), arm64. No BambuStudio binary was built or launched here. |
| macOS 27 GUI result | **Pending.** Fill the Observed column only after a real run. |

The issue's downloadable project and crash attachments are not in this tree. Start with a built-in primitive. Use the supplied project only if it shows up later.

## Setup

1. Build with the repository's existing macOS configuration (`doc/How to build - Mac OS.md` or `./BuildMac.sh`).
2. Launch that build. No printer connection is required.
3. Right-click the plate and choose **Add Primitive → Cube**. Leave this as the only object unless a step says otherwise.
4. Count objects on the plate and note the Undo menu state before each case.

Entry points, all of which reach `Plater::clone_selection()`:

- Menu: **Edit → Clone selected** (on macOS the item is labeled with Ctrl+K; wx maps that accelerator to Command).
- Context menu: right-click the object → **Clone**.
- Toolbar event: focus the 3D view and press Command+K. That posts `EVT_GLTOOLBAR_CLONE`. The top GL toolbar has no separate Clone icon.

On macOS the dialog is a text field. Its prompt is **Clone**, its title is **Number of copies:**, and the field starts as `1`. OK / Enter accepts. Cancel, Escape, and the window close button must leave the scene alone. A value is accepted only when `wxString::ToLong` succeeds and the integer is in the inclusive range 0–1000. Anything else shows the existing **Invalid number** message and reopens the field with the rejected text still in it. `Selection::clone` adds exactly N copies for N > 0, and returns immediately for 0 without a snapshot.

## Cases

Run the pre-fix crash on a separate unpatched build of 02.08.03.66 (or with this macOS branch reverted). It terminates the process. Do the rest on the patched build.

| ID | Steps | Expected | Observed |
| --- | --- | --- | --- |
| P0 | Unpatched build, macOS 27. Select the cube. **Edit → Clone selected**. | Process crashes while the numeric dialog is laying out. No copy is added. | Pending |
| A1 | Patched build. With the cube selected, open Clone from the Edit menu, then again from the object context menu, then again with Command+K. | The text dialog opens every time. It does not crash. Prompt **Clone**, title **Number of copies:**, initial text `1`. | Pending |
| A2 | Accept the default `1` with Enter. | Exactly one copy is added (2 objects). One **Selection-clone** undo step. | Pending |
| A3 | Select the original only. Enter `3` and press OK. | Exactly 3 copies are added. | Pending |
| A4 | Select the original only. Enter `0` and press OK. | No copy is added. Object count, selection, and undo history stay as they were. | Pending |
| A5 | New disposable project, one cube only. Enter `1000` and press OK. | Dialog accepts 1000. 1000 copies are added. Duplication may be slow; that cost is existing `Selection::clone` behavior. | Pending |
| A6 | Select two objects. Enter `1` and press OK. | One copy of the whole selection is added (two new objects). | Pending |
| B1 | Enter an empty field and press OK. | **Invalid number** is shown. No clone, no scene change. The field reopens with the empty text. Replace it with `1`, press OK. Exactly one copy is added. | Pending |
| B2 | Repeat B1's rejection, separately, for `abc`, `1.5`, `-1`, `1001`, and `999999999999999999999`. After each rejection, Cancel. | Each value is rejected with **Invalid number**. No clone and no scene change. Cancel after the warning leaves objects and selection unchanged. | Pending |
| C1 | Open the dialog and press Cancel. Reopen and press Escape. Reopen and click the window close button. | Each dismissal adds nothing and does not change the selection. | Pending |
| C2 | Enter `abc`, dismiss **Invalid number**, then Cancel, Escape, or close the reopened dialog. | Rejected text was still in the field. Final dismissal adds nothing. | Pending |
| C3 | Focus is in the text field. Type a value and press Enter. | Enter accepts a valid value the same way OK does. | Pending |
| C4 | Repeat A2 and B1 in the app light theme and dark theme. On macOS, also glance at system light and dark appearance. | Prompt, field text, buttons, and **Invalid number** stay readable. | Pending |
| D1 | After A2, Undo, then Redo. | Undo removes the copies from that clone. Redo puts them back. | Pending |
| D2 | Deselect everything. Try **Edit → Clone selected**, the object context menu, and Command+K. | The action is unavailable or returns immediately. No dialog. Scene unchanged. | Pending |
| E1 | Smoke-test the same default-`1` clone on an older supported macOS. | Text dialog opens and adds one copy. Record the OS version in Observed. | Pending |
| E2 | Smoke-test default-`1` clone on Windows and on Linux, built with that platform's existing repository configuration. | Those builds still show the numeric `wxGetNumberFromUser` dialog, not the text workaround. Accepting 1 adds one copy. Record OS and build in Observed. | Pending |

## Result

| Check | Status |
| --- | --- |
| macOS 27 pre-fix crash reproduced | Pending |
| macOS 27 patched dialog opens and clones | Pending |
| Text dialog reproduced the same crash | Pending — stop and diagnose if this becomes Yes |
| Workaround claimed to resolve #12285 | No, until the macOS 27 rows above are filled from a real run |
