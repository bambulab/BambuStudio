# Local RFID spool assignments

Reuse an RFID-equipped spool with replacement filament of the same base material,
assign its actual color and exact slicing profile, and apply that assignment with
explicit Prepare Sync.

## Boundaries

- Assignments use device `tray_uuid` (inventory `tag_uid`), not product ID,
  material/color, slot, or printer serial. Device `tag_uid` is a different field.
- Local `filament_inventory/rfid_overrides.json` stores color, base material, and
  exact filament profiles per printer preset, separate from cloud spool records.
  Atomic replacement preserves the previous file on write failure.
- Raw device state, RFID contents, cloud inventory, and firmware remain unchanged.
- Assigned and reported base material types must match. Mixed nozzle mode is
  rejected. A different printer preset needs an explicit profile choice.
- Missing or incompatible profiles prevent sync rather than silently substituting
  the original or a generic profile. Overrides are checked on AMS rebuild.
- Saving an assignment does not change the current project. Prepare Sync is explicit.
- Filament Manager shows the assigned identity and original-tag reference. Device
  tiles and send-dialog destinations use the assigned color and an asterisk.
- Firmware weight estimates and empty/refill status are not reset.
- Device detail profile availability currently uses the Prepare preset. Separating
  device nozzle context remains a follow-up. Project slots do not yet persist
  override provenance or an asterisk.

## Automated validation

Build `bambu-studio` and `rfid_override_tests`, then run:

```sh
ctest --test-dir build -R '^rfid_override_tests$' --output-on-failure
```

The native suite covers persistence, malformed input, missing/incompatible profiles,
and sync rejection without replacing the existing project (6 cases, 44 assertions).
In `src/slic3r/GUI/DeviceWeb/device_page`, run the production build and
`playwright test tests/e2e/rfid-override.spec.ts`. The browser test exercises
assignment, Cancel, reopening and removal with synthetic bridge responses.

## Manual Studio validation

An existing Bambu PLA Basic Gray spool in an AMS HT was assigned to blue CC3D PLA
Silk with a custom X1 Carbon 0.6 mm profile. Filament Manager, Device, the Prepare
AMS selector, explicit Sync, support-interface selection, sliced preview and the
send-dialog destination reflected the assignment. The destination picker also
showed the override marker. A subsequent PETG test print completed successfully
with the original gray PLA used as support interface while the override was active.

This establishes the display, sync, slice-role, mapping and a successful physical
print with the original Bambu PLA still on the spool. It does not verify a
third-party refill or the exact emitted temperature and flow commands. Debug provenance fields
have compiled; their live contents remain to be checked.

## Additional checks and scope

1. Compare distinct spools of the same product and check identity stability through
   scans, rotation, reinsertion, restart, slot movement and another printer.
2. Independently inspect emitted temperature and flow settings against the assigned
   profile. A third-party refill print would test material/profile suitability,
   rather than a different Studio override path.
3. Check saved/reopened projects, unaffected spools, assignment removal followed by
   sync, and errors when the assigned profile is deleted.
4. Check old empty status, weight estimates, calibration and auto-refill behavior.

Related work: upstream PR #12314 concerns preferred profiles for spools. Coordinate
storage and behavior if that work merges. Find Filament integration (PR #12982)
and local splash branding are excluded from this standalone submission.
