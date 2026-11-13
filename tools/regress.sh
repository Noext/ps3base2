#!/bin/sh
# regress.sh -- run every port to its own reference point and judge each one
# mechanically. Run this after ANY change to the runtime or the lifter.
#
# "Judge mechanically" means exit code and a rising counter, never a grep for a
# word. A handled page fault prints a dozen stack lines; counting them once
# looked like sixteen crashes while the game was running fine.
#
# THE COMPARISON RULE -- every comparison is an A/B on the same machine within
# the same minute.
#
#   Each game is run in pairs, back to back on the same display: the reference
#   binary (the pristine main checkout's build) and then this tree's binary. The
#   candidate is judged against the reference measured in that same pair, not
#   against a number recorded days ago. This machine is shared: the SAME minecraft
#   binary gave 6601 and 6661 flips in one pair and 6421 vs 5341 in another, and
#   gta-sa gave 5881 vs 6301. A bar that does not know about that spread reports
#   regressions that are not there, and one did.
#
#   Attempts repeat, and the best attempt of each side is compared. The best of a
#   few runs is the title's own capability; one run is the machine's mood. The
#   count per game is in the baseline file.
#
# THE NUMBERS live in docs/regress-baseline.md, one line per game, and are the
# floor used only when no reference binary is available (a partial tree, a
# bisect). The history table beside them records what was measured and why.
#
# Reference points, per game:
#
#   minecraft   a generated world (docs/PLAY.md), alive at a 120 s budget
#   gta-sa      in game at Grove Street (docs/recon/gta-sa.md), alive
#   rdr         the TITLE SCREEN: RED DEAD REDEMPTION with "Press START" on black,
#               drawn from the title's own assets at ~22 s. Read back from a 30 fps
#               recording of the run by tools/frame_marker.py -- a poll steps over
#               it (66 window grabs over 23 s, no hit; the recording catches eight
#               frames of it).
#
# Usage:
#   tools/regress.sh                 # all three
#   tools/regress.sh minecraft gta-sa
#   tools/regress.sh --list
#
# Environment:
#   SECONDS_PER_GAME=120   budget per run
#   DISPLAY_NUM=:0         display to run on and to record
#   REF_ROOT               main checkout holding the generated images and the
#                          reference binaries (default: ../ps3base2)
#   REF_REQUIRED=1         a missing reference binary is a failure, not a fallback
#   MC_BIN GTA_BIN RDR_BIN candidate binaries (default: this tree's builds)
#   MC_REF_BIN GTA_REF_BIN RDR_REF_BIN reference binaries
#
# Exit: 0 all present games passed, 1 something regressed, 2 environment problem
# (a requested game could not be run, or another run holds the lock).
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SECONDS_PER_GAME=${SECONDS_PER_GAME:-120}
DISPLAY_NUM=${DISPLAY_NUM:-:0}
REF_ROOT=${REF_ROOT:-$ROOT/../ps3base2}
REF_REQUIRED=${REF_REQUIRED:-0}

# How far behind the reference a candidate may be and still pass. This is the
# measured spread of runs of the SAME code on this machine, and it is large:
#
#   minecraft, same commit, adjacent pairs: 6601 vs 6661 (0.9%), 6421 vs 5341
#                                          (17.0%), 6241 vs 6061 (2.9%)
#   gta-sa,    same commit, adjacent pairs: 5881 vs 6301 (7.1%), 5761 vs 5941 (3.1%)
#
# So the noise floor of a single pair is about 17%, and a bar tighter than that
# reports regressions that are not there. 20% is the jitter floor; what the bar
# therefore catches is a COLLAPSE -- the failures this port has actually had were
# 7081 flips to 0 (the net calls) and 5401 to 0, not a tenth of a frame rate. A
# change that costs less than a fifth of the frames cannot be told apart from this
# machine's mood on one pair, and the script says so instead of pretending.
JITTER_PCT=20

