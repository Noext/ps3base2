#!/usr/bin/env bash
# ps3base -- play the game in a window, on a real X server, with a real GPU.
#
# This is the "user" entry point: one command, a window you can play in, a
# gamepad if you have one, and a keyboard layout that works if you do not.
# It is deliberately separate from tools/run_world.sh, which exists for
# measurement (its own Xvfb, a capture loop, a mechanical verdict) rather than
# for playing.
#
# Usage:
#   tools/play.sh [--vfs DIR] [--fullscreen] [--no-build] [--log FILE]
#                 [--build-dir DIR] [--display :N] [--xvfb] [--seconds N]
#                 [--scale K] [--no-pad] [-- ARGS...]
#
#   --vfs DIR        VFS directory (default: <tree>/vfs)
#   --fullscreen     start fullscreen (F11 or Alt+Enter toggles it at runtime)
#   --no-build       run the existing binary instead of building
#   --log FILE       where the game log goes (default: /tmp/play-ps3base.log)
#   --build-dir DIR  build directory (default: <tree>/build)
#   --display :N     X display to use (default: whatever DISPLAY says)
#   --xvfb           no X server? start a private Xvfb purely to present into.
#                    The GPU still rasterises; only presentation is software.
#                    A container fallback, not the intended way to play.
#   --seconds N      exit after N seconds (default: run until the window closes)
#   --scale K        window scale, 1..4 (GOWA_RES_SCALE)
#   --no-pad         do not attach the virtual gamepad
#   -- ARGS...       everything after -- is passed to the title
#
# Exit status: 0 the game exited normally, 1 the game crashed, 2 the environment
# could not be prepared (no display, no build), 3 no window appeared in time.
set -uo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TREE="$(cd "$SELF_DIR/.." && pwd)"
MAIN_TREE="${PS3BASE_MAIN_TREE:-/root/Dev/ps3base2}"
DISC_DIR="${PS3BASE_DISC_DIR:-/root/Dev/mc-port/disc}"

VFS=""
BUILD_DIR="$TREE/build"
LOG="${PLAY_LOG:-/tmp/play-ps3base.log}"
DISP="${DISPLAY:-}"
USE_XVFB=0
FULLSCREEN=0
DO_BUILD=1
SECONDS_BUDGET=0
SCALE=""
NO_PAD=0
GAME_ARGS=()

say() { printf 'play: %s\n' "$*"; }
die() { printf 'play: %s\n' "$*" >&2; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --vfs) VFS="$2"; shift 2 ;;
        --fullscreen) FULLSCREEN=1; shift ;;
        --no-build) DO_BUILD=0; shift ;;
        --log) LOG="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --display) DISP="$2"; shift 2 ;;
        --xvfb) USE_XVFB=1; shift ;;
        --seconds) SECONDS_BUDGET="$2"; shift 2 ;;
        --scale) SCALE="$2"; shift 2 ;;
        --no-pad) NO_PAD=1; shift ;;
        --) shift; GAME_ARGS=("$@"); break ;;
        -h|--help) sed -n '2,34p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) die "unknown option $1 (try --help)" ;;
        *) die "unexpected argument $1 (did you mean --vfs $1?)" ;;
    esac
done

