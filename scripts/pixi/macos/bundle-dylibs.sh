#!/usr/bin/env bash
# Walk the Mach-O dependency graph of $1 (binary or .app's main exec),
# copy every @rpath conda dylib into $2 (Contents/Frameworks of the .app),
# and rewrite $1's LC_RPATH so it resolves them at @executable_path/../Frameworks.
#
# Why this is short:
#   conda-forge dylibs already ship with install-name = @rpath/<basename>
#   and a single LC_RPATH of @loader_path/. Co-locating them in one dir
#   makes inter-dylib resolution Just Work — no per-edge `-change` needed.
#
# Args:
#   $1  path to the binary to start the walk from
#   $2  destination directory for copied dylibs (will be mkdir -p'd)
#
# Requires env: CONDA_PREFIX

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <binary> <frameworks-dir>" >&2
    exit 2
fi

binary="$1"
fwdir="$2"
[[ -f "$binary" ]] || { echo "bundle-dylibs: binary not found: $binary" >&2; exit 1; }
[[ -n "${CONDA_PREFIX:-}" ]] || { echo "bundle-dylibs: CONDA_PREFIX not set" >&2; exit 1; }

src_lib="$CONDA_PREFIX/lib"
mkdir -p "$fwdir"

# bash 3.2 on macOS: simulate a set with a delimited string.
visited=":"

# Append a basename to the queue (one per line) if we haven't seen it.
queue=""
enqueue() {
    local path="$1" base
    base="$(basename "$path")"
    case "$visited" in
        *":$base:"*) return 0 ;;
    esac
    visited="$visited$base:"
    queue="$queue$path
"
}

# Scan otool -L output for one Mach-O file and enqueue its deps.
scan() {
    local f="$1" line base src
    # `otool -L` first line is the file header, skip with tail -n +2.
    while IFS= read -r line; do
        # Each dep line is "<path> (compatibility version ...)".
        line="${line#"${line%%[![:space:]]*}"}"
        line="${line%% (*}"
        case "$line" in
            ""|/System/*|/usr/lib/*) continue ;;
            @rpath/*) base="${line#@rpath/}" ;;
            *)        base="$(basename "$line")" ;;
        esac
        src="$src_lib/$base"
        if [[ -f "$src" ]]; then
            enqueue "$src"
        else
            echo "bundle-dylibs: WARN cannot resolve $line (no $src)" >&2
        fi
    done < <(otool -L "$f" | tail -n +2 | awk '{print $1}')
}

# 1. Seed from the binary.
scan "$binary"

# 2. BFS over the dep graph.
while [[ -n "$queue" ]]; do
    src="${queue%%$'\n'*}"
    queue="${queue#*$'\n'}"
    [[ -n "$src" ]] || continue
    base="$(basename "$src")"
    dst="$fwdir/$base"
    if [[ ! -f "$dst" ]]; then
        cp -L "$src" "$dst"
        chmod u+w "$dst"
    fi
    scan "$dst"
done

# 3. Defensive: every copied dylib gets install-name = @rpath/<basename>.
#    conda's are already correct, but rewriting is idempotent and cheap.
for f in "$fwdir"/*.dylib; do
    [[ -f "$f" ]] || continue
    install_name_tool -id "@rpath/$(basename "$f")" "$f"
done

# 4. Flip the binary's rpaths.
#    Drop any absolute LC_RPATH (the .pixi/envs/default/lib one we baked in
#    at build time); keep relative @-style rpaths conda compilers added.
while IFS= read -r rp; do
    [[ -n "$rp" ]] || continue
    case "$rp" in
        @*) ;;   # @loader_path/, @executable_path/ - keep
        *)  install_name_tool -delete_rpath "$rp" "$binary" 2>/dev/null || true ;;
    esac
done < <(otool -l "$binary" | awk '/LC_RPATH/{found=1; next} found && /path /{print $2; found=0}')

# Add our canonical rpath if not already present.
if ! otool -l "$binary" \
        | awk '/LC_RPATH/{found=1; next} found && /path /{print $2; found=0}' \
        | grep -qx '@executable_path/../Frameworks'; then
    install_name_tool -add_rpath '@executable_path/../Frameworks' "$binary"
fi

# 5. Sanity check.
missing=0
while IFS= read -r line; do
    line="${line#"${line%%[![:space:]]*}"}"
    line="${line%% (*}"
    case "$line" in
        @rpath/*)
            base="${line#@rpath/}"
            if [[ ! -f "$fwdir/$base" ]]; then
                echo "bundle-dylibs: MISSING $line (not in $fwdir)" >&2
                missing=1
            fi
            ;;
    esac
done < <(otool -L "$binary" | tail -n +2 | awk '{print $1}')

count=$(find "$fwdir" -maxdepth 1 -name '*.dylib' | wc -l | tr -d ' ')
echo "bundle-dylibs: $count dylibs in $fwdir"
[[ $missing -eq 0 ]] || exit 1
