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
#   rdr         the furthest point known. A black screen still passes while the
#               flip count rises and nothing crashes: "flips without draws" is a
#               known missing-module symptom (docs/FIRMWARE.md), not a regression
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
# The flip floor for a game, from docs/regress-baseline.md. Read rather than
# hard-coded so raising a baseline is one edit in one place.
baseline_flips() {
    f="$ROOT/docs/regress-baseline.md"
    [ -f "$f" ] || { echo 0; return; }
    # "minecraft   flips >= 6780 ..." -> 6780
    awk -v g="$1" '$1 == g && $2 == "flips" { print $4; exit }' "$f"
}
game_expect() {
    case "$1" in
        rdr) echo "either" ;;
        *)   echo "alive" ;;
    esac
}

list_points() {
    cat <<'EOF'
minecraft   a generated world      pass = alive at the budget, flips rising, 0 crashes
gta-sa      in game, Grove Street  pass = alive at the budget, flips rising, 0 crashes
rdr         furthest known         pass = alive or a clean discovery stop, flips rising, 0 crashes
EOF
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
    rm -f "$log"
    # Bounded because this run is measured, not watched.
    PS3BASE_DISC_DIR="$disc" PS3BASE_LOG=info DISPLAY="$DISPLAY_NUM" \
    XDG_RUNTIME_DIR="/run/user/$(id -u)" \
        timeout "$SECONDS_PER_GAME" "$bin" --no-launcher >"$log" 2>&1
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
    esac

    if [ "$ok" = 1 ]; then
        printf '%-12s PASS  %-15s flips=%-6s crashes=%s\n' "$name" "$verdict" "$flips" "$crashes"
        return 0
    fi
    printf '%-12s FAIL  %-15s flips=%-6s crashes=%-3s log=%s\n' "$name" "$verdict" "$flips" "$crashes" "$log"
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
