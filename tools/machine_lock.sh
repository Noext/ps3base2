#!/bin/sh
# machine_lock.sh -- ONE machine-wide lock for game runs and heavy builds.
#
# Why this exists
# ---------------
# Two projects share this machine: ps3base2 and the t-T1 worktree. On 2026-11-13
# a regress.sh instance that was believed dead kept spawning RDR runs (its
# signature is `timeout 120 ... --no-launcher`) while another measurement was in
# flight. The load went to 22 and the measurement was void. Nothing in either
# tree prevented that, because each tree had its own lock and neither knew about
# the other.
#
# The rule is therefore machine-wide and single: /var/lock/ps3-machine.lock is
# taken for ANY game launch and ANY heavy compilation, by either project. It is
# held for the duration of the work and released by the process that took it.
#
# Usage
#   machine_lock.sh run <reason> -- <command...>   take, run, release
#   machine_lock.sh acquire <reason>               take (prints the token)
#   machine_lock.sh release                        release (owner only)
#   machine_lock.sh status                         who holds it, if anyone
#   machine_lock.sh wait <seconds>                 block until free or time out
#
# Environment
#   PS3_MACHINE_LOCK   lock path        (default /var/lock/ps3-machine.lock)
#   PS3_MACHINE_WAIT   seconds to wait for a free lock before giving up (default 0)
#
# Exit: 0 held/released/ran, 3 the lock is held by someone else (and we did not
# wait long enough), 2 usage.
set -u

LOCK=${PS3_MACHINE_LOCK:-/var/lock/ps3-machine.lock}
WAIT=${PS3_MACHINE_WAIT:-0}

die() { echo "machine_lock: $*" >&2; exit 2; }

# A lock whose owner is gone is stale, not held. The owner writes its pid, so
# liveness is a kill -0 and not a guess about age: a legitimate 40-minute run
# must not be reclaimed just because it is old.
#
# Two defects were measured in the first version of this function and both are
# fixed here:
#
# 1. The pid file is written AFTER the mkdir, so there is a window in which the
#    lock exists and has no owner yet. A second caller saw "no pid" and reclaimed
#    a lock that had just been taken. The fix is a grace period: a lock younger
#    than GRACE seconds with no pid is "being taken", not "abandoned".
# 2. `acquire` from a separate shell invocation left a dead pid behind as soon
#    as that shell exited, so the next caller reclaimed it instantly and two
#    holders existed. `acquire` is therefore only meaningful inside a process
#    that stays alive; `run` is the interface that holds for the duration, and
#    its trap is what releases. This is documented, not silently tolerated.
GRACE=5

lock_age_seconds() {
    # The directory's own mtime, in whole seconds. Portable enough for /bin/sh.
    now=$(date +%s)
    then=$(date -r "$LOCK" +%s 2>/dev/null) || then=$now
    echo $((now - then))
}

owner_alive() {
    [ -f "$LOCK/pid" ] || return 1
    opid=$(cat "$LOCK/pid" 2>/dev/null) || return 1
    [ -n "$opid" ] || return 1
    kill -0 "$opid" 2>/dev/null
}

# "Abandoned" is stricter than "not alive": it also requires the lock to be old
# enough that its owner would have written its pid by now.
lock_abandoned() {
    owner_alive && return 1
    [ "$(lock_age_seconds)" -ge "$GRACE" ] && return 0
    return 1
}

show_status() {
    if [ -d "$LOCK" ]; then
        opid=$(cat "$LOCK/pid" 2>/dev/null || echo '?')
        oreason=$(cat "$LOCK/reason" 2>/dev/null || echo '?')
        osince=$(cat "$LOCK/since" 2>/dev/null || echo '?')
        if owner_alive; then
            echo "held by pid $opid since $osince: $oreason"
            return 0
        fi
        if [ "$(lock_age_seconds)" -lt "$GRACE" ]; then
            echo "held (being taken, no pid yet) since $osince: $oreason"
            return 0
        fi
        echo "ABANDONED (owner $opid is gone): $oreason since $osince"
        return 1
    fi
    echo "free"
    return 0
}

acquire() {
    reason=${1:-unspecified}
    # The owner is the process that must stay alive for the lock to be valid.
    # It is NOT always $$: when a shell script calls `machine_lock.sh acquire`,
    # this script is a child that exits immediately, so recording $$ produced a
    # lock that was abandoned the moment it was taken -- measured, and it made
    # the lock useless. A caller that holds the lock itself passes its own pid.
    owner=${PS3_MACHINE_OWNER_PID:-$$}
    waited=0
    while :; do
        if mkdir "$LOCK" 2>/dev/null; then
            echo "$owner" > "$LOCK/pid"
            echo "$reason" > "$LOCK/reason"
            date -u '+%Y-%m-%dT%H:%M:%SZ' > "$LOCK/since"
            echo "machine_lock: took $LOCK (owner pid $owner): $reason" >&2
            return 0
        fi
        if lock_abandoned; then
            # Reclaim only a lock whose owner is provably gone AND which is old
            # enough that the owner would have written its pid. Two waiters can
            # still race here; the mkdir below is what decides, not this removal.
            echo "machine_lock: reclaiming abandoned lock ($(show_status))" >&2
            rm -rf "$LOCK"
            continue
        fi
        if [ "$waited" -ge "$WAIT" ]; then
            echo "machine_lock: $LOCK is held -- $(show_status)" >&2
            return 3
        fi
        [ "$waited" = 0 ] && echo "machine_lock: waiting for $LOCK -- $(show_status)" >&2
        sleep 2
        waited=$((waited + 2))
    done
}

release() {
    if [ ! -d "$LOCK" ]; then return 0; fi
    opid=$(cat "$LOCK/pid" 2>/dev/null || echo '')
    owner=${PS3_MACHINE_OWNER_PID:-$$}
    if [ "${1:-}" != "--force" ] && [ "$opid" != "$owner" ]; then
        echo "machine_lock: not the owner ($opid), refusing to release; use --force" >&2
        return 3
    fi
    rm -rf "$LOCK"
    echo "machine_lock: released $LOCK" >&2
}

cmd=${1:-}; shift 2>/dev/null || true
case "$cmd" in
    run)
        reason=${1:-}; shift 2>/dev/null || true
        [ "${1:-}" = "--" ] && shift
        [ "$#" -gt 0 ] || die "run needs a command after --"
        acquire "$reason" || exit $?
        trap 'release' EXIT INT TERM
        "$@"
        rc=$?
        release
        trap - EXIT INT TERM
        exit $rc
        ;;
    # acquire/release as separate invocations are for a shell that will hold the
    # lock itself; the pid recorded is that shell's, so it must stay alive. For
    # anything else use `run`, which holds and releases around a command.
    acquire) acquire "${1:-unspecified}" ;;
    release) release "${1:-}" ;;
    status)  show_status ;;
    wait)
        WAIT=${1:-60}
        acquire "wait-only" >/dev/null 2>&1 && { release; exit 0; }
        exit 3
        ;;
    *) die "usage: machine_lock.sh run <reason> -- <cmd...> | acquire <reason> | release [--force] | status | wait <seconds>" ;;
esac
