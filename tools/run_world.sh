#!/usr/bin/env bash
# ps3base -- bring up the whole environment, launch the game, capture, verdict.
#
# One command, from any worktree, no manual setup. It:
#   * builds RelWithDebInfo with clang/clang++ (never gcc, never Release: the gcc
#     Release profile defines a \`slow\` macro that breaks the SPU float helpers),
#   * links bin/ and fw/ from the main checkout when this tree lacks them,
#   * prepares the VFS it is given, copying the main one on first use,
#   * starts Xvfb and restarts it if it dies, relaunching the game when it does,
#   * runs the title with no forcing by default (forcing #1 is a proven no-op),
#   * captures every 30 s to the capture directory,
#   * prints a mechanical verdict: exit status, crashes, flips, last frame time.
#
# Usage:
#   tools/run_world.sh <vfs-dir> [--seconds N] [--build-dir DIR] [--force SPEC]
#                                  [--tag NAME] [--no-build] [--env K=V ...]
#
# Exit status: 0 if the title was still alive when the budget ran out, 1 if it
# crashed, 2 if the environment could not be prepared.
set -uo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TREE="$(cd "$SELF_DIR/.." && pwd)"
MAIN_TREE="/root/Dev/ps3base2"
DISC_DIR="${PS3BASE_DISC_DIR:-/root/Dev/mc-port/disc}"

VFS=""
SECONDS_BUDGET=900
BUILD_DIR=""
FORCE_SPEC=""
TAG=""
DO_BUILD=1
EXTRA_ENV=()

die() { echo "run_world: $*" >&2; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --seconds) SECONDS_BUDGET="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --force) FORCE_SPEC="$2"; shift 2 ;;
        --tag) TAG="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --env) EXTRA_ENV+=("$2"); shift 2 ;;
        -h|--help) sed -n '2,25p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) die "unknown option $1" ;;
        *) VFS="$1"; shift ;;
    esac
done

