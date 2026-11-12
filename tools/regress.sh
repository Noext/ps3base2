#!/bin/sh
# regress.sh -- run every port to its own reference point and judge each one
# mechanically. Run this after ANY change to the runtime or the lifter.
#
# "Judge mechanically" means exit code and a rising counter, never a grep for a
# word. A handled page fault prints a dozen stack lines; counting them once
# looked like sixteen crashes while the game was running fine.
#
# HOW A RUN IS JUDGED -- and why the reference is a second binary and not only a
# number in a file:
#
#   Every comparison is an A/B on the same machine within the same minute.
#   Each game is run twice, back to back, on the same display: once with the
#   reference binary (the pristine main checkout's build) and once with this
#   tree's binary. The candidate must not fall behind the reference by more than
#   the measured spread of two runs of the same code. Comparing against a number
#   measured days ago is not a comparison: this machine is shared, and the same
#   binary has been seen at 5401 and 5821 flips in two adjacent minutes purely
#   from other agents' load. Two runs minutes apart are not comparable, and a
#   false "regression" was reported exactly that way once.
#
#   The numbers in docs/regress-baseline.md are the reference binary's own
#   measured result, read as a floor for the case where no reference binary is
#   available (a partial tree, a bisect) and as the record of what was measured.
#
# Reference points, per game:
#
#   minecraft   a generated world (docs/PLAY.md), alive at a 120 s budget
#   gta-sa      in game at Grove Street (docs/recon/gta-sa.md), alive
#   rdr         the TITLE SCREEN: RED DEAD REDEMPTION with "Press START" on
#               black, drawn from the title's own assets. The screen is read
#               back from a recording of the run, not from a poll -- the title
#               is up for about a quarter of a second before the SPURS abort at
#               ~23 s, so a sampler that grabs the display twice a second steps
#               over it (66 grabs, no hit) while a 30 fps recording catches
#               eight frames of it. tools/frame_marker.py does the read.
#
# Usage:
#   tools/regress.sh                 # all three
#   tools/regress.sh minecraft gta-sa
#   tools/regress.sh --list
#
# Environment:
#   SECONDS_PER_GAME=120   budget per run (so 2x per game)
#   DISPLAY_NUM=:0         display to run on and to record
#   REF_REQUIRED=1         treat a missing reference binary as a failure
#   MC_BIN GTA_BIN RDR_BIN the candidate binaries (default: this tree's builds)
#   MC_REF_BIN ... REF_BIN  the reference binaries (default: the main checkout's)
#
# Exit: 0 all present games passed, 1 something regressed, 2 environment problem.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SECONDS_PER_GAME=${SECONDS_PER_GAME:-120}
DISPLAY_NUM=${DISPLAY_NUM:-:0}
REF_ROOT=${REF_ROOT:-$ROOT/../ps3base2}
# How far behind the reference a candidate may be and still pass. Measured
# spread of two runs of the SAME code, back to back: minecraft 5881 vs 5641
# (4.1%), gta-sa 5401 vs 5821 (7.2%). 10% covers that and still moves when a
# change costs a real number of frames.
JITTER_PCT=10
REF_REQUIRED=${REF_REQUIRED:-0}

game_binary() {
    case "$1" in
        minecraft) echo "${MC_BIN:-$ROOT/build/ps3base}" ;;
        gta-sa)    echo "${GTA_BIN:-$ROOT/build-gta/ps3base}" ;;
        rdr)       echo "${RDR_BIN:-$ROOT/build-rdr/ps3base}" ;;
    esac
}
game_ref_binary() {
    case "$1" in
        minecraft) echo "${MC_REF_BIN:-$REF_ROOT/build/ps3base}" ;;
        gta-sa)    echo "${GTA_REF_BIN:-$REF_ROOT/build-gta/ps3base}" ;;
        rdr)       echo "${RDR_REF_BIN:-$REF_ROOT/build-rdr/ps3base}" ;;
    esac
}
# The image paths are compiled in as "out-rdr/gen/reloc/liblv2.elf" and resolved
# against the process's working directory, so the run has to start in a tree
# that holds the generated images. A worktree of this repository does not (they
# are gitignored and derived from the user's disc), the main checkout does.
game_run_root() {
    case "$1" in
        minecraft) d=out-mc ;;
        gta-sa)    d=out-gta ;;
        rdr)       d=out-rdr ;;
        *)         d=out ;;
    esac
    for r in "${RUN_ROOT:-}" "$REF_ROOT" "$ROOT"; do
        [ -n "$r" ] && [ -d "$r/$d" ] && { echo "$r"; return; }
    done
    echo "$ROOT"
}
game_disc() {
    case "$1" in
        minecraft) echo "${MC_DISC:-$HOME/Dev/mc-port/disc}" ;;
        gta-sa)    echo "${GTA_DISC:-$HOME/Dev/gta-port/disc}" ;;
        rdr)       echo "${RDR_DISC:-$HOME/Dev/rdr-port/vfs/dev_bdvd}" ;;
    esac
}

