# fff_print Legacy UT Migration Inventory

This document tracks migration of legacy `tests/fff_print` coverage into focused smoke targets.

Policy:
- Preserve production behavior. Test migration must not change business logic.
- Split by real ownership boundaries, not just to make tests easy.
- Migrating a legacy case into the repository does not automatically mean it belongs in the PR gate.
- Keep fixture-heavy G-code export and performance checks out of PR smoke until they have a separate manual/nightly target.

Status key:
- `done`: Migrated into a focused smoke target and verified locally.
- `partial`: Important coverage exists, but legacy scenarios are not fully migrated.
- `pending`: Candidate for migration.
- `manual/nightly`: Valuable, but too heavy or fixture-dependent for the fast PR gate.
- `skip`: Disabled, placeholder, obsolete, or not meaningful as an executable regression.

## Current Smoke Targets

| Target | Boundary | Current role |
|---|---|---|
| `libslic3r_config_tests` | config-core rules that do not require `Print::apply()` or full print state | Fast PR smoke |
| `gcodewriter_smoke_tests` | `GCodeWriter`, `Extruder`, writer-local config/state | Fast PR smoke |
| `model_basic_smoke_tests` | `Model` assembly, placement, `TriangleMeshBasic` | Fast PR smoke |
| `print_filament_mapping_smoke_tests` | print filament mapping and config-facing map updates | Fast PR smoke |
| `print_apply_validate_smoke_tests` | `Print::apply()` / `Print::validate()` initialization invariants without full slicing | Fast PR smoke when apply/validate paths change |
| `print_perimeters_stage_smoke_tests` | perimeter generation stage | Fast PR smoke |
| `print_process_math_smoke_tests` | Flow math and `ExtrusionEntityCollection` flattening | Fast PR smoke |
| `print_process_core_smoke_tests` | solid layer re-slice behavior that still needs the heavy print core | Fast PR smoke when core print process paths change |
| `print_object_layers_smoke_tests` | object layer height generation | Fast PR smoke when object layer/slicing paths change |
| `print_adhesion_smoke_tests` | skirt/brim adhesion generation | Fast PR smoke when adhesion paths change |
| `fill_smoke_tests` | fill geometry and path generation | Fast PR smoke when fill paths change |
| `support_material_smoke_tests` | support material layer generation | Fast PR smoke when support paths change |
| `trianglemesh_geometry_smoke_tests` | TriangleMesh geometry, topology, primitive factories, and slicing | Fast PR smoke when geometry paths change |

## Legacy File Inventory

