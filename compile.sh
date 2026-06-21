#!/usr/bin/env bash
# hft-recorder compile.sh - WSL build orchestrator.
#
# Modes:
#   ./compile.sh              build hft-recorder app using existing dependency shared libraries
#   ./compile.sh --force      build/install CXETCPP, hft-trader, hft-backtest, then hft-recorder app
#   ./compile.sh --force-cxet build/install CXETCPP core library, then build hft-recorder app
#   ./compile.sh --force-trader build hft-trader, hft-backtest, then hft-recorder app
#   ./compile.sh --force-back build hft-backtest, then hft-recorder app
#   ./compile.sh --force-compr build hft-compressor shared library, then build hft-recorder app
#   ./compile.sh --build-compressor alias for --force-compr
#   ./compile.sh --compressor-only build only hft-compressor shared library
#   ./compile.sh --force        rebuild with Clang by default
#
# Optional:
#   --clean                   remove hft-recorder/build before app configure
#   --compiler clang           explicit Clang toolchain
#   --metrics-off             build with hot-path metrics disabled by default
#   p|parallel|--parallel     use all CPU jobs and forward p to hft-trader
#   -j N                      parallel jobs (default: nproc)
set -euo pipefail

APP="$(cd "$(dirname "$0")" && pwd)"
COMPRESSOR="$(cd "$APP/../hft-compressor" && pwd)"
TRADER="$(cd "$APP/../hft-trader" && pwd)"
BACKTEST="$(cd "$APP/../hft-backtest" && pwd)"
INSTALL_DIR="$HOME/.local/cxet"
JOBS="$(nproc 2>/dev/null || echo 4)"
FULL_PARALLEL=0
MODE="app"
CLEAN=0
COMPILER="clang"
C_COMPILER=""
CXX_COMPILER=""
CMAKE_COMPILER_ARGS=()
HOTPATH_METRICS_DEFAULT="ON"
CXET_REFRESHED=0
RECORDER_DEPS_REFRESHED=0

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
    sed -n '2,21p' "$0"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build-compressor|--force-compr) MODE="app-with-compressor"; shift ;;
        --compressor-only) MODE="compressor-only"; shift ;;
        --force-cxet) MODE="app-with-cxet"; shift ;;
        --force-trader) MODE="app-with-trader"; shift ;;
        --force-back) MODE="app-with-backtest"; shift ;;
        --force)      MODE="all"; shift ;;
        --clean)      CLEAN=1; shift ;;
        --compiler)   COMPILER="${2:-}"; shift 2 ;;
        --metrics-off) HOTPATH_METRICS_DEFAULT="OFF"; shift ;;
        p|parallel|--parallel) FULL_PARALLEL=1; JOBS="$(nproc 2>/dev/null || echo 4)"; shift ;;
        clang)        COMPILER="$1"; shift ;;
        gcc)          echo "ERROR: GCC is not supported for CXETCPP/hft-trader builds; use clang." >&2; exit 2 ;;
        -j)           JOBS="${2:-}"; shift 2 ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "unknown flag: $1" >&2; usage >&2; exit 2 ;;
    esac
done

_select_compiler() {
    case "$COMPILER" in
        clang)
            C_COMPILER="$(command -v clang || true)"
            CXX_COMPILER="$(command -v clang++ || true)"
            ;;
        *)
            echo "ERROR: unsupported compiler '$COMPILER' (expected: clang)" >&2
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
    if [ ! -d "$dir" ] || [ -z "$CXX_COMPILER" ]; then
        return 0
    fi
    local cache_file="$dir/CMakeCache.txt"
    if [ ! -f "$cache_file" ]; then
        return 0
    fi
    local cached_cxx
    cached_cxx="$(grep -E '^CMAKE_CXX_COMPILER:FILEPATH=' "$cache_file" 2>/dev/null | cut -d= -f2- || true)"
    if [ "$cached_cxx" = "$CXX_COMPILER" ]; then
        return 0
    fi

    local parent base resolved
    parent="$(cd "$(dirname "$dir")" && pwd)"
    base="$(basename "$dir")"
    resolved="$parent/$base"

    case "$resolved" in
        "$COMPRESSOR/build"|"$TRADER/build"|"$BACKTEST/build"|"$CXETCPP/build"|"$APP/build") ;;
        *)
            echo "ERROR: refusing to remove unexpected build directory: $resolved" >&2
            exit 2
            ;;
    esac

    echo ">>> Compiler changed to '$COMPILER' ($CXX_COMPILER); removing $label CMake cache: $resolved"
    rm -rf "$resolved"
}

