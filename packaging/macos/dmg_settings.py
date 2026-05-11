"""dmgbuild settings for BambuStudio.

Invoked by scripts/pixi/macos/dmg.sh. Stays minimal in v1; dmgbuild
also supports `background`, `icon_locations`, `window_rect`,
`badge_icon` if we ever want a polished mounted-window experience.
"""
import os

# Inputs from dmg.sh
files = [os.environ["DMG_APP_PATH"]]
symlinks = {"Applications": "/Applications"}

# UDZO = zlib-compressed read-only DMG (standard for distribution).
# HFS+ mounts on every macOS version we'd ever ship to; APFS DMGs
# require 10.13+.
format = "UDZO"
filesystem = "HFS+"

volume_name = "BambuStudio"
