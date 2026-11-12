#!/bin/sh
# Build slot: at most two compilations at a time, independent of run slots.
#
# Three agents rebuilding RDR concurrently put the machine at load 29 and none of
# them produced a binary in 45 minutes. The cap is a semaphore, not a schedule:
# whoever arrives takes a slot if one is free, otherwise waits.
#
#   tools/build_slot.sh <command...>
set -u
SLOTS=${BUILD_SLOTS:-2}
LOCKDIR=${BUILD_LOCKDIR:-/tmp/ps3base-build-slots}
mkdir -p "$LOCKDIR" 2>/dev/null

acquire() {
    i=1
    while [ "$i" -le "$SLOTS" ]; do
        if mkdir "$LOCKDIR/slot$i" 2>/dev/null; then
            echo "build_slot: took slot $i of $SLOTS" >&2
            echo "$LOCKDIR/slot$i"
            return 0
        fi
        i=$((i + 1))
    done
    return 1
}

while :; do
    slot=$(acquire) && break
    sleep 2
done

trap 'rmdir "$slot" 2>/dev/null' EXIT INT TERM
"$@"
rc=$?
rmdir "$slot" 2>/dev/null
trap - EXIT
exit $rc