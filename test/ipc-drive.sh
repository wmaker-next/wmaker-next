#!/bin/sh
# Exercise the Window Maker IPC socket end to end under Xvfb.
#
# Harness conventions:
#   - private XDG_RUNTIME_DIR per run so the test wmaker's socket cannot
#     collide with the live :0 desktop
#   - readiness gate is a bounded `wmakermsg -q get_version` poll, not sleep
#   - event capture goes to a logfile, fenced by `send_tick`, ordering
#     asserted by line number
#   - background processes killed by recorded PID only
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${IPC_DRIVE_DISPLAY:-:96}
RUNTIME=$(mktemp -d /tmp/wmaker-ipc-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=${IPC_DRIVE_LOG:-/tmp/ipc-drive.log}
EVLOG="$RUNTIME/events"
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
SOCKET=

PASSED=0
FAILED=0
SKIPPED=0
MON_PID=

: > "$PIDFILE"
: > "$LOG"

cleanup() {
	[ -n "$MON_PID" ] && kill "$MON_PID" 2>/dev/null
	while read -r p; do
		[ -n "$p" ] && kill "$p" 2>/dev/null
	done < "$PIDFILE"
	rm -rf "$RUNTIME"
}
trap cleanup EXIT

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
fatal() { printf 'FATAL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
skip_all() { printf 'SKIP: %s\n' "$*" | tee -a "$LOG"; exit 77; }

record_pass() { PASSED=$((PASSED + 1)); printf 'PASS: %s\n' "$*" | tee -a "$LOG"; }
record_fail() { FAILED=$((FAILED + 1)); printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; }
record_skip() { SKIPPED=$((SKIPPED + 1)); printf 'SKIP: %s\n' "$*" | tee -a "$LOG"; }

check() {
	check_desc=$1
	shift
	if "$@"; then
		record_pass "$check_desc"
	else
		record_fail "$check_desc"
	fi
}

wait_for() {
	wf_n=$1
	shift
	while [ "$wf_n" -gt 0 ]; do
		"$@" && return 0
		sleep 0.1
		wf_n=$((wf_n - 1))
	done
	return 1
}

pid_gone() { ! kill -0 "$1" 2>/dev/null; }

msg() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" "$@"
}

ipc_ready() {
	msg -q get_version >/dev/null 2>&1
}

# evlog_line PATTERN : print 1-based line number of first match in $EVLOG
evlog_line() {
	grep -n -m1 -F "$1" "$EVLOG" 2>/dev/null | cut -d: -f1
}

evlog_has() {
	grep -qF "$1" "$EVLOG" 2>/dev/null
}

# wait_tick MARK : send_tick and poll until the tick event with that
# payload lands in $EVLOG, this is the race-free fence
wait_tick() {
	wt_mark=$1
	msg send_tick "$wt_mark" >/dev/null 2>&1 || return 1
	wait_for 50 evlog_has "payload = \"$wt_mark\""
}

start_monitor() {
	: > "$EVLOG"
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" -m "$@" > "$EVLOG" 2>>"$LOG" &
	MON_PID=$!
	echo "$MON_PID" >> "$PIDFILE"
	# monitor mode prints events only, the SUBSCRIBE reply is
	# swallowed, gate on the first=YES tick that follows it
	wait_for 50 evlog_has "first = YES"
}

stop_monitor() {
	[ -n "$MON_PID" ] && kill "$MON_PID" 2>/dev/null
	wait_for 30 pid_gone "$MON_PID"
	MON_PID=
}

start_wmaker() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY \
	XDG_RUNTIME_DIR="$RUNTIME" \
	WMAKER_USER_ROOT="$WMHOME" \
	"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
	WMAKER_PID=$!
	echo "$WMAKER_PID" >> "$PIDFILE"
	wait_for 80 ipc_ready
}

finish() {
	finish_total=$((PASSED + FAILED))
	note "IPC drive summary"
	printf 'PASS: %d, FAIL: %d, SKIP: %d\n' "$PASSED" "$FAILED" "$SKIPPED" \
		| tee -a "$LOG"
	[ "$FAILED" -eq 0 ] && [ "$finish_total" -gt 0 ]
}

# ----------------------------------------------------------------------

for tool in Xvfb xdpyinfo; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
# a Defaults/WindowMaker file must exist or check_defaults() runs the
# first-run installer, which overwrites WMState with the stock template
# and discards the Workspaces and Dock entries seeded below
cat > "$WMHOME/Defaults/WindowMaker" <<'WMEOD'
{
}
WMEOD
cat > "$WMHOME/Defaults/WMState" <<'WMEOD'
{
  Workspaces = ( { Name = "ipc-one"; }, { Name = "ipc-two"; }, { Name = "ipc-three"; } );
  Dock = {
    Applications = (
      { Name = "ipc-drive-fail.ipc-drive-fail";
        Command = "/nonexistent-ipc-drive";
        AutoLaunch = NO; Lock = NO; Forced = NO; BuggyApplication = NO;
        Position = "0,1"; }
    );
  };
}
WMEOD
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set IPC_DRIVE_DISPLAY to a free one" >&2
	exit 1
fi

note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 1024x768x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
	kill -0 "$XVFB_PID" 2>/dev/null || fatal "Xvfb exited early"
	DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fatal "Xvfb failed to start"

note "resolve socket path"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY XDG_RUNTIME_DIR="$RUNTIME" \
	"$WMAKERMSG" --get-socketpath)
case "$SOCKET" in
	"$RUNTIME"/wmaker/ipc.*) record_pass "--get-socketpath under private XDG_RUNTIME_DIR" ;;
	*) record_fail "--get-socketpath returned $SOCKET" ;;
