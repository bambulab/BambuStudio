# Material color labels

The tests exercise the production formatter with synthetic filament data. They cover named and unnamed single colors, multicolor and gradient separators, gradient order, opacity, empty colors, and a Chinese name. They do not require a printer or AMS.

Build and run:

```sh
cmake --build build --target material_color_label_tests
ctest --test-dir build -R '^material_color_label_tests$' --output-on-failure
```

To generate the synthetic review image, from a graphical desktop session:

```sh
cmake --build build --target material_color_label_preview
build/tests/material_color_label/material_color_label_preview /tmp/material-color-label-preview.png
```

The preview uses the production label formatter and illustrative swatches. It is not a screenshot of the AMS dialog and does not verify dialog layout or device data. Check those separately with a real filament in Materials Setting.