[ -n "$VFS" ] || die "usage: tools/run_world.sh <vfs-dir> [options]"
case "$VFS" in /*) ;; *) VFS="$PWD/$VFS" ;; esac

TAG="${TAG:-$(basename "$VFS")}"
RUN_DIR="${PS3BASE_RUN_DIR:-/tmp/run-world-$TAG}"
CAP_DIR="$RUN_DIR/caps"
LOG="$RUN_DIR/game.log"
ICMD="$RUN_DIR/input.cmd"
mkdir -p "$RUN_DIR" "$CAP_DIR"
: > "$ICMD"

BUILD_DIR="${BUILD_DIR:-$TREE/build}"
BIN="$BUILD_DIR/ps3base"

echo "run_world: tree      $TREE"
echo "run_world: build     $BUILD_DIR"
echo "run_world: vfs       $VFS"
echo "run_world: run dir   $RUN_DIR"

# ---------------------------------------------------------------- toolchain --
# clang is required. gcc plus the Release profile trips over a 'slow' macro in
# runtime/src/spu/edge/spu_float.h, and Release strips the symbols the crash
# report needs.
command -v clang++ >/dev/null || die "clang++ not found"
command -v clang   >/dev/null || die "clang not found"

prep_links() {
    # A fresh worktree has no bin/ or fw/ (they are large and gitignored). The
    # title ELF and the firmware modules live in the main checkout.
    for d in bin fw; do
        if [ ! -e "$TREE/$d" ]; then
            [ -e "$MAIN_TREE/$d" ] || die "missing $d and cannot link from $MAIN_TREE"
            ln -s "$MAIN_TREE/$d" "$TREE/$d"
            echo "run_world: linked $d -> $MAIN_TREE/$d"
        fi
    done
}

prep_disc() {
    [ -d "$DISC_DIR" ] || die "disc dir not found: $DISC_DIR"
}

prep_vfs() {
    # First use: seed from the main checkout so existing worlds are visible.
    if [ ! -d "$VFS" ]; then
        [ -d "$MAIN_TREE/vfs" ] || die "no VFS at $VFS and none to copy from $MAIN_TREE/vfs"
        mkdir -p "$(dirname "$VFS")"
        cp -a "$MAIN_TREE/vfs" "$VFS"
        echo "run_world: seeded VFS from $MAIN_TREE/vfs"
    fi
}

prep_generated() {
    # The recompiled PPU/SPU sources are produced from the disc and never
    # tracked (40 MB, 243 files). A fresh worktree has none, so CMake refuses to
    # configure. Copy them from the main checkout once.
    if [ ! -d "$TREE/out/ppu" ]; then
        [ -d "$MAIN_TREE/out/ppu" ] || die "no out/ppu here and none in $MAIN_TREE"
        echo "run_world: copying generated sources from $MAIN_TREE/out"
        mkdir -p "$TREE/out"
        cp -a "$MAIN_TREE/out/." "$TREE/out/"
    fi
}

prep_build() {
    [ "$DO_BUILD" = 1 ] || { [ -x "$BIN" ] || die "no binary at $BIN and --no-build given"; return; }
    local cc
    cc="$(grep -m1 '^CMAKE_CXX_COMPILER:' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2)"
    local bt
    bt="$(grep -m1 '^CMAKE_BUILD_TYPE:' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2)"
    if [ "$cc" != "/usr/bin/clang++" ] || [ "$bt" != "RelWithDebInfo" ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
        echo "run_world: configuring (clang++, RelWithDebInfo)"
        rm -rf "$BUILD_DIR"
        mkdir -p "$BUILD_DIR"
        cmake -S "$TREE" -B "$BUILD_DIR" \
              -DCMAKE_BUILD_TYPE=RelWithDebInfo \
              -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
              > "$RUN_DIR/cmake.log" 2>&1 || { tail -20 "$RUN_DIR/cmake.log"; die "cmake configure failed"; }
    fi
    echo "run_world: building (-j 8)"
    if ! cmake --build "$BUILD_DIR" -j 8 --target ps3base > "$RUN_DIR/build.log" 2>&1; then
        grep -E 'error:' "$RUN_DIR/build.log" | head -10
        die "build failed (see $RUN_DIR/build.log)"
    fi
    [ -x "$BIN" ] || die "build produced no $BIN"
}

# -------------------------------------------------------------------- Xvfb --
Xvfb_pid=""
start_xvfb() {
    # A stale lock from a killed server blocks the restart.
    rm -f /tmp/.X11-unix/X0 /tmp/.X0-lock 2>/dev/null
    Xvfb :0 -screen 0 1280x720x24 -ac +extension GLX +render -noreset \
          > "$RUN_DIR/xvfb.log" 2>&1 &
    Xvfb_pid=$!
    for _ in $(seq 1 40); do
        if DISPLAY=:0 timeout 2 xdpyinfo >/dev/null 2>&1; then return 0; fi
        sleep 0.25
    done
    return 1
}

ensure_xvfb() {
    # Reuse a healthy server; replace a hung or missing one.
    if DISPLAY=:0 timeout 2 xdpyinfo >/dev/null 2>&1; then
        Xvfb_pid="$(pgrep -f 'Xvfb :0' | head -1 || true)"
        return 0
    fi
    pkill -9 -f 'Xvfb :0' 2>/dev/null
    sleep 0.5
    start_xvfb || die "cannot start Xvfb"
    echo "run_world: Xvfb up (pid $Xvfb_pid)"
}

xvfb_alive() {
    DISPLAY=:0 timeout 2 xdpyinfo >/dev/null 2>&1
}

# ------------------------------------------------------------------ capture --
game_window() {
    # The title's window. Several Minecraft windows can coexist when other
    # agents run, so prefer the one whose pid is our own game -- but keep the
    # search cheap: a window is only resolved once and then reused until it
    # stops answering.
    local w p
    for w in $(DISPLAY=:0 timeout 4 xdotool search --name 'Minecraft' 2>/dev/null); do
        p="$(DISPLAY=:0 timeout 1 xdotool getwindowpid "$w" 2>/dev/null)"
        if [ -n "${GAME_PID:-}" ] && [ "$p" = "$GAME_PID" ]; then echo "$w"; return; fi
    done
    DISPLAY=:0 timeout 4 xdotool search --name 'Minecraft' 2>/dev/null | tail -1
}

capture_loop() {
    local stop="$1" i=0
    local win=""
    while [ ! -f "$stop" ]; do
        i=$((i + 1))
        if [ -z "$win" ] || ! DISPLAY=:0 timeout 2 xdotool getwindowname "$win" >/dev/null 2>&1; then
            win="$(game_window)"
        fi
        if [ -n "$win" ]; then
            DISPLAY=:0 import -window "$win" "$CAP_DIR/cap-$(printf '%03d' "$i").png" 2>/dev/null
        fi
        # 30 s, in slices so the stop file is noticed promptly
        for _ in $(seq 1 30); do [ -f "$stop" ] && break; sleep 1; done
    done
}

# --------------------------------------------------------------------- run --
prep_disc
prep_links
prep_generated
prep_vfs
prep_build
ensure_xvfb

launch_game() {
    : > "$LOG"
    : > "$ICMD"
    # Build the environment as an argv list for env(1). Expanding an empty
    # array inline under \`set -u\` makes the assignment word itself the command,
    # which is how "PS3BASE_SAVEDATA_TRACE=1: command not found" happened.
    local -a env_args=(
        "DISPLAY=:0"
        "XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/0}"
        "PS3BASE_DISC_DIR=$DISC_DIR"
        "PS3BASE_LOG=${PS3BASE_LOG:-info}"
        "PS3BASE_INPUT_CMD=$ICMD"
        "PS3BASE_VFS_DIR=$VFS"
    )
    [ -n "$FORCE_SPEC" ] && env_args+=("PS3BASE_FORCE=$FORCE_SPEC")
    local e
    for e in ${EXTRA_ENV[@]+"${EXTRA_ENV[@]}"}; do env_args+=("$e"); done
    # No subshell: \`cd ... && cmd &\` would put the subshell's pid in $!, and a
    # pid that is not the game's makes the window lookup below match nothing --
    # capture then falls back to any Minecraft window and can photograph a
    # neighbour's instance when two agents share DISPLAY=:0.
    # setsid: the game becomes its own process-group leader, so it can be killed
    # as a group. Otherwise \`kill -9 \$GAME_PID\` kills only \`timeout\` and the
    # game survives -- which is how two instances ended up sharing DISPLAY=:0
    # after an Xvfb restart, each stealing the other's input.
    setsid env --chdir="$TREE" "${env_args[@]}" timeout "$SECONDS_BUDGET" "$BIN" --no-launcher > "$LOG" 2>&1 &
    GAME_PID=$!
}

launch_game
echo "run_world: game pid $GAME_PID, budget ${SECONDS_BUDGET}s"

STOP="$RUN_DIR/stop"
rm -f "$STOP"
capture_loop "$STOP" &
CAP_PID=$!

RESTARTS=0
DEAD_WHY=""
while kill -0 "$GAME_PID" 2>/dev/null; do
    sleep 5
    if ! xvfb_alive; then
        RESTARTS=$((RESTARTS + 1))
        echo "run_world: Xvfb died mid-run, restarting (restart $RESTARTS) and relaunching the game"
        kill -9 -- "-$GAME_PID" 2>/dev/null || kill -9 "$GAME_PID" 2>/dev/null
        wait "$GAME_PID" 2>/dev/null
        if [ "$RESTARTS" -ge 3 ]; then DEAD_WHY="Xvfb kept dying"; break; fi
        ensure_xvfb || { DEAD_WHY="cannot restart Xvfb"; break; }
        launch_game
    fi
done

touch "$STOP"
wait "$CAP_PID" 2>/dev/null
wait "$GAME_PID" 2>/dev/null
EXIT_CODE=$?

# ------------------------------------------------------------------ verdict --
CRASHES=$(grep -c 'crash: signal' "$LOG" 2>/dev/null || true)
FLIPS=$(grep -c 'rsx: flip' "$LOG" 2>/dev/null || true)
FIRST_FLIP=$(grep -m1 'rsx: flip' "$LOG" 2>/dev/null | grep -oE '^.[[:space:]]*[0-9.]+' | grep -oE '[0-9.]+' || true)
LAST_FLIP=$(grep 'rsx: flip' "$LOG" 2>/dev/null | tail -1 | grep -oE '^.[[:space:]]*[0-9.]+' | grep -oE '[0-9.]+' || true)
LAST_T=$(tail -1 "$LOG" 2>/dev/null | grep -oE '^.[[:space:]]*[0-9.]+' | grep -oE '[0-9.]+' || true)
CAPS=$(ls "$CAP_DIR"/cap-*.png 2>/dev/null | wc -l)
SAVES=$(find "$VFS/dev_hdd0/home" -name GAMEDATA 2>/dev/null | wc -l)

STATE="alive"
[ "$EXIT_CODE" = 124 ] && STATE="alive (budget reached)"
if [ "$CRASHES" != 0 ]; then STATE="crashed"; fi
[ -n "$DEAD_WHY" ] && STATE="environment failure: $DEAD_WHY"

echo
echo "================ verdict ================"
echo "state      : $STATE"
echo "exit code  : $EXIT_CODE   (124 = budget reached)"
echo "crashes    : $CRASHES"
echo "flips      : $FLIPS"
echo "flip span  : ${FIRST_FLIP:-?} s -> ${LAST_FLIP:-?} s"
echo "log end    : ${LAST_T:-?} s"
echo "captures   : $CAPS in $CAP_DIR"
echo "world saves: $SAVES"
echo "log        : $LOG"
[ "$RESTARTS" != 0 ] && echo "xvfb restarts: $RESTARTS"
if [ "$CRASHES" != 0 ]; then
    echo "first crash:"
    grep -m1 'crash: signal' "$LOG" | cut -c1-200
fi
echo "========================================="

if [ "$STATE" = "crashed" ]; then exit 1; fi
case "$DEAD_WHY" in "") exit 0 ;; *) exit 2 ;; esac
