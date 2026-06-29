#!/bin/sh
# Count event-loop wakeups of an idle wmaker over a fixed window and
# assert the rate stays below a threshold. The libev backend blocks in
# epoll_wait() on Linux (poll/kqueue elsewhere) and only returns when
# the X fd, the IPC socket, the inotify watch or a timer fires, so on a
# desktop with the pointer parked the only periodic source is the
# wDefaultsCheckDomains timer at DEFAULTS_CHECK_INTERVAL (2 s). A
# reintroduced root PointerMotion selection or a short-period timer
# pushes the count into the hundreds and trips the assertion.
#
# The wakeup count is the delta of voluntary_ctxt_switches in
# /proc/$pid/status, which increments every time the process blocks of
# its own accord, so it needs no ptrace, no perf_event_paranoid change
# and no extra tooling. On a kernel without that field (non-Linux) the
# script falls back to strace -c on the multiplexing syscalls and exits
# 77 (SKIP) when neither path is available.
#
# In bench mode (BENCH=1) a second sample is taken while sweeping the
# pointer over the root window, both rates are printed, and the script
# never fails so it can be diffed across changes.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
DPY=${IDLE_DRIVE_DISPLAY:-:91}
WINDOW_S=${IDLE_DRIVE_WINDOW:-3}
LIMIT=${IDLE_DRIVE_LIMIT:-15}
PIDFILE=$(mktemp /tmp/idle-wakeups.pids.XXXXXX)
WMHOME=$(mktemp -d /tmp/idle-wakeups-home.XXXXXX)
RUNTIME=$(mktemp -d /tmp/idle-wakeups-rt.XXXXXX)
LOG=/tmp/idle-wakeups-drive.log
BENCH=${BENCH:-0}
: > "$PIDFILE"
: > "$LOG"

note() { printf '%s\n' "$*" | tee -a "$LOG"; }
fail() { printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; exit 1; }

cleanup() {
    while read -r p; do
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done < "$PIDFILE"
    rm -f "$PIDFILE"
    rm -rf "$WMHOME" "$RUNTIME"
}
trap cleanup EXIT

wait_for() {
    wf_n=$1; shift
    while [ "$wf_n" -gt 0 ]; do
        "$@" && return 0
        sleep 0.1
        wf_n=$((wf_n - 1))
    done
    return 1
}

command -v Xvfb >/dev/null 2>&1 || { echo "SKIP: Xvfb not installed"; exit 77; }
[ -x "$ROOT/src/.libs/wmaker" ] || fail "src/.libs/wmaker not built"

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY already in use, set IDLE_DRIVE_DISPLAY" >&2
    exit 1
fi

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
printf '{\n}\n' > "$WMHOME/Defaults/WindowMaker"
# a minimal root menu so OpenRootMenu does not pop the centred
# could-not-load alert panel, that panel selects PointerMotion and
# would skew both samples if the pointer landed on it
printf '("Test", ("XTerm", EXEC, "xterm"))\n' > "$WMHOME/Defaults/WMRootMenu"

Xvfb "$DPY" -screen 0 1280x800x24 -ac >>"$LOG" 2>&1 &
echo $! >> "$PIDFILE"
wait_for 40 sh -c "DISPLAY=$DPY xdpyinfo >/dev/null 2>&1" || fail "Xvfb failed to start"

LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
ASAN_OPTIONS="abort_on_error=0:halt_on_error=0:detect_leaks=0" \
DISPLAY=$DPY \
XDG_RUNTIME_DIR="$RUNTIME" \
WMAKER_USER_ROOT="$WMHOME" \
"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMPID=$!
echo "$WMPID" >> "$PIDFILE"

wm_ready() {
    DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null \
        | grep 'window id' >/dev/null
}
wait_for 80 wm_ready || fail "wmaker did not come up, see $LOG"

# park the pointer on bare root (clear of the dock at the right edge,
# the clip at top-left, and the centred fallback alert panel) and let
# the startup repaints and the first wDefaultsCheckDomains tick drain
DISPLAY=$DPY xdotool mousemove 200 700 >/dev/null 2>&1
sleep 1.2

vcs_of() {
    awk '/^voluntary_ctxt_switches:/{print $2}' "/proc/$1/status" 2>/dev/null
}

count_wakeups() {
    cw_a=$(vcs_of "$WMPID")
    if [ -n "$cw_a" ]; then
        sleep "$WINDOW_S"
        cw_b=$(vcs_of "$WMPID")
        echo $((cw_b - cw_a))
        return 0
    fi
    # non-Linux fallback, strace -c writes the summary to stderr
    command -v strace >/dev/null 2>&1 || { echo SKIP; return 0; }
    strace -qc -e trace=epoll_wait,epoll_pwait,ppoll,poll,pselect6,select,kevent \
        -p "$WMPID" 2>"$RUNTIME/strace.out" >/dev/null &
    cw_sp=$!
    sleep "$WINDOW_S"
    kill -INT "$cw_sp" 2>/dev/null
    wait "$cw_sp" 2>/dev/null
    grep -qi 'ptrace.*not permitted' "$RUNTIME/strace.out" && { echo SKIP; return 0; }
    awk '$NF ~ /^(epoll_wait|epoll_pwait|ppoll|poll|pselect6|select|kevent)$/ {s+=$(NF-1)} END{print s+0}' \
        "$RUNTIME/strace.out"
}

IDLE_N=$(count_wakeups)
[ "$IDLE_N" = SKIP ] && { echo "SKIP: no /proc voluntary_ctxt_switches and no usable strace"; exit 77; }
kill -0 "$WMPID" 2>/dev/null || fail "wmaker exited during the sample window, see $LOG"

IDLE_R10=$(( IDLE_N * 10 / WINDOW_S ))
note "idle: $IDLE_N event-loop wakeups in ${WINDOW_S}s = $((IDLE_R10/10)).$((IDLE_R10%10))/s (limit $LIMIT)"

if [ "$BENCH" = 1 ] && command -v xdotool >/dev/null 2>&1; then
    # second sample with the pointer sweeping root, both numbers should
    # be within noise of each other in the default config
    ( i=0
      while kill -0 "$WMPID" 2>/dev/null; do
          DISPLAY=$DPY xdotool mousemove $((150 + i % 800)) 700
          i=$((i + 4))
          sleep 0.005
      done ) >/dev/null 2>&1 &
    MOVER=$!
    echo "$MOVER" >> "$PIDFILE"
    sleep 0.3
    MOVE_N=$(count_wakeups)
    kill "$MOVER" 2>/dev/null
    MOVE_R10=$(( MOVE_N * 10 / WINDOW_S ))
    note "root motion: $MOVE_N wakeups in ${WINDOW_S}s = $((MOVE_R10/10)).$((MOVE_R10%10))/s"
    exit 0
fi

if [ "$IDLE_N" -gt "$LIMIT" ]; then
    fail "idle wmaker woke $IDLE_N times in ${WINDOW_S}s (limit $LIMIT), a periodic source has regressed"
fi

note "ok: $IDLE_N <= $LIMIT"
exit 0
