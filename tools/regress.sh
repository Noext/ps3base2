#!/bin/sh
# regress.sh -- run every port to its own reference point and judge each one
# mechanically. Run this after ANY change to the runtime or the lifter.
#
# "Judge mechanically" means exit code and a rising counter, never a grep for a
# word. A handled page fault prints a dozen stack lines; counting them once
# looked like sixteen crashes while the game was running fine.
#
# Reference points differ per game because each has got further than the last
# time anyone checked:
#
#   minecraft   a generated world (docs/PLAY.md), reachable at about 410 s
#   gta-sa      in game at Grove Street (docs/recon/gta-sa.md)
#   rdr         the Rockstar Social Club legal notice -- the title's own front
#               end, drawn from its own assets -- with more than 2000 flips and
#               no SPURS abort before 60 s. The screen is checked by reading the
#               captured frame back (tesseract), because a flip count alone
#               cannot tell the legal notice from a black screen; the text is
#               the title's, so seeing it proves its renderer is drawing its
#               own resource, not just presenting an empty buffer.
#
# Usage:
#   tools/regress.sh                 # all three
#   tools/regress.sh minecraft gta-sa
#   tools/regress.sh --list
#
# Exit: 0 all present games passed, 1 something regressed, 2 environment problem.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SECONDS_PER_GAME=${SECONDS_PER_GAME:-120}
DISPLAY_NUM=${DISPLAY_NUM:-:0}

# Each game: binary, disc dir, and what a pass looks like.
#   alive   must reach the budget (exit 124) with flips
#   either  may also stop cleanly on a discovery (exit 3)
#   screen  must reach a named screen (baseline 'screen='), keep flips up, and
#           not abort before the recorded deadline (baseline 'abort_after=')
game_binary() {
    case "$1" in
        minecraft) echo "$ROOT/build/ps3base" ;;
        gta-sa)    echo "$ROOT/build-gta/ps3base" ;;
        rdr)       echo "$ROOT/build-rdr/ps3base" ;;
    esac
}
game_disc() {
    case "$1" in
        minecraft) echo "${MC_DISC:-$HOME/Dev/mc-port/disc}" ;;
        gta-sa)    echo "${GTA_DISC:-$HOME/Dev/gta-port/disc}" ;;
        rdr)       echo "${RDR_DISC:-$HOME/Dev/rdr-port/vfs/dev_bdvd}" ;;
    esac
}
game_window() {
    case "$1" in
        minecraft) echo "Minecraft" ;;
        gta-sa)    echo "Grand Theft Auto" ;;
        rdr)       echo "Red Dead Redemption" ;;
        *)         echo "" ;;
    esac
}
# The numbers that decide a pass live in docs/regress-baseline.md, one line per
# game: "rdr   flips=2000 screen=socialclub abort_after=60". Raising a bar is
# one edit in one place and the history table beside it says why.
baseline_file() { echo "$ROOT/docs/regress-baseline.md"; }
baseline_get() {
    f=$(baseline_file)
    [ -f "$f" ] || return 0
    awk -v g="$1" -v k="$2" '$1 == g { for (i = 2; i <= NF; i++) { split($i, a, "="); if (a[1] == k) { print a[2]; exit } } }' "$f"
}
baseline_flips() {
    n=$(baseline_get "$1" flips)
    echo "${n:-0}"
}

# Frame capture needs a display, a window finder, a grabber and an OCR reader.
# Without them the 'screen' criterion cannot be judged, and the run is reported
# as an environment problem rather than as a pass it did not earn.
capture_ready() {
    command -v xdotool >/dev/null 2>&1 &&
    command -v import  >/dev/null 2>&1 &&
    command -v tesseract >/dev/null 2>&1
}
window_of() {  # display, window name -> id (empty if not found yet)
    w=$(DISPLAY="$1" xdotool search --name "$2" 2>/dev/null | tail -1)
    [ -n "$w" ] || w=$(DISPLAY="$1" xdotool search --class ps3base 2>/dev/null | tail -1)
    printf '%s' "$w"
}
frame_shows() {  # png, marker text
    tesseract "$1" - 2>/dev/null | grep -qi -- "$2"
}