_clear_stale_cmake_cache() {
    local dir="$1"
    local source_dir="$2"
    local label="$3"
    local cache_file="$dir/CMakeCache.txt"

    if [ -d "$dir/_deps" ]; then
        local deps_parent deps_base deps_resolved subbuild_cache subbuild_dir cached_subbuild
        deps_parent="$(cd "$(dirname "$dir")" && pwd)"
        deps_base="$(basename "$dir")"
        deps_resolved="$deps_parent/$deps_base"

        case "$deps_resolved" in
            "$COMPRESSOR/build"|"$TRADER/build"|"$BACKTEST/build"|"$CXETCPP/build"|"$APP/build") ;;
            *)
                echo "ERROR: refusing to clean unexpected $label dependency directory: $deps_resolved" >&2
                exit 2
                ;;
        esac

        shopt -s nullglob
        for subbuild_cache in "$deps_resolved"/_deps/*-subbuild/CMakeCache.txt; do
            subbuild_dir="${subbuild_cache%/CMakeCache.txt}"
            cached_subbuild="$(grep -E '^CMAKE_CACHEFILE_DIR:INTERNAL=' "$subbuild_cache" 2>/dev/null | cut -d= -f2- || true)"
            if [ "$cached_subbuild" != "$subbuild_dir" ]; then
                echo ">>> CMake cache for $label dependency was created from a different path; regenerating: $subbuild_dir"
                rm -rf "$subbuild_dir"
            fi
        done
        shopt -u nullglob
    fi

    if [ ! -f "$cache_file" ]; then
        return 0
    fi

    local parent base resolved cached_source cached_build
    parent="$(cd "$(dirname "$dir")" && pwd)"
    base="$(basename "$dir")"
    resolved="$parent/$base"
    cached_source="$(grep -E '^CMAKE_HOME_DIRECTORY:INTERNAL=' "$cache_file" 2>/dev/null | cut -d= -f2- || true)"
    cached_build="$(grep -E '^CMAKE_CACHEFILE_DIR:INTERNAL=' "$cache_file" 2>/dev/null | cut -d= -f2- || true)"
    if [ "$cached_source" = "$source_dir" ] && [ "$cached_build" = "$resolved" ]; then
        return 0
    fi

    case "$resolved" in
        "$COMPRESSOR/build"|"$TRADER/build"|"$BACKTEST/build"|"$CXETCPP/build"|"$APP/build") ;;
        *)
            echo "ERROR: refusing to clean unexpected $label build directory: $resolved" >&2
            exit 2
            ;;
    esac

    echo ">>> CMake cache for $label was created from a different path; regenerating: $resolved"
    rm -rf "$resolved/CMakeCache.txt" \
           "$resolved/CMakeFiles" \
           "$resolved/compile_commands.json" \
           "$resolved/cmake_install.cmake" \
           "$resolved/Makefile" \
           "$resolved/build.ninja" \
           "$resolved/.ninja_deps" \
           "$resolved/.ninja_log"
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
    if ! command -v clang++ >/dev/null 2>&1; then
        echo "ERROR: clang++ is not installed in the active Linux environment." >&2
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
    (cd "$COMPRESSOR" && CC="$C_COMPILER" CXX="$CXX_COMPILER" ./compile.sh)
    RECORDER_DEPS_REFRESHED=1
}

_require_trader_tree() {
    if [ ! -x "$TRADER/compile.sh" ]; then
        echo "ERROR: hft-trader compile script not found: $TRADER/compile.sh" >&2
        exit 2
    fi
}

_require_backtest_tree() {
    if [ ! -x "$BACKTEST/compile.sh" ]; then
        echo "ERROR: hft-backtest compile script not found: $BACKTEST/compile.sh" >&2
        exit 2
    fi
}

_build_trader() {
    _require_trader_tree
    _reset_build_dir_for_explicit_compiler "$TRADER/build" "hft-trader"
    echo ">>> Building hft-trader shared runtime"
    local trader_args=()
    if [ "$CXET_REFRESHED" = "1" ]; then
        trader_args+=(--force)
    fi
    if [ "$FULL_PARALLEL" = "1" ]; then
        trader_args+=(p)
    fi
    (cd "$TRADER" && CC="$C_COMPILER" CXX="$CXX_COMPILER" ./compile.sh "${trader_args[@]}")
    RECORDER_DEPS_REFRESHED=1
}

_build_backtest() {
    _require_backtest_tree
    _clear_stale_cmake_cache "$BACKTEST/build" "$BACKTEST" "hft-backtest"
    _reset_build_dir_for_explicit_compiler "$BACKTEST/build" "hft-backtest"
    _resolve_trader_lib >/dev/null
    echo ">>> Building hft-backtest library"
    (cd "$BACKTEST" && CC="$C_COMPILER" CXX="$CXX_COMPILER" ./compile.sh)
    RECORDER_DEPS_REFRESHED=1
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
    echo "ERROR: hft-compressor library is missing. Run: ./compile.sh --force-compr" >&2
    exit 2
}

_resolve_trader_lib() {
    local candidate
    for candidate in \
        "$TRADER/build/libhft_trader_runtime.so" \
        "$TRADER/build/lib/libhft_trader_runtime.so" \
        "$INSTALL_DIR/lib/libhft_trader_runtime.so"
    do
        if [ -f "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    echo "ERROR: hft-trader shared runtime is missing. Run: ./compile.sh --force-trader or ./compile.sh --force" >&2
    exit 2
}
_resolve_backtest_lib() {
    local candidate
    for candidate in \
        "$BACKTEST/build/libhft_backtest_core.so" \
        "$BACKTEST/build/lib/libhft_backtest_core.so" \
        "$INSTALL_DIR/lib/libhft_backtest_core.so"
    do
        if [ -f "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    echo "ERROR: hft-backtest library is missing. Run: ./compile.sh --force-back or ./compile.sh --force" >&2
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
          -DCXET_BUILD_REPLAY=OFF \
          -DCXET_ENABLE_HFTRECORDER_LOCAL=OFF \
          -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
          -DCMAKE_BUILD_TYPE=Release \
          "${CMAKE_COMPILER_ARGS[@]}" \
          -DCMAKE_CXX_FLAGS="-w" \
          > /dev/null
    cmake --build build --target cxet_lib -j"$JOBS"
    cmake --install build > /dev/null

    mkdir -p "$INSTALL_DIR/lib"
    _copy_runtime_glob "build/extra/wolfssl/libwolfssl.so*"
    _copy_runtime_glob "build/extra/simdjson/libsimdjson.so*"
    find build/extra/abseil-cpp/absl -name 'libabsl*.so*' -exec cp -a {} "$INSTALL_DIR/lib/" \;
    CXET_REFRESHED=1
    cd "$APP"
}

_resolve_cxet_paths() {
    CXET_INCLUDE="$INSTALL_DIR/include/cxet"
    CXET_LIB="$INSTALL_DIR/lib/libcxet_lib.so"
    if [ ! -f "$CXET_LIB" ] && [ -f "$INSTALL_DIR/lib/libcxet_lib.so.1" ]; then
        CXET_LIB="$INSTALL_DIR/lib/libcxet_lib.so.1"
    fi
    # Replay-core is intentionally disabled for the recorder build. The app has an offline SessionReplay fallback.
    CXET_REPLAY_LIB=""
    if [ ! -d "$CXET_INCLUDE" ] || [ ! -f "$CXET_LIB" ]; then
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
    local backtest_lib_dir="$2"
    local trader_lib_dir="$3"
    local trader_cxet_lib_dir="$4"
    local build_compiler="$COMPILER"
    local build_cxx="$CXX_COMPILER"
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
BACKTEST_LIB_DIR="\$APP_DIR/../hft-backtest/build"
TRADER_LIB_DIR="\$APP_DIR/../hft-trader/build"
TRADER_CXET_LIB_DIR="\$APP_DIR/../hft-trader/build/cxetcpp/lib"
if [ ! -f "\$BACKTEST_LIB_DIR/libhft_backtest_core.so" ]; then
    BACKTEST_LIB_DIR="$backtest_lib_dir"
fi
if [ ! -f "\$TRADER_LIB_DIR/libhft_trader_runtime.so" ]; then
    TRADER_LIB_DIR="$trader_lib_dir"
fi
if [ ! -f "\$TRADER_CXET_LIB_DIR/libcxet_lib.so" ]; then
    TRADER_CXET_LIB_DIR="$trader_cxet_lib_dir"
fi
export LD_LIBRARY_PATH="\$BACKTEST_LIB_DIR:\$TRADER_LIB_DIR:\$TRADER_CXET_LIB_DIR:\$COMPRESSOR_LIB_DIR:\$INSTALL_DIR/lib:\${LD_LIBRARY_PATH:-}"
export HFTREC_METRICS_PORT="\${HFTREC_METRICS_PORT:-8080}"
export HFTREC_METRICS_MODE="\${HFTREC_METRICS_MODE:-$default_metrics_mode}"
export HFTREC_BUILD_COMPILER="\${HFTREC_BUILD_COMPILER:-$build_compiler}"
export HFTREC_BUILD_CXX="\${HFTREC_BUILD_CXX:-$build_cxx}"

MODE="cpu"
FOREGROUND="0"
NEW_INSTANCE="0"
while [ "\$#" -gt 0 ]; do
    case "\${1:-}" in
        --gpu)
            MODE="gpu"
            shift
            ;;
        --foreground)
            FOREGROUND="1"
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

if [ "\$NEW_INSTANCE" != "1" ]; then
    existing_pids="\$(
        for exe in /proc/[0-9]*/exe; do
            target="\$(readlink "\$exe" 2>/dev/null || true)"
            if [ "\$target" = "\$APP_DIR/build/bin/hft-recorder-gui" ]; then
                pid="\${exe#/proc/}"
                printf '%s\n' "\${pid%/exe}"
            fi
        done
    )"
    if [ -n "\$existing_pids" ]; then
        echo ">>> hft-recorder gui already running pid(s): \$existing_pids"
        echo ">>> keeping this terminal free; pass --new-instance to force another GUI window"
        exit 0
    fi
