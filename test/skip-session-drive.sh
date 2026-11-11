#!/bin/sh
# Drive the skip-flag session behaviour (SaveSessionOnExit):
#   1. an EWMH-derived _NET_WM_STATE_SKIP_TASKBAR flag is NOT written to
#      the WMState entry (no key), so it re-derives from the client and
#   2. its later removal takes effect (the merged value must never be
#      pinned as a user choice),
#   3. the legacy _WINDOWMAKER_STATE skip slots written by an older
#      binary are ignored without breaking the rest of the property,
#   4. a window-menu toggle applies live to the window, leaves the
#      same-class sibling alone, and is never written to the
#      per-application WMState entry (whose (instance, class, command)
#      matching cannot tell same-class siblings apart); it rides the
#      client-owned _WINDOWMAKER_STATE property instead,
#   5. that per-window property restores the toggle across a warm
#      restart and a cold re-adopt (clients kept), while the same-class
#      sibling stays clean.
# wmaker is restarted between phases on the same display so the
# saved WMState is consumed by wSessionRestoreState.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${SKIP_SESSION_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-skipsession-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/skip-session-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
WMSTATE="$WMHOME/Defaults/WMState"
PASSED=0
FAILED=0
SOCKET=
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
record_pass() { PASSED=$((PASSED + 1)); printf 'PASS: %s\n' "$*" | tee -a "$LOG"; }
record_fail() { FAILED=$((FAILED + 1)); printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; }
fatal() { printf 'FATAL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
skip_all() { printf 'SKIP: %s\n' "$*" | tee -a "$LOG"; exit 77; }

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
	_wf=$1; shift
	while [ "$_wf" -gt 0 ]; do
		"$@" && return 0
		sleep 0.1
		_wf=$((_wf - 1))
	done
	return 1
}

pid_gone() { ! kill -0 "$1" 2>/dev/null; }

for tool in Xvfb xdpyinfo xterm wmctrl xdotool python3 wmakermsg; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

msg() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" "$@"
}
ipc_ready() { msg -q get_version >/dev/null 2>&1; }

win_entry() {
	msg get_windows 2>/dev/null | sed 's/}, *{/}\n{/g' | grep -F "id = $1;"
}
win_skip() {
	win_entry "$1" | sed -n 's/.*skip_window_list = \([^;]*\);.*/\1/p'
}
win_skip_is() {
	[ "$(win_skip "$1")" = "$2" ]
}
xterm_xids() {
	msg get_windows 2>/dev/null | sed 's/}, *{/}\n{/g' \
		| sed -n '/class = XTerm/{s/.*id = \(0x[0-9a-fA-F]*\);.*/\1/p;}'
}
# open the focused window's menu (WindowMenuKey) and walk to
# Options > Skip window list (window_menu_entries index 9 while the
# Restore geometry entry is hidden, options index 8)
menu_toggle_skip_window_list() {
	DISPLAY=$DPY xdotool key ctrl+Escape
	sleep 0.4
	DISPLAY=$DPY xdotool key --delay 150 \
		Down Down Down Down Down Down Down Down Down Down Right
	sleep 0.4
	DISPLAY=$DPY xdotool key --delay 150 Down Down Down Down Down Down Down Down
	DISPLAY=$DPY xdotool key Return
	sleep 0.3
}
wmaker_alive() {
	kill -0 "$WMAKER_PID" 2>/dev/null
}

start_wmaker() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		DISPLAY=$DPY \
		"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
	WMAKER_PID=$!
	echo "$WMAKER_PID" >> "$PIDFILE"
	SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
	wait_for 120 ipc_ready || fatal "wmaker IPC did not come up"
}

stop_wmaker() {
	msg -q exit >>"$LOG" 2>&1
	wait_for 80 pid_gone "$WMAKER_PID" || fatal "wmaker did not exit"
	sleep 0.5
}

spawn_xterm() {
	DISPLAY=$DPY xterm -geometry 80x24+100+100 -e 'sleep 900' >>"$LOG" 2>&1 &
	XTERM_PID=$!
	echo "$XTERM_PID" >> "$PIDFILE"
}

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set SKIP_SESSION_DRIVE_DISPLAY" >&2
	exit 77
fi

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  SaveSessionOnExit = YES;
}
EOF
cat >"$WMHOME/Defaults/WMState" <<'EOF'
{
  Workspaces = (
    { Name = "ws-0"; }
  );
}
EOF

export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET

note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 1024x768x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
	kill -0 "$XVFB_PID" 2>/dev/null || return 1
	DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fatal "Xvfb failed to start"

# --- 1. EWMH-derived flag is not session-saved -----------------------------
note "phase 1: EWMH-derived skip flag"
start_wmaker
spawn_xterm
xterm_managed() {
	msg get_windows 2>/dev/null | grep -F 'XTerm' >/dev/null
}
XID=
if wait_for 50 xterm_managed; then
	XID=$(msg get_windows \
		| sed 's/}, *{/}\n{/g' \
		| sed -n '/class = XTerm/{s/.*id = \(0x[0-9a-fA-F]*\);.*/\1/p;}' \
		| head -1)
