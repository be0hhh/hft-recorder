#!/usr/bin/env bash
# hft-recorder - WSL-only build driver.
#
# Subcommands:
#   install-cxet   - build + install libcxet_lib.so to ~/.local/cxet
#   build          - configure + build all hft-recorder targets
#   gui            - run the Qt GUI [--gpu]
#   cli            - run the support CLI
#   clean          - remove build/ (prompts)
#   help           - show this message
set -e

APP="$(cd "$(dirname "$0")" && pwd)"

resolveCxetRoot() {
    local candidate
    candidate="$(cd "$APP/.." && pwd)"
    if [ -f "$candidate/CMakeLists.txt" ] && [ -d "$candidate/src/src" ]; then
        printf '%s\n' "$candidate"
        return 0
    fi

    candidate="$(cd "$APP/../.." && pwd)"
    if [ -f "$candidate/CMakeLists.txt" ] && [ -d "$candidate/src/src" ]; then
        printf '%s\n' "$candidate"
        return 0
    fi

    echo "ERROR: failed to locate CXETCPP root from $APP" >&2
    exit 2
}

CXETCPP="$(resolveCxetRoot)"
INSTALL_DIR="$HOME/.local/cxet"

_require_linux_build_env() {
    if [ "$(uname -s)" != "Linux" ]; then
        echo "ERROR: hft-recorder build is currently supported from Linux/WSL only." >&2
        echo "Install and initialize a WSL distro first, then run ./compile.sh inside WSL." >&2
        exit 2
    fi

    if ! command -v cmake >/dev/null 2>&1; then
        echo "ERROR: cmake is not installed in the active Linux environment." >&2
        exit 2
    fi

    if ! command -v c++ >/dev/null 2>&1 && ! command -v g++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1; then
        echo "ERROR: no C++ compiler found in the active Linux environment." >&2
        echo "Install g++ or clang++ in WSL, then rerun ./compile.sh." >&2
        exit 2
    fi
}

_write_start_launcher() {
    mkdir -p "$APP/build"
    cat > "$APP/build/start" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

APP_DIR="$(cd "$(dirname "$0")/.." && pwd)"
INSTALL_DIR="${HOME}/.local/cxet"
export LD_LIBRARY_PATH="$INSTALL_DIR/lib:${LD_LIBRARY_PATH:-}"

MODE="cpu"
if [ "${1:-}" = "--gpu" ]; then
    MODE="gpu"
    shift
fi

if [ "$MODE" = "gpu" ]; then
    export HFTREC_RENDER_MODE=gpu
    unset LIBGL_ALWAYS_SOFTWARE
    unset QT_XCB_GL_INTEGRATION
    unset QT_QUICK_BACKEND
    export QSG_RHI_BACKEND=opengl
    export QSG_INFO=1
    echo ">>> hft-recorder launcher: GPU mode (Qt Quick/OpenGL requested)"
else
    export HFTREC_RENDER_MODE=cpu
    export LIBGL_ALWAYS_SOFTWARE=1
    export QT_XCB_GL_INTEGRATION=none
    export QSG_RHI_BACKEND=software
    export QT_QUICK_BACKEND=software
    echo ">>> hft-recorder launcher: CPU-safe software mode"
fi

exec "$APP_DIR/build/bin/hft-recorder-gui" "$@"
EOF
    chmod +x "$APP/build/start"
}

cmd_install_cxet() {
    _require_linux_build_env
    local force="${1:-}"
    local so="$INSTALL_DIR/lib/libcxet_lib.so"
    if [ "$force" != "--force" ] && { [ -f "$so" ] || [ -f "$so.1" ]; }; then
        echo ">>> CXETCPP already installed at $INSTALL_DIR"
        return 0
    fi
    if [ "$force" = "--force" ]; then
        echo ">>> Force reinstall requested for CXETCPP"
    fi
    echo ">>> Building CXETCPP ..."
    cd "$CXETCPP"
    cmake -B build \
          -DCXET_FULL_BUILD=OFF \
          -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
          -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_CXX_FLAGS="-w" \
          2>&1 | grep -vE "^--|Detecting|Performing|Check for" || true
    cmake --build build --target cxet_lib -j"$(nproc)"
    echo ">>> Installing CXETCPP ..."
    cmake --install build
}

