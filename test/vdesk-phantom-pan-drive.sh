#!/bin/sh
# Drive the virtual-desktop edge pan across an empty stretch of the
# virtual desktop (finding: edge resistance applied to empty desktops).
#
# getClosestEdge() returned (0,0) both for "a window edge exactly at the
# boundary" and for "no qualifying window", so the resistance path could
# not tell a real snap from an empty desktop stretch: the first
# VirtualEdgeResistance pixels of every drag were eaten before the pan
# resumed. The fix makes getClosestEdge() report whether any window
# qualified; an empty stretch pans freely with no resistance and no
# attraction snap.
#
# Scenario: one client parked off-screen left (PPosition -1500) so the
# virtual desktop extends past the screen. Drag-pan left until the
# window sits flush with the screen's left boundary (the range end),
# then drag-pan right: the window's left edge crosses the boundary after
# the resistance is dragged through and no corner qualifies any more,
# while the viewport still has ~1500 px of room. With the fix those
# injections pan freely; before it they were re-eaten by the phantom
# edge at 0.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${VDESK_PHANTOM_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-vdphantom-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/vdesk-phantom-pan-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
PROBE="$RUNTIME/probe.py"
SCR_W=1024
SCR_H=768
CENTER_X=$((SCR_W / 2))
CENTER_Y=$((SCR_H / 2))
STEP=50
PHANTOM_STEP=8
PHANTOM_EVENTS=40
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

for tool in Xvfb xdpyinfo xprop xdotool wmctrl python3 wmakermsg; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
python3 -c 'import Xlib' >/dev/null 2>&1 || skip_all "python3-xlib not installed"
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "wmaker not built"

msg() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" "$@"
}
ipc_ready() { msg -q get_version >/dev/null 2>&1; }
wmaker_alive() { kill -0 "$WMAKER_PID" 2>/dev/null; }

win_entry() {
	msg get_windows 2>/dev/null | sed 's/}, *{/}\n{/g' | grep -F "id = $1;"
}

# the pan relocates the windows themselves, and _NET_DESKTOP_VIEWPORT
# clamps negatives to 0 (EWMH has no signed viewport), so the probe
# window's on-screen position is the observable: at view 0 the window
# sits at world -1500 (fully off-screen left); panning the viewport to
# -1500 slides the window flush with the screen's left boundary
win_abs_x() {
	DISPLAY=$DPY xwininfo -id "$PROBE_XID" 2>/dev/null \
		| awk '/Absolute upper-left X/ {print $4}'
}

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set VDESK_PHANTOM_DRIVE_DISPLAY" >&2
	exit 77
fi

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  EnableVirtualDesktop = YES;
  DefaultWorkspaceLayout = float;
  VirtualEdgeResistance = 30;
}
EOF

export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET

cat > "$PROBE" <<'EOF'
import os, time
from Xlib import X, Xatom, display

d = display.Display()
s = d.screen()
# maps on-screen; the drive then moves it off-screen left with a plain
# ConfigureRequest (xdotool windowmove), which the WM honours
w = s.root.create_window(10, 10, 300, 200, 0, s.root_depth,
			 X.InputOutput, X.CopyFromParent)
w.set_wm_class('voff', 'Voff')
w.set_wm_name('Offscreen Probe')
w.change_property(Xatom.WM_COMMAND, Xatom.STRING, 8, b'voffclient\x00')
w.map()
d.flush()
with open(os.environ['PROBE_XIDFILE'], 'w') as f:
	f.write('0x%x\n' % w.id)
time.sleep(900)
EOF

note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 ${SCR_W}x${SCR_H}x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
	kill -0 "$XVFB_PID" 2>/dev/null || return 1
	DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fatal "Xvfb failed to start"

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

note "park the probe window off-screen left"
PROBE_XIDFILE="$RUNTIME/probe.xid" DISPLAY=$DPY python3 "$PROBE" >>"$LOG" 2>&1 &
PROBE_PID=$!
echo "$PROBE_PID" >> "$PIDFILE"
probe_xid_ready() { grep -q '^0x[0-9a-fA-F]*$' "$RUNTIME/probe.xid" 2>/dev/null; }
wait_for 50 probe_xid_ready || fatal "probe client did not map"
PROBE_XID=$(head -1 "$RUNTIME/probe.xid")
probe_managed() { win_entry "$PROBE_XID" >/dev/null; }
wait_for 50 probe_managed || fatal "probe window never got managed"
check "wmaker alive after the probe maps" wmaker_alive