esac

note "start wmaker (readiness via get_version poll)"
start_wmaker || fatal "wmaker did not answer get_version within 8s"
record_pass "IPC readiness poll succeeds"

# ---- exit-status convention ----------------------------------------------

note "exit-status convention"
msg -q get_version >/dev/null 2>&1
check "get_version exits 0" [ $? -eq 0 ]

msg -q frob >/dev/null 2>&1
check "unknown verb exits 2" [ $? -eq 2 ]

LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	"$WMAKERMSG" -s "$RUNTIME/nonexistent" -q get_version >/dev/null 2>&1
check "transport failure exits 1" [ $? -eq 1 ]

msg get_version 2>/dev/null | grep -q "pid = $WMAKER_PID;"
check "GET_VERSION reports wmaker pid" [ $? -eq 0 ]

# ---- row 3: workspace event scope ----------------------------------------

note "workspace event"
start_monitor workspace tick || fatal "monitor did not see first tick"

msg -q workspace next >>"$LOG" 2>&1
wait_tick "ws-done" || record_fail "workspace fence tick never arrived"

WS_LINE=$(evlog_line 'change = focus')
TICK_LINE=$(evlog_line 'payload = "ws-done"')
check "workspace next emits change=focus" [ -n "$WS_LINE" ]
check "exactly one workspace event" \
	[ "$(grep -cF 'change = focus' "$EVLOG")" -eq 1 ]
check "workspace event arrives before tick fence" \
	sh -c "[ -n '$WS_LINE' ] && [ '$WS_LINE' -lt '$TICK_LINE' ]"

msg get_workspaces 2>/dev/null | grep -q 'current = YES'
check "GET_WORKSPACES marks one current" [ $? -eq 0 ]

stop_monitor
msg -q workspace 0 >>"$LOG" 2>&1

# ---- row 1: child_exit ordering ------------------------------------------

note "child_exit before dialog"
start_monitor child_exit dialog tick || fatal "monitor did not see first tick"

# the seeded WMState docks /nonexistent at yindex 1, dock-launch takes
# the same execCommand path a double-click does so the death handler
# is registered with source=dock/slot=1 and the failure dialog is
# raised by that handler after the child_exit event has been emitted
msg -q dock-launch 1 >>"$LOG" 2>&1
check "dock-launch 1 accepted" [ $? -eq 0 ]

# the verb reply arrives as soon as the child is forked, the SIGCHLD
# and the dialog the death handler raises are asynchronous to that, so
# gate on the dialog event before fencing, the failure dialog is modal
# but its event loop still drives the IPC input handler so send_tick
# is answered while the dialog is up
wait_for 50 evlog_has 'kind = message' \
	|| record_fail "no dialog event within 5s of dock-launch"
wait_tick "child-done" || record_fail "child_exit fence tick never arrived"

CE_LINE=$(evlog_line 'status = 127')
DLG_LINE=$(evlog_line 'kind = message')
T_LINE=$(evlog_line 'payload = "child-done"')

check "child_exit {status=127} delivered" [ -n "$CE_LINE" ]
check "child_exit reports source=dock" evlog_has 'source = dock'
check "child_exit reports slot=1" evlog_has 'slot = 1'
if [ -n "$DLG_LINE" ]; then
	check "child_exit precedes dialog" \
		sh -c "[ -n '$CE_LINE' ] && [ '$CE_LINE' -lt '$DLG_LINE' ]"
	check "dialog precedes tick fence" \
		sh -c "[ -n '$T_LINE' ] && [ '$DLG_LINE' -lt '$T_LINE' ]"
else
	record_skip "no failure dialog raised, ordering check skipped"
fi
stop_monitor
# the failure dialog is still mapped, the next row's restart tears it
# down with everything else so no explicit dismiss is needed

# ---- row 2: shutdown observability and restart re-bind -------------------

note "shutdown event and restart re-bind"
start_monitor shutdown tick || fatal "monitor did not see first tick"

msg -q restart >>"$LOG" 2>&1
# the monitor connection sees shutdown {change=restart} then EOF, the
# monitor process exits 0 on orderly EOF (ECONNRESET path)
wait_for 50 evlog_has 'change = restart'
check "shutdown {change=restart} delivered before EOF" \
	evlog_has 'change = restart'
wait_for 50 pid_gone "$MON_PID"
check "monitor sees EOF after restart" pid_gone "$MON_PID"
MON_PID=

# old wmaker pid is replaced (--for-real re-execs in place), the same
# socket path must answer again
wait_for 80 ipc_ready
check "same socket path re-binds after restart" ipc_ready

# refresh the recorded pid, restart re-exec'd in place so the original
# entry in $PIDFILE is stale
WMAKER_PID=$(msg get_version 2>/dev/null | sed -n 's/.*pid = \([0-9]*\);.*/\1/p')
[ -n "$WMAKER_PID" ] && echo "$WMAKER_PID" >> "$PIDFILE"

start_monitor shutdown tick || record_fail "could not re-subscribe after restart"
msg -q exit >>"$LOG" 2>&1
wait_for 50 evlog_has 'change = exit'
check "shutdown {change=exit} delivered" evlog_has 'change = exit'
wait_for 50 pid_gone "$MON_PID"
check "monitor sees EOF after exit" pid_gone "$MON_PID"
MON_PID=

wait_for 50 pid_gone "$WMAKER_PID"
check "wmaker process exits on IPC exit" pid_gone "$WMAKER_PID"

# ----------------------------------------------------------------------

finish
exit $?
