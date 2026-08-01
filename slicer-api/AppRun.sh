#!/bin/bash
SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="${SELF_DIR}/lib:${LD_LIBRARY_PATH}"
exec "${SELF_DIR}/bin/bambu-studio" "$@"
