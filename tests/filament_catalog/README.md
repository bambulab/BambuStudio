# Find Filament

## User story

As a maker choosing colors from physical filament swatches, I want to search by
color name across filament families and my own inventory, so I can design a
project's color scheme before loading spools into the printer.

## Behavior

Choose **Find Filament...** from a project filament slot's three-dot menu or
from the bottom of its filament dropdown, above **Add/Remove filaments**.
The picker reuses Studio's installed Bambu color catalog and Filament Manager
inventory. It does not maintain a separate catalog or require loaded AMS spools.

Search uses case-insensitive partial words across names, translations, brands,
materials, product codes, hex colors and inventory notes. Every search word must
match; typo correction is not included. **My filaments only** limits results to
active inventory of any brand. Known Bambu inventory colors merge with catalog
rows; other brands keep their saved color names or hex values.

Applying a row changes the selected project slot's profile and ordered color
components through the existing profile-change path. Double-click is equivalent
to Apply. Missing profiles and invalid saved colors are visible in gray, with
Apply disabled; double-click explains the problem. A matching profile means
installed settings for the selected printer/nozzle, not a calibrated combination.
Cancel leaves the project unchanged. Existing Undo behavior is unchanged.

All six columns sort in either direction. Remaining weight uses measured net
weight when available, otherwise capacity and remaining percentage. Matching
Bambu spools show a count and combined weight. Unknown weights are not treated as
zero; partial totals explicitly include unknown weight. Distinct inventory notes
are combined, searchable, and displayed in full below the selected row.

## Automated checks

Build `filament_catalog_tests`, then run:

```sh
ctest --test-dir build -R '^filament_catalog_tests$' --output-on-failure
```

Coverage includes cross-family and translated search, exact profile identity,
printer/nozzle compatibility, hidden profiles, inventory vendor matching,
malformed colors, gradient order and alpha, inventory status, remaining-weight
calculation and sorting, and note aggregation/search.

## Interactive verification

Verified in the Linux local build: project-slot profile/color selection, the
three-dot menu and filament-dropdown entry points, inventory filtering including
non-Bambu spools, matching profiles
after changing printer/nozzle, remaining weights, searchable notes, sorting,
centering, sort indicators, double-click application, and the unavailable-profile
explanation.

Additional regression checks for reviewers:

- Cancel a modified-preset prompt and verify neither profile nor color changes;
  also exercise Save/Discard/Transfer and multiple project slots.
- Select a gradient and verify its ordered components in the existing palette.
- Save/reopen a 3MF and verify profile/color persistence and fresh slicing.
- Exercise keyboard navigation, dark mode, resizing, and non-English names.
- Confirm the existing color picker and AMS sync still work.
- Double-click a spool with an invalid saved color and confirm the explanation
  leaves the project unchanged.
