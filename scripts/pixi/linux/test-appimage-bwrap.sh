#!/usr/bin/env bash
# Smoke-launch the latest AppImage in an isolated env to simulate a clean
# user machine, then grep the output for known startup-time failure modes.
#
# Isolation: outer bwrap masks the build host's $CONDA_PREFIX with a tmpfs
# so the AppImage cannot fall back to "happens to exist locally"; everything
# must resolve via the AppRun's inner bwrap bind onto AppDir/usr.
#
# Display: an Xvfb on :99 is started so wxWidgets / GTK can open the main
# window. We don't attach a screen-grabber -- pass/fail comes from stderr.

set -euo pipefail

cd "$(dirname "$0")/../../.."   # repo root

AI="$(ls -t build/release/BambuStudio-*-x86_64.AppImage 2>/dev/null | head -1)"
[[ -n "$AI" ]] || { echo "No AppImage in build/release/. Run 'pixi run dist' first." >&2; exit 2; }

build_prefix="$PWD/.pixi/envs/default"

# 1. Virtual display (optional -- Fontconfig/SSL errors surface before GUI;
# WebKit/WebView errors need a display).
if command -v Xvfb >/dev/null; then
    Xvfb :99 -screen 0 1280x800x24 >/tmp/xvfb.log 2>&1 &
    XVFB=$!
    trap 'kill $XVFB 2>/dev/null; wait $XVFB 2>/dev/null || true' EXIT
    for _ in 1 2 3 4 5; do [[ -e /tmp/.X11-unix/X99 ]] && break; sleep 0.2; done
    DISPLAY_ARG=(--setenv DISPLAY :99)
else
    echo "Xvfb not found; running without virtual display (Fontconfig/SSL still visible)." >&2
    DISPLAY_ARG=()
fi

LOG=$(mktemp /tmp/appimage-test.XXXXXX.log)
echo "Log: $LOG"
echo "AppImage: $AI"
echo "Masking build prefix: $build_prefix"

# 2. Launch under outer bwrap that masks the build host's $CONDA_PREFIX so the
# AppImage cannot accidentally satisfy baked-in absolute paths from the dev
# tree. --appimage-extract-and-run skips squashfuse (which bwrap can't help
# with) and lets AppRun resolve $HERE inside the extracted dir.
set +e
timeout --signal=TERM --kill-after=3 12 \
    bwrap \
        --ro-bind / / \
        --tmpfs "$build_prefix" \
        --dev-bind /dev /dev \
        --proc /proc \
        --bind /tmp /tmp \
        "${DISPLAY_ARG[@]}" \
        --setenv HOME /tmp \
        --setenv XAUTHORITY "" \
        "$AI" --appimage-extract-and-run \
    >"$LOG" 2>&1 &
PID=$!

# 3. Watch for ~10s, then check liveness.
sleep 10
if kill -0 $PID 2>/dev/null; then
    ALIVE=1
    kill -TERM $PID 2>/dev/null
else
    ALIVE=0
fi
wait $PID 2>/dev/null || true
set -e

# 4. Grep known fatal signatures. Add new patterns as new errors surface.
PATTERNS='Fontconfig error|error adding trust anchors|WebKitNetworkProcess|Unable to spawn|cannot open shared object|undefined symbol|GLib-GIO-ERROR|Segmentation fault|core dumped|Bus error|Aborted \(core dumped\)|terminate called'

ERRS=$(grep -E "$PATTERNS" "$LOG" || true)

if [[ "$ALIVE" -eq 1 && -z "$ERRS" ]]; then
    echo "PASS: process alive at t=10s, no known error signatures."
    echo "Log preserved at $LOG (head):"
    head -20 "$LOG"
    exit 0
fi

echo "FAIL:"
[[ "$ALIVE" -eq 0 ]] && echo "  - Process exited before t=10s."
[[ -n "$ERRS"   ]]   && { echo "  - Known error signatures hit:"; echo "$ERRS" | sed 's/^/      /'; }
echo "--- log tail ---"
tail -60 "$LOG"
exit 1
