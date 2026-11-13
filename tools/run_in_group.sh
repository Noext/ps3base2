#!/bin/sh
# run_in_group.sh <pgid-file> <command...>
#
# The caller invokes this under `setsid`, so this shell is a session leader and
# its process group id equals its own pid. It writes that pid where the caller
# can read it, then `exec`s the real command so the group keeps the same leader.
#
# Why: killing a title by pid leaves its children (the SPU threads, the rsx
# thread, a spawned ffmpeg) alive, and those are what kept spawning runs after a
# regress.sh was believed dead on 2026-11-13 -- the load went to 22 and voided a
# measurement. Killing the GROUP is the only kill that is complete.
set -u
[ "$#" -ge 2 ] || { echo "run_in_group: usage: <pgid-file> <command...>" >&2; exit 2; }
pgid_file=$1
shift
echo $$ > "$pgid_file"
exec "$@"
