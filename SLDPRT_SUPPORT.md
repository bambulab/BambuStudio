# SolidWorks part import

Open or drag a `.sldprt` / `.SLDPRT` file into Bambu Studio. The command-line
model loader and Reload from disk use the same importer. No SolidWorks
installation, separate converter, account, or network connection is needed.

The importer reads the **saved display tessellation** from modern SolidWorks
part files. Coordinates are converted from metres to millimetres once; small
parts are excluded from the GUI's automatic inch/metre guessing. Faces and
bodies share one mesh, which can be split using Bambu Studio's mesh tools.

The source part's saved image quality determines curve resolution. In
SolidWorks, enable **Tools → Options → Document Properties → Image Quality →
Save tessellation with part document**, increase shaded image quality as needed,
and save the part. See the [SolidWorks documentation](https://help.solidworks.com/2026/english/SolidWorks/sldworks/HIDD_OPTIONS_IMAGE_QUALITY.htm).

This imports the saved mesh, not the feature tree or exact Parasolid surfaces.
It cannot rebuild a missing/stale cache, retessellate surfaces, or select a
configuration. Save the desired configuration before importing. Assemblies,
drawings, legacy OLE files (normally 2014 and earlier), and ZIP/3DExperience
containers are not supported. Ambiguous multiple display caches are rejected.
A cache may be coarse, incomplete, or non-manifold; inspect normal mesh warnings
and the sliced preview. Use STEP for controlled CAD tessellation.

## Tests

The core binary parser can be tested without compiling Bambu Studio:

```sh
cmake -S tests/solidworks -B build/sldprt-tests
cmake --build build/sldprt-tests
ctest --test-dir build/sldprt-tests --output-on-failure
```

Tests cover metre conversion, alternating triangle-strip winding, unaligned
records, all stream-name rotation keys, duplicate/ambiguous streams, channel
validation, cancellation, truncated/corrupt/oversized inputs, deterministic
mutations, and a native NIST SolidWorks 2018 part. For memory checks on Clang,
configure with `-DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'`.

The `sldprt_model_tests` target also checks the shared model loader,
case-insensitive extensions, Unicode paths, cancellation, and a real part.
Configure the full application with `-DSLIC3R_BUILD_TESTS=ON`, build that target,
then run `ctest --test-dir <app-build> -R '^sldprt_model$' --output-on-failure`.
These checks are also included in the full `libslic3r_tests` suite. An optional
`[.local-sldprt]` test imports a caller-owned part specified by the
`BAMBU_SLDPRT_SAMPLE` environment variable and verifies its topology and slices
at nine interior heights; the private part is not added to the repository.

The parser caps input at 512 MiB, each inflated display stream at 256 MiB,
total inflation at 512 MiB, and vertices at 8,388,608. Unsupported files produce
an import error with a STEP fallback instead of an empty successful import.

Format references and third-party notices are in
`src/libslic3r/Format/SLDPRT-LICENSE.txt`. NIST fixture provenance is in
`tests/data/test_sldprt/README.md`.
