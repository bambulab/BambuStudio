# Repository Guidelines

## Project Structure & Module Organization
- `src/` contains the C++ sources for the slicer (GUI under `src/slic3r/GUI`, core logic in `src/libslic3r`).
- `resources/` stores machine, filament, and UI assets; JSON profiles live under `resources/profiles`.
- `tests/` mirrors libslic3r and GUI modules; sample geometries are in `tests/data`.
- `cmake/` and `deps/` hold build scripts and bundled third-party libraries.

## Build, Test, and Development Commands
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` — configure a release build in `build/`.
- `cmake --build build --target BambuStudio` — compile the application.
- `ninja -C build tests` or `cmake --build build --target tests` — build unit/integration tests.
- `ctest --test-dir build` — run the compiled test suite.
- `./build/src/BambuStudio` — launch the GUI from a local build.

## Coding Style & Naming Conventions
- Follow the existing C++ style: 4-space indentation, braces on new lines, and CamelCase for classes (`CalibrationDialog`) with snake_case for locals and functions (`update_calibration_button_status`).
- Prefer `std::` types and early returns; avoid raw pointers unless required by wxWidgets.
- Run `clang-format` with the repository’s `.clang-format` before submitting C++ changes; use `eslint`/`prettier` for any TypeScript under tooling scripts if touched.

## Testing Guidelines
- Tests reside under `tests/libslic3r` (Catch2) and `tests/fff_print`; name files `test_<feature>.cpp`.
- Add regression fixtures to `tests/data` when reproducing geometry-specific issues.
- Ensure `ctest --output-on-failure` passes locally; aim to keep or raise coverage indicated by `tests/coverage` targets.

## Commit & Pull Request Guidelines
- Follow imperative commit messages (`Add Darkmoon plate temps`) with scoped prefixes where helpful (`GUI:`, `Profile:`).
- Squash fixup commits before opening a PR; reference GitHub issues using `Fixes #123` when closing bugs.
- PRs should describe user impact, include repro steps or screenshots for UI work, and mention relevant build/test commands executed.
- Request review from domain owners listed in `docs/MAINTAINERS.md`; leave drafts open until CI is green.

## Configuration & Security Tips
- Store local secrets (MakerWorld tokens, firmware credentials) outside the repo; never commit them.
- Use `resources/local_override` for experimental profiles rather than editing shared JSON when testing.
