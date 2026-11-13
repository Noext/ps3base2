#!/bin/sh
# regress_one.sh -- run tools/regress.sh so that a forced stop is COMPLETE.
#
# Why this wrapper exists
# -----------------------
# regress.sh launches each title in its own process group and kills those groups
# in its trap. A trap does not run on SIGKILL, so the guarantee has to come from
# outside. This wrapper starts regress.sh with setsid, making it a session leader
# whose process group id equals its own pid; killing that group then reaches
# regress.sh AND every title and recorder it started.
#
# Measured failure this prevents: on 2026-11-13 a regress.sh believed dead kept
# spawning RDR runs. Killing it by pid left the children alive, the load reached
# 22, and a measurement was void.
#
# The group id is written to a file BEFORE exec, because `setsid cmd &` forks:
# the pid the caller sees with $! is not necessarily the group leader.
#
# usage: regress_one.sh <display> <game> [out-log]
# Env:   same as tools/regress.sh (RUN_ROOT, REF_ROOT, SECONDS_PER_GAME)
set -u
DISP=${1:?:display}
GAME=${2:?:game}
OUT=${3:-/tmp/regress-$GAME-out.log}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PGID_FILE=${PGID_FILE:-/tmp/regress-one.pgid}

rm -f "/tmp/.X11-unix/X${DISP#:}"
Xvfb "$DISP" -screen 0 1280x720x24 -ac +extension GLX +render -noreset > /tmp/xvfb-regress.log 2>&1 &
XPID=$!
sleep 2

# Our own pid IS the group id once we are a session leader (the caller uses
# setsid). Record it so a forced stop can kill the group even if we never run a
# trap.
echo $$ > "$PGID_FILE"

cleanup() {
    kill "$XPID" 2>/dev/null
    # NOT "kill -TERM -- -$$": dash parses the "-" after "--" as a signal number.
    kill -TERM "-$$" 2>/dev/null
}
trap cleanup EXIT INT TERM

DISPLAY_NUM="$DISP" "$ROOT/tools/regress.sh" "$GAME" > "$OUT" 2>&1
RC=$?
echo "REGRESS EXIT=$RC" >> "$OUT"
exit "$RC"
