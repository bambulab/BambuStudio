# Native MCP local acceptance

Use the freshly built `build/mcp-arm64/src/BambuStudio.app` with a **new disposable profile**. Quit any installed Bambu Studio first, then confirm the running app is the freshly built bundle; macOS may otherwise activate an existing app with the same bundle ID. Never point `--datadir` at the normal Bambu Studio profile. The scripts do not launch, close, print from, or connect a device to the app. All paths below are examples; use unique unused paths and an unused port. Keep printer actions and cloud account reads **off** in MCP Preferences.

1. Check the pinned legacy contract without starting the app:

   ```sh
   python3 -m unittest tests/mcp/test_legacy_contract.py
   ```

   The fixture `legacy_tools_oracle.json` was generated directly from the former `src/tools.ts` registry at SHA-256 `b24d932e715fd7ccf94f46a9ab2cfbccfec612049394e57e5cf281509ed5e099`. It contains all 90 external names, descriptions, JSON schemas, and annotations. The test compares the native manifest to this independent snapshot. It cannot prove GUI behavior.

2. Create an isolated profile and a 16 × 16 × 12 mm cube. Launch only the fresh app with that profile. In Preferences, leave MCP off and check that the chosen port is closed. Complete first-run setup using local bundled presets if needed.

   ```sh
   python3 tests/mcp/native_acceptance.py prepare --profile /private/tmp/bambu-native-check-profile --workdir /private/tmp/bambu-native-check-work --port 27183
   open -n build/mcp-arm64/src/BambuStudio.app --args --datadir /private/tmp/bambu-native-check-profile
   python3 tests/mcp/native_acceptance.py off --profile /private/tmp/bambu-native-check-profile --workdir /private/tmp/bambu-native-check-work --port 27183
   ```

3. Enable MCP in that app's Preferences on the chosen port. Check the visible status, then run the protocol, schema, and safety phase. It compares all 90 published contracts, checks `app_get_capabilities` advertises all 90 native methods, exercises safe reads, rejects malformed requests, and confirms all six printer-action and two cloud-read paths remain blocked by default. The guard probes use syntactically valid dummy references and run only after capabilities confirms both gates are off.

   ```sh
   python3 tests/mcp/native_acceptance.py on --profile /private/tmp/bambu-native-check-profile --workdir /private/tmp/bambu-native-check-work --port 27183
   ```

4. Run the official MCP SDK check. Set `MCP_SDK_DIR` to an existing `@modelcontextprotocol/sdk` package directory. The prior checkout's installed package is suitable for local development; record its version. The Python client speaks HTTP directly and does not replace this check.

   ```sh
   MCP_SDK_DIR=/path/to/node_modules/@modelcontextprotocol/sdk node tests/mcp/native_sdk_client.mjs /private/tmp/bambu-native-check-profile 27183
   ```

5. Run the safe preparation flow in the empty disposable scene. It checks import, stable string IDs, absolute transform, revision rejection and refresh, valid saved 3MF, and observed completion of a tracked arrangement job. Inspect the visible placement and reopen the saved 3MF in the same isolated app after closing or saving the test scene. Native arrange may remove an empty second plate, so inspect the resulting plate list instead of requiring both plates to remain.

   ```sh
   python3 tests/mcp/native_acceptance.py flow --profile /private/tmp/bambu-native-check-profile --workdir /private/tmp/bambu-native-check-work --port 27183
   ```

6. For slicing, create **another** empty disposable profile and work directory, enable MCP there, select a safe local printer and filament preset, and ensure effective post-processing scripts are empty. Run `flow --slice`. It additionally requires slice validation, an observed completed job with `sliceResultId`, matching toolpath preview, exported G-code, and rejection of the result after a model edit. Confirm the slice and toolpaths in the GUI. The tiny cube run is bounded, but native slicing can still fail due to missing presets; report that exact failure rather than claiming a pass.

