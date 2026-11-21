#!/bin/sh
# ICCCM 2.0 window-manager replacement regression.
#
# Boots wmaker (A) on an isolated Xvfb, then drives two handoffs:
#   - a second instance without --replace must refuse to start
#     ("already a window manager running", exit status 1)
#   - a third instance with --replace steals WM_Sn, A sees
#     SelectionClear and exits, the replacer waits for the outgoing
#     info_window DestroyNotify and finishes startup
#   - a fourth instance replaces the replacer again, so the watch on a
#     live selection-owner window is exercised too
#
# Asserts the handoff completes well under REPLACE_WM_TIMEOUT (15 s),
# that the outgoing process actually dies, and that the incoming
# instance answers IPC with its own pid. Runs against an isolated
# WMAKER_USER_ROOT and XDG_RUNTIME_DIR, tracks every PID it spawns,
# the live desktop is untouched.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${REPLACE_DRIVE_DISPLAY:-:$(drive_default_display)}
W=1280
H=800
RUNTIME=$(mktemp -d /tmp/replace-drive.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=${REPLACE_DRIVE_LOG:-/tmp/replace-drive.log}
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
: > "$PIDFILE"
: > "$LOG"

# generous ceiling, the real budget in replace_existing_wm() is 15 s
HANDOFF_MS_LIMIT=5000

PASSED=0
FAILED=0

record_pass() { PASSED=$((PASSED + 1)); printf 'PASS: %s\n' "$*" | tee -a "$LOG"; }
record_fail() { FAILED=$((FAILED + 1)); printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; }
note()        { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
fatal()       { printf 'FATAL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
skip_all()    { printf 'SKIP: %s\n' "$*" | tee -a "$LOG"; exit 77; }

cleanup() {
	while read -r p; do
		[ -n "$p" ] && kill "$p" 2>/dev/null
	done < "$PIDFILE"
	rm -rf "$RUNTIME"
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
pid_gone() { ! kill -0 "$1" 2>/dev/null; }
# date +%N is GNU-only, the BSDs print a literal N. Fall back to
# whole-second precision where millisecond stamps are unavailable.
case "$(date +%s%3N 2>/dev/null)" in
	'' | *[!0-9]*) now_ms() { date +%s000; } ;;
	*)             now_ms() { date +%s%3N; } ;;
esac

command -v Xvfb    >/dev/null 2>&1 || skip_all "Xvfb not found"
command -v xdpyinfo >/dev/null 2>&1 || skip_all "xdpyinfo not found"
command -v xprop   >/dev/null 2>&1 || skip_all "xprop not found"
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
[ -f "$ROOT/config.h" ] && grep -q '#define USE_ICCCM_WMREPLACE' "$ROOT/config.h" \
	|| skip_all "built without ICCCM window manager replacement"

# --- isolated defaults ------------------------------------------------------
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = Yes;
  DisableClip = Yes;
  DisableDrawers = Yes;
}
EOF

export WMAKER_USER_ROOT="$WMHOME"
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET WMAKER_USER_XDG

# --- Xvfb ------------------------------------------------------------------
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set REPLACE_DRIVE_DISPLAY" >&2
	exit 77
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 ${W}x${H}x24 -bs -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
	kill -0 "$XVFB_PID" 2>/dev/null || fatal "Xvfb exited early"
	DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fatal "Xvfb failed to start"

# --- helpers -----------------------------------------------------------------
ipc_pid() {
	SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		DISPLAY=$DPY "$WMAKERMSG" --get-socketpath 2>/dev/null)
	[ -n "$SOCKET" ] || return 1
	case "$SOCKET" in
		"$RUNTIME"/*) : ;;
		*) return 1 ;;
	esac
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		DISPLAY=$DPY "$WMAKERMSG" -s "$SOCKET" get_version 2>/dev/null \
		| sed -n 's/.*pid = \([0-9]*\);.*/\1/p'
}
ipc_answers_pid() {
	[ "$(ipc_pid)" = "$1" ]
}
wm_running() {
	DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id'
}

start_replaced() {
	# $1 = extra args, e.g. "--replace", echoes nothing, records PID_*
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY \
	"$ROOT/src/.libs/wmaker" --for-real "$@" >>"$LOG" 2>&1 &
	echo $! >> "$PIDFILE"
}

# --- first instance ----------------------------------------------------------
note "boot wmaker A (no competition)"
start_replaced
A_PID=$(tail -1 "$PIDFILE")
wait_for 120 wm_running            || fatal "wmaker A did not come up"
wait_for 80 ipc_answers_pid "$A_PID" || fatal "wmaker A IPC did not come up"
sleep 0.5
record_pass "wmaker A is up and owns WM_S0"

# --- refusal without --replace ----------------------------------------------
note "second instance refuses without --replace"
if LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1; then
	record_fail "duplicate wmaker exited 0 instead of refusing"
else
	record_pass "duplicate wmaker refused (non-zero exit)"
fi
grep -q "already a window manager running" "$LOG" \
	&& record_pass "refusal message logged" \
	|| record_fail "refusal message missing from log"

# --- replace A ----------------------------------------------------------------
note "replace A with --replace"
T0=$(now_ms)
start_replaced --replace
B_PID=$(tail -1 "$PIDFILE")
wait_for 50 pid_gone "$A_PID" || fatal "wmaker A did not exit after the takeover"
T1=$(now_ms)
ELAPSED=$((T1 - T0))
[ "$ELAPSED" -lt "$HANDOFF_MS_LIMIT" ] \
	&& record_pass "A exited ${ELAPSED}ms after the steal (limit ${HANDOFF_MS_LIMIT}ms)" \
	|| record_fail "handoff took ${ELAPSED}ms (limit ${HANDOFF_MS_LIMIT}ms)"
wait_for 120 wm_running              || fatal "wmaker B did not come up"
wait_for 120 ipc_answers_pid "$B_PID" || fatal "wmaker B IPC did not come up"
record_pass "replacer B is up and answers IPC with its own pid"
grep -q "another window manager is replacing us!" "$LOG" \
	&& record_pass "A logged the SelectionClear shutdown" \
	|| record_fail "A did not log the SelectionClear shutdown"
grep -q "replacing the other window manager" "$LOG" \
	&& record_pass "B logged the completed handoff" \
	|| record_fail "B did not log the completed handoff"
if grep -q "hasn't exited" "$LOG"; then
	record_fail "B hit the REPLACE_WM_TIMEOUT deadline"
else
	record_pass "no REPLACE_WM_TIMEOUT expiry logged"
fi

# --- replace B again ----------------------------------------------------------
note "replace B with a third instance"
start_replaced --replace
C_PID=$(tail -1 "$PIDFILE")
wait_for 50 pid_gone "$B_PID"          || fatal "wmaker B did not exit after the takeover"
wait_for 120 ipc_answers_pid "$C_PID"  || fatal "wmaker C IPC did not come up"
record_pass "second handoff replaced a live owner and converged on C"

grep -q "ERROR: LeakSanitizer" "$LOG" \
	&& record_fail "replacement reported a memory leak" \
	|| record_pass "replacement reported no memory leaks"

exit $([ "$FAILED" -eq 0 ] && echo 0 || echo 1)