list_points() {
    cat <<'EOF'
minecraft   a generated world      pass = alive at the budget, flips rising, 0 crashes
gta-sa      in game, Grove Street  pass = alive at the budget, flips rising, 0 crashes
rdr         Social Club notice     pass = notice read on screen, flips >= 2000, no abort before 60 s
EOF
}

game_expect() {
    case "$1" in
        rdr) echo "screen" ;;
        *)   echo "alive" ;;
    esac
}

run_one() {
    name=$1
    bin=$(game_binary "$name")
    disc=$(eval echo "$(game_disc "$name")")
    expect=$(game_expect "$name")

    if [ ! -x "$bin" ]; then
        printf '%-12s SKIP  no binary at %s\n' "$name" "$bin"
        return 2
    fi
    if [ ! -d "$disc" ]; then
        printf '%-12s SKIP  no disc at %s\n' "$name" "$disc"
        return 2
    fi

    log="/tmp/regress-$name.log"
    shots="/tmp/regress-$name-shots"
    rm -f "$log"; rm -rf "$shots"; mkdir -p "$shots"
    screen_hit=0
    screen_at=""

    marker=""
    [ "$expect" = screen ] && marker=$(baseline_get "$name" screen)

    if [ "$expect" = screen ] && ! capture_ready; then
        printf '%-12s SKIP  screen check needs xdotool, import and tesseract\n' "$name"
        return 2
    fi
    if [ "$expect" = screen ] && [ -z "$marker" ]; then
        printf '%-12s SKIP  no screen= marker in docs/regress-baseline.md\n' "$name"
        return 2
    fi

    # A previous game can leave its guest running: the sampler kills the shell,
    # not the child, and a stray ps3base keeps burning every core while the next
    # title is being measured. That is not a regression of the title, so it is
    # removed and the load is reported, rather than silently lowering the flips.
    for p in /proc/[0-9]*; do
        c=$(tr "\0" " " < "$p/cmdline" 2>/dev/null) || continue
        case "$c" in
            *ps3base*--no-launcher*)
                [ "$(cat "$p/comm" 2>/dev/null)" != "tmux: server" ] && kill -9 "${p#/proc/}" 2>/dev/null
                ;;
        esac
    done 2>/dev/null

    load=$(cut -d" " -f1 /proc/loadavg)
    printf "%-12s (load %s before the run)\n" "$name" "$load"
    # Bounded because this run is measured, not watched. The title is started in
    # the background so the frame sampler can read the display while it runs;
    # the sampler stops as soon as the marker is seen.
    PS3BASE_DISC_DIR="$disc" PS3BASE_LOG=info DISPLAY="$DISPLAY_NUM" \
    XDG_RUNTIME_DIR="/run/user/$(id -u)" \
        timeout "$SECONDS_PER_GAME" "$bin" --no-launcher >"$log" 2>&1 &
    pid=$!
    started=$(date +%s)
    if [ "$expect" = screen ]; then
        wname=$(game_window "$name")
        i=0
        # Two seconds is the sampling interval: finer than the ~9 s the notice
        # stays up, coarse enough that OCR does not load the machine the title
        # is running on, which would move the flip count it is being judged by.
        while [ "$i" -lt 30 ] && kill -0 "$pid" 2>/dev/null; do
            sleep 2
            i=$((i + 1))
            [ "$i" -ge 2 ] || continue          # the window does not exist yet
            w=$(window_of "$DISPLAY_NUM" "$wname")
            [ -n "$w" ] || continue
            png="$shots/f$(printf '%02d' "$i").png"
            DISPLAY="$DISPLAY_NUM" import -window "$w" "$png" 2>/dev/null || continue
            if frame_shows "$png" "$marker"; then
                screen_hit=1
                screen_at=$(($(date +%s) - started))
                cp "$png" "$shots/hit.png" 2>/dev/null
                break
            fi
        done
    fi
    wait "$pid"
    code=$?

    flips=$(grep -a 'rsx: flip' "$log" 2>/dev/null | tail -1 | grep -oE 'flip [0-9]+' | grep -oE '[0-9]+')
    flips=${flips:-0}
    crashes=$(grep -ac 'crash: signal' "$log" 2>/dev/null)
    crashes=${crashes:-0}

    case "$code" in
        124) verdict="alive" ;;
        3)   verdict="discovery stop" ;;
        0)   verdict="exited cleanly" ;;
        139) verdict="SEGFAULT" ;;
        134) verdict="ABORT" ;;
        *)   verdict="exit $code" ;;
    esac
    # A SPURS stop exits through _sys_process_exit, so the code is the title's
    # own and not a signal; name the stop instead of printing a bare number.
    grep -aq 'The SPURS is aborted' "$log" 2>/dev/null && verdict="SPURS abort"

    # When the SPURS side gives up, the title exits through _sys_process_exit
    # with CELL_SPURS_TASK_ERROR_FATAL. That is a stop, not a crash, so it is
    # judged by *when* it happens: the criterion is no abort before the deadline.
    abort_t=$(awk '/SPURS is aborted|_sys_process_exit\(0x8001000c\)|PS3 SDK INTERNAL ASSERTION FAILURE/ { print $2; exit }' "$log" 2>/dev/null)
    abort_t=${abort_t:-}
    abort_after=$(baseline_get "$name" abort_after)
    abort_after=${abort_after:-60}
    abort_note="none"
    [ -n "$abort_t" ] && abort_note=$(printf 'at %.0fs' "$abort_t")

    # Compare against the recorded baseline, not against zero. A game that is
    # meant to flip must keep flipping; a game that is blocked at zero passes on
    # liveness and crash-freedom alone, so its slow progress is not mistaken for
    # a regression. docs/regress-baseline.md carries the numbers and their history.
    floor=$(baseline_flips "$name")
    ok=1
    [ "$flips" -ge "$floor" ] || ok=0
    [ "$crashes" -eq 0 ] || ok=0
    case "$expect" in
        alive)  [ "$code" = 124 ] || ok=0 ;;
        either) case "$code" in 124|3) ;; *) ok=0 ;; esac ;;
        screen)
            # The screen itself, then the deadline. An abort after the deadline
            # is allowed and expected: it is the next milestone's work, not a
            # regression, and the exit code is therefore not judged here.
            [ "$screen_hit" = 1 ] || ok=0
            if [ -n "$abort_t" ]; then
                awk -v t="$abort_t" -v lim="$abort_after" 'BEGIN { exit !(t+0 < lim+0) }' && ok=0
            fi
            ;;
    esac

    screen_note=""
    if [ "$expect" = screen ]; then
        if [ "$screen_hit" = 1 ]; then screen_note=" screen=$marker@${screen_at}s"
        else screen_note=" screen=MISSING"; fi
        screen_note="$screen_note abort=$abort_note"
    fi

    if [ "$ok" = 1 ]; then
        printf '%-12s PASS  %-15s flips=%-6s crashes=%s%s\n' "$name" "$verdict" "$flips" "$crashes" "$screen_note"
        return 0
    fi
    printf '%-12s FAIL  %-15s flips=%-6s crashes=%-3s%s log=%s\n' "$name" "$verdict" "$flips" "$crashes" "$screen_note" "$log"
    return 1
}

if [ "${1:-}" = "--list" ]; then list_points; exit 0; fi

games="${*:-minecraft gta-sa rdr}"
echo "regress: ${SECONDS_PER_GAME}s per game on display $DISPLAY_NUM"
failed=0
skipped=0
for g in $games; do
    run_one "$g"
    case $? in
        0) ;;
        1) failed=$((failed + 1)) ;;
        2) skipped=$((skipped + 1)) ;;
    esac
done
echo
if [ "$failed" -gt 0 ]; then
    echo "regress: $failed game(s) REGRESSED"
    exit 1
fi
echo "regress: all present games passed ($skipped skipped)"
exit 0
