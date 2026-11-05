#!/bin/sh
# Warm-restart focus regression under PerHeadWorkspaces: the window that
# held the focus must still have it after wmaker re-execs, even when it
# sits on a head that is not switched last.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${HEAD_FOCUS_DRIVE_DISPLAY:-:$(drive_default_display)}
PARENT_DISPLAY=${HEAD_FOCUS_DRIVE_PARENT_DISPLAY:-":$(( ${DPY#:} + 1 ))"}
RUNTIME=$(mktemp -d /tmp/head-focus-drive.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/head-focus-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
SOCKET=
: > "$PIDFILE"
: > "$LOG"

PASSED=0
FAILED=0

cleanup() {
	while read -r p; do
		[ -n "$p" ] && kill "$p" 2>/dev/null
	done < "$PIDFILE"
	rm -rf "$RUNTIME"
}
trap cleanup EXIT

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
ok()   { PASSED=$((PASSED + 1)); printf 'PASS: %s\n' "$*" | tee -a "$LOG"; }
fail() { FAILED=$((FAILED + 1)); printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; }
fatal() { printf 'FATAL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
skip_all() { printf 'SKIP: %s\n' "$*" | tee -a "$LOG"; exit 77; }

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

msg() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" "$@"
}
ipc_ready() { msg -q get_version >/dev/null 2>&1; }
pid_rebind() {
	WMAKER_PID=$(msg get_version 2>/dev/null \
		| sed -n 's/.*pid = \([0-9]*\);.*/\1/p')
	[ -n "$WMAKER_PID" ]
}
active_is() {
	[ "$(DISPLAY=$DPY xdotool getactivewindow 2>/dev/null)" = "$1" ]
}
window_id() {
	DISPLAY=$DPY wmctrl -l 2>/dev/null | awk -v t="$1" '$NF == t { print $1; exit }'
}
xr() { DISPLAY=$DPY xrandr "$@"; }

for tool in Xvfb Xephyr xrandr xdpyinfo xterm xdotool wmctrl; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use" >&2
	exit 77
fi
if DISPLAY=$PARENT_DISPLAY xdpyinfo >/dev/null 2>&1; then
	echo "parent display $PARENT_DISPLAY is already in use" >&2
	exit 77
fi

export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG WMAKER_IPC_SOCKET
export XDG_RUNTIME_DIR="$RUNTIME"
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<'WMCONF'
{
  PerHeadWorkspaces = YES;
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  NoAutoSnap = YES;
}
WMCONF

note "start Xvfb $PARENT_DISPLAY and Xephyr $DPY"
Xvfb "$PARENT_DISPLAY" -screen 0 1600x1200x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
wait_for 40 sh -c "kill -0 $XVFB_PID 2>/dev/null && DISPLAY=$PARENT_DISPLAY xdpyinfo >/dev/null 2>&1" \
	|| fatal "Xvfb failed to start"
DISPLAY=$PARENT_DISPLAY Xephyr "$DPY" -screen 1024x768 -ac -noreset >>"$LOG" 2>&1 &
XEPHYR_PID=$!
echo "$XEPHYR_PID" >> "$PIDFILE"
wait_for 40 sh -c "kill -0 $XEPHYR_PID 2>/dev/null && DISPLAY=$DPY xdpyinfo >/dev/null 2>&1" \
	|| fatal "Xephyr failed to start"

note "split $DPY into left/right monitors"
xr --setmonitor left 512/135x768/203+0+0 default >>"$LOG" 2>&1 \
	|| fatal "could not create left monitor"
xr --setmonitor right 512/135x768/203+512+0 none >>"$LOG" 2>&1 \
	|| fatal "could not create right monitor"
xr --output default --mode 800x600 >>"$LOG" 2>&1 || fatal "could not nudge the mode"
sleep 0.4
xr --output default --mode 1024x768 >>"$LOG" 2>&1 || fatal "could not restore the mode"
sleep 0.5
wait_for 30 sh -c "DISPLAY=$DPY xrandr --listmonitors | grep -q ' left '" \
	|| fatal "left head not advertised"
wait_for 30 sh -c "DISPLAY=$DPY xrandr --listmonitors | grep -q ' right '" \
	|| fatal "right head not advertised"

note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	ASAN_OPTIONS="$(drive_asan_options)" \
	DISPLAY=$DPY \
	"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
wait_for 80 ipc_ready || fatal "wmaker IPC did not come up"

note "spawn one xterm per head"
DISPLAY=$DPY xterm -geometry 30x8+40+40 -T head-left -e 'sleep 600' >>"$LOG" 2>&1 &
echo "$!" >> "$PIDFILE"
sleep 1
DISPLAY=$DPY xterm -geometry 30x8+600+40 -T head-right -e 'sleep 600' >>"$LOG" 2>&1 &
echo "$!" >> "$PIDFILE"
wait_for 40 sh -c "DISPLAY=$DPY wmctrl -l | grep -q ' head-left\$'" \
	|| fatal "head-left did not map"
wait_for 40 sh -c "DISPLAY=$DPY wmctrl -l | grep -q ' head-right\$'" \
	|| fatal "head-right did not map"
sleep 0.5
LEFT=$(window_id head-left)
LEFT_DEC=$(printf '%d' "$LEFT")
DISPLAY=$DPY wmctrl -i -a "$LEFT" >>"$LOG" 2>&1
wait_for 30 active_is "$LEFT_DEC" || fatal "could not focus head-left before restart"

note "wmakermsg restart"
msg -q restart >>"$LOG" 2>&1
wait_for 120 ipc_ready || fatal "wmaker IPC did not re-bind after restart"
wait_for 50 pid_rebind || fatal "wmaker pid not reported after restart"
echo "$WMAKER_PID" >> "$PIDFILE"
sleep 3
if active_is "$LEFT_DEC"; then
	ok "focus stays on the window of the earlier head"
else
	fail "focus moved off the earlier head (active=$(DISPLAY=$DPY xdotool getactivewindow 2>/dev/null))"
fi

note "focus the window of the last head and restart again"
RIGHT=$(window_id head-right)
RIGHT_DEC=$(printf '%d' "$RIGHT")
DISPLAY=$DPY wmctrl -i -a "$RIGHT" >>"$LOG" 2>&1
wait_for 30 active_is "$RIGHT_DEC" || fatal "could not focus head-right before restart"
msg -q restart >>"$LOG" 2>&1
wait_for 120 ipc_ready || fatal "wmaker IPC did not re-bind after the second restart"
wait_for 50 pid_rebind || fatal "wmaker pid not reported after the second restart"
echo "$WMAKER_PID" >> "$PIDFILE"
sleep 3
if active_is "$RIGHT_DEC"; then
	ok "focus stays on the window of the last head"
else
	fail "focus moved off the last head (active=$(DISPLAY=$DPY xdotool getactivewindow 2>/dev/null))"
fi

note "head focus drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
[ "$FAILED" -eq 0 ] && [ "$PASSED" -gt 0 ]
