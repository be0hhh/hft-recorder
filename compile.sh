#!/usr/bin/env bash
# hft-recorder compile.sh - WSL build orchestrator.
#
# Modes:
#   ./compile.sh              build only hft-compressor shared library
#   ./compile.sh --force-cxet build/install CXETCPP, then build hft-recorder app
#   ./compile.sh --force      build hft-compressor, build/install CXETCPP, then build hft-recorder app
#   ./compile.sh --force clang  same as --force, but rebuild with Clang
#   ./compile.sh --force gcc    same as --force, but rebuild with GCC
#
# Optional:
#   --clean                   remove hft-recorder/build before app configure
#   --compiler clang|gcc       explicit compiler toolchain
#   --metrics-off             build with hot-path metrics disabled by default
#   -j N                      parallel jobs (default: nproc)
set -euo pipefail

APP="$(cd "$(dirname "$0")" && pwd)"
COMPRESSOR="$(cd "$APP/../hft-compressor" && pwd)"
INSTALL_DIR="$HOME/.local/cxet"
JOBS="$(nproc 2>/dev/null || echo 4)"
MODE="compressor-only"
CLEAN=0
COMPILER="default"
C_COMPILER=""
CXX_COMPILER=""
CMAKE_COMPILER_ARGS=()
HOTPATH_METRICS_DEFAULT="ON"

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

usage() {
    sed -n '2,14p' "$0"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --force-cxet) MODE="app-with-cxet"; shift ;;
        --force)      MODE="all"; shift ;;
        --clean)      CLEAN=1; shift ;;
        --compiler)   COMPILER="${2:-}"; shift 2 ;;
        --metrics-off) HOTPATH_METRICS_DEFAULT="OFF"; shift ;;
        clang|gcc)    COMPILER="$1"; shift ;;
        -j)           JOBS="${2:-}"; shift 2 ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "unknown flag: $1" >&2; usage >&2; exit 2 ;;
    esac
done

_select_compiler() {
    case "$COMPILER" in
        default)
            return 0
            ;;
        clang)
            C_COMPILER="$(command -v clang || true)"
            CXX_COMPILER="$(command -v clang++ || true)"
            ;;
        gcc)
            C_COMPILER="$(command -v gcc || true)"
            CXX_COMPILER="$(command -v g++ || true)"
            ;;
        *)
            echo "ERROR: unsupported compiler '$COMPILER' (expected: clang or gcc)" >&2
            exit 2
            ;;
    esac

    if [ -z "$C_COMPILER" ] || [ -z "$CXX_COMPILER" ]; then
        echo "ERROR: requested compiler '$COMPILER' is not installed in the active Linux environment." >&2
        exit 2
    fi

    CMAKE_COMPILER_ARGS=(
        -DCMAKE_C_COMPILER="$C_COMPILER"
        -DCMAKE_CXX_COMPILER="$CXX_COMPILER"
    )
}

_reset_build_dir_for_explicit_compiler() {
    local dir="$1"
    local label="$2"
    if [ "$COMPILER" = "default" ] || [ ! -d "$dir" ]; then
        return 0
    fi

    local parent base resolved
    parent="$(cd "$(dirname "$dir")" && pwd)"
    base="$(basename "$dir")"
    resolved="$parent/$base"

    case "$resolved" in
        "$COMPRESSOR/build"|"$CXETCPP/build"|"$APP/build") ;;
        *)
            echo "ERROR: refusing to remove unexpected build directory: $resolved" >&2
            exit 2
            ;;
    esac

    echo ">>> Compiler '$COMPILER' requested; removing $label CMake cache: $resolved"
    rm -rf "$resolved"
}

_require_linux_build_env() {
    if [ "$(uname -s)" != "Linux" ]; then
        echo "ERROR: hft-recorder build is Linux/WSL only." >&2
        exit 2
    fi
    if ! command -v cmake >/dev/null 2>&1; then
        echo "ERROR: cmake is not installed in the active Linux environment." >&2
        exit 2
    fi
    if ! command -v c++ >/dev/null 2>&1 && ! command -v g++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1; then
        echo "ERROR: no C++ compiler found in the active Linux environment." >&2
        exit 2
    fi
}

_require_compressor_tree() {
    if [ ! -x "$COMPRESSOR/compile.sh" ]; then
        echo "ERROR: hft-compressor compile script not found: $COMPRESSOR/compile.sh" >&2
        exit 2
    fi
}

_build_compressor() {
    _require_compressor_tree
    _reset_build_dir_for_explicit_compiler "$COMPRESSOR/build" "hft-compressor"
    echo ">>> Building hft-compressor library"
    if [ "$COMPILER" = "default" ]; then
        (cd "$COMPRESSOR" && ./compile.sh)
    else
        (cd "$COMPRESSOR" && CC="$C_COMPILER" CXX="$CXX_COMPILER" ./compile.sh)
    fi
}