fi

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

if [ "\$FOREGROUND" = "1" ]; then
    echo ">>> hft-recorder gui foreground mode, log: \$LOG_FILE"
    "\$APP_DIR/build/bin/hft-recorder-gui" "\$@" 2>&1 | tee -a "\$LOG_FILE"
    exit "\${PIPESTATUS[0]}"
fi

if command -v setsid >/dev/null 2>&1; then
    setsid "\$APP_DIR/build/bin/hft-recorder-gui" "\$@" </dev/null >>"\$LOG_FILE" 2>&1 &
else
    nohup "\$APP_DIR/build/bin/hft-recorder-gui" "\$@" </dev/null >>"\$LOG_FILE" 2>&1 &
fi
gui_pid="\$!"

sleep 0.35
if ! kill -0 "\$gui_pid" >/dev/null 2>&1; then
    echo ">>> hft-recorder gui failed during startup, log: \$LOG_FILE" >&2
    tail -n 80 "\$LOG_FILE" >&2 || true
    exit 1
fi

disown 2>/dev/null || true
nudge_cursor_after_spawn
echo ">>> hft-recorder gui started pid=\$gui_pid, log: \$LOG_FILE"
EOF
    chmod +x "$APP/build/start"
}

_build_recorder_app() {
    local compressor_lib="$1"
    local backtest_lib="$2"
    _resolve_cxet_paths
    local trader_lib trader_cxet_lib_dir
    trader_lib="$(_resolve_trader_lib)"
    trader_cxet_lib_dir="$(cd "$TRADER/build/cxetcpp/lib" 2>/dev/null && pwd || printf '%s\n' "$INSTALL_DIR/lib")"

    cd "$APP"
    if [ "$CLEAN" = "1" ]; then
        echo ">>> Removing hft-recorder/build"
        rm -rf build
    fi
    _clear_stale_cmake_cache "$APP/build" "$APP" "hft-recorder"
    _reset_build_dir_for_explicit_compiler "$APP/build" "hft-recorder"

    if [ ! -f build/CMakeCache.txt ] || [ "$CLEAN" = "1" ] || [ "$CXET_REFRESHED" = "1" ] || [ "$RECORDER_DEPS_REFRESHED" = "1" ]; then
        echo ">>> Configuring hft-recorder app"
        cmake -B build \
              -DCMAKE_BUILD_TYPE=Release \
              "${CMAKE_COMPILER_ARGS[@]}" \
              -DCXET_PUBLIC_INCLUDE_DIR="$CXET_INCLUDE" \
              -DCXET_SHARED_LIB="$CXET_LIB" \
              -DCXET_REPLAY_SHARED_LIB="$CXET_REPLAY_LIB" \
              -DHFT_TRADER_PUBLIC_INCLUDE_DIR="$TRADER/include" \
              -DHFT_TRADER_GENERATED_INCLUDE_DIR="$TRADER/build/generated" \
              -DHFT_TRADER_SHARED_LIB="$trader_lib" \
              -DHFT_TRADER_CXET_INCLUDE_DIR="$TRADER/build/cxetcpp/include" \
              -DHFT_TRADER_CXET_LIB_DIR="$trader_cxet_lib_dir" \
              -DHFTREC_HOTPATH_METRICS_DEFAULT="$HOTPATH_METRICS_DEFAULT" \
              -DHFT_COMPRESSOR_PUBLIC_INCLUDE_DIR="$COMPRESSOR/include" \
              -DHFT_COMPRESSOR_SHARED_LIB="$compressor_lib" \
              -DHFT_BACKTEST_PUBLIC_INCLUDE_DIR="$BACKTEST/include" \
              -DHFT_BACKTEST_SHARED_LIB="$backtest_lib"
    else
        echo ">>> Using existing hft-recorder/build CMake cache"
    fi

    echo ">>> Building hft-recorder app (jobs=$JOBS)"
    cmake --build build --target hft-recorder hft-recorder-gui -j"$JOBS"

    local compressor_lib_dir backtest_lib_dir trader_lib_dir
    compressor_lib_dir="$(cd "$(dirname "$compressor_lib")" && pwd)"
    backtest_lib_dir="$(cd "$(dirname "$backtest_lib")" && pwd)"
    trader_lib_dir="$(cd "$(dirname "$trader_lib")" && pwd)"
    _write_start_launcher "$compressor_lib_dir" "$backtest_lib_dir" "$trader_lib_dir" "$trader_cxet_lib_dir"

    echo ""
    echo ">>> Done."
    echo ">>> Launch: ./build/start        (CPU-safe software mode)"
    echo ">>> Launch: ./build/start --gpu  (Qt Quick OpenGL mode)"
}

