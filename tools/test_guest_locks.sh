#!/usr/bin/env bash
# ps3base -- the guest synchronisation tests, run together.
#
# These four answer 'can two guest threads corrupt shared memory through our
# lock machinery, and if so which layer is at fault":
#
#   reservation_increment  the title's OWN lwarx/stwcx retry loop, no lock at
#                          all -- a lost update is a field overwritten by a
#                          thread that thought it had excluded the other
#   guest_spinlock         the canonical lwarx/stwcx acquire, two threads
#   lwmutex_contention     the REAL lv2::LwMutex (syscalls 95-99)
#   lwmutex_hle            the sysPrxForUser imports (create/lock/unlock/
#                          destroy on a guest structure), the layer the title
#                          actually calls
#
# Run from the tree root after cmake --build build -j 8 --target <name>.
set -uo pipefail
B="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/build"
fail=0
for t in reservation_increment guest_spinlock lwmutex_contention lwmutex_hle; do
    [ -x "$B/$t" ] || { echo "SKIP $t (not built)"; continue; }
    printf "%-24s " "$t"
    if timeout 300 "$B/$t" > /tmp/$t.out 2>&1; then
        tail -1 /tmp/$t.out
    else
        echo "FAILED (see /tmp/$t.out)"
        fail=1
    fi
done
exit $fail