# One regress run at a time. Two runs on the same display share /tmp paths and
# each one's clear_strays kills the other's game, so the numbers are void before
# any correction helps. The lock is a directory (atomic) and is removed by the
# trap. Owner liveness is checked so a killed run does not block the next one.
REGRESS_LOCK=${REGRESS_LOCK:-/tmp/ps3base-regress.lock}
acquire_lock() {
    served=0
    while ! mkdir "$REGRESS_LOCK" 2>/dev/null; do
        lock_pid=$(cat "$REGRESS_LOCK/pid" 2>/dev/null || echo "")
        if [ -n "$lock_pid" ] && ! kill -0 "$lock_pid" 2>/dev/null; then
            rm -rf "$REGRESS_LOCK"
            continue
        fi
        served=$((served + 1))
        if [ "$served" -gt 900 ]; then
            echo "regress: $REGRESS_LOCK has been held for 30 minutes; remove it by hand" >&2
            exit 2
        fi
        [ "$served" = 1 ] && echo "regress: waiting for the run holding $REGRESS_LOCK" >&2
        sleep 2
    done
    echo $$ > "$REGRESS_LOCK/pid"
}
release_lock() { rm -rf "$REGRESS_LOCK"; }

# Every game runs in its own process group, and every group this script started
# is killed on exit, on INT and on TERM. Without this a killed regress.sh leaves
# its titles running: measured 2026-11-13, an orphaned `timeout 120 ... --no-launcher`
# kept spawning runs after the script was believed dead, and the load reached 22.
RUN_TITLE_PIDS=""
# Kill every title this script started, and every recorder, on exit or signal.
#
# Two measured facts shaped this:
#  - "kill -TERM -- -$pgid" FAILS under dash (this script's /bin/sh): it parses
#    the "-" after "--" as a signal number and returns "Illegal number: -",
#    silently killing nothing. The working form is "kill -TERM -$pgid".
#  - a title launched through setsid gets its own group and therefore escapes a
#    caller's group kill of regress.sh. The title is a plain child here instead.
kill_run_groups() {
    for p in $RUN_TITLE_PIDS; do
        [ -n "$p" ] || continue
        kill -TERM "$p" 2>/dev/null
        sleep 0.2
        kill -KILL "$p" 2>/dev/null
    done
    RUN_TITLE_PIDS=""
    # Belt and braces: any title left in our own process group, by any path.
    for d in /proc/[0-9]*; do
        c=$(tr '\0' ' ' < "$d/cmdline" 2>/dev/null) || continue
        case "$c" in
            *ps3base*--no-launcher*)
                pg=$(awk '/^Pgid:/{print $2}' "$d/status" 2>/dev/null)
                [ "$pg" = "$$" ] && kill -9 "${d#/proc/}" 2>/dev/null
                ;;
        esac
    done 2>/dev/null
}
release_all() { kill_run_groups; release_lock; }
trap 'release_all' EXIT INT TERM

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
game_disc() {
    case "$1" in
        minecraft) echo "${MC_DISC:-$HOME/Dev/mc-port/disc}" ;;
        gta-sa)    echo "${GTA_DISC:-$HOME/Dev/gta-port/disc}" ;;
        rdr)       echo "${RDR_DISC:-$HOME/Dev/rdr-port/vfs/dev_bdvd}" ;;
    esac
}
# The image paths are compiled in as "out-rdr/gen/reloc/liblv2.elf" and are
# resolved against the working directory, so a run starts in the tree that holds
# the generated images. A worktree does not hold them (gitignored, derived from
# the user's own disc); the main checkout does.
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

baseline_file() { echo "$ROOT/docs/regress-baseline.md"; }
# baseline_get <game> <key>: one whitespace-separated key=value field from the
# game's line. The screen marker is written without its space (PressSTART) for
# this reason; frame_marker.py compares with whitespace removed on both sides.
baseline_get() {
    f=$(baseline_file)
    [ -f "$f" ] || return 0
    awk -v g="$1" -v k="$2" '$1 == g { for (i = 2; i <= NF; i++) { split($i, a, "="); if (a[1] == k) { print a[2]; exit } } }' "$f"
}
baseline_flips() {
    n=$(baseline_get "$1" flips)
    echo "${n:-0}"
}

# The screen criterion needs a recorder, an OCR reader and a level stretch.
# Without them it cannot be judged, and the run is reported as an environment
# problem rather than as a pass it did not earn.
capture_ready() {
    command -v ffmpeg    >/dev/null 2>&1 &&
    command -v tesseract >/dev/null 2>&1 &&
    command -v convert   >/dev/null 2>&1 &&
    python3 -c 'import PIL' >/dev/null 2>&1
}

list_points() {
    cat <<'EOF'
minecraft   a generated world      pass = alive at the budget, flips >= reference (same minute), 0 crashes
gta-sa      in game, Grove Street  pass = alive at the budget, flips >= reference (same minute), 0 crashes
rdr         the title screen       pass = "Press START" read from a recording, flips >= floor, 0 crashes
EOF
}