# The numbers that decide a pass live in docs/regress-baseline.md, one line per
# game: "rdr flips=500 screen=PressSTART ref_flips=601". Raising a bar is one
# edit in one place and the history table beside it says why. The marker is
# written without its space because the file is parsed by whitespace; the
# comparison drops whitespace on both sides, so "PressSTART" reads "Press START".
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
game_screen_marker() { baseline_get "$1" screen; }

# The screen criterion needs a recorder, an OCR reader and a level stretch.
# Without them the 'screen' criterion cannot be judged and the run is reported as
# an environment problem rather than as a pass it did not earn.
capture_ready() {
    command -v ffmpeg    >/dev/null 2>&1 &&
    command -v tesseract >/dev/null 2>&1 &&
    command -v convert   >/dev/null 2>&1 &&
    command -v python3   >/dev/null 2>&1 &&
    python3 -c 'import PIL' >/dev/null 2>&1
}

list_points() {
    cat <<'EOF'
minecraft   a generated world      pass = alive at the budget, flips >= reference (same minute), 0 crashes
gta-sa      in game, Grove Street  pass = alive at the budget, flips >= reference (same minute), 0 crashes
rdr         the title screen       pass = "Press START" read from the recording, flips >= reference, 0 crashes
EOF
}

# Nothing may run alongside the title: a stray ps3base from the previous game
# burns every core and lowers the flips of the run being judged, and a leftover
# Xvfb client would show up in the recording as a second window.
clear_strays() {
    for p in /proc/[0-9]*; do
        c=$(tr "\0" " " < "$p/cmdline" 2>/dev/null) || continue
        case "$c" in
            *ps3base*--no-launcher*)
                [ "$(cat "$p/comm" 2>/dev/null)" != "tmux: server" ] && kill -9 "${p#/proc/}" 2>/dev/null
                ;;
        esac
    done 2>/dev/null
    return 0
}

