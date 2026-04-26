#!/usr/bin/env bash
# hft-recorder compile.sh - one-shot build driver.
#
# Typical flow:
#   cd apps/hft-recorder
#   ./compile.sh            # incremental build (first run bootstraps CXETCPP)
#   ./build/start           # launch the Qt GUI in CPU-safe software mode
#   ./build/start --gpu     # launch the Qt GUI requesting Qt Quick/OpenGL
#
# Everything runs in WSL/Linux. Paths are /mnt/c/... under Windows.
#
# Flags (optional):
#   --clean          remove build/ before configuring (slow; full rebuild)
#   --force-cxet     rebuild + reinstall libcxet_lib.so to ~/.local/cxet
#   -j N             parallel jobs (default: $(nproc))

set -euo pipefail

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
    local replay_so="$INSTALL_DIR/lib/libcxet_replay_core.so"
    if [ "$FORCE_CXET" = "0" ] \
        && { [ -f "$so" ] || [ -f "$so.1" ]; } \
        && { [ -f "$replay_so" ] || [ -f "$replay_so.1" ]; } \
        && [ -f "$INSTALL_DIR/lib/libwolfssl.so.44" ] \
        && [ -f "$INSTALL_DIR/lib/libabsl_raw_hash_set.so" ] \
        && [ -f "$INSTALL_DIR/lib/libabsl_hash.so" ]; then
        return 0
    fi
    echo ">>> Building CXETCPP -> $INSTALL_DIR"
    cd "$CXETCPP"
    cmake -B build \
          -DCXET_FULL_BUILD=OFF \
          -DCXET_BUILD_REPLAY=ON \
          -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
          -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_CXX_FLAGS="-w" \
          > /dev/null
    cmake --build build --target cxet_lib cxet_replay_core -j"$JOBS"
    cmake --install build > /dev/null
    mkdir -p "$INSTALL_DIR/lib"
    cp -a build/extra/wolfssl/libwolfssl.so* "$INSTALL_DIR/lib/"
    find build/extra/abseil-cpp/absl -name 'libabsl*.so*' -exec cp -a {} "$INSTALL_DIR/lib/" \;
    cd "$APP"
}

_write_start_launcher() {
    mkdir -p "$APP/build"
    cat > "$APP/build/start" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

APP_DIR="$(cd "$(dirname "$0")/.." && pwd)"
INSTALL_DIR="${HOME}/.local/cxet"
export LD_LIBRARY_PATH="$INSTALL_DIR/lib:${LD_LIBRARY_PATH:-}"
export HFTREC_METRICS_PORT="${HFTREC_METRICS_PORT:-8080}"
export HFTREC_METRICS_MODE="${HFTREC_METRICS_MODE:-full}"

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

restore_cursor() {
    if command -v tput >/dev/null 2>&1; then
        tput cnorm 2>/dev/null || true
    fi
    printf '\e[?25h' || true
}

restore_cursor_tty() {
    if [ -w /dev/tty ]; then
        if command -v tput >/dev/null 2>&1; then
            tput cnorm >/dev/tty 2>/dev/null || true
        fi
        printf '\e[?25h' >/dev/tty 2>/dev/null || true
    else
        restore_cursor
    fi
}

nudge_cursor_after_spawn() {
    (
        sleep 0.2
        restore_cursor_tty
        sleep 0.5
        restore_cursor_tty
        sleep 1
        restore_cursor_tty
        sleep 2
        restore_cursor_tty
    ) >/dev/null 2>&1 &
}

trap restore_cursor_tty EXIT INT TERM
LOG_DIR="$APP_DIR/build/logs"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/gui.log"

: > "$LOG_FILE"
{
    printf '=== hft-recorder launch ===\n'
    printf 'mode=%s\n' "$MODE"
    printf 'HFTREC_RENDER_MODE=%s\n' "${HFTREC_RENDER_MODE:-}"
    printf 'HFTREC_METRICS_MODE=%s\n' "${HFTREC_METRICS_MODE:-}"
    printf 'HFTREC_METRICS_PORT=%s\n' "${HFTREC_METRICS_PORT:-}"
    printf 'QSG_RHI_BACKEND=%s\n' "${QSG_RHI_BACKEND:-}"
    printf 'QT_QUICK_BACKEND=%s\n' "${QT_QUICK_BACKEND:-}"
    printf 'QT_XCB_GL_INTEGRATION=%s\n' "${QT_XCB_GL_INTEGRATION:-}"
    printf 'LIBGL_ALWAYS_SOFTWARE=%s\n' "${LIBGL_ALWAYS_SOFTWARE:-}"
    printf '\n'
} >>"$LOG_FILE"
if command -v setsid >/dev/null 2>&1; then
    setsid "$APP_DIR/build/bin/hft-recorder-gui" "$@" </dev/null >>"$LOG_FILE" 2>&1 &
else
    nohup "$APP_DIR/build/bin/hft-recorder-gui" "$@" </dev/null >>"$LOG_FILE" 2>&1 &
fi

disown 2>/dev/null || true
nudge_cursor_after_spawn
echo ">>> hft-recorder gui started, log: $LOG_FILE"
EOF
    chmod +x "$APP/build/start"
}

_install_cxet_if_missing

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

cd "$APP"

if [ "$CLEAN" = "1" ]; then
    echo ">>> Removing build/"
    rm -rf build
fi

echo ">>> Configuring"
cmake -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DCXET_PUBLIC_INCLUDE_DIR="$CXET_INCLUDE" \
      -DCXET_SHARED_LIB="$CXET_LIB" \
      -DCXET_REPLAY_SHARED_LIB="$CXET_REPLAY_LIB"

echo ">>> Building (jobs=$JOBS)"
cmake --build build --target hft-recorder hft-recorder-gui -j"$JOBS"

_write_start_launcher

echo ""
echo ">>> Done."
echo ">>> Launch: ./build/start        (CPU-safe software mode)"
echo ">>> Launch: ./build/start --gpu  (Qt Quick OpenGL mode)"
echo ">>> Tests:  ctest --test-dir build"