| Legacy file | Legacy scenario | Status | Target / future bucket | Notes |
|---|---|---|---|---|
| `test_gcodewriter.cpp` | `lift() is not ignored after unlift()` | done | `gcodewriter_smoke_tests` | Migrated as representative lift-state heights; the obsolete legacy source is no longer part of `fff_print_tests` because it calls removed `GCodeWriter` / `GCodeConfig` APIs. |
| `test_gcodewriter.cpp` | `set_speed emits values with fixed-point output` | done | `gcodewriter_smoke_tests` | Migrated; the current smoke file is the maintained coverage for the modern writer API. |
| `test_gcode.cpp` | `Origin manipulation` | manual/nightly | `fff_print_tests`; future `gcode_core_smoke_tests` or G-code export target | Kept in the residual legacy target for now. `GCode.cpp` pulls the full export dependency chain, so do not force it into `GCodeWriter` or PR smoke until a proper G-code core/export boundary exists. |
| `test_model.cpp` | `Model construction` | done | `model_basic_smoke_tests` | Migrated via object, volume, instance, mesh preservation checks; the obsolete legacy source is no longer part of `fff_print_tests` because its remaining print/export assertion still crosses full print application and failed on the current bed-fit path. |
| `test_trianglemesh.cpp` | basic mesh statistics | done | `model_basic_smoke_tests`, `trianglemesh_geometry_smoke_tests` | Migrated within `TriangleMeshBasic` boundary for volume, facet count, size, bounds, and vertex/facet arrays; `center()` is covered in the geometry target because it widens the light model-basic link boundary through `BoundingBox3Base`. The obsolete legacy source is no longer part of `fff_print_tests` because all maintained cases are now covered by smoke targets and the old exact area equality is float-precision fragile on Windows. |
| `test_trianglemesh.cpp` | translation / cube factory basics | done | `model_basic_smoke_tests` | Added within `TriangleMeshBasic` boundary, including translated bounds and the legacy single-origin-vertex cube factory representative. |
| `test_trianglemesh.cpp` | transformation functions | done | `trianglemesh_geometry_smoke_tests` | Migrated and path-filtered into PR smoke, including axis scale-up/down, vector/double translation, rotation, and origin alignment representatives. |
| `test_trianglemesh.cpp` | slice behavior | done | `trianglemesh_geometry_smoke_tests` | Migrated and path-filtered into PR smoke. |
| `test_trianglemesh.cpp` | cylinder/sphere factory helpers | done | `trianglemesh_geometry_smoke_tests` | Migrated and path-filtered into PR smoke, including primitive topology counts and approximate volumes. |
| `test_trianglemesh.cpp` | split / merge / cut behavior | done | `trianglemesh_geometry_smoke_tests` | Migrated and path-filtered into PR smoke. |
| `test_trianglemesh.cpp` | issue #4486 performance/profile tests | manual/nightly | nightly regression bucket | Performance guard; not PR smoke. |
| `test_flow.cpp` | non-bridge flow math | done | `print_process_math_smoke_tests` | Migrated into light math target; the obsolete legacy source is no longer part of `fff_print_tests` because remaining export-specific setup references removed config key `first_layer_extrusion_width`. |
| `test_flow.cpp` | bridge flow math | done | `print_process_math_smoke_tests` | Migrated into light math target; the old source also contains empty bridge-flow placeholders that should not be kept as executable coverage. |
| `test_flow.cpp` | 0.25mm nozzle auto-width edge cases | done | `print_process_math_smoke_tests` | Migrated into light math target. |
| `test_flow.cpp` | disabled solid spacing non-zero edge case | done | `print_process_math_smoke_tests` | Migrated against the current `Fill::_adjust_solid_spacing` helper boundary; the old `Flow::new_from_spacing` API no longer exists. |
| `test_flow.cpp` | extrusion width specifics through G-code export | manual/nightly | future G-code export target | Depends on full G-code generation. |
| `test_flow.cpp` | bridge flow specifics placeholders | skip | none | Empty placeholder sections. |
| `test_extrusion_entity.cpp` | collection flattening | done | `fff_print_tests`, `print_process_math_smoke_tests` | Migrated into light math/data-structure target. The legacy residual target still keeps this low-cost case because it is stable and does not cross print/export boundaries. |
| `test_printobject.cpp` | object layer heights | done | `print_object_layers_smoke_tests` | Migrated and split from print process core by object-layer responsibility; the obsolete legacy source is no longer part of `fff_print_tests` because it still initializes through the old full-print helper path and failed on current bed-fit validation. |
| `test_printobject.cpp` | disabled nozzle/max layer-height limit block | skip | none | Legacy block was already disabled; current layer-height generation does not preserve that obsolete fixed-count assumption, so do not change production behavior to satisfy it. |
| `test_print.cpp` | perimeter generation | done | `print_perimeters_stage_smoke_tests` | Migrated as stage executable smoke; the obsolete legacy source is no longer part of `fff_print_tests` because remaining brim assertions reference removed `Print::brim` API. |
| `test_print.cpp` | skirt generation | done | `print_adhesion_smoke_tests` | Migrated with adhesion scenarios and split from print process core. |
| `test_print.cpp` | solid surface re-slice classification | done | `print_process_core_smoke_tests` | Migrated. |
| `test_print.cpp` | brim generation | done | `print_adhesion_smoke_tests` | Migrated with adhesion scenarios and split from print process core. |
| `test_skirt_brim.cpp` | print-core skirt/brim geometry cases | done | `print_adhesion_smoke_tests` | Migrated representative adhesion and config edge cases, including generated skirt loops, large brim vs skirt, disabled skirt height, brim width/line-width scaling, and large minimum skirt length; the obsolete legacy source is no longer part of `fff_print_tests` because remaining cases are export/manual or reference removed `Print::brim` API. |
| `test_skirt_brim.cpp` | skirt height across G-code layers | manual/nightly | future G-code export smoke | Requires full G-code export and parser speed/layer inspection; keep out of PR smoke until a stable export harness exists. |
| `test_skirt_brim.cpp` | brim generated in G-code | manual/nightly | future G-code export smoke | Requires G-code export/parser inspection; current print-core smoke covers brim geometry directly instead. |
| `test_skirt_brim.cpp` | brim tool-selection through exported G-code | skip/manual | future G-code export smoke only if behavior is revalidated | Legacy cases are inside `#if 0` and note a real historical mismatch; do not revive without product confirmation and a stable export harness. |
| `test_skirt_brim.cpp` | brim ears cases | skip | none | Legacy cases are disabled and current config/behavior boundary is not established for PR smoke. |
| `test_skirt_brim.cpp` | overhang support plus brim exports G-code | manual/nightly | future G-code export smoke | Crosses support + adhesion + export; too broad for PR smoke. |
| `test_skirt_brim.cpp` | support-containing skirt length parser check | skip/manual | future export/support regression only if completed | Legacy block is disabled and marked unfinished; do not migrate as an executable regression yet. |
| `test_fill.cpp` | rectilinear path length / hole avoidance | done | `fill_smoke_tests` | Migrated and path-filtered into PR smoke. |
| `test_fill.cpp` | missing infill segment regression | done | `fill_smoke_tests` | Migrated and path-filtered into PR smoke. |
| `test_fill.cpp` | rotated square fill | done | `fill_smoke_tests` | Migrated and path-filtered into PR smoke. |
| `test_fill.cpp` | adjusted solid distance | done | `print_process_math_smoke_tests` | Migrated against current `Fill::_adjust_solid_spacing` helper boundary, including the legacy 250/47 representative and a non-zero narrow-width regression. |
| `test_fill.cpp` | solid surface fill helper cases | partial | `fill_smoke_tests` | Migrated narrow representative; the obsolete legacy source is no longer part of `fff_print_tests` because the large direct-fill representative is unstable against current Fill internals and crashed the full legacy executable on Windows. |
| `test_fill.cpp` | legacy path chaining comment block | done | `print_process_math_smoke_tests` | Migrated the lightweight `ExtrusionEntityCollection::chained_path_from()` representatives from the old Perl comment block into the current extrusion-entity boundary. The horizontal fixture is asserted against current nearest-path behavior rather than the obsolete exact order from the comment, preserving production logic. |
| `test_support_material.cpp` | raft layer count | done | `support_material_smoke_tests` | Migrated and path-filtered into PR smoke; the obsolete legacy source is no longer part of `fff_print_tests` because remaining checks use old config access patterns and incomplete support internals. |
| `test_support_material.cpp` | support layer Z/contact distance | partial | `support_material_smoke_tests` | Migrated stable first-Z and thickness-bound variants through raft-backed support layers and the legacy cube-with-hole support mesh, including 0.3mm first layer and nozzle-sized layer-height representatives; contact-distance/top-spacing internals remain manual/nightly until a stable support-core inspection boundary exists. |
| `test_support_material.cpp` | forced support / bridge speed disabled block | skip/manual | none or nightly | Currently disabled / incomplete. |
| `test_printgcode.cpp` | basic G-code output structure | manual/nightly | future G-code export smoke | Valuable, but should not be mixed into current PR smoke; keep out of `fff_print_tests` until a proper export harness owns the full-export dependency and Windows compile/runtime behavior. |
| `test_printgcode.cpp` | complete objects, support material, macros | manual/nightly | future G-code export smoke | Heavy export coverage; keep separate behind the same export harness boundary and out of the current legacy full target. |
| `test_data.cpp` | single-mesh `init_print` model/print setup | done | `print_apply_validate_smoke_tests` | Migrated as a direct apply/validate initialization invariant without full slicing or G-code export; the helper source is no longer part of the residual `fff_print_tests` executable, and its embedded legacy scenario remains compile-gated for any future explicit helper-test target. |
| `test_data.cpp` | `print.process()` and G-code output through helpers | manual/nightly | future G-code export smoke | Helper coverage crosses into full slicing/export; keep out of PR smoke. |

