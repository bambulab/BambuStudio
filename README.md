# BambuStudio-ZAA

This is a fork of [BambuStudio-ZAA](https://github.com/adob/BambuStudio-ZAA) by Aleksandr Dobkin, ported to **BambuStudio v2.5.3 Beta** (v02.05.03.61). It implements a non-planar slicing feature called **Z anti-aliasing** or **contouring**. The basic idea is to dynamically vary the height of extrusions within a single layer to reduce stair-stepping artifacts on top surfaces.

Prebuilt releases for **Windows, macOS (x86 & ARM) and Linux** are available on the [Releases page](https://github.com/mproper23/BambuStudio-ZAA/releases/).

When starting the application, Z contouring can be controlled under the "Z Contouring" section in Global or Objects settings, under the Quality tab.

## Common Problems

If you are seeing poor quality extrusion, you may be printing too fast. Slow down outer and inner wall and top surface speed to 20 mm/s.

If you are seeing stringing, it may be due to collisions. If you try to extrude a line segment at high z and then try to extrude another line next to it at a low Z, the nozzle will collide with the previous extrusion, damaging it. Try reducing the number of wall loops to 1 or use outer/inner order of walls. The concentric top surface pattern can also cause this depending on the direction.

## Prior Work

This work is based on

- [[1609.03032] Anti-aliasing for fused filament deposition](https://arxiv.org/abs/1609.03032) by Hai-Chuan Song et al.

- [Theaninova/GCodeZAA](https://github.com/Theaninova/GCodeZAA) - GCode post-processing script to enable smooth(-ish) non-planar top surfaces

- [Non-planar models](https://www.printables.com/model/848822-non-planar-3d-printing-test-models) from [@TeachingTech](https://www.printables.com/@TeachingTech)

## Limitations

Collisions are currently not handled though a method of doing so is described in the paper.

The algorithm performs a large number of ray intersection tests and this is done using a general purpose ray intersection algorithm. This can likely be sped up using a purpose-specific structure since the direction vector is always fixed.

## Downloads

| Platform | Download |
|----------|----------|
| Windows (Portable) | [BambuStudio-ZAA_v2.5.3_Windows_portable.zip](https://github.com/mproper23/BambuStudio-ZAA/releases/latest) |
| macOS ARM (Apple Silicon) | [BambuStudio-ZAA_v2.5.3_Mac_universal.zip](https://github.com/mproper23/BambuStudio-ZAA/releases/latest) |
| macOS x86 (Intel) | [BambuStudio-ZAA_v2.5.3_Mac_x86_64.zip](https://github.com/mproper23/BambuStudio-ZAA/releases/latest) |
| Linux (AppImage) | [BambuStudio-ZAA_v2.5.3_Linux.AppImage](https://github.com/mproper23/BambuStudio-ZAA/releases/latest) |

### Linux Notes

If the AppImage crashes on startup, install `libfuse2` or run without FUSE:
```bash
# Option A: Install libfuse2
sudo apt install libfuse2t64   # Ubuntu 24.04+
sudo apt install libfuse2      # Ubuntu 22.04

# Option B: Run without FUSE
chmod +x BambuStudio-ZAA_*.AppImage
./BambuStudio-ZAA_*.AppImage --appimage-extract-and-run
```

## Known Limitations

- **Camera/Cloud features do not work** in forks. Bambu Lab servers reject third-party builds. Use the official BambuStudio for camera feed, cloud printing, and SD card access via cloud. You can still export G-Code and send it via SD card or LAN mode.
- Collisions are currently not handled, though a method of doing so is described in the paper.
- The algorithm performs a large number of ray intersection tests using a general purpose algorithm. This can likely be sped up using a purpose-specific structure since the direction vector is always fixed.

## How to compile

Following platforms are supported for compilation:

- Windows 64-bit, [Compile Guide](https://github.com/bambulab/BambuStudio/wiki/Windows-Compile-Guide)
- Mac 64-bit, [Compile Guide](https://github.com/bambulab/BambuStudio/wiki/Mac-Compile-Guide)
- Linux, [Compile Guide](https://github.com/bambulab/BambuStudio/wiki/Linux-Compile-Guide)

## Report issue

You can add an issue to the [github tracker](https://github.com/mproper23/BambuStudio-ZAA/issues) if **it isn't already present.**

For issues related to the ZAA algorithm itself, see the [original project tracker](https://github.com/adob/BambuStudio-ZAA/issues).

## Original BambuStudio

Bambu Studio is a cutting-edge, feature-rich slicing software.
It contains project-based workflows, systematically optimized slicing algorithms, and an easy-to-use graphic interface, bringing users an incredibly smooth printing experience.

Bambu Studio is based on [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) by Prusa Research, which is from [Slic3r](https://github.com/Slic3r/Slic3r) by Alessandro Ranellucci and the RepRap community.

See the [wiki](https://github.com/bambulab/BambuStudio/wiki) and the [documentation directory](https://github.com/bambulab/BambuStudio/tree/master/doc) for more information.

# License
Bambu Studio is licensed under the GNU Affero General Public License, version 3. Bambu Studio is based on PrusaSlicer by PrusaResearch.

PrusaSlicer is licensed under the GNU Affero General Public License, version 3. PrusaSlicer is owned by Prusa Research. PrusaSlicer is originally based on Slic3r by Alessandro Ranellucci.

Slic3r is licensed under the GNU Affero General Public License, version 3. Slic3r was created by Alessandro Ranellucci with the help of many other contributors.

The GNU Affero General Public License, version 3 ensures that if you use any part of this software in any way (even behind a web server), your software must be released under the same license.

The bambu networking plugin is based on non-free libraries. It is optional to the Bambu Studio and provides extended networking functionalities for users.
By default, after installing Bambu Studio without the networking plugin, you can initiate printing through the SD card after slicing is completed.