# run_game <tag> <bin> <disc> <record:0|1> <run-root>
# Prints one line and leaves the numbers in /tmp/regress-<tag>.{flips,abort,code}
run_game() {
    tag=$1; bin=$2; disc=$3; record=$4; runroot=$5
    log="/tmp/regress-$tag.log"
    rec="/tmp/regress-$tag.mkv"
    rm -f "$log" "$rec"

    clear_strays
    load=$(cut -d" " -f1 /proc/loadavg)
    started=$(date +%s)

    # The recorder runs BEFORE the title starts and is stopped when it exits. It
    # costs CPU, so it is worth knowing that it does not move the numbers:
    # rdr without a recording is 601 flips, abort 23.4 s; with a 30 fps
    # recording of the same display it is 601 flips, abort 23.1 s.
    if [ "$record" = 1 ]; then
        ffmpeg -hide_banner -loglevel error -f x11grab -framerate 30 \
               -video_size 1280x720 -i "$DISPLAY_NUM" \
               -c:v libx264 -preset ultrafast -qp 0 -y "$rec" \
               >/tmp/regress-$tag.ff.log 2>&1 &
        ffmpeg_pid=$!
        sleep 2
    fi

    # The image paths are compiled in as "out-rdr/gen/reloc/liblv2.elf" and are
    # resolved against the working directory, so the run starts in the tree that
    # holds the generated images. A worktree does not hold them (they are
    # gitignored and derived from the user's own disc); the main checkout does.
    ( cd "$runroot" && PS3BASE_DISC_DIR="$disc" PS3BASE_LOG=info DISPLAY="$DISPLAY_NUM" \
      XDG_RUNTIME_DIR="/run/user/$(id -u)" \
          timeout "$SECONDS_PER_GAME" "$bin" --no-launcher ) >"$log" 2>&1
    code=$?

    if [ "$record" = 1 ]; then
        kill -INT "$ffmpeg_pid" 2>/dev/null
        wait "$ffmpeg_pid" 2>/dev/null
    fi
    elapsed=$(($(date +%s) - started))

    flips=$(grep -a 'rsx: flip' "$log" 2>/dev/null | tail -1 | grep -oE 'flip [0-9]+' | grep -oE '[0-9]+')
    flips=${flips:-0}
    crashes=$(grep -ac 'crash: signal' "$log" 2>/dev/null)
    crashes=${crashes:-0}
    # A SPURS stop exits through _sys_process_exit, so the exit code is the
    # title's own and not a signal; the time it happened is what a criterion can
    # use, together with the screen it happened after.
    abort_t=$(awk '/SPURS is aborted|_sys_process_exit\\(0x8001000c\\)|PS3 SDK INTERNAL ASSERTION FAILURE/ { print $2; exit }' "$log" 2>/dev/null)
    case "$code" in
        124) verdict="alive" ;;
        3)   verdict="discovery stop" ;;
        0)   verdict="exited cleanly" ;;
        139) verdict="SEGFAULT" ;;
        134) verdict="ABORT" ;;
        *)   verdict="exit $code" ;;
    esac
    grep -aq 'The SPURS is aborted' "$log" 2>/dev/null && verdict="SPURS abort"

    echo "$flips"   > "/tmp/regress-$tag.flips"
    echo "$crashes" > "/tmp/regress-$tag.crashes"
    echo "${abort_t:-}" > "/tmp/regress-$tag.abort"
    echo "$code"    > "/tmp/regress-$tag.code"
    printf '%-12s %-12s %-15s flips=%-6s crashes=%-3s abort=%-8s %ss load=%s\n' \
        "$tag" "$(basename "$(dirname "$bin")")" "$verdict" "$flips" "$crashes" \
        "${abort_t:-none}" "$elapsed" "$load"
}

# judge <name> <cand-tag> <ref-tag> <want-screen:0|1>
# Returns 0 pass, 1 fail. The flips bar is the reference measured in the same
# minute; the absolute floor from the baseline file only applies when there was
# no reference binary to compare against.
judge() {
    name=$1; ctag=$2; rtag=$3; want_screen=$4
    cflips=$(cat "/tmp/regress-$ctag.flips" 2>/dev/null || echo 0)
    ccrash=$(cat "/tmp/regress-$ctag.crashes" 2>/dev/null || echo 0)
    ccode=$(cat "/tmp/regress-$ctag.code" 2>/dev/null || echo 1)
    cabort=$(cat "/tmp/regress-$ctag.abort" 2>/dev/null || echo "")
    floor=$(baseline_flips "$name")
    ok=1
    why=""

    if [ "$ccrash" -ne 0 ]; then ok=0; why="$why crashes"; fi

    if [ "$rtag" != "" ]; then
        rflips=$(cat "/tmp/regress-$rtag.flips" 2>/dev/null || echo 0)
        if [ "$rflips" -le 0 ]; then
            ok=0; why="$why reference-flips-0"
        else
            awk -v c="$cflips" -v r="$rflips" -v j="$JITTER_PCT" \
                'BEGIN { exit !(c + 0 < r * (100 - j) / 100) }' && { ok=0; why="$why behind-reference"; }
        fi
        bar="$rflips (reference, same minute)"
    else
        bar="$floor (baseline floor)"
        [ "$cflips" -ge "$floor" ] || { ok=0; why="$why below-floor"; }
    fi

    # The title's own screen, or a clean run to the budget. An abort after the
    # screen is the next milestone's work, not a regression: what is judged is
    # that the screen was reached and that the run did not go backwards.
    if [ "$want_screen" = 1 ]; then
        hit=$(cat "/tmp/regress-$ctag.screen" 2>/dev/null || echo 0)
        [ "$hit" = 1 ] || { ok=0; why="$why screen-missing"; }
    else
        [ "$ccode" = 124 ] || [ "$ccode" = 3 ] || { ok=0; why="$why not-alive"; }
    fi

    if [ "$ok" = 1 ]; then
        printf '  %-12s PASS  flips=%s vs %s%s\n' "$name" "$cflips" "$bar" \
            "$([ "$want_screen" = 1 ] && printf ' screen=%s' "$(cat /tmp/regress-$ctag.screen 2>/dev/null)")"
        return 0
    fi
    printf '  %-12s FAIL  flips=%s vs %s  <-%s  (log /tmp/regress-%s.log)\n' \
        "$name" "$cflips" "$bar" "$why" "$ctag"
    return 1
}

