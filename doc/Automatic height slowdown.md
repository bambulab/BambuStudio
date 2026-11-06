# Automatic height slowdown

Enable **Process → Speed → Automatic height slowdown (100% to 50%)** to gradually reduce motion as each model gets taller. The checkbox is available in Simple mode; no starting speeds or heights need to be entered.

The normal ramp starts at 100% on the first model layer, reaches 75% halfway through the model-layer height range, and reaches 50% on the last model layer. Each object uses its own layer range, including on plates with objects of different heights. A single-layer object retains its normal values.

Printing speed is scaled after the existing process and filament volumetric speed limits are applied. Acceleration and eligible travel use the same factor against their own normal values. Extrusion distance per millimeter, flow calibration settings, temperatures and cooling settings are unchanged. The existing cooling and firmware limits may reduce the resulting speed further.

The ramp applies to model extrusion paths and their associated travel, including short-distance travel and Z-hop descent. Support, skirt, brim, purge and other excluded paths retain their existing settings. It is calculated when slicing, so it is visible in the toolpath preview and time estimate; it does not switch the printer's runtime speed mode.

## Optional initial boost

With automatic slowdown enabled, **Start at 120% printing speed** permits a faster start above the first three model layers. Those first three layers retain their normal printing speeds. From the fourth model layer, the printing-speed factor ramps from up to 120% to 50% at the top. If the fourth layer is already the last, it uses 50%; objects with three or fewer model layers retain their normal printing speeds.

The boost is limited by the active filament's configured or adaptive volumetric cap and the active physical extruder's configured standard XY motion envelope, with a valid E-axis limit also applied. Unknown or nonpositive flow/XY limits prevent an increase. A path already at its flow limit may receive no boost. Upward boosting is not applied during flow calibration, to nonplanar sloped paths, or in spiral-vase mode; later slowdown is still applied. Travel and acceleration retain the ordinary 100% to 50% ramp and are never boosted.

## Existing projects and custom tuning

Both options default off. Existing profiles and the manual **Slow down by height** settings keep their previous behavior. Automatic mode takes precedence while enabled and hides the manual controls; disabling it restores them. An unset nullable override does not enable either option.

A percentage ramp is a preset, not an estimate of the model's stiffness. Inspect the sliced speeds and assess the physical result before treating it as a proven profile for a particular model.
