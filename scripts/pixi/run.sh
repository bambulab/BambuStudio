#!/usr/bin/env bash
# Launch BambuStudio with the env workarounds the pixi-based build needs:
#
#   LIBGL_ALWAYS_SOFTWARE=1         conda libGLX dispatches to host vendor
#                                   drivers (e.g. nvidia) and the resulting
#                                   mix can crash MakeCurrent. Software GL
#                                   sidesteps it; remove if your driver/conda
#                                   GL combo is known good.
#   XDG_DATA_DIRS / GTK_THEME        pixi env's libgtk-3 needs to see host
#                                   theme files (e.g. /usr/share/themes/Adwaita)
#                                   to populate gtk_settings.
#   SSL_CERT_FILE / CURL_CA_BUNDLE   conda's libcurl bakes its OPENSSLDIR at
#                                   build time; outside `pixi run` it can't
#                                   find a CA bundle, breaking https auth.
#
# Args pass through to the binary, e.g.:
#   pixi run bambu-studio --help
#   pixi run bambu-studio /path/to/file.3mf

set -euo pipefail

: "${PIXI_PROJECT_ROOT:?run via 'pixi run' so PIXI_PROJECT_ROOT is set}"
: "${CONDA_PREFIX:?CONDA_PREFIX must be set; run via 'pixi run'}"

# Build type selected by the calling task via env (default debug, matching
# `pixi run build`'s default). User CLI args pass through to the binary.
build_type="${BAMBU_BUILD_TYPE:-debug}"
build_dir="$PIXI_PROJECT_ROOT/build/${build_type}"
bin="$build_dir/src/bambu-studio"
if [[ ! -x "$bin" ]]; then
    echo "$bin not found. Run 'pixi run build-${build_type}' first." >&2
    exit 1
fi

# In-tree binary expects resources at <bin>/../../resources/ (i.e. build/<type>/resources/).
# `pixi run build` doesn't create this — the AppImage staging usually does — so
# a one-shot symlink keeps the workflow seamless.
if [[ ! -e "$build_dir/resources" ]]; then
    ln -sfn "$PIXI_PROJECT_ROOT/resources" "$build_dir/resources"
fi

# Linux-only workarounds: macOS doesn't ship GTK, uses its own GL, and
# conda's libcurl on osx-arm64 already finds the system trust store.
if [[ "$(uname)" != "Darwin" ]]; then
    export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-1}"
    export XDG_DATA_DIRS="${XDG_DATA_DIRS:-/usr/share:/usr/local/share}:${CONDA_PREFIX}/share"
    export GTK_THEME="${GTK_THEME:-Adwaita}"

    # Probe the usual CA bundle locations and stop at the first one that exists.
    for ca in \
        /etc/ssl/certs/ca-certificates.crt \
        /etc/pki/tls/certs/ca-bundle.crt \
        /etc/ssl/cert.pem \
        "${CONDA_PREFIX}/ssl/cacert.pem"; do
        if [[ -f "$ca" ]]; then
            export SSL_CERT_FILE="${SSL_CERT_FILE:-$ca}"
            export CURL_CA_BUNDLE="${CURL_CA_BUNDLE:-$ca}"
            break
        fi
    done
fi

exec "$bin" "$@"