fi
[ -n "$XID" ] || fatal "xterm never got managed"

DISPLAY=$DPY wmctrl -i -r "$XID" -b add,skip_taskbar >>"$LOG" 2>&1
if wait_for 40 win_skip_is "$XID" "YES"; then
	record_pass "EWMH SKIP_TASKBAR sets the merged flag"
else
	record_fail "EWMH SKIP_TASKBAR sets the merged flag (got $(win_skip "$XID"))"
fi

stop_wmaker
if grep -q 'SkipWindowList' "$WMSTATE"; then
	record_fail "WMState entry has no SkipWindowList key"
else
	record_pass "WMState entry has no SkipWindowList key"
fi
if grep -q 'SkipSwitchPanel' "$WMSTATE"; then
	record_fail "WMState entry has no SkipSwitchPanel key"
else
	record_pass "WMState entry has no SkipSwitchPanel key"
fi

# --- 2. nothing pinned: the client path stays live -------------------------
note "phase 2: flag re-derives from the client, no pinning"
# wmaker deletes _NET_WM_STATE at unmanage, so the adopted window starts
# unskipped; before the fix the saved merged value was re-applied as a
# user choice and pinned here
start_wmaker
adopted() {
	win_entry "$XID" >/dev/null
}
if wait_for 50 adopted; then
	record_pass "xterm re-adopted on cold start"
else
	record_fail "xterm re-adopted on cold start"
fi
if wait_for 40 win_skip_is "$XID" "NO"; then
	record_pass "no skip flag pinned from the saved session"
else
	record_fail "no skip flag pinned from the saved session (got $(win_skip "$XID"))"
fi

DISPLAY=$DPY wmctrl -i -r "$XID" -b add,skip_taskbar >>"$LOG" 2>&1
if wait_for 40 win_skip_is "$XID" "YES"; then
	record_pass "re-added EWMH flag honored"
else
	record_fail "re-added EWMH flag honored (got $(win_skip "$XID"))"
fi

DISPLAY=$DPY wmctrl -i -r "$XID" -b remove,skip_taskbar >>"$LOG" 2>&1
if wait_for 40 win_skip_is "$XID" "NO"; then
	record_pass "the client's later removal takes effect (not pinned)"
else
	record_fail "the client's later removal takes effect (not pinned) (got $(win_skip "$XID"))"
fi
check "wmaker alive after unpinned toggle" wmaker_alive

# --- 3. legacy _WINDOWMAKER_STATE skip slots are ignored --------------------
note "phase 3: legacy _WINDOWMAKER_STATE skip slots ignored"
# slots 14-17 carried the (defined, user) skip pairs before the menu
# toggles became live only; a property written by an older binary must
# leave the window unskipped and must not reach WMState
kill "$XTERM_PID" 2>/dev/null
wait_for 40 pid_gone "$XTERM_PID" || fatal "old xterm did not go away"

STATE_PROBE="$RUNTIME/stated.py"
cat > "$STATE_PROBE" <<'EOF'
import os, time
from Xlib import X, Xatom, display

d = display.Display()
s = d.screen()
w = s.root.create_window(60, 60, 300, 200, 0, s.root_depth,
			 X.InputOutput, X.CopyFromParent)
w.set_wm_class('stated', 'Stated')
w.set_wm_name('State Property Probe')
w.change_property(Xatom.WM_COMMAND, Xatom.STRING, 8, b'statedclient\x00')
wmstate = d.intern_atom('_WINDOWMAKER_STATE')
slots = [0] * 18
slots[14] = 1		# legacy skip_window_list defined (user choice)
slots[15] = 1		# legacy skip_window_list user value = YES
w.change_property(wmstate, wmstate, 32, slots)
w.map()
d.flush()
with open(os.environ['STATED_XIDFILE'], 'w') as f:
	f.write('0x%x\n' % w.id)
time.sleep(900)
EOF

STATED_XIDFILE="$RUNTIME/stated.xid" DISPLAY=$DPY python3 "$STATE_PROBE" >>"$LOG" 2>&1 &
STATED_PID=$!
echo "$STATED_PID" >> "$PIDFILE"
NEWXID=
stated_xid_ready() { grep -q '^0x[0-9a-fA-F]*$' "$RUNTIME/stated.xid" 2>/dev/null; }
if wait_for 50 stated_xid_ready; then
	NEWXID=$(head -1 "$RUNTIME/stated.xid")
fi
[ -n "$NEWXID" ] || fatal "stated probe never got managed"
if wait_for 50 win_entry "$NEWXID" >/dev/null; then
	record_pass "stated probe managed"