## Post-Migration Responsibility Splits

| Area | Status | Target / future bucket | Notes |
|---|---|---|---|
| Filament mapping pure rules | done | `libslic3r_config_tests` | `Print::is_dynamic_group_reorder()` and `Print::get_available_filament_map_modes()` now delegate their pure decisions to `FilamentMappingRules`, keeping the existing `Print` API behavior while covering those rules in the lighter config-core target. |
| Filament mapping config synchronization | pending | `print_filament_mapping_smoke_tests` until a narrower state-sync boundary exists | `update_filament_maps_to_config()` still needs `Print` state and config synchronization, so it remains in the heavier print filament mapping boundary. |
| Print apply/validate initialization | done | `print_apply_validate_smoke_tests` | `test_print_init_smoke.cpp` now runs against real `PrintApply.cpp` and `PrintValidate.cpp`; `Print::process()` remains guarded by apply/validate compat so the target cannot silently become full slicing coverage. |

## Next Migration Candidates

1. Remaining `Fill` large solid surface helper case: keep manual unless current Fill behavior is intentionally updated; the narrow representative is in `fill_smoke_tests`, and solid-spacing adjustment representatives are in the lighter `print_process_math_smoke_tests`.
2. `SupportMaterial` contact-distance/top-spacing internals: keep manual/nightly unless a stable support-core harness can inspect those layers without broad export or brittle geometry assumptions; the cube-with-hole layer-bound representatives are now in PR smoke.
3. `GCode` origin manipulation, `PrintGCode` export checks, and the export half of `Model construction` / `test_data` helpers: require a proper G-code core/export harness first; direct `GCode.cpp` / full-export linkage is too broad and currently unsafe for PR smoke.
4. `Skirt/Brim` G-code parser/tool-selection leftovers: keep manual/nightly or skip as documented above; print-core geometry representatives are already in PR smoke.
5. Filament mapping config synchronization: extract only after a stable state-sync boundary is identified; do not move `update_filament_maps_to_config()` merely to reduce test time.
6. With the stable legacy items now migrated or explicitly classified, continue runtime reduction with target-level path filter tuning and CI timing evidence.

## Manual / Future Targets

| Future target | Moves from | Purpose |
|---|---|---|
| `gcode_export_smoke_tests` | `test_printgcode.cpp` candidates | Manual/nightly G-code export regressions. |