7. In Preferences, change to another unused port while enabled: the old port must pass `off` and the new one `on`. Reject invalid ports without changing the effective listener. Disable and verify `off`, then enable again. Save the disposable token snapshot shown below, quit this app normally, and reopen the same built bundle with the same disposable `--datadir`. Run `persisted` and `on` to verify the enabled setting, port, exact token, and live listener survived restart. The script checks the token file is regular and owner-only (0600); do not print the token. A port-conflict test may use a dummy listener on a third unused port. Confirm the visible error and that the prior endpoint remains usable.

   ```sh
   install -m 600 /private/tmp/bambu-native-check-profile/mcp.token /private/tmp/bambu-native-check-work/restart-mcp.token
   python3 tests/mcp/native_acceptance.py persisted --profile /private/tmp/bambu-native-check-profile --workdir /private/tmp/bambu-native-check-work --port 27184 --token-snapshot /private/tmp/bambu-native-check-work/restart-mcp.token
   python3 tests/mcp/native_acceptance.py on --profile /private/tmp/bambu-native-check-profile --workdir /private/tmp/bambu-native-check-work --port 27184
   ```

   To check startup failure, close that app, occupy its saved port with a temporary local listener, then reopen the same built bundle and profile. Preferences should keep Enable checked, show the bind error, and show the address as unavailable. Run `persisted` while the port is occupied. Release the temporary listener and change Port to a free value in Preferences; `persisted` on that port and `on` must pass without rotating the token. Only the disposable profile and local listener belong in this check.

   Test explicit token regeneration separately: the prior token must return HTTP 401 and the new token pass `on`.

   Before regenerating, save only the **disposable profile's** old token in an owner-only file inside its work directory. After enabling on the new port and regenerating, run the settings phase with the old port and that file. It verifies that the old endpoint is closed, the old token returns 401, the new token connects, and the enabled state and port are saved. Delete the snapshot when acceptance is complete. Do not copy or display a normal profile's token.

   ```sh
   install -m 600 /private/tmp/bambu-native-check-profile/mcp.token /private/tmp/bambu-native-check-work/initial-mcp.token
   python3 tests/mcp/native_acceptance.py settings --profile /private/tmp/bambu-native-check-profile --workdir /private/tmp/bambu-native-check-work --port 27184 --old-port 27183 --old-token-file /private/tmp/bambu-native-check-work/initial-mcp.token
   ```

The 90-tool contract and dispatch checks do **not** mean every operation was executed. This workflow deliberately avoids physical printer commands, cloud account queries, and destructive mesh edits. Record the exact app binary/build identity, passed phases, GUI observations, resulting file paths, and anything untested. A successful local acceptance requires the static contract check, live official-SDK discovery, safe flow, and GUI evidence; device and cloud behavior remains unverified until separate controlled testing is authorized.

## 2026-10-05 isolated run

The final arm64 executable at `build/mcp-arm64/src/BambuStudio.app/Contents/MacOS/BambuStudio` had SHA-256 `ca2201e221b629e7316e818c1a81d411b52caee6b2ae5e056ce0def9bcd35577`. Static legacy contract checks passed 3/3. An isolated app with public bundled presets and port 27185 passed live discovery of all 90 legacy tool contracts, official MCP SDK 1.32.0 discovery, safe read/invalid-argument checks, and default denial of all eight printer-action/cloud-read paths. The safe flow imported a generated 16 × 16 × 12 mm STL, verified stable IDs and plate revision guards, saved a 3MF, and observed a tracked arrange job succeed. Arrange removed the empty second plate; `ArrangeJob::finalize()` calls `PartPlateList::rebuild_plates_after_arrangement(..., true)`, whose recycle path removes empty extra plates.

With the same disposable profile, local slice validation passed, the tracked slice completed, preview returned positive toolpath move count, and a tracked G-code export wrote 156,612 bytes. Moving the cube 1 mm made the previous preview result return `STALE_REFERENCE`. The saved project, G-code, hashes, and per-phase result are in `/private/tmp/bambu-native-90-seeded.lmYz0e/work/evidence.json`. No physical printer or cloud operation was run.

The first isolated app reached the Printer Selection wizard, but accessibility could not activate its visible Next button. The second profile was seeded only from public bundled preset names. Thus the MCP Preferences UI, visible arrangement/toolpaths, port changes, token rotation, and GUI save/reopen remain **unverified**. The 90-tool result establishes catalog and schema parity, not execution of every tool.

## Later 2026-10-05 isolated run

The newer arm64 executable had SHA-256 `2554cd3065ea28b5d3651f525e8bce539464d432cc3b984a6296d06470b7355b`. Static contract checks passed 3/3. A new disposable app on port 27947 passed all 90 live legacy contracts, official MCP SDK discovery, representative reads, invalid-argument checks, and default denial of the eight printer-action/cloud-read paths. It imported, transformed, saved, and arranged a generated cube. The app reported a named, idle `MCP Settings Test` project with one object and one plate in Prepare. Its profile and project are under `/private/tmp/bambu-pr-ready.HysGoQ/`; this app was left open for a manual Preferences check.

A second disposable app on port 27950, seeded only with bundled P1S, PLA, and 0.20 mm presets, passed the full `flow --slice` phase: slice validation, completed arrange and slice jobs, nonempty toolpath preview, G-code export, and rejection of the stale slice result after a model edit. The valid saved 3MF was 29,457 bytes (SHA-256 `16f1d98f3b3e1da1ee480fd4ca53432f705f33609a232a59dd3292005c304bcd`); G-code was 156,612 bytes (SHA-256 `aa776d8f65ae512a2192094c4cc45d182c5912483444221cc63250f043ef507c`). After the app was idle, terminating only that disposable process closed its port. A process restart restored the saved enabled state and port, kept the same token, and passed the live `on` checks again. The second app was then closed. This was a process restart, not a GUI Quit or GUI project reopen test.