else
	record_fail "stated probe managed"
fi
if wait_for 40 win_skip_is "$NEWXID" "NO"; then
	record_pass "legacy skip slots ignored"
else
	record_fail "legacy skip slots ignored (got $(win_skip "$NEWXID"))"
fi
check "wmaker alive before the save" wmaker_alive

stop_wmaker
if grep -q 'SkipWindowList' "$WMSTATE"; then
	record_fail "WMState entry has no SkipWindowList key"
else
	record_pass "WMState entry has no SkipWindowList key"
fi
if grep -q 'SkipSwitchPanel' "$WMSTATE"; then
	record_fail "WMState entry has no SkipSwitchPanel key"
else
	record_pass "WMState entry has no SkipSwitchPanel key"
fi

# --- 4. a window-menu toggle is per-window, not in the WMState entry ---------
note "phase 4: window-menu toggle is per-window, not in the WMState Applications entry"
# two xterms with an identical WM_COMMAND, the same-class sibling pair
# that used to inherit the flag through the per-application entry
start_wmaker
spawn_xterm
spawn_xterm
two_xterms_managed() {
	[ "$(xterm_xids | wc -l)" -ge 2 ]
}
wait_for 50 two_xterms_managed || fatal "two xterms never got managed"
XID_A=$(xterm_xids | sed -n 1p)
XID_B=$(xterm_xids | sed -n 2p)
[ -n "$XID_A" ] && [ -n "$XID_B" ] || fatal "missing xterm ids"

DISPLAY=$DPY wmctrl -i -a "$XID_A" >>"$LOG" 2>&1
menu_toggle_skip_window_list
if wait_for 40 win_skip_is "$XID_A" "YES"; then
	record_pass "menu toggle applies live to the window"
else
	record_fail "menu toggle applies live to the window (got $(win_skip "$XID_A"))"
fi
if wait_for 40 win_skip_is "$XID_B" "NO"; then
	record_pass "menu toggle leaves the same-class sibling alone"
else
	record_fail "menu toggle leaves the same-class sibling alone (got $(win_skip "$XID_B"))"
fi
check "wmaker alive after the live toggle" wmaker_alive

# --- 5. the per-window toggle survives a warm restart ------------------------
note "phase 5: per-window skip toggle survives a warm restart"
msg -q restart >>"$LOG" 2>&1 || fatal "wmakermsg restart failed"
wait_for 80 ipc_ready || fatal "IPC did not re-bind after the warm restart"
WMAKER_PID=$(msg get_version 2>/dev/null \
	| sed -n 's/.*pid = \([0-9]*\);.*/\1/p')
[ -n "$WMAKER_PID" ] && echo "$WMAKER_PID" >> "$PIDFILE"
sleep 0.5
if wait_for 40 win_skip_is "$XID_A" "YES"; then
	record_pass "skip toggle restored on warm restart from _WINDOWMAKER_STATE"
else
	record_fail "skip toggle restored on warm restart (got $(win_skip "$XID_A"))"
fi
if wait_for 40 win_skip_is "$XID_B" "NO"; then
	record_pass "warm restart leaves the same-class sibling clean"
else
	record_fail "warm restart leaves the same-class sibling clean (got $(win_skip "$XID_B"))"
fi
check "wmaker alive after the warm restart" ipc_ready

stop_wmaker
if grep -q 'SkipWindowList' "$WMSTATE"; then
	record_fail "WMState entry has no SkipWindowList key"
else
	record_pass "WMState entry has no SkipWindowList key"
fi
if grep -q 'SkipSwitchPanel' "$WMSTATE"; then
	record_fail "WMState entry has no SkipSwitchPanel key"
else
	record_pass "WMState entry has no SkipSwitchPanel key"
fi

# --- 6. the per-window toggle survives a cold re-adopt -----------------------
note "phase 6: per-window skip toggle survives a cold re-adopt"
start_wmaker
if wait_for 50 win_entry "$XID_A" >/dev/null; then
	record_pass "first xterm re-adopted on cold start"
else
	record_fail "first xterm re-adopted on cold start"
fi
if wait_for 50 win_entry "$XID_B" >/dev/null; then
	record_pass "second xterm re-adopted on cold start"
else
	record_fail "second xterm re-adopted on cold start"
fi
if wait_for 40 win_skip_is "$XID_A" "YES"; then
	record_pass "per-window flag restored on re-adopt from _WINDOWMAKER_STATE"
else
	record_fail "per-window flag restored on re-adopt (got $(win_skip "$XID_A"))"
fi
if wait_for 40 win_skip_is "$XID_B" "NO"; then
	record_pass "no flag leaked onto the same-class sibling"
else
	record_fail "no flag leaked onto the same-class sibling (got $(win_skip "$XID_B"))"
fi
check "wmaker alive at the end" wmaker_alive

note "summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

kill "$STATED_PID" 2>/dev/null
msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