# Nothing may run alongside the title: a stray ps3base from the previous game
# burns every core and lowers the flips of the run being judged, and a leftover
# client would show up in the recording as a second window.
clear_strays() {
    # Never kill this script's own children: a driver that hard-linked the binary
    # to another name was the only way round the old rule.
    for p in /proc/[0-9]*; do
        c=$(tr "\0" " " < "$p/cmdline" 2>/dev/null) || continue
        case "$c" in
            *ps3base*--no-launcher*)
                [ "$(cat "$p/comm" 2>/dev/null)" = "tmux: server" ] && continue
                # The game runs inside a subshell, so its PPid is that
                # subshell, not $$ -- comparing one level was not enough and
                # the harness killed its own runs (exit 137, flips 181). Walk
                # the whole parent chain instead.
                anc=$p; own=1
                while [ -n "$anc" ] && [ "$anc" != "0" ] && [ "$anc" != "1" ]; do
                    anc=$(awk '/^PPid:/{print $2}' "/proc/$anc/status" 2>/dev/null)
                    if [ "$anc" = "$$" ]; then own=0; break; fi
                done
                [ "$own" = "1" ] && kill -9 "${p#/proc/}" 2>/dev/null
                kill -9 "${p#/proc/}" 2>/dev/null
                ;;
        esac
    done 2>/dev/null
    return 0
}

# run_game <tag> <bin> <disc> <record:0|1> <run-root>
# Leaves the numbers in /tmp/regress-<tag>.{flips,crashes,abort,code}, prints one
# line. The tag carries the attempt number and the side, so nothing collides.
#
# EVERY variable in this function is prefixed rg_. These are shell globals, and a
# helper that assigns the same names as its caller silently rewrites the caller's
# state: an earlier version set "bin" to its own argument, so after the reference
# run the candidate slot re-ran the REFERENCE binary -- and a negative control
# with RDR_BIN=/bin/false came back PASS with 601 flips. That is a false green of
# exactly the kind this script exists to prevent. The prefixes are the fix and the
# negative control is the test that catches it.
run_game() {
    rg_tag=$1; rg_bin=$2; rg_disc=$3; rg_record=$4; rg_runroot=$5
    rg_log="/tmp/regress-$rg_tag.log"
    rg_rec="/tmp/regress-$rg_tag.mkv"
    rm -f "$rg_log" "$rg_rec"

    clear_strays
    rg_load=$(cut -d" " -f1 /proc/loadavg)
    rg_started=$(date +%s)

    # The recorder starts BEFORE the title does and is stopped after it exits. It
    # costs CPU, so it is worth knowing it does not move what is judged: rdr
    # without a recording is 601 flips, abort 23.4 s; with a 30 fps recording of
    # the same display it is 601 flips, abort 23.1 s.
    rg_ffmpeg=""
    if [ "$rg_record" = 1 ]; then
        ffmpeg -hide_banner -loglevel error -f x11grab -framerate 30 \
               -video_size 1280x720 -i "$DISPLAY_NUM" \
               -c:v libx264 -preset ultrafast -qp 0 -y "$rg_rec" \
               >"/tmp/regress-$rg_tag.ff.log" 2>&1 &
        rg_ffmpeg=$!
        sleep 2
    fi

    # The title stays in THIS script's process group, deliberately, and
    # `timeout --foreground` is what keeps it there: GNU timeout otherwise puts
    # its child in a NEW process group, so the title had its own pgid and escaped
    # a group kill of regress.sh. Measured in the process listing: the title's
    # pgid equalled its own pid instead of regress.sh's.
    #
    # Measured why: on 2026-11-13 a regress.sh believed dead kept spawning RDR
    # runs; killing it by pid left them alive and the load reached 22, voiding a
    # measurement. The first fix here launched the title with setsid, which gave
    # it a group of its OWN -- and a caller that kills regress.sh's group then
    # misses it entirely, which is the same failure with more steps. So the
    # title is a plain child in our group, the trap kills it by pid, and
    # tools/regress_one.sh makes regress.sh a session leader so a caller's group
    # kill reaches the whole tree.
    rg_title_pid=""
    (
        cd "$rg_runroot" || exit 2
        exec env GOWA_FLASH_DIR="${GOWA_FLASH_DIR:-/root/Dev/devflash-out/dev_flash}" \
            PS3BASE_DISC_DIR="$rg_disc" PS3BASE_LOG=info DISPLAY="$DISPLAY_NUM" \
            XDG_RUNTIME_DIR="/run/user/$(id -u)" \
            timeout --foreground "$SECONDS_PER_GAME" "$rg_bin" --no-launcher
    ) >"$rg_log" 2>&1 &
    rg_wait=$!
    # The subshell execs, so its pid is the timeout's pid: killable directly.
    RUN_TITLE_PIDS="$RUN_TITLE_PIDS $rg_wait"
    wait "$rg_wait"
    rg_code=$?

    # Nothing from this run may outlive it: kill the title and the recorder.
    kill -TERM "$rg_wait" 2>/dev/null
    sleep 0.2
    kill -KILL "$rg_wait" 2>/dev/null
    if [ -n "$rg_ffmpeg" ]; then
        kill -INT "$rg_ffmpeg" 2>/dev/null
        wait "$rg_ffmpeg" 2>/dev/null
    fi
    rg_elapsed=$(($(date +%s) - rg_started))

    rg_flips=$(grep -a 'rsx: flip' "$rg_log" 2>/dev/null | tail -1 | grep -oE 'flip [0-9]+' | grep -oE '[0-9]+')
    rg_flips=${rg_flips:-0}
    rg_crashes=$(grep -ac 'crash: signal' "$rg_log" 2>/dev/null)
    rg_crashes=${rg_crashes:-0}
    # A SPURS stop exits through _sys_process_exit, so the exit code is the title's
    # own and not a signal. The time it happened is recorded because it is what
    # tells a slow start from a changed one.
    rg_abort=$(awk '/SPURS is aborted|_sys_process_exit\(0x8001000c\)|PS3 SDK INTERNAL ASSERTION FAILURE/ { print $2; exit }' "$rg_log" 2>/dev/null)
    rg_verdict="exit $rg_code"
    case "$rg_code" in
        124) rg_verdict="alive" ;;
        3)   rg_verdict="discovery stop" ;;
        0)   rg_verdict="exited cleanly" ;;
        139) rg_verdict="SEGFAULT" ;;
        134) rg_verdict="ABORT" ;;
    esac
    grep -aq 'The SPURS is aborted' "$rg_log" 2>/dev/null && rg_verdict="SPURS abort"

    echo "$rg_flips"          > "/tmp/regress-$rg_tag.flips"
    echo "$rg_crashes"        > "/tmp/regress-$rg_tag.crashes"
    echo "${rg_abort:-}"      > "/tmp/regress-$rg_tag.abort"
    echo "$rg_code"           > "/tmp/regress-$rg_tag.code"
    printf '  %-14s %-13s flips=%-6s crashes=%-3s abort=%-9s %3ss load=%s\n' \
        "$rg_tag" "$rg_verdict" "$rg_flips" "$rg_crashes" "${rg_abort:-none}" "$rg_elapsed" "$rg_load"
}

