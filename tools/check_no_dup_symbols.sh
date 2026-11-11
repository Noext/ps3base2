#!/bin/sh
# check_no_dup_symbols.sh -- catch a duplicate definition before the linker does.
#
# runtime/src/gowa_stubs.cpp exists to satisfy symbols a title does NOT link. Four
# times it has grown a definition of a symbol that IS linked, and each time all
# three builds died with 'multiple definition':
#
#   edge::gpu::g_active       also defined in spu/edge/edge_gpu.cpp:131
#   jsedge::spu_idle          also defined in jobsys/edge_consumer.cpp:997
#   jsedge::enabled/init/...  also defined in jobsys/edge_consumer.cpp:1002+
#
# The root cause is always the same belief -- 'this directory is not linked for
# this title' -- and it is always false: GOWA_RUNTIME_SOURCES globs
# runtime/src/jobsys/*.cpp and runtime/src/spu/edge/*.cpp unconditionally.
#
# Rather than parse C++ (which produced false positives on legitimately shared
# namespaces like spu), this checks the specific symbols that have actually
# caused the break. Add a line when a new one appears.
#
# Usage: tools/check_no_dup_symbols.sh   (exit 0 clean, 1 duplicate)
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
STUBS="$ROOT/runtime/src/gowa_stubs.cpp"
[ -f "$STUBS" ] || { echo "no $STUBS"; exit 2; }

SYMS='g_active spu_idle on_wait_previous on_set_priorities set_prev_guard ext_stats'

bad=0
for sym in $SYMS; do
    # Only real code counts, so comment lines are stripped first.
    # Strip full comment lines and trailing comments first: the file documents the
    # duplicates it must not contain, and matching its own prose would report every
    # one of them as present.
    if sed -E 's@//.*@@; s@/\*.*?\*/@@' "$STUBS" | grep -vE '^[[:space:]]*(\*|/\*)' \
        | grep -qE "(^|[^a-zA-Z0-9_:])${sym}[ (]"; then
        echo "DUPLICATE: ${sym} is defined in gowa_stubs.cpp and in the jobsys/edge module"
        bad=$((bad + 1))
    fi
done

if [ "$bad" -gt 0 ]; then
    echo
    echo "$bad symbol(s) duplicated. Remove them from gowa_stubs.cpp: the other"
    echo "file is globbed into every binary, so the stub is never needed there."
    echo "If a title really links no job system, exclude the directory in"
    echo "CMakeLists.txt -- do not define the symbols twice."
    exit 1
fi
echo "gowa_stubs: no known duplicate definitions"
exit 0