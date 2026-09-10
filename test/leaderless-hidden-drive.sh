#!/bin/sh
# Drive hidden-window recovery through wMakeWindowVisible for a window
# with no WApplication (no WM_HINTS window_group and no WM_CLIENT_LEADER,
# so wApplicationOf() is NULL). The leaderless window gets its
# auto-hide-on-unfocus flag from the WMState session entry; focusing a
# second window fires the AutoHideOnUnfocus hook, which hides it through
# wHideWindow(NULL icon) - the only WM-side hide path that needs no
# application. A pager-style _NET_ACTIVE_WINDOW request (wmctrl -a) then
# must bring it back: before the fix wMakeWindowVisible only unhid
# through the application and left the window unmapped while promoting
# it to focused_window. A grouped window is driven through the
# application path as the regression guard.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${LEADERLESS_HIDDEN_DRIVE_DISPLAY:-:116}
RUNTIME=$(mktemp -d /tmp/wmaker-leadhidden-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/leaderless-hidden-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
LEAD_PROBE="$RUNTIME/lead.py"
GRP_PROBE="$RUNTIME/grp.py"
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

for tool in Xvfb xdpyinfo xprop xwininfo xdotool wmctrl xterm python3; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
python3 -c 'import Xlib' >/dev/null 2>&1 || skip_all "python3-xlib not installed"
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

msg() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" "$@"
}
ipc_ready() { msg -q get_version >/dev/null 2>&1; }

# frame_of CLIENT : the WM frame of a managed client window
frame_of() {
	DISPLAY=$DPY xwininfo -tree -id "$1" 2>/dev/null \
		| awk '/Parent window id:/ {print $4; exit}'
}
# frame_map_state XID : "IsViewable" / "IsUnMapped" / "IsUnviewable"
frame_map_state() {
	local f
	f=$(frame_of "$1")
	[ -n "$f" ] && [ "$f" != "0x0" ] || { printf 'noframe'; return; }
	DISPLAY=$DPY xwininfo -id "$f" 2>/dev/null \
		| sed -n 's/.*Map State: //p'
}
frame_hidden() {
	local s
	s=$(frame_map_state "$1")
	[ "$s" = "IsUnMapped" ] || [ "$s" = "IsUnviewable" ]
}
frame_visible() {
	[ "$(frame_map_state "$1")" = "IsViewable" ]
}

win_entry() {
	msg get_windows 2>/dev/null | sed 's/}, *{/}\n{/g' | grep -F "id = $1;"
}

cat > "$LEAD_PROBE" <<'EOF'
import os, time
from Xlib import X, Xatom, display

d = display.Display()
s = d.screen()
w = s.root.create_window(20, 20, 300, 200, 0, s.root_depth,
			 X.InputOutput, X.CopyFromParent)
w.set_wm_class('lead', 'Lead')
w.set_wm_name('Leaderless Probe')
w.change_property(Xatom.WM_COMMAND, Xatom.STRING, 8, b'leadclient\x00')
# no WM_HINTS, no WM_CLIENT_LEADER: main_window stays XCB_NONE
w.map()
d.flush()
with open(os.environ['LEAD_XIDFILE'], 'w') as f:
	f.write('0x%x\n' % w.id)
time.sleep(900)
EOF

cat > "$GRP_PROBE" <<'EOF'
import os, time
from Xlib import X, Xatom, display

d = display.Display()
s = d.screen()
w = s.root.create_window(360, 20, 300, 200, 0, s.root_depth,
			 X.InputOutput, X.CopyFromParent)
w.set_wm_class('grpd', 'Grp')
w.set_wm_name('Grouped Probe')
w.change_property(Xatom.WM_COMMAND, Xatom.STRING, 8, b'grpdclient\x00')
w.set_wm_hints(flags=64, window_group=w.id)
w.map()
d.flush()
with open(os.environ['GRP_XIDFILE'], 'w') as f:
	f.write('0x%x\n' % w.id)
time.sleep(900)
EOF

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set LEADERLESS_HIDDEN_DRIVE_DISPLAY" >&2
	exit 1