# screen_check <tag> <marker>: writes /tmp/regress-<tag>.screen 1|0, and prints
# the one line frame_marker.py produced so the log says hit or miss and why.
screen_check() {
    sc_tag=$1; sc_marker=$2
    sc_video="/tmp/regress-$sc_tag.mkv"
    if [ ! -f "$sc_video" ]; then
        echo 0 > "/tmp/regress-$sc_tag.screen"
        echo "    no recording at $sc_video"
        return 1
    fi
    if "$ROOT/tools/frame_marker.py" "$sc_video" "$sc_marker" > "/tmp/regress-$sc_tag.marker" 2>&1; then
        echo 1 > "/tmp/regress-$sc_tag.screen"
        sed -n '1p' "/tmp/regress-$sc_tag.marker" | sed 's/^/    /'
        return 0
    fi
    echo 0 > "/tmp/regress-$sc_tag.screen"
    sed -n '1p' "/tmp/regress-$sc_tag.marker" | sed 's/^/    /'
    return 1
}

run_one() {
    ro_name=$1
    ro_bin=$(game_binary "$ro_name")
    ro_ref=$(game_ref_binary "$ro_name")
    ro_disc=$(eval echo "$(game_disc "$ro_name")")

    if [ ! -x "$ro_bin" ]; then
        printf '%-12s SKIP  no binary at %s\n' "$ro_name" "$ro_bin"
        return 2
    fi
    if [ ! -d "$ro_disc" ]; then
        printf '%-12s SKIP  no disc at %s\n' "$ro_name" "$ro_disc"
        return 2
    fi

    ro_marker=$(baseline_get "$ro_name" screen)
    ro_want_screen=0
    [ -n "$ro_marker" ] && ro_want_screen=1
    if [ "$ro_want_screen" = 1 ] && ! capture_ready; then
        printf '%-12s SKIP  the screen check needs ffmpeg, tesseract, convert and python3+PIL\n' "$ro_name"
        return 2
    fi

    ro_have_ref=0
    if [ -x "$ro_ref" ] && [ "$ro_ref" != "$ro_bin" ]; then ro_have_ref=1; fi
    if [ "$ro_have_ref" = 0 ] && [ "$REF_REQUIRED" = 1 ]; then
        printf '%-12s SKIP  no reference binary at %s (REF_REQUIRED=1)\n' "$ro_name" "$ro_ref"
        return 2
    fi

    # One pair is one sample and one sample is noisy (17% between adjacent pairs
    # of the same code). Best-of-N is the title's own capability.
    ro_attempts=$(baseline_get "$ro_name" attempts)
    ro_attempts=${ro_attempts:-2}
    if [ "$ro_want_screen" = 1 ]; then
        ro_attempts=$(baseline_get "$ro_name" screen_attempts)
        ro_attempts=${ro_attempts:-6}
    fi
    ro_runroot=$(game_run_root "$ro_name")
    ro_floor=$(baseline_flips "$ro_name")
    ro_metric=$(baseline_get "$ro_name" metric)
    ro_metric=${ro_metric:-flips}

    printf '%-12s candidate %s\n' "$ro_name" "$ro_bin"
    if [ "$ro_have_ref" = 1 ]; then
        printf '%-12s reference %s  (paired back to back)\n' "" "$ro_ref"
    else
        printf '%-12s no reference binary: judging against the baseline floor %s\n' "" "$ro_floor"
    fi
    printf '%-12s run root %s, up to %s attempt(s)\n' "" "$ro_runroot" "$ro_attempts"

    ro_start=$(date +%s)
    ro_cbest=0; ro_rbest=0; ro_ccrash=0; ro_ccode=3; ro_chit=0; ro_rhit=0
    ro_a=1
    while [ "$ro_a" -le "$ro_attempts" ]; do
        if [ "$ro_have_ref" = 1 ]; then
            run_game "$ro_name-ref$ro_a" "$ro_ref" "$ro_disc" "$ro_want_screen" "$ro_runroot"
            if [ "$ro_want_screen" = 1 ] && screen_check "$ro_name-ref$ro_a" "$ro_marker"; then
                ro_rhit=$((ro_rhit + 1))
            fi
            ro_rf=$(cat "/tmp/regress-$ro_name-ref$ro_a.flips" 2>/dev/null || echo 0)
            [ "$ro_rf" -gt "$ro_rbest" ] && ro_rbest=$ro_rf
        fi

        run_game "$ro_name-cand$ro_a" "$ro_bin" "$ro_disc" "$ro_want_screen" "$ro_runroot"
        if [ "$ro_want_screen" = 1 ] && screen_check "$ro_name-cand$ro_a" "$ro_marker"; then
            ro_chit=$((ro_chit + 1))
        fi
        ro_cf=$(cat "/tmp/regress-$ro_name-cand$ro_a.flips" 2>/dev/null || echo 0)
        [ "$ro_cf" -gt "$ro_cbest" ] && ro_cbest=$ro_cf
        ro_ccrash=$((ro_ccrash + $(cat "/tmp/regress-$ro_name-cand$ro_a.crashes" 2>/dev/null || echo 0)))
        ro_ccode=$(cat "/tmp/regress-$ro_name-cand$ro_a.code" 2>/dev/null || echo 3)

        # A hit on the screen ends the attempts: the criterion is "reached", not
        # "reached every time", and every further pair costs half a minute.
        if [ "$ro_want_screen" = 1 ] && [ "$ro_chit" -gt 0 ]; then break; fi
        ro_a=$((ro_a + 1))
    done
    ro_window=$(($(date +%s) - ro_start))

    ro_ok=1
    ro_why=""
    if [ "$ro_ccrash" -ne 0 ]; then ro_ok=0; ro_why="$ro_why crashes=$ro_ccrash"; fi

    # metric=flips (the default): the flip count is the measure, compared against
    # the reference measured in the same pair.
    #
    # metric=screen: for this title the flip count is NOT the measure and the A/B
    # is the screen. Justification, measured: the SAME reference binary, three
    # consecutive pairs, gave 601, 601 and 721 flips with aborts at 22.8 s, 23.3 s
    # and 35.2 s. The abort time -- not the code -- sets the flips, so a flips bar
    # would fail unchanged code. The screen is the far point, and whether it is
    # reached is comparable. The flips still have to clear an absolute floor, so a
    # title that stops drawing cannot pass by presenting a black screen.
    if [ "$ro_metric" = screen ]; then
        ro_bar="floor $ro_floor (metric=screen)"
        [ "$ro_cbest" -ge "$ro_floor" ] || { ro_ok=0; ro_why="$ro_why below-floor"; }
        if [ "$ro_have_ref" = 1 ]; then
            ro_bar="$ro_bar; reference flips $ro_rbest (recorded, not a bar)"
        fi
    elif [ "$ro_have_ref" = 1 ]; then
        ro_bar="reference $ro_rbest"
        if [ "$ro_rbest" -le 0 ]; then
            ro_ok=0; ro_why="$ro_why reference-flips=0"
        else
            awk -v c="$ro_cbest" -v r="$ro_rbest" -v j="$JITTER_PCT" \
                'BEGIN { exit !(c + 0 < r * (100 - j) / 100) }' && { ro_ok=0; ro_why="$ro_why behind-reference"; }
        fi
    else
        ro_bar="floor $ro_floor"
        [ "$ro_cbest" -ge "$ro_floor" ] || { ro_ok=0; ro_why="$ro_why below-floor"; }
    fi

    ro_screen_note=""
    if [ "$ro_want_screen" = 1 ]; then
        [ "$ro_chit" -gt 0 ] || { ro_ok=0; ro_why="$ro_why screen-missing"; }
        ro_screen_note=" screen=$ro_marker $ro_chit/$ro_a candidate, $ro_rhit/$ro_a reference"
    else
        case "$ro_ccode" in
            124|3) ;;
            *) ro_ok=0; ro_why="$ro_why not-alive(exit $ro_ccode)" ;;
        esac
    fi

    if [ "$ro_ok" = 1 ]; then
        printf '  %-12s PASS  flips=%-6s vs %-14s over %s pair(s) in %ss%s\n' \
            "$ro_name" "$ro_cbest" "$ro_bar" "$ro_a" "$ro_window" "$ro_screen_note"
        return 0
    fi
    printf '  %-12s FAIL  flips=%-6s vs %-14s over %s pair(s) in %ss%s  <-%s\n' \
        "$ro_name" "$ro_cbest" "$ro_bar" "$ro_a" "$ro_window" "$ro_screen_note" "$ro_why"
    printf '  %-12s       logs /tmp/regress-%s-{ref,cand}1.log\n' "" "$ro_name"
    return 1
}

