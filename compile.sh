#!/usr/bin/env bash
# hft-recorder compile.sh — one-shot build driver.
#
# Typical flow:
#   cd apps/hft-recorder
#   ./compile.sh            # incremental build (first run bootstraps CXETCPP)
#   ./build/start           # launch the Qt GUI
#
# Everything runs in WSL/Linux. Paths are /mnt/c/... under Windows.
#
# Flags (optional):
#   --clean          remove build/ before configuring (slow; full rebuild)
#   --force-cxet     rebuild + reinstall libcxet_lib.so to ~/.local/cxet
#   -j N             parallel jobs (default: $(nproc))

set -euo pipefail

APP="$(cd "$(dirname "$0")" && pwd)"
CXETCPP="$(cd "$APP/../.." && pwd)"
INSTALL_DIR="$HOME/.local/cxet"

CLEAN=0
FORCE_CXET=0
JOBS="$(nproc 2>/dev/null || echo 4)"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --clean)       CLEAN=1; shift ;;
        --force-cxet)  FORCE_CXET=1; shift ;;
        -j)            JOBS="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,20p' "$0"
            exit 0
            ;;
        *) echo "unknown flag: $1" >&2; exit 2 ;;
    esac
done

if [ "$(uname -s)" != "Linux" ]; then
    echo "ERROR: hft-recorder build is Linux/WSL only." >&2
    exit 2
fi

_install_cxet_if_missing() {
    local so="$INSTALL_DIR/lib/libcxet_lib.so"
    if [ "$FORCE_CXET" = "0" ] && { [ -f "$so" ] || [ -f "$so.1" ]; }; then
        return 0
    fi
    echo ">>> Building CXETCPP -> $INSTALL_DIR"
    cd "$CXETCPP"
    cmake -B build \
          -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
          -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_CXX_FLAGS="-w" \
          > /dev/null
    cmake --build build --target cxet_lib -j"$JOBS"
    cmake --install build > /dev/null
    cd "$APP"
}

_write_start_launcher() {
    mkdir -p "$APP/build"
    # Under WSLg the default Mesa ZINK OpenGL path fails ("failed to choose
    # pdev"), so we pin the Qt scene graph to software. Real GPU acceleration
    # needs a scene-graph rewrite (QSGGeometryNode) — tracked as future V.10.
    cat > "$APP/build/start" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
APP_DIR="$(cd "$(dirname "$0")/.." && pwd)"
INSTALL_DIR="${HOME}/.local/cxet"
export LD_LIBRARY_PATH="$INSTALL_DIR/lib:${LD_LIBRARY_PATH:-}"
export LIBGL_ALWAYS_SOFTWARE=1
export QT_XCB_GL_INTEGRATION=none
export QSG_RHI_BACKEND=software
export QT_QUICK_BACKEND=software
exec "$APP_DIR/build/bin/hft-recorder-gui" "$@"
EOF
    chmod +x "$APP/build/start"
    # Clean up the stray GPU fallback launcher if a previous build left it.
    rm -f "$APP/build/start-software"
}

_install_cxet_if_missing

CXET_INCLUDE="$INSTALL_DIR/include/cxet"
CXET_LIB="$INSTALL_DIR/lib/libcxet_lib.so"
if [ ! -f "$CXET_LIB" ] && [ -f "$INSTALL_DIR/lib/libcxet_lib.so.1" ]; then
    CXET_LIB="$INSTALL_DIR/lib/libcxet_lib.so.1"
fi
if [ ! -d "$CXET_INCLUDE" ] || [ ! -f "$CXET_LIB" ]; then
    echo "ERROR: CXETCPP install incomplete at $INSTALL_DIR" >&2
    exit 2
fi

cd "$APP"

if [ "$CLEAN" = "1" ]; then
    echo ">>> Removing build/"
    rm -rf build
fi

if [ ! -f "$APP/build/CMakeCache.txt" ]; then
    echo ">>> Configuring"
    cmake -B build \
          -DCMAKE_BUILD_TYPE=Release \
          -DCXET_PUBLIC_INCLUDE_DIR="$CXET_INCLUDE" \
          -DCXET_SHARED_LIB="$CXET_LIB"
fi

echo ">>> Building (jobs=$JOBS)"
cmake --build build -j"$JOBS"

_write_start_launcher

echo ""
echo ">>> Done."
echo ">>> Launch:  ./build/start"
echo ">>> Tests:   ctest --test-dir build"