fi

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
}
EOF
cat >"$WMHOME/Defaults/WMState" <<'EOF'
{
  Workspaces = (
    { Name = "ws-0"; }
  );
  Applications = (
    {
      Name = "lead.Lead";
      Command = "leadclient";
      AutoHideOnUnfocus = Yes;
    },
    {
      Name = "grpd.Grp";
      Command = "grpdclient";
      Hidden = Yes;
    }
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

note "start probe clients (before wmaker so they are adopted, not launched)"
LEAD_XIDFILE="$RUNTIME/lead.xid" \
	DISPLAY=$DPY python3 "$LEAD_PROBE" >>"$LOG" 2>&1 &
LEAD_PID=$!
echo "$LEAD_PID" >> "$PIDFILE"
GRP_XIDFILE="$RUNTIME/grp.xid" \
	DISPLAY=$DPY python3 "$GRP_PROBE" >>"$LOG" 2>&1 &
GRP_PID=$!
echo "$GRP_PID" >> "$PIDFILE"
lead_xid_ready() { grep -q '^0x[0-9a-fA-F]*$' "$RUNTIME/lead.xid" 2>/dev/null; }
grp_xid_ready() { grep -q '^0x[0-9a-fA-F]*$' "$RUNTIME/grp.xid" 2>/dev/null; }
wait_for 50 lead_xid_ready || fatal "leaderless probe did not map"
wait_for 50 grp_xid_ready || fatal "grouped probe did not map"
LEAD_XID=$(head -1 "$RUNTIME/lead.xid")
GRP_XID=$(head -1 "$RUNTIME/grp.xid")

note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY \
	"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
wait_for 120 ipc_ready || fatal "wmaker IPC did not come up"
sleep 1

check "wmaker alive after cold adopt" kill -0 "$WMAKER_PID"
check "leaderless window managed" win_entry "$LEAD_XID"
check "grouped window managed" win_entry "$GRP_XID"

note "the grouped window is adopted hidden from its WMState entry"
if wait_for 40 frame_hidden "$GRP_XID"; then
	record_pass "grouped window adopted hidden"
else
	record_fail "grouped window adopted hidden (frame state: $(frame_map_state "$GRP_XID"))"
fi

note "recover the grouped window (application path, regression guard)"
DISPLAY=$DPY wmctrl -i -a "$GRP_XID" >>"$LOG" 2>&1
if wait_for 40 frame_visible "$GRP_XID"; then
	record_pass "grouped window recovered through its application"
else
	record_fail "grouped window recovered through its application (frame state: $(frame_map_state "$GRP_XID"))"
fi
check "wmaker alive after grouped recovery" kill -0 "$WMAKER_PID"

note "fire the AutoHideOnUnfocus hook on the leaderless window"
# the flag comes from the session entry; unfocusing it hides it through
# wHideWindow with a NULL icon - the no-application hide path
DISPLAY=$DPY wmctrl -i -a "$LEAD_XID" >>"$LOG" 2>&1
if wait_for 40 frame_visible "$LEAD_XID"; then
	record_pass "leaderless window focusable before the hook"
else
	record_fail "leaderless window focusable before the hook (frame state: $(frame_map_state "$LEAD_XID"))"
fi

DISPLAY=$DPY xterm -geometry 80x24+500+400 -e 'sleep 600' >>"$LOG" 2>&1 &
XTERM_PID=$!
echo "$XTERM_PID" >> "$PIDFILE"
xterm_managed() {
	msg get_windows 2>/dev/null | grep -F 'XTerm' >/dev/null
}
XTERM_XID=
if wait_for 50 xterm_managed; then
	XTERM_XID=$(msg get_windows \
		| sed 's/}, *{/}\n{/g' \
		| sed -n 's/.*id = \(0x[0-9a-fA-F]*\);.*/\1/p' \
		| grep -v -e "^$LEAD_XID\$" -e "^$GRP_XID\$" | head -1)
fi
[ -n "$XTERM_XID" ] || fatal "unfocus-target xterm never got managed (windows: $(msg get_windows 2>/dev/null | tr '\n' ' '))"

DISPLAY=$DPY wmctrl -i -a "$XTERM_XID" >>"$LOG" 2>&1
if wait_for 40 frame_hidden "$LEAD_XID"; then
	record_pass "auto-hide hook hid the leaderless window"
else
	record_fail "auto-hide hook hid the leaderless window (frame state: $(frame_map_state "$LEAD_XID"))"
fi
check "wmaker alive after the hook" kill -0 "$WMAKER_PID"

note "recover the leaderless window (the direct unhide branch)"
DISPLAY=$DPY wmctrl -i -a "$LEAD_XID" >>"$LOG" 2>&1
if wait_for 40 frame_visible "$LEAD_XID"; then
	record_pass "leaderless window recovered by wMakeWindowVisible"
else
	record_fail "leaderless window recovered by wMakeWindowVisible (frame state: $(frame_map_state "$LEAD_XID"))"
fi
check "wmaker alive after leaderless recovery" kill -0 "$WMAKER_PID"
check "both windows still managed" win_entry "$LEAD_XID"

note "summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

kill "$LEAD_PID" "$GRP_PID" "$XTERM_PID" 2>/dev/null
msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