_resolve_compressor_lib() {
    local candidate
    for candidate in \
        "$COMPRESSOR/build/libhft_compressor_core.so" \
        "$COMPRESSOR/build/lib/libhft_compressor_core.so" \
        "$INSTALL_DIR/lib/libhft_compressor_core.so"
    do
        if [ -f "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    echo "ERROR: hft-compressor library is missing. Run: ./compile.sh or ./compile.sh --force" >&2
    exit 2
}

_copy_runtime_glob() {
    local pattern="$1"
    local copied=0
    mkdir -p "$INSTALL_DIR/lib"
    shopt -s nullglob
    local file
    for file in $pattern; do
        cp -a "$file" "$INSTALL_DIR/lib/"
        copied=1
    done
    shopt -u nullglob
    return 0
}

_install_cxet_force() {
    echo ">>> Building CXETCPP -> $INSTALL_DIR"
    cd "$CXETCPP"
    _reset_build_dir_for_explicit_compiler "$CXETCPP/build" "CXETCPP"
    cmake -B build \
          -DCXET_FULL_BUILD=OFF \
          -DCXET_BUILD_REPLAY=ON \
          -DCXET_ENABLE_HFTRECORDER_LOCAL=ON \
          -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
          -DCMAKE_BUILD_TYPE=Release \
          "${CMAKE_COMPILER_ARGS[@]}" \
          -DCMAKE_CXX_FLAGS="-w" \
          > /dev/null
    cmake --build build --target cxet_lib cxet_replay_core -j"$JOBS"
    cmake --install build > /dev/null

    mkdir -p "$INSTALL_DIR/lib"
    _copy_runtime_glob "build/extra/wolfssl/libwolfssl.so*"
    _copy_runtime_glob "build/extra/simdjson/libsimdjson.so*"
    find build/extra/abseil-cpp/absl -name 'libabsl*.so*' -exec cp -a {} "$INSTALL_DIR/lib/" \;
    cd "$APP"
}

_resolve_cxet_paths() {
    CXET_INCLUDE="$INSTALL_DIR/include/cxet"
    CXET_LIB="$INSTALL_DIR/lib/libcxet_lib.so"
    if [ ! -f "$CXET_LIB" ] && [ -f "$INSTALL_DIR/lib/libcxet_lib.so.1" ]; then
        CXET_LIB="$INSTALL_DIR/lib/libcxet_lib.so.1"
    fi
    CXET_REPLAY_LIB="$INSTALL_DIR/lib/libcxet_replay_core.so"
    if [ ! -f "$CXET_REPLAY_LIB" ] && [ -f "$INSTALL_DIR/lib/libcxet_replay_core.so.1" ]; then
        CXET_REPLAY_LIB="$INSTALL_DIR/lib/libcxet_replay_core.so.1"
    fi
    if [ ! -d "$CXET_INCLUDE" ] || [ ! -f "$CXET_LIB" ] || [ ! -f "$CXET_REPLAY_LIB" ]; then
        echo "ERROR: CXETCPP install incomplete at $INSTALL_DIR" >&2
        exit 2
    fi
}

_install_compressor_runtime() {
    local src="$1"
    mkdir -p "$INSTALL_DIR/lib"
    cp -f "$src" "$INSTALL_DIR/lib/libhft_compressor_core.so"
    printf '%s\n' "$INSTALL_DIR/lib/libhft_compressor_core.so"
}

_write_start_launcher() {
    local compressor_lib_dir="$1"
    local build_compiler="$COMPILER"
    local build_cxx="$CXX_COMPILER"
    if [ "$build_compiler" = "default" ]; then
        build_compiler="unknown"
        build_cxx="unknown"
    fi
    local default_metrics_mode="full"
    if [ "$HOTPATH_METRICS_DEFAULT" = "OFF" ]; then
        default_metrics_mode="off"
    fi
    mkdir -p "$APP/build"
    cat > "$APP/build/start" <<EOF
#!/usr/bin/env bash
set -euo pipefail

APP_DIR="\$(cd "\$(dirname "\$0")/.." && pwd)"
INSTALL_DIR="\${HOME}/.local/cxet"
COMPRESSOR_LIB_DIR="$compressor_lib_dir"
export LD_LIBRARY_PATH="\$COMPRESSOR_LIB_DIR:\$INSTALL_DIR/lib:\${LD_LIBRARY_PATH:-}"
export HFTREC_METRICS_PORT="\${HFTREC_METRICS_PORT:-8080}"
export HFTREC_METRICS_MODE="\${HFTREC_METRICS_MODE:-$default_metrics_mode}"
export HFTREC_BUILD_COMPILER="\${HFTREC_BUILD_COMPILER:-$build_compiler}"
export HFTREC_BUILD_CXX="\${HFTREC_BUILD_CXX:-$build_cxx}"

MODE="cpu"
if [ "\${1:-}" = "--gpu" ]; then
    MODE="gpu"
    shift
fi

if [ "\$MODE" = "gpu" ]; then
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

restore_cursor_tty() {
    if [ -w /dev/tty ]; then
        if command -v tput >/dev/null 2>&1; then
            tput cnorm >/dev/tty 2>/dev/null || true
        fi
        printf '\e[?25h' >/dev/tty 2>/dev/null || true
    else
        printf '\e[?25h' || true
    fi
}

nudge_cursor_after_spawn() {
    (
        sleep 0.2; restore_cursor_tty
        sleep 0.5; restore_cursor_tty
        sleep 1; restore_cursor_tty
        sleep 2; restore_cursor_tty
    ) >/dev/null 2>&1 &
}

trap restore_cursor_tty EXIT INT TERM
LOG_DIR="\$APP_DIR/build/logs"
mkdir -p "\$LOG_DIR"
LOG_FILE="\$LOG_DIR/gui.log"

: > "\$LOG_FILE"
{
    printf '=== hft-recorder launch ===\n'
    printf 'mode=%s\n' "\$MODE"
    printf 'HFTREC_RENDER_MODE=%s\n' "\${HFTREC_RENDER_MODE:-}"
    printf 'HFTREC_METRICS_MODE=%s\n' "\${HFTREC_METRICS_MODE:-}"
    printf 'HFTREC_METRICS_PORT=%s\n' "\${HFTREC_METRICS_PORT:-}"
    printf 'HFTREC_BUILD_COMPILER=%s\n' "\${HFTREC_BUILD_COMPILER:-}"
    printf 'HFTREC_BUILD_CXX=%s\n' "\${HFTREC_BUILD_CXX:-}"
    printf 'QSG_RHI_BACKEND=%s\n' "\${QSG_RHI_BACKEND:-}"
    printf 'QT_QUICK_BACKEND=%s\n' "\${QT_QUICK_BACKEND:-}"
    printf 'QT_XCB_GL_INTEGRATION=%s\n' "\${QT_XCB_GL_INTEGRATION:-}"
    printf 'LIBGL_ALWAYS_SOFTWARE=%s\n' "\${LIBGL_ALWAYS_SOFTWARE:-}"
    printf '\n'
} >>"\$LOG_FILE"

if command -v setsid >/dev/null 2>&1; then
    setsid "\$APP_DIR/build/bin/hft-recorder-gui" "\$@" </dev/null >>"\$LOG_FILE" 2>&1 &
else
    nohup "\$APP_DIR/build/bin/hft-recorder-gui" "\$@" </dev/null >>"\$LOG_FILE" 2>&1 &
fi

disown 2>/dev/null || true
nudge_cursor_after_spawn
echo ">>> hft-recorder gui started, log: \$LOG_FILE"
EOF
    chmod +x "$APP/build/start"
}

_build_recorder_app() {
    local compressor_lib="$1"
    _resolve_cxet_paths

    cd "$APP"
    if [ "$CLEAN" = "1" ]; then
        echo ">>> Removing hft-recorder/build"
        rm -rf build
    fi
    _reset_build_dir_for_explicit_compiler "$APP/build" "hft-recorder"

    echo ">>> Configuring hft-recorder app"
    cmake -B build \
          -DCMAKE_BUILD_TYPE=Release \
          "${CMAKE_COMPILER_ARGS[@]}" \
          -DCXET_PUBLIC_INCLUDE_DIR="$CXET_INCLUDE" \
          -DCXET_SHARED_LIB="$CXET_LIB" \
          -DCXET_REPLAY_SHARED_LIB="$CXET_REPLAY_LIB" \
          -DHFTREC_HOTPATH_METRICS_DEFAULT="$HOTPATH_METRICS_DEFAULT" \
          -DHFT_COMPRESSOR_PUBLIC_INCLUDE_DIR="$COMPRESSOR/include" \
          -DHFT_COMPRESSOR_SHARED_LIB="$compressor_lib"

    echo ">>> Building hft-recorder app (jobs=$JOBS)"
    cmake --build build --target hft-recorder hft-recorder-gui -j"$JOBS"

    local compressor_lib_dir
    compressor_lib_dir="$(cd "$(dirname "$compressor_lib")" && pwd)"
    _write_start_launcher "$compressor_lib_dir"

    echo ""
    echo ">>> Done."
    echo ">>> Launch: ./build/start        (CPU-safe software mode)"
    echo ">>> Launch: ./build/start --gpu  (Qt Quick OpenGL mode)"
}

_require_linux_build_env
_select_compiler

if [ "$COMPILER" != "default" ]; then
    echo ">>> Selected compiler: $COMPILER ($CXX_COMPILER)"
fi

case "$MODE" in
    compressor-only)
        _build_compressor
        COMPRESSOR_LIB="$(_resolve_compressor_lib)"
        compressor_lib_dir="$(cd "$(dirname "$COMPRESSOR_LIB")" && pwd)"
        _write_start_launcher "$compressor_lib_dir"
        ;;
    app-with-cxet)
        _install_cxet_force
        COMPRESSOR_LIB="$(_resolve_compressor_lib)"
        _build_recorder_app "$COMPRESSOR_LIB"
        ;;
    all)
        _build_compressor
        COMPRESSOR_LIB="$(_resolve_compressor_lib)"
        INSTALLED_COMPRESSOR_LIB="$(_install_compressor_runtime "$COMPRESSOR_LIB")"
        _install_cxet_force
        _build_recorder_app "$INSTALLED_COMPRESSOR_LIB"
        ;;
    *)
        echo "ERROR: internal unknown mode: $MODE" >&2
        exit 2
        ;;
esac
