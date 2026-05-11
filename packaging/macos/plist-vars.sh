# Source-only helper: export SLIC3R_* version vars from version.inc.
# Used by scripts/pixi/macos/{package,dmg}.sh so they share a single
# parse of the canonical version source.
#
# Requires: $PIXI_PROJECT_ROOT

if [[ -z "${PIXI_PROJECT_ROOT:-}" ]]; then
    echo "plist-vars.sh: PIXI_PROJECT_ROOT not set" >&2
    exit 1
fi

_inc="$PIXI_PROJECT_ROOT/version.inc"
[[ -f "$_inc" ]] || { echo "plist-vars.sh: $_inc not found" >&2; exit 1; }

SLIC3R_APP_NAME=$(awk -F'"' '/SLIC3R_APP_NAME /{print $2}' "$_inc")
SLIC3R_APP_KEY=$(awk  -F'"' '/SLIC3R_APP_KEY /{print  $2}' "$_inc")
SLIC3R_VERSION=$(awk  -F'"' '/SLIC3R_VERSION /{print  $2}' "$_inc")
SLIC3R_BUILD_ID="${SLIC3R_BUILD_ID:-$SLIC3R_VERSION}"

export SLIC3R_APP_NAME SLIC3R_APP_KEY SLIC3R_VERSION SLIC3R_BUILD_ID
unset _inc
