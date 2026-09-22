#!/bin/sh
# Drive wmaker's XSMP client half against a minimal in-tree
# SmsInitialize() listener under Xvfb.
#
# Asserts: wmaker registers with $SESSION_MANAGER, publishes
# SmRestartCommand with the assigned -clientid and without --for-real,
# answers a SaveYourself by requesting Phase 2 before SaveYourselfDone,
# and hands a user-initiated exit to the session manager as
# SaveYourselfRequest(shutdown=True) rather than tearing down locally.
#
# Runs against an isolated WMAKER_USER_ROOT and tracks every PID it
# spawns, the live desktop is untouched.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${XSMP_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/xsmp-drive.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
SMLOG="$RUNTIME/sm.log"
SMBIN="$RUNTIME/sm-listener"
LOG=${XSMP_DRIVE_LOG:-/tmp/xsmp-drive.log}
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
SOCKET=

PASSED=0
FAILED=0

: > "$PIDFILE"
: > "$LOG"

cleanup() {
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

sm_has()  { grep -qF "$1" "$SMLOG" 2>/dev/null; }
sm_line() { grep -n -m1 "$1" "$SMLOG" 2>/dev/null | cut -d: -f1; }

msg() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" "$@"
}
ipc_ready() { msg -q get_version >/dev/null 2>&1; }

# ---------------------------------------------------------------------------

for tool in Xvfb xdpyinfo; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

# libSM/libICE are auto-enabled and packaged everywhere xcb is, so a
# link failure here means --disable-xsmp or a stripped-down chroot,
# either way there is nothing to drive
${CC:-cc} -std=gnu11 "$ROOT/test/sm-listener.c" -o "$SMBIN" \
	$(pkg-config --cflags --libs sm ice 2>/dev/null || echo '-lSM -lICE') \
	>>"$LOG" 2>&1 || skip_all "could not build test/sm-listener.c"

nm -D "$ROOT/src/.libs/wmaker" 2>/dev/null | grep -q ' U SmcOpenConnection' \
	|| skip_all "wmaker built without XSMP support"

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
echo '{}' > "$WMHOME/Defaults/WindowMaker"
# without a valid WMRootMenu wScreenRestoreState() -> OpenRootMenu() pops
# a modal wMessageDialog("The applications menu could not be loaded") and
# StartUp() never returns, wXSMPConnect() runs after StartUp() so the
# session manager would never see a client
echo '("Menu", ("Run", EXEC, "true"))' > "$WMHOME/Defaults/WMRootMenu"
export WMAKER_USER_ROOT="$WMHOME"
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set XSMP_DRIVE_DISPLAY" >&2
	exit 77
fi

# --- Xvfb ------------------------------------------------------------------
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 1024x768x24 -ac -noreset >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
	kill -0 "$XVFB_PID" 2>/dev/null || fatal "Xvfb exited early"
	DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fatal "Xvfb failed to start"

# --- session-manager listener ---------------------------------------------
note "start sm-listener"
: > "$SMLOG"
"$SMBIN" > "$SMLOG" 2>>"$LOG" &
SM_PID=$!
echo "$SM_PID" >> "$PIDFILE"
wait_for 40 sm_has "ready" || fatal "sm-listener did not come up"

SM_ENV=$(grep -m1 '^SESSION_MANAGER=' "$SMLOG")
[ -n "$SM_ENV" ] || fatal "sm-listener did not print SESSION_MANAGER"
eval "export $SM_ENV"

# --- wmaker ----------------------------------------------------------------
note "start wmaker with SESSION_MANAGER=$SESSION_MANAGER"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
DISPLAY=$DPY \
"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"

SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
wait_for 120 ipc_ready || fatal "wmaker did not answer IPC within 12s"

# --- 1. registration and initial SaveYourself round-trip -------------------
note "registration"
check "wmaker registered with the session manager" \
	wait_for 40 sm_has "register id="
CLIENT_ID=$(sed -n 's/^register id=//p' "$SMLOG" | head -n 1)
[ -n "$CLIENT_ID" ] || record_fail "could not read assigned client id"

# the listener sends the protocol's post-register SaveYourself and wmaker
# should complete it via Phase 2, this is also the fence for the property
# set which is published from wXSMPConnect() before the first callback
check "initial SaveYourself completes" \
	wait_for 40 sm_has "save-yourself-done success=1"

# --- 2. published property set --------------------------------------------
note "property set"
check "SmProgram published" sm_has "set-prop Program ="
check "SmCloneCommand published" sm_has "set-prop CloneCommand ="
check "SmRestartStyleHint = SmRestartIfRunning" \
	sm_has "set-prop RestartStyleHint = 0"

RESTART=$(grep -m1 '^set-prop RestartCommand =' "$SMLOG")
if [ -n "$RESTART" ]; then
	record_pass "SmRestartCommand published"
	case "$RESTART" in
	*"\"-clientid\" \"$CLIENT_ID\""*)
		record_pass "SmRestartCommand carries -clientid $CLIENT_ID" ;;
	*)
		record_fail "SmRestartCommand missing -clientid $CLIENT_ID: $RESTART" ;;
	esac
	case "$RESTART" in
	*--for-real*)
		record_fail "SmRestartCommand still contains --for-real: $RESTART" ;;
	*)
		record_pass "SmRestartCommand strips --for-real" ;;
	esac
else
	record_fail "SmRestartCommand not published"
fi

# --- 3. checkpoint: SIGUSR1 -> SaveYourself -> Phase 2 -> Done -------------
note "checkpoint via SM-initiated SaveYourself"
BASE_DONE=$(grep -c '^save-yourself-done' "$SMLOG")
kill -USR1 "$SM_PID"
done_count() { [ "$(grep -c '^save-yourself-done' "$SMLOG")" -gt "$BASE_DONE" ]; }
check "checkpoint SaveYourselfDone received" wait_for 40 done_count

L_P2=$(sm_line '^phase2-request')
L_DONE=$(sm_line '^save-yourself-done')
if [ -n "$L_P2" ] && [ -n "$L_DONE" ] && [ "$L_P2" -lt "$L_DONE" ]; then
	record_pass "Phase 2 requested before SaveYourselfDone"
else
	record_fail "Phase 2 ordering wrong (phase2@${L_P2:-none} done@${L_DONE:-none})"
fi

# WMState written from save_phase2()
check "WMState written on checkpoint" test -f "$WMHOME/Defaults/WMState"

# --- 4. user-initiated exit hands off to the session manager ---------------
note "wmakermsg exit -> SaveYourselfRequest(shutdown=True)"
msg exit >>"$LOG" 2>&1
check "wmaker sent SaveYourselfRequest(shutdown=True)" \
	wait_for 40 sm_has "save-yourself-request shutdown=1"
check "shutdown request is global" sm_has "save-yourself-request shutdown=1 global=1"

# the listener turns the request round as SaveYourself(shutdown=True) and
# sends Die on SaveYourselfDone, wmaker should exit from cb_die()
check "wmaker exits on Die from the session manager" \
	wait_for 60 pid_gone "$WMAKER_PID"
check "clean ICE close on exit" sm_has "close-connection"

# --- teardown --------------------------------------------------------------
kill "$SM_PID" 2>/dev/null
wait_for 30 pid_gone "$SM_PID"

note "XSMP drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
[ "$FAILED" -eq 0 ] && [ "$PASSED" -gt 0 ]
