#!/bin/sh
# Drive the crash-recovery monitor path under Xvfb: SIGSEGV the wmaker
# child twice in quick succession so the crash dialog opens (the monitor
# only raises it when the child died within 3s of its start) and assert
# the monitor blocks on it, then prove a crash outside the 3s window
# restarts directly, without the dialog.
#
# The dialog cannot be observed or driven from a second client while it
# is up: the monitor holds a server grab for the dialog's lifetime by
# design (the screen must stay frozen while it asks what to do), so
# every other client's requests are held, xwininfo/xdotool included.
# The blocking is therefore asserted from the process side: the monitor
# stays alive with no respawned child for far longer than the direct
# restart path ever takes. The dialog's keycode resolution (Return /
# Up / Down through W_KeysymToKeycode) is covered headlessly by
# WINGs/Tests/check_im.c.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${CRASH_DIALOG_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/crash-dialog-drive.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=${CRASH_DIALOG_DRIVE_LOG:-/tmp/crash-dialog-drive.log}
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
: > "$PIDFILE"
: > "$PIDFILE.groups"
: > "$LOG"

cleanup() {
	# the monitor runs in its own session (setsid) so the process
	# group kill takes any wmaker child or dialog it still has, the
	# groups file holds the monitor's pgid recorded at start
	while read -r g; do
		[ -n "$g" ] && kill -- "-$g" 2>/dev/null
	done < "$PIDFILE.groups"
	while read -r p; do
		[ -n "$p" ] && kill "$p" 2>/dev/null
	done < "$PIDFILE"
	rm -rf "$RUNTIME"
}
trap cleanup EXIT

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
fail() { printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
ok()   { note "ok: $*"; }

wait_for() {
	_wf=$1; shift
	while [ "$_wf" -gt 0 ]; do
		"$@" && return 0
		sleep 0.1
		_wf=$((_wf - 1))
	done
	return 1
}

command -v Xvfb    >/dev/null || { echo "SKIP: Xvfb not found"; exit 77; }
[ -x "$ROOT/src/.libs/wmaker" ] || { echo "SKIP: wmaker not built"; exit 77; }

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY already in use, set CRASH_DIALOG_DRIVE_DISPLAY" >&2
	exit 77
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 1024x768x24 -ac -noreset >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
	kill -0 "$XVFB_PID" 2>/dev/null || fail "Xvfb exited early"
	DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fail "Xvfb failed to start"

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
printf '{\n}\n' > "$WMHOME/Defaults/WindowMaker"
export WMAKER_USER_ROOT="$WMHOME"
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET

start_monitor() {
	# no --for-real: wmaker stays in MonitorLoop and respawns the
	# real window manager itself, which is the process under test.
	# setsid gives the monitor its own process group so cleanup can
	# take a live dialog or a respawn race with one kill.
	# handle_segv=0 keeps the sanitizer runtime from installing its
	# own SEGV handler: that handler would turn the injected crash
	# below into a clean exit and the monitor would never see
	# WIFSIGNALED. Non-sanitizer builds ignore the variable.
	setsid env LD_LIBRARY_PATH="$WMLIBS" DISPLAY="$DPY" \
		ASAN_OPTIONS="$(drive_asan_options):handle_segv=0" \
		XDG_RUNTIME_DIR="$RUNTIME" WMAKER_USER_ROOT="$WMHOME" \
		"$ROOT/src/.libs/wmaker" >>"$LOG" 2>&1 &
	echo $!
}

track_monitor() {
	echo "$1" >> "$PIDFILE"
	# setsid keeps the pid when the shell background job is not a
	# group leader, but do not trust it: record the observed pgid
	_pg=$(ps -o pgid= -p "$1" 2>/dev/null | tr -d ' ')
	[ -n "$_pg" ] && echo "$_pg" >> "$PIDFILE.groups"
}

wmaker_children() {
	pgrep -P "$1" 2>/dev/null
}

first_child() {
	# wait for the monitor to fork its wmaker and print the pid, the
	# poll must be tight so the caller's SIGSEGV lands inside the
	# 3s dialog window
	_fc=$1
	_i=0
	while [ "$_i" -lt 60 ]; do
		_c=$(wmaker_children "$_fc" | head -1)
		[ -n "$_c" ] && { echo "$_c"; return 0; }
		sleep 0.1
		_i=$((_i + 1))
	done
	return 1
}

# --- scenario 1: a fast crash opens the dialog and blocks the monitor ------

note "start monitor and crash the first child immediately"
MON=$(start_monitor)
track_monitor "$MON"

CHILD=$(first_child "$MON") || fail "wmaker child never started"
note "first child $CHILD, SIGSEGV inside the 3s window"
kill -SEGV "$CHILD" || fail "could not signal the child"

# the direct restart path respawns within a second, the dialog holds
# the monitor indefinitely, so a sustained childless stretch with the
# monitor alive is the dialog
sleep 4
kill -0 "$MON" 2>/dev/null || fail "monitor died on a fast crash"
[ -z "$(wmaker_children "$MON")" ] || fail "monitor respawned instead of raising the dialog"
ok "monitor blocked on the crash dialog, no respawn for 4s"

# a second client's requests are held by the server grab while the
# dialog is up, which is the freeze the dialog is supposed to provide
grab_held() {
	! timeout 2 env DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
grab_held || fail "server grab not held while the dialog is up"
ok "server grab held while the dialog is up"

note "kill the monitor to release the dialog and the grab"
kill -9 "$MON" 2>/dev/null
sleep 1
wait_for 20 xvfb_ready || fail "server did not answer after the monitor died"
ok "grab released with the monitor gone"

# --- scenario 2: a crash outside the 3s window restarts without a dialog ---

note "start a fresh monitor and crash late"
MON2=$(start_monitor)
track_monitor "$MON2"

CHILD2=$(first_child "$MON2") || fail "second wmaker child never started"

# stream only lines written from here on, so the restart warning that
# scenario 1's monitor logged cannot satisfy the assertion below
LATE="$RUNTIME/late.log"
: > "$LATE"
tail -n 0 -f "$LOG" >>"$LATE" &
TAILPID=$!
echo "$TAILPID" >> "$PIDFILE"

# let the 3s dialog window lapse before the crash
sleep 4
kill -SEGV "$CHILD2" || fail "could not signal the second child"

# the restart warning prints right after waitpid in the no-dialog
# branch, a dialog would hold the monitor until the panel is driven,
# so a fast arrival proves the late crash skipped the dialog
restart_logged() {
	grep -qF 'and will be restarted' "$LATE"
}
wait_for 15 restart_logged || {
	kill "$TAILPID" 2>/dev/null
	fail "late crash did not produce an immediate restart log"
}
kill "$TAILPID" 2>/dev/null
ok "late crash restarted without the dialog"

child_back() {
	[ -n "$(wmaker_children "$MON2")" ]
}
wait_for 40 child_back || fail "late crash did not respawn wmaker"
ok "wmaker respawned after the late crash"

note "PASS"
exit 0