Native UI automation could not bind the exact test app window. Preferences status/address display, token Show/Hide and regeneration, enabled and port controls, conflict handling, visible placement/toolpaths, and GUI save/reopen still require direct GUI observation. The 90-tool result remains a contract and dispatch check, not execution of every tool. This binary predates the later Windows-only portability helper changes; the final post-change macOS app was not relinked locally.

## Final 2026-10-05 GUI and native run

The freshly linked arm64 executable had SHA-256 `d1a694bdfeb07a8e7cf15b122db50a431da5e2cde7e6576cb69004b57d426bc3`. All testing used the marked disposable profile `/private/tmp/bambu-pr-ready.HysGoQ/profile`; the user's Bambu Studio profile and printer were not used. The saved `MCP Settings Test.3mf` was reopened from the app's Recent list in the GUI, and its cube was visible on the Prepare plate. Preferences showed the local server enabled at `http://127.0.0.1:27947/mcp`, with printer actions and cloud reads off and the token masked. Show token exposed a value; Hide token restored the mask. The token value was never logged.

Changing the port in Preferences to 27948 moved the live listener: 27947 closed, 27948 passed the live `on` phase, and the new port was saved. Entering invalid 1023 showed the range error, reset the field, and retained 27948. A temporary listener on 27949 caused a visible bind error; the previous 27948 listener remained active. Turning the server off in Preferences closed 27948; turning it on restored the listener and passed `on`. Regenerate changed only the disposable token. The `settings` phase confirmed the old token returned HTTP 401, the new token worked, the old port stayed closed, and the token file remained owner-only (0600). GUI Quit closed the disposable listener. Reopening the freshly linked app with that profile restored enabled state, port 27948, masked token, and disabled permission gates; the saved test project reopened in the GUI.

The final binary passed the live `on` phase, all 90 exact legacy contracts and dispatch checks, and official MCP SDK 1.32.0 discovery. The final `flow --slice` used a fresh empty scene and `/private/tmp/bambu-pr-ready.HysGoQ/final-work`: import, transform, valid 3MF save, tracked arrange and slice completion, nonempty preview, G-code export, and stale-result rejection all passed. The saved 3MF was 29,457 bytes (SHA-256 `256b88b74349a73ae81a4d3622fec2442771afd90ebb95ff064a8eb8e9542d48`); G-code was 156,682 bytes (SHA-256 `19bbb5e7a8e883f514a6e79064becfd4323802d0c6fb0423a7a9b95262ee1711`). The GUI Preview visibly showed the cube's green toolpath lines, 60 layers, and slice statistics. A first attempt with the earlier work directory stopped at `project_save` because `saved.3mf` already existed and the tool correctly requires `overwrite=true`; acceptance flows need a fresh work directory or an explicit artifact cleanup.

Copy address displayed `Address copied to clipboard`. After the Mac was unlocked, the Preferences window was raised and the copied address was pasted into the disposable Port field using only direct UI clicks and `super+a`/`super+v`. Accessibility showed the exact `http://127.0.0.1:27948/mcp` value. The Port field was restored to 27948 before committing; the same listener and saved port remained active. The earlier failed paste was an automation focus problem, not a product failure. Preferences was closed and the named `MCP Settings Test.3mf` was reopened; the GUI title was clean. Physical printer commands, cloud account reads, and every individual tool implementation were not exercised.

After final source cleanup, the static legacy contract suite passed 3/3 again. The last read-only app state reported `dirty=false`, `modalOpen=false`, `slicingRunning=false`, `uiJobRunning=false`, one object and one plate in Prepare. No Python bytecode artifact was left in `tests/mcp`.

## 2026-10-08 persistence fix check

The updated arm64 Release executable had SHA-256 `fe04ee219e7af341b92003cd12d7981f10629e7e7007fdd2f9f1659d64efef9b`. An isolated app used `/private/tmp/bambu-mcp-persist-1jx7iwrb/profile` and port 62078. With the saved MCP setting enabled, `persisted` confirmed the saved port and byte-identical owner-only token, and the live `on` phase passed all 90 contract checks and default permission guards.

After that owned process stopped, a temporary local listener occupied 62078 during the next launch. The app kept the saved enabled setting and the same token, with 0600 file permissions. The process was stopped, the temporary listener was released, and relaunching the same built app with the same profile restored the MCP listener. `persisted` and the full live `on` phase passed again. A final launch with only that disposable profile's enabled setting changed to false left 62078 closed and kept the saved port and token unchanged.

These checks used controlled SIGTERM process restarts and a disposable profile. The final owned app process was stopped and the test port was closed. The native Preferences UI could not be observed in this run, so GUI Quit, checkbox/status display, in-process port retry, and GUI token controls remain unverified for this build.