if [ "${1:-}" = "--list" ]; then list_points; exit 0; fi

games="${*:-minecraft gta-sa rdr}"
# The machine-wide lock, shared with the t-T1 worktree: no game runs and no heavy
# build while another project holds it. This is a second lock on purpose -- the
# regress lock above serialises regress.sh against itself, this one serialises
# EVERYTHING on the machine.
MLOCK="$ROOT/tools/machine_lock.sh"
if [ -x "$MLOCK" ]; then
    # PS3_MACHINE_OWNER_PID is required here: machine_lock.sh is a CHILD process,
    # so its own $$ is dead by the time the lock is checked. Without this the
    # lock read as ABANDONED one second after it was taken -- measured.
    if PS3_MACHINE_OWNER_PID=$$ PS3_MACHINE_WAIT="${PS3_MACHINE_WAIT:-7200}" \
            "$MLOCK" acquire "regress.sh: $*"; then
        trap 'kill_run_groups; PS3_MACHINE_OWNER_PID=$$ "$MLOCK" release 2>/dev/null; release_lock' EXIT INT TERM
    else
        echo "regress: the machine lock is held; not measuring" >&2
        exit 2
    fi
fi

acquire_lock
echo "regress: ${SECONDS_PER_GAME}s per run; reference and candidate paired back to back"
echo "regress: display $DISPLAY_NUM, reference root $REF_ROOT"
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
# A run where a requested game could not be started is not a green run: a typo in
# a binary path or a missing disc would otherwise read as "all passed".
if [ "$skipped" -gt 0 ]; then
    echo "regress: no game regressed, but $skipped game(s) could not be run (environment)"
    exit 2
fi
echo "regress: all present games passed"
exit 0
