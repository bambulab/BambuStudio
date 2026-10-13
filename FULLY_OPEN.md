# Open networking build

This fork integrates Open Bamboo Networking into Bambu Studio and disables the acquisition of the stock proprietary networking/camera packages when `FULLY_OPEN_BUILD=ON` (the default).

Start with [the README](README.md), then use:

- [Build and run instructions](docs/BUILDING.md)
- [Architecture and source map](docs/ARCHITECTURE.md)
- [Verification and build provenance](docs/STATUS.md)
- [Troubleshooting](docs/FAQ.md)

Both application and networking source retain their upstream licenses. The Windows build still uses Microsoft platform components and some prebuilt open-source runtime packages. The provenance guide describes the recorded local build without claiming an entirely source-built or independently reproducible distribution.
