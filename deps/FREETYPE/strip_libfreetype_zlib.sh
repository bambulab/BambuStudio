#!/usr/bin/env bash
# Strip global zlib symbols out of FreeType's bundled gzip code on macOS.
#
# FreeType 2.12.1 always embeds its own copy of zlib inside ftgzip.c.o, even
# when configured with FT_DISABLE_ZLIB=TRUE, because some other FreeType TUs
# call FT_Gzip_Uncompress / FT_Stream_OpenGzip. Those zlib internals
# (_inflate, _inflateInit_, _zcalloc, _adler32, ...) are exported as global T
# symbols. When BambuStudio is statically linked, the linker resolves
# Assimp's `inflateInit_` reference to FreeType's bundled 1.2.12 copy while
# `inflate` still binds at runtime to the system libz.1.dylib (1.2.11). The
# resulting z_stream/state ABI mismatch makes Assimp's FBX 7.4 binary parser
# return CompressionFailure for any FBX that uses compressed property arrays
# (the user-visible "无法解析文件格式" for chess_set_2k.fbx / Dragon.fbx).
#
# To keep FT_Gzip_* functional while preventing the symbol pollution, this
# script rewrites ftgzip.c.o inside libfreetype.a so that all the zlib
# helpers it inlines become private (lowercase t) while the public
# FT_Gzip_Uncompress / FT_Stream_OpenGzip stay externally visible.
#
# The script is idempotent: re-running it on an already-patched archive is a
# no-op.

set -euo pipefail

LIBFREETYPE_PATH="${1:?usage: $0 <path-to-libfreetype.a>}"

if [[ ! -f "${LIBFREETYPE_PATH}" ]]; then
    echo "[strip_libfreetype_zlib] libfreetype.a not found: ${LIBFREETYPE_PATH}" >&2
    exit 1
fi

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "[strip_libfreetype_zlib] skipped on non-macOS"
    exit 0
fi

# Symbols that must remain external (anything else in ftgzip.c.o gets hidden).
LOCALIZE_SYMBOLS=(
    _crc32_combine
    _crc32_combine64
    _crc32_combine_gen
    _crc32_combine_gen64
    _crc32_combine_op
    _crc32_z
    _get_crc_table
    _inflate
    _inflateInit_
    _inflateInit2_
    _inflateEnd
    _inflateReset
    _inflateReset2
    _inflateResetKeep
    _inflateSetDictionary
    _inflateSyncPoint
    _inflateUndermine
    _inflateValidate
    _inflate_fast
    _inflate_table
    _zError
    _zcalloc
    _zcfree
    _zlibCompileFlags
    _zlibVersion
    _zmemcmp
    _zmemcpy
    _zmemzero
    _adler32
    _adler32_combine
    _adler32_combine64
    _adler32_z
)

# Quick guard: if libfreetype.a no longer exports zlib globals, we're done.
if ! nm "${LIBFREETYPE_PATH}" 2>/dev/null | grep -qE "^[0-9a-f]+ T _(inflate|inflateInit_|zcalloc|zlibVersion)$"; then
    echo "[strip_libfreetype_zlib] libfreetype.a already clean: ${LIBFREETYPE_PATH}"
    exit 0
fi

WORK_DIR="$(mktemp -d -t libfreetype_strip.XXXXXX)"
trap 'rm -rf "${WORK_DIR}"' EXIT

LOCALIZE_LIST="${WORK_DIR}/localize.list"
printf '%s\n' "${LOCALIZE_SYMBOLS[@]}" > "${LOCALIZE_LIST}"

ARCHES=$(lipo -archs "${LIBFREETYPE_PATH}" 2>/dev/null || echo "")
if [[ -z "${ARCHES}" ]]; then
    ARCHES="$(uname -m)"
fi

PATCHED_SLICES=()
for ARCH in ${ARCHES}; do
    SLICE_DIR="${WORK_DIR}/${ARCH}"
    mkdir -p "${SLICE_DIR}"
    SLICE_LIB="${SLICE_DIR}/libfreetype.a"
    if [[ "${ARCHES}" == *" "* ]]; then
        lipo "${LIBFREETYPE_PATH}" -thin "${ARCH}" -output "${SLICE_LIB}"
    else
        cp "${LIBFREETYPE_PATH}" "${SLICE_LIB}"
    fi

    pushd "${SLICE_DIR}" >/dev/null
    ar -x libfreetype.a ftgzip.c.o
    # macOS' ld -r needs an explicit platform_version, otherwise it complains
    # about missing LC_BUILD_VERSION. We mirror the toolchain default
    # (10.15 → SDK 13.1) used by BambuStudio's CMake setup.
    xcrun ld -r ftgzip.c.o \
        -unexported_symbols_list "${LOCALIZE_LIST}" \
        -arch "${ARCH}" \
        -platform_version macos 10.15 13.1 \
        -o ftgzip.clean.c.o
    mv ftgzip.clean.c.o ftgzip.c.o
    ar -d libfreetype.a ftgzip.c.o
    ar -q libfreetype.a ftgzip.c.o
    ranlib libfreetype.a 2>/dev/null || true
    popd >/dev/null

    PATCHED_SLICES+=("${SLICE_LIB}")
done

if (( ${#PATCHED_SLICES[@]} == 1 )); then
    cp "${PATCHED_SLICES[0]}" "${LIBFREETYPE_PATH}"
else
    lipo -create "${PATCHED_SLICES[@]}" -output "${LIBFREETYPE_PATH}"
fi

if nm "${LIBFREETYPE_PATH}" 2>/dev/null | grep -qE "^[0-9a-f]+ T _(inflate|inflateInit_|zcalloc|zlibVersion)$"; then
    echo "[strip_libfreetype_zlib] FAILED to hide zlib externals in ${LIBFREETYPE_PATH}" >&2
    exit 2
fi

if ! nm "${LIBFREETYPE_PATH}" 2>/dev/null | grep -qE "^[0-9a-f]+ T _FT_Gzip_Uncompress$"; then
    echo "[strip_libfreetype_zlib] FAILED to preserve _FT_Gzip_Uncompress in ${LIBFREETYPE_PATH}" >&2
    exit 3
fi

echo "[strip_libfreetype_zlib] patched ${LIBFREETYPE_PATH} (arches: ${ARCHES})"