# move the client off-screen through a ConfigureRequest; the WM honours
# it, the virtual desktop then extends past the screen's left edge
DISPLAY=$DPY xdotool windowmove "$PROBE_XID" -1500 100 >>"$LOG" 2>&1
probe_offscreen() {
	[ "$(DISPLAY=$DPY xwininfo -id "$PROBE_XID" 2>/dev/null \
		| awk '/Absolute upper-left X/ {print $4}')" -lt -1000 ]
}
if wait_for 40 probe_offscreen; then
	record_pass "probe window parked off-screen left"
else
	record_fail "probe window parked off-screen left (at $(DISPLAY=$DPY xwininfo -id "$PROBE_XID" 2>/dev/null | awk '/Absolute upper-left X/ {print $4}'))"
fi

check "window starts off-screen left" [ "$(win_abs_x)" -lt -1000 ]

# --- 1. drag-pan left until the window sits flush with the boundary -------
note "pan left: drag the window into view up to the range end"
DISPLAY=$DPY xdotool mousemove $((CENTER_X / 2)) $CENTER_Y >>"$LOG" 2>&1
sleep 0.3
DISPLAY=$DPY xdotool mousemove 1 $CENTER_Y >>"$LOG" 2>&1
sleep 0.4
i=0
while [ "$i" -lt 40 ]; do
	DISPLAY=$DPY xdotool mousemove $((CENTER_X - STEP)) $CENTER_Y >>"$LOG" 2>&1
	sleep 0.12
	i=$((i + 1))
done
# the modal loop idles out 500 ms after the last injection
sleep 0.8
FLUSH_X=$(win_abs_x)
if [ -n "$FLUSH_X" ] && [ "$FLUSH_X" -ge -5 ] 2>/dev/null && [ "$FLUSH_X" -le 5 ] 2>/dev/null; then
	record_pass "pan left reaches the range end (window flush at $FLUSH_X)"
else
	record_fail "pan left reaches the range end (window flush) (window at ${FLUSH_X:-unset})"
fi
check "wmaker alive after the left pan" wmaker_alive

# --- 2. a real edge at the boundary resists (regression guard) ------------
note "right-edge drag: the real edge at the boundary still resists"
DISPLAY=$DPY xdotool mousemove 600 $CENTER_Y >>"$LOG" 2>&1
sleep 0.3
DISPLAY=$DPY xdotool mousemove $((SCR_W - 1)) $CENTER_Y >>"$LOG" 2>&1
sleep 0.4
DISPLAY=$DPY xdotool mousemove $((CENTER_X + PHANTOM_STEP)) $CENTER_Y >>"$LOG" 2>&1
sleep 0.3
RESIST_X=$(win_abs_x)
if [ -n "$RESIST_X" ] && [ "$RESIST_X" -ge -5 ] 2>/dev/null && [ "$RESIST_X" -le 5 ] 2>/dev/null; then
	record_pass "one small drag through a real edge does not pan (window at $RESIST_X)"
else
	record_fail "one small drag through a real edge does not pan (window at ${RESIST_X:-unset})"
fi

# --- 3. the empty stretch pans freely -------------------------------------
note "inject $PHANTOM_EVENTS small drags across the empty stretch"
i=0
while [ "$i" -lt "$PHANTOM_EVENTS" ]; do
	DISPLAY=$DPY xdotool mousemove $((CENTER_X + PHANTOM_STEP)) $CENTER_Y >>"$LOG" 2>&1
	sleep 0.12
	i=$((i + 1))
done
sleep 0.8

# 40 x 8 px = 320 px injected; the 30 px real-edge resistance is the same
# before and after the fix, so the fix is proven by the phantom stretch
# panning most of the remainder instead of re-eating it (pre-fix the
# phantom edge at 0 re-arms the resistance and the window only creeps a
# few px past the boundary)
END_X=$(win_abs_x)
if [ -n "$END_X" ] && [ "$END_X" -le -150 ] 2>/dev/null; then
	record_pass "empty stretch pans freely (window ended at $END_X)"
else
	record_fail "empty stretch pans freely (window ended at ${END_X:-unset})"
fi
check "wmaker alive after the phantom pan" wmaker_alive

note "summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

kill "$PROBE_PID" 2>/dev/null
msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