_resolve_cxet_paths() {
    CXET_INCLUDE="$INSTALL_DIR/include/cxet"
    CXET_LIB="$INSTALL_DIR/lib/libcxet_lib.so"
    if [ ! -f "$CXET_LIB" ] && [ -f "$INSTALL_DIR/lib/libcxet_lib.so.1" ]; then
        CXET_LIB="$INSTALL_DIR/lib/libcxet_lib.so.1"
    fi
    if [ ! -d "$CXET_INCLUDE" ] || [ ! -f "$CXET_LIB" ]; then
        echo "ERROR: CXETCPP not installed. Run: ./run.sh install-cxet" >&2
        exit 2
    fi
}

cmd_build() {
    _require_linux_build_env
    local force="${1:-}"
    if [ "$force" = "--force-cxet" ]; then
        cmd_install_cxet --force
    else
        cmd_install_cxet
    fi
    _resolve_cxet_paths
    cd "$APP"
    rm -rf "$APP/build"
    cmake -B build \
          -DCMAKE_BUILD_TYPE=Release \
          -DCXET_PUBLIC_INCLUDE_DIR="$CXET_INCLUDE" \
          -DCXET_SHARED_LIB="$CXET_LIB"
    cmake --build build -j"$(nproc)"
    _write_start_launcher
    echo ">>> Build complete"
    echo ">>> GUI launcher: $APP/build/start"
}

cmd_gui() {
    _require_linux_build_env
    _resolve_cxet_paths
    export LD_LIBRARY_PATH="$INSTALL_DIR/lib:${LD_LIBRARY_PATH:-}"
    local mode="cpu"
    if [ "${1:-}" = "--gpu" ]; then
        mode="gpu"
        shift
    fi
    if [ "$mode" = "gpu" ]; then
        export HFTREC_RENDER_MODE=gpu
        unset LIBGL_ALWAYS_SOFTWARE
        unset QT_XCB_GL_INTEGRATION
        unset QT_QUICK_BACKEND
        export QSG_RHI_BACKEND=opengl
        export QSG_INFO=1
        echo ">>> hft-recorder GUI: GPU mode (Qt Quick/OpenGL requested)"
    else
        export HFTREC_RENDER_MODE=cpu
        export LIBGL_ALWAYS_SOFTWARE=1
        export QT_XCB_GL_INTEGRATION=none
        export QSG_RHI_BACKEND=software
        export QT_QUICK_BACKEND=software
        echo ">>> hft-recorder GUI: CPU-safe software mode"
    fi
    cd "$APP"
    exec "$APP/build/bin/hft-recorder-gui" "$@"
}

cmd_cli() {
    _require_linux_build_env
    _resolve_cxet_paths
    export LD_LIBRARY_PATH="$INSTALL_DIR/lib:${LD_LIBRARY_PATH:-}"
    cd "$APP"
    exec "$APP/build/bin/hft-recorder" "$@"
}

cmd_clean() {
    read -r -p "Remove $APP/build ? [y/N] " a
    case "$a" in
        y|Y) rm -rf "$APP/build"; echo "build/ removed" ;;
        *)   echo "aborted" ;;
    esac
}

cmd_help() {
    sed -n '2,10p' "$0"
}

case "${1:-help}" in
    install-cxet) shift; cmd_install_cxet "$@" ;;
    build)        shift; cmd_build "$@" ;;
    gui)          shift; cmd_gui "$@" ;;
    cli)          shift; cmd_cli "$@" ;;
    clean)        shift; cmd_clean "$@" ;;
    help|-h|--help) cmd_help ;;
    *) echo "unknown subcommand: $1" >&2; cmd_help; exit 2 ;;
esac