_require_linux_build_env
_select_compiler

echo ">>> Selected compiler: $COMPILER ($CXX_COMPILER)"

case "$MODE" in
    app)
        COMPRESSOR_LIB="$(_resolve_compressor_lib)"
        BACKTEST_LIB="$(_resolve_backtest_lib)"
        INSTALLED_COMPRESSOR_LIB="$(_install_compressor_runtime "$COMPRESSOR_LIB")"
        _build_recorder_app "$INSTALLED_COMPRESSOR_LIB" "$BACKTEST_LIB"
        ;;
    app-with-compressor)
        _build_compressor
        COMPRESSOR_LIB="$(_resolve_compressor_lib)"
        BACKTEST_LIB="$(_resolve_backtest_lib)"
        INSTALLED_COMPRESSOR_LIB="$(_install_compressor_runtime "$COMPRESSOR_LIB")"
        _build_recorder_app "$INSTALLED_COMPRESSOR_LIB" "$BACKTEST_LIB"
        ;;
    compressor-only)
        _build_compressor
        COMPRESSOR_LIB="$(_resolve_compressor_lib)"
        BACKTEST_LIB="$(_resolve_backtest_lib)"
        TRADER_LIB="$(_resolve_trader_lib)"
        compressor_lib_dir="$(cd "$(dirname "$COMPRESSOR_LIB")" && pwd)"
        backtest_lib_dir="$(cd "$(dirname "$BACKTEST_LIB")" && pwd)"
        trader_lib_dir="$(cd "$(dirname "$TRADER_LIB")" && pwd)"
        trader_cxet_lib_dir="$(cd "$TRADER/build/cxetcpp/lib" 2>/dev/null && pwd || printf '%s\n' "$INSTALL_DIR/lib")"
        _write_start_launcher "$compressor_lib_dir" "$backtest_lib_dir" "$trader_lib_dir" "$trader_cxet_lib_dir"
        ;;
    app-with-cxet)
        _install_cxet_force
        COMPRESSOR_LIB="$(_resolve_compressor_lib)"
        BACKTEST_LIB="$(_resolve_backtest_lib)"
        INSTALLED_COMPRESSOR_LIB="$(_install_compressor_runtime "$COMPRESSOR_LIB")"
        _build_recorder_app "$INSTALLED_COMPRESSOR_LIB" "$BACKTEST_LIB"
        ;;
    app-with-trader)
        _build_trader
        _build_backtest
        COMPRESSOR_LIB="$(_resolve_compressor_lib)"
        BACKTEST_LIB="$(_resolve_backtest_lib)"
        INSTALLED_COMPRESSOR_LIB="$(_install_compressor_runtime "$COMPRESSOR_LIB")"
        _build_recorder_app "$INSTALLED_COMPRESSOR_LIB" "$BACKTEST_LIB"
        ;;
    app-with-backtest)
        _build_backtest
        COMPRESSOR_LIB="$(_resolve_compressor_lib)"
        BACKTEST_LIB="$(_resolve_backtest_lib)"
        INSTALLED_COMPRESSOR_LIB="$(_install_compressor_runtime "$COMPRESSOR_LIB")"
        _build_recorder_app "$INSTALLED_COMPRESSOR_LIB" "$BACKTEST_LIB"
        ;;
    all)
        _install_cxet_force
        _build_trader
        _build_backtest
        COMPRESSOR_LIB="$(_resolve_compressor_lib)"
        BACKTEST_LIB="$(_resolve_backtest_lib)"
        INSTALLED_COMPRESSOR_LIB="$(_install_compressor_runtime "$COMPRESSOR_LIB")"
        _build_recorder_app "$INSTALLED_COMPRESSOR_LIB" "$BACKTEST_LIB"
        ;;
    *)
        echo "ERROR: internal unknown mode: $MODE" >&2
        exit 2
        ;;
esac
