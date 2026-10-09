#!/bin/bash
# Apple Silicon macOS build (Vulkan through MoltenVK -> build/spirula).
# Bakes in the sibling COLMAP build (../colmap/BUILD_MAC.sh) as the GUI's
# default; COLMAP_EXE=/path/to/colmap overrides it. Extra args go to CMake.
set -euo pipefail

cd "$(dirname "$0")"

COLMAP_EXE=${COLMAP_EXE:-$(cd .. && pwd)/colmap/build/src/colmap/exe/colmap}
if [ ! -x "$COLMAP_EXE" ]; then
    echo "COLMAP not found at $COLMAP_EXE -- run ../colmap/BUILD_MAC.sh first" >&2
    exit 1
fi

bash build_develop.bash \
    -DSS_BACKEND=vulkan \
    -DSS_BUILD_GUI=ON \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DSS_COLMAP_EXE="$COLMAP_EXE" \
    "$@"

# A saved settings file wins over the built-in default. Repoint it only if it
# still holds the stock bare "colmap"; a path picked in the GUI is left alone.
conf="${XDG_CONFIG_HOME:-$HOME/.config}/spirula-studio/gui.conf"
if [ -f "$conf" ] && grep -qx 'colmap_exe=colmap' "$conf"; then
    sed -i '' "s|^colmap_exe=colmap\$|colmap_exe=$COLMAP_EXE|" "$conf"
    echo "Updated $conf: colmap_exe=$COLMAP_EXE"
fi

echo "COLMAP default: $COLMAP_EXE"
