#!/usr/bin/env bash
# hft-recorder - WSL-only build driver.
#
# Subcommands:
#   install-cxet   - build + install libcxet_lib.so to ~/.local/cxet
#   build          - configure + build all hft-recorder targets
#   gui            - run the Qt GUI [--gpu]
#   cli            - run the support CLI
#   tui            - run the terminal recorder UI
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

    if ! command -v clang >/dev/null 2>&1 || ! command -v clang++ >/dev/null 2>&1; then
        echo "ERROR: clang and clang++ are required in the active Linux environment." >&2
        echo "Install clang/clang++ in WSL, then rerun ./compile.sh." >&2
        exit 2
    fi
}

_clang_c() {
    command -v clang
}

_clang_cxx() {
    command -v clang++
}

_clear_cmake_cache_for_clang() {
    local build_dir="$1"
    local expected_source="$2"
    local label="$3"
    local cache_file="$build_dir/CMakeCache.txt"
    if [ ! -f "$cache_file" ]; then
        return 0
    fi

    local cached_source cached_c cached_cxx
    cached_source="$(grep -E '^CMAKE_HOME_DIRECTORY:INTERNAL=' "$cache_file" 2>/dev/null | cut -d= -f2- || true)"
    cached_c="$(grep -E '^CMAKE_C_COMPILER:FILEPATH=' "$cache_file" 2>/dev/null | cut -d= -f2- || true)"
    cached_cxx="$(grep -E '^CMAKE_CXX_COMPILER:FILEPATH=' "$cache_file" 2>/dev/null | cut -d= -f2- || true)"
    if [ "$cached_source" = "$expected_source" ] \
        && { [ -z "$cached_c" ] || [ "$cached_c" = "$(_clang_c)" ]; } \
        && [ "$cached_cxx" = "$(_clang_cxx)" ]; then
        return 0
    fi

    case "$build_dir" in
        "$CXETCPP/build"|"$APP/build") ;;
        *)
            echo "ERROR: refusing to clean unexpected $label build dir: $build_dir" >&2
            exit 2
            ;;
    esac

    echo ">>> $label CMake cache uses a different path or compiler; regenerating"
    rm -rf "$build_dir/CMakeCache.txt" \
           "$build_dir/CMakeFiles" \
           "$build_dir/compile_commands.json" \
           "$build_dir/cmake_install.cmake" \
           "$build_dir/Makefile" \
           "$build_dir/build.ninja" \
           "$build_dir/.ninja_deps" \
           "$build_dir/.ninja_log"
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
NEW_INSTANCE="0"
while [ "$#" -gt 0 ]; do
    case "${1:-}" in
        --gpu)
            MODE="gpu"
            shift
            ;;
        --new-instance)
            NEW_INSTANCE="1"
            shift
            ;;
        --)
            shift
            break
            ;;
        *)
            break
            ;;
    esac
done

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

if [ "$NEW_INSTANCE" != "1" ]; then
    existing_pids="$(
        for exe in /proc/[0-9]*/exe; do
            target="$(readlink "$exe" 2>/dev/null || true)"
            if [ "$target" = "$APP_DIR/build/bin/hft-recorder-gui" ]; then
                pid="${exe#/proc/}"
                printf '%s\n' "${pid%/exe}"
            fi
        done
    )"
    if [ -n "$existing_pids" ]; then
        echo ">>> hft-recorder gui already running pid(s): $existing_pids"
        echo ">>> keeping this terminal free; pass --new-instance to force another GUI window"
        exit 0
    fi
fi

    exec "$APP_DIR/build/bin/hft-recorder-gui" "$@"
EOF
    chmod +x "$APP/build/start"

    cat > "$APP/build/cli" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

APP_DIR="$(cd "$(dirname "$0")/.." && pwd)"
INSTALL_DIR="${HOME}/.local/cxet"
export LD_LIBRARY_PATH="$INSTALL_DIR/lib:${LD_LIBRARY_PATH:-}"

exec "$APP_DIR/build/bin/hft-recorder" tui "$@"
EOF
    chmod +x "$APP/build/cli"
}

cmd_install_cxet() {
    _require_linux_build_env
    local force="${1:-}"
    local so="$INSTALL_DIR/lib/libcxet_lib.so"
    if [ "$force" != "--force" ] \
        && { [ -f "$so" ] || [ -f "$so.1" ]; } \
        && [ -f "$INSTALL_DIR/lib/libwolfssl.so.44" ] \
        && [ -f "$INSTALL_DIR/lib/libabsl_raw_hash_set.so" ] \
        && [ -f "$INSTALL_DIR/lib/libabsl_hash.so" ]; then
        echo ">>> CXETCPP already installed at $INSTALL_DIR"
        return 0
    fi
    if [ "$force" = "--force" ]; then
        echo ">>> Force reinstall requested for CXETCPP"
    fi
    echo ">>> Building CXETCPP ..."
    cd "$CXETCPP"
    _clear_cmake_cache_for_clang "$CXETCPP/build" "$CXETCPP" "CXETCPP"
    cmake -B build \
          -DCXET_FULL_BUILD=OFF \
          -DCMAKE_C_COMPILER="$(_clang_c)" \
          -DCMAKE_CXX_COMPILER="$(_clang_cxx)" \
          -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
          -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_CXX_FLAGS="-w" \
          2>&1 | grep -vE "^--|Detecting|Performing|Check for" || true
    cmake --build build --target cxet_lib -j"$(nproc)"
    echo ">>> Installing CXETCPP ..."
    cmake --install build
    mkdir -p "$INSTALL_DIR/lib"
    cp -a build/extra/wolfssl/libwolfssl.so* "$INSTALL_DIR/lib/"
    find build/extra/abseil-cpp/absl -name 'libabsl*.so*' -exec cp -a {} "$INSTALL_DIR/lib/" \;
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
          -DCMAKE_CXX_COMPILER="$(_clang_cxx)" \
          -DCMAKE_BUILD_TYPE=Release \
          -DCXET_PUBLIC_INCLUDE_DIR="$CXET_INCLUDE" \
          -DCXET_SHARED_LIB="$CXET_LIB"
    cmake --build build -j"$(nproc)"
    _write_start_launcher
    echo ">>> Build complete"
    echo ">>> GUI launcher: $APP/build/start"
    echo ">>> TUI launcher: $APP/build/cli"
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

cmd_tui() {
    _require_linux_build_env
    _resolve_cxet_paths
    export LD_LIBRARY_PATH="$INSTALL_DIR/lib:${LD_LIBRARY_PATH:-}"
    cd "$APP"
    exec "$APP/build/bin/hft-recorder" tui "$@"
}

cmd_clean() {
    read -r -p "Remove $APP/build ? [y/N] " a
    case "$a" in
        y|Y) rm -rf "$APP/build"; echo "build/ removed" ;;
        *)   echo "aborted" ;;
    esac
}

cmd_help() {
    sed -n '2,11p' "$0"
}

case "${1:-help}" in
    install-cxet) shift; cmd_install_cxet "$@" ;;
    build)        shift; cmd_build "$@" ;;
    gui)          shift; cmd_gui "$@" ;;
    cli)          shift; cmd_cli "$@" ;;
    tui)          shift; cmd_tui "$@" ;;
    clean)        shift; cmd_clean "$@" ;;
    help|-h|--help) cmd_help ;;
    *) echo "unknown subcommand: $1" >&2; cmd_help; exit 2 ;;
esac
