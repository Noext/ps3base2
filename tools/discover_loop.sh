#!/bin/sh
# discover_loop.sh -- run, seed, relift, rebuild, repeat until nothing new.
#
# PS3BASE_DISCOVERY makes the runtime record every indirect call target it could
# not resolve and stop with exit 3. This script feeds those seeds back into the
# lift, rebuilds, and runs again. It finishes when a run records no new address.
#
# Why a loop and not a one-shot: 0x2b0187a8 is a {code, toc} read from a struct
# field the module fills at run time (libsysutil 0x2b01c750: lwz r0,0(r9) ->
# mtctr, lwz r2,4(r9)). That value exists nowhere in the file, so no static
# scanner can see it. Measured: the loop converged in 2 rounds.
#
# Usage: tools/discover_loop.sh <image-set> <build-dir> <disc-dir> [max-rounds]
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
IMAGES=${1:?image manifest, e.g. recomp/images-rdr.json}
BUILD=${2:?build dir, e.g. build-rdr}
DISC=${3:?disc dir}
MAX=${4:-10}
OUTROOT=${OUTROOT:-out-rdr}
SECS=${SECS:-120}

SEEDS="$ROOT/$OUTROOT/gen/indirect_seeds.txt"
mkdir -p "$(dirname "$SEEDS")"

round=1
while [ "$round" -le "$MAX" ]; do
    before=$(wc -l <"$SEEDS" 2>/dev/null || echo 0)
    echo "== round $round: $before seed(s) so far"

    # Lift: codegen reads $SEEDS next to the output tree and seeds the image that
    # owns each address.
    ( cd "$ROOT" && PS3BASE_PROFILE= python3 tools/setup.py --steps ppu \
        --images "$IMAGES" --elf "$ROOT/bin/rdr/EBOOT.elf" \
        --out-root "$OUTROOT" --gen-root "$OUTROOT/gen" --allow-unknown \
        >"$OUTROOT/discover_lift_$round.log" 2>&1 ) || { echo "lift failed, see log"; exit 2; }

    ( cd "$ROOT" && cmake --build "$BUILD" -j 8 --target ps3base \
        >"$OUTROOT/discover_build_$round.log" 2>&1 ) || { echo "build failed, see log"; exit 2; }

    echo "== round $round: running"
    ( cd "$ROOT" && PS3BASE_DISC_DIR="$DISC" PS3BASE_LOG=info \
        PS3BASE_DISCOVERY="$SEEDS" DISPLAY=${DISPLAY:-:0} \
        XDG_RUNTIME_DIR="/run/user/$(id -u)" \
        timeout "$SECS" "./$BUILD/ps3base" --no-launcher \
        >"$OUTROOT/discover_run_$round.log" 2>&1 )
    code=$?
    after=$(wc -l <"$SEEDS" 2>/dev/null || echo 0)
    echo "== round $round: exit=$code seeds $before -> $after"

    case "$code" in
        0|124)
            # Reached the budget without discovering anything new: converged.
            if [ "$after" -eq "$before" ]; then
                echo "== converged after $round round(s), $after seed(s)"
                exit 0
            fi
            ;;
        3) : ;;   # a discovery stop, exactly what this loop is for
        *) echo "== run ended with $code, not a discovery stop"; exit 1 ;;
    esac
    round=$((round + 1))
done
echo "== did not converge in $MAX rounds" >&2
exit 1