VFS="${VFS:-$TREE/vfs}"
case "$VFS" in /*) ;; *) VFS="$PWD/$VFS" ;; esac

# ------------------------------------------------------------- the display --
# The runtime presents through SDL3 into an X11 window: there is no DRM/KMS
# path in it, so a display server is not optional. This says so in one
# paragraph with a way forward, instead of letting SDL print
# "Couldn't connect to display" and exit.
have_display() {
    [ -n "$DISP" ] || return 1
    DISPLAY="$DISP" timeout 3 xdpyinfo >/dev/null 2>&1
}

if ! have_display; then
    if [ "$USE_XVFB" = 1 ]; then
        command -v Xvfb >/dev/null || die "no X server on '$DISP' and Xvfb is not installed either"
        # A display of our own: on a shared one, two instances steal each
        # other's keyboard focus, which is the most confusing failure this tree
        # has had -- and a capture then photographs the neighbour's window.
        # run_world.sh derives its display from 10..49, so stay clear of it:
        # a collision there put three games on one display during this work.
        n=$(( 60 + ($(printf '%s' "$VFS" | cksum | cut -d' ' -f1) % 20) ))
        DISP=":$n"
        rm -f "/tmp/.X11-unix/X$n" "/tmp/.X$n-lock" 2>/dev/null
        Xvfb "$DISP" -screen 0 1920x1080x24 -ac +extension GLX +render -noreset >/tmp/play-xvfb.log 2>&1 &
        XVFB_PID=$!
        for _ in $(seq 1 40); do have_display && break; sleep 0.25; done
        have_display || die "Xvfb started but $DISP never answered (see /tmp/play-xvfb.log)"
        say "no X server found; started Xvfb on $DISP (the GPU rasterises, presentation is software)"
    else
        cat >&2 <<'PLAYNOX'
play: no usable X display.

  This emulator presents into an X11 window through SDL3. There is no DRM/KMS
  path in the runtime, so it needs a display server: on a desktop, log into
  your session and run this again (DISPLAY is then already set).

  In a headless container -- no Xorg, no xinit, no way to present on DRM/KMS --
  the only way to see the game is a virtual server:

      tools/play.sh --xvfb

  which presents through Xvfb: the GPU still rasterises, only the swapchain
  presentation is software. To measure rather than play, use
  tools/run_world.sh, which sets that up along with captures and a verdict.

PLAYNOX
        exit 2
    fi
fi
export DISPLAY="$DISP"

[ -d "$DISC_DIR" ] || die "disc directory not found: $DISC_DIR (set PS3BASE_DISC_DIR)"

# ------------------------------------------------------------------- build --
BIN="$BUILD_DIR/ps3base"
BUILD_LOG="/tmp/play-ps3base-build.log"
if [ "$DO_BUILD" = 1 ]; then
    command -v clang++ >/dev/null || die "clang++ not found (this tree must not be built with gcc)"
    # A fresh worktree has no bin/ or fw/ and no generated sources: they are
    # large, gitignored, and produced from the disc.
    for d in bin fw; do
        [ -e "$TREE/$d" ] && continue
        [ -e "$MAIN_TREE/$d" ] || die "missing $d and nothing to link from $MAIN_TREE"
        ln -s "$MAIN_TREE/$d" "$TREE/$d"
        say "linked $d -> $MAIN_TREE/$d"
    done
    if [ ! -d "$TREE/out/ppu" ]; then
        [ -d "$MAIN_TREE/out/ppu" ] || die "no generated sources here and none in $MAIN_TREE"
        say "copying generated sources from $MAIN_TREE/out"
        mkdir -p "$TREE/out" && cp -a "$MAIN_TREE/out/." "$TREE/out/"
    fi
    cc="$(grep -m1 '^CMAKE_CXX_COMPILER:' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2)"
    bt="$(grep -m1 '^CMAKE_BUILD_TYPE:' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2)"
    if [ "$cc" != "/usr/bin/clang++" ] || [ "$bt" != "RelWithDebInfo" ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
        say "configuring (clang++, RelWithDebInfo)"
        rm -rf "$BUILD_DIR" && mkdir -p "$BUILD_DIR"
        cmake -S "$TREE" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
              -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
              >"$BUILD_LOG.configure" 2>&1 || { tail -20 "$BUILD_LOG.configure"; die "cmake configure failed"; }
    fi
    say "building (-j 8)"
    if ! cmake --build "$BUILD_DIR" -j 8 --target ps3base >"$BUILD_LOG" 2>&1; then
        grep -E 'error:' "$BUILD_LOG" | head -10 >&2
        die "build failed (see $BUILD_LOG)"
    fi
fi
[ -x "$BIN" ] || die "no binary at $BIN (drop --no-build, or point --build-dir at a built tree)"

# --------------------------------------------------------------------- VFS --
# The VFS holds the saved worlds and the title's own files. Seed it from the
# main checkout on first use, so a world that already exists there is visible.
if [ ! -d "$VFS" ]; then
    [ -d "$MAIN_TREE/vfs" ] || die "no VFS at $VFS and none to copy from $MAIN_TREE/vfs"
    say "seeding VFS from $MAIN_TREE/vfs"
    mkdir -p "$(dirname "$VFS")" && cp -a "$MAIN_TREE/vfs" "$VFS"
fi

# --------------------------------------------------------------------- run --
mkdir -p "$(dirname "$LOG")"
: >"$LOG"

env_args=(
    "DISPLAY=$DISPLAY"
    "XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/0}"
    "PS3BASE_DISC_DIR=$DISC_DIR"
    "PS3BASE_VFS_DIR=$VFS"
    "PS3BASE_LOG=${PS3BASE_LOG:-info}"
)
[ "$FULLSCREEN" = 1 ] && env_args+=("GOWA_FULLSCREEN=1")
[ -n "$SCALE" ] && env_args+=("GOWA_RES_SCALE=$SCALE")
[ "$NO_PAD" = 1 ] && env_args+=("PS3BASE_NO_VIRTUAL_PAD=1")
# On a real desktop the keyboard focus belongs to whichever window has it,
# which is what you want. Under Xvfb with no window manager the focus is
# PointerRoot, so it follows the mouse and the game loses input as soon as a
# second window exists; there the guard is lifted.
if [ "$USE_XVFB" = 1 ]; then env_args+=("PS3BASE_ALLOW_UNFOCUSED_INPUT=1"); fi

say "vfs    $VFS"
say "log    $LOG"
say "keys   z=Cross x=Circle a=Square s=Triangle   i/k/j/l=left stick"
say "       r/f/t/g=right stick (aim)   1=L2 place   3=R2 mine   Q=L1 E=R1=hotbar"
say "       F11 / Alt+Enter toggles fullscreen; close the window to quit"

# setsid: the game becomes its own process-group leader, so it can be killed as
# a group. Without it, killing the pid leaves the guest threads running.
if [ "$SECONDS_BUDGET" -gt 0 ]; then
    setsid env --chdir="$TREE" "${env_args[@]}" timeout "$SECONDS_BUDGET" "$BIN" "${GAME_ARGS[@]+"${GAME_ARGS[@]}"}" --no-launcher >"$LOG" 2>&1 &
else
    setsid env --chdir="$TREE" "${env_args[@]}" "$BIN" "${GAME_ARGS[@]+"${GAME_ARGS[@]}"}" --no-launcher >"$LOG" 2>&1 &
fi
GAME_PID=$!

cleanup() {
    if kill -0 "$GAME_PID" 2>/dev/null; then kill -9 -- "-$GAME_PID" 2>/dev/null || kill -9 "$GAME_PID" 2>/dev/null; fi
    if [ "${XVFB_PID:-0}" != 0 ]; then kill -9 "${XVFB_PID:-0}" 2>/dev/null; fi
}
trap cleanup EXIT INT TERM

# Wait for the window and report which device the presenter actually chose:
# presenting through llvmpipe means the Vulkan loader did not see the real
# driver, which deserves a name rather than a puzzled "why is it slow".
WIN=""
for _ in $(seq 1 60); do
    if ! kill -0 "$GAME_PID" 2>/dev/null; then
        say "the process exited before a window appeared:"
        tail -5 "$LOG" >&2
        exit 1
    fi
    WIN="$(DISPLAY="$DISPLAY" timeout 2 xdotool search --name 'Minecraft' 2>/dev/null | tail -1)"
    [ -n "$WIN" ] && break
    sleep 0.5
done

DEV="$(grep -m1 'Vulkan presenter on' "$LOG" 2>/dev/null | sed 's/.*Vulkan presenter on //')"
[ -n "$DEV" ] && say "GPU    $DEV"
case "$DEV" in
    *llvmpipe*|*swiftshader*|*lavapipe*) say "WARNING: software Vulkan device -- the real driver was not found" ;;
    "") say "note: no 'Vulkan presenter on' line yet; see $LOG" ;;
esac

if [ -z "$WIN" ]; then
    say "no window titled 'Minecraft' appeared within 30 s; see $LOG"
    exit 3
fi
DISPLAY="$DISPLAY" xdotool windowactivate "$WIN" 2>/dev/null
DISPLAY="$DISPLAY" xdotool windowfocus "$WIN" 2>/dev/null

say "playing (pid $GAME_PID, window $WIN). Close the window, or Ctrl-C here, to stop."

# Follow the log so the operator sees what the emulator says while playing.
tail -n 2 -F "$LOG" 2>/dev/null &
TAIL_PID=$!
wait "$GAME_PID"
STATUS=$?
kill "$TAIL_PID" 2>/dev/null
if grep -q 'crash: signal' "$LOG" 2>/dev/null; then
    say "the game crashed:"
    grep -m1 'crash: signal' "$LOG" | cut -c1-200 >&2
    exit 1
fi
case "$STATUS" in
    0|130|143) say "exited"; exit 0 ;;
    124) say "time budget reached"; exit 0 ;;
    *) say "exited with status $STATUS"; exit 1 ;;
esac