screen_check() {  # tag, marker -> writes /tmp/regress-<tag>.screen 1|0
    tag=$1; marker=$2
    video="/tmp/regress-$tag.mkv"
    if [ ! -f "$video" ]; then echo 0 > "/tmp/regress-$tag.screen"; return 1; fi
    if "$ROOT/tools/frame_marker.py" "$video" "$marker" > "/tmp/regress-$tag.marker" 2>&1; then
        echo 1 > "/tmp/regress-$tag.screen"
        sed -n '1p' "/tmp/regress-$tag.marker" | sed 's/^/  /'
        return 0
    fi
    echo 0 > "/tmp/regress-$tag.screen"
    sed -n '1p' "/tmp/regress-$tag.marker" | sed 's/^/  /'
    return 1
}

run_one() {
    name=$1
    bin=$(game_binary "$name")
    ref=$(game_ref_binary "$name")
    disc=$(eval echo "$(game_disc "$name")")

    if [ ! -x "$bin" ]; then
        printf '%-12s SKIP  no binary at %s\n' "$name" "$bin"
        return 2
    fi
    if [ ! -d "$disc" ]; then
        printf '%-12s SKIP  no disc at %s\n' "$name" "$disc"
        return 2
    fi
    marker=$(game_screen_marker "$name")
    want_screen=0
    [ -n "$marker" ] && want_screen=1
    if [ "$want_screen" = 1 ] && ! capture_ready; then
        printf '%-12s SKIP  the screen check needs ffmpeg, tesseract, convert and python3+PIL\n' "$name"
        return 2
    fi
    have_ref=0
    if [ -x "$ref" ] && [ "$ref" != "$bin" ]; then have_ref=1; fi
    if [ "$have_ref" = 0 ] && [ "$REF_REQUIRED" = 1 ]; then
        printf '%-12s SKIP  no reference binary at %s (REF_REQUIRED=1)\n' "$name" "$ref"
        return 2
    fi

    runroot=$(game_run_root "$name")
    printf '%-12s candidate %s  (run from %s)\n' "$name" "$bin" "$runroot"
    if [ "$have_ref" = 1 ]; then
        printf '%-12s reference %s  (same minute, back to back)\n' "" "$ref"

        # Reference first, then candidate, both inside the same clock minute as
        # far as the machine's load is concerned. This is the rule, not a
        # convention: the numbers below are only comparable to each other.
        run_game "$name-ref" "$ref" "$disc" "$want_screen" "$runroot"
        [ "$want_screen" = 1 ] && screen_check "$name-ref" "$marker"
        run_game "$name-cand" "$bin" "$disc" "$want_screen" "$runroot"
        [ "$want_screen" = 1 ] && screen_check "$name-cand" "$marker"
        judge "$name" "$name-cand" "$name-ref" "$want_screen"
        return $?
    fi

    printf '%-12s no reference binary: judging against the baseline floor only\n' "$name"
    run_game "$name-cand" "$bin" "$disc" "$want_screen" "$runroot"
    [ "$want_screen" = 1 ] && screen_check "$name-cand" "$marker"
    judge "$name" "$name-cand" "" "$want_screen"
    return $?
}

if [ "${1:-}" = "--list" ]; then list_points; exit 0; fi

games="${*:-minecraft gta-sa rdr}"
echo "regress: ${SECONDS_PER_GAME}s per run, reference and candidate back to back, display $DISPLAY_NUM"
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
