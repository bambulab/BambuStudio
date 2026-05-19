# ColorCut Validation Asset

`colorcut_validation_cube.obj` is a small manual validation model for the guarded ColorCut path.

Validation intent:
- one single model-part volume
- non-uniform per-face color layout after import
- geometry spans both sides of a mid-plane cut

Suggested manual check:
1. Import `colorcut_validation_cube.obj` into the validation build.
2. Confirm the importer preserves the three face materials as distinct paint regions.
3. Cut the model at `Z = 0` with the standard planar cut tool.
4. Verify both resulting parts retain mixed face colors rather than collapsing to one color.
5. Verify the newly generated cut surfaces use a consistent fallback color per side instead of losing appearance completely.

Automated companion check:
- build and run the optional `colorcut_validation` sandbox target to exercise the same provenance-based transfer path with synthetic MMU paint and synthetic repository-backed 3MF triangle colors.