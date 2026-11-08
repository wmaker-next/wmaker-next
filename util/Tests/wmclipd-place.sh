#!/bin/sh
# Regression test for wmclipd popup placement: the typing-triggered
# popup must anchor to the focused window, not the mouse pointer, so on
# a multi-head setup it shows on the monitor the user is typing in.
#
# Xephyr +xinerama provides the two heads (the Xinerama fallback path of
# clip_geom.c), hosted by a private Xvfb since Xephyr needs a display to
# draw into.

set -u

top_builddir=${top_builddir:-../..}
builddir=${builddir:-.}

HOSTDPY=${WMCLIPD_PLACE_HOST_DISPLAY:-:93}
DPY=${WMCLIPD_PLACE_DISPLAY:-:94}
RUNTIME=$(mktemp -d /tmp/wmclipd-place.XXXXXX) || exit 99
PIDFILE="$RUNTIME/pids"
LOG="$RUNTIME/wmclipd.log"

WMCLIPD="$top_builddir/util/wmclipd"
CLIPTEST="$builddir/wmclipd_cliptest"
WMLIBS="$top_builddir/WINGs/.libs:$top_builddir/wrlib/.libs"

: > "$PIDFILE"
: > "$LOG"

cleanup()
{
	while read -r pid; do
		[ -n "$pid" ] && kill "$pid" 2>/dev/null
	done < "$PIDFILE"
	rm -rf "$RUNTIME"
}

trap cleanup EXIT

skip_all()
{
	printf 'SKIP: %s\n' "$*"
	exit 77
}

fail()
{
	printf 'FAIL: %s\n' "$*"
	printf '%s\n' '--- wmclipd log ---'
	cat "$LOG"
	exit 1
}

wait_for()
{
	wf_count=$1
	shift
	while [ "$wf_count" -gt 0 ]; do
		"$@" && return 0
		sleep 0.1
		wf_count=$((wf_count - 1))
	done
	return 1
}

pid_gone()
{
	! kill -0 "$1" 2>/dev/null
}

log_has()
{
	grep -qF "$1" "$LOG" 2>/dev/null
}

stop_pid()
{
	spid=$1
	kill "$spid" 2>/dev/null || return 0
	wait_for 30 pid_gone "$spid" && return 0
	kill -9 "$spid" 2>/dev/null || true
	wait_for 10 pid_gone "$spid"
}

start_wmclipd()
{
	: > "$LOG"
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY WMAKER_IPC_SOCKET="$RUNTIME/ipc.sock" \
		"$WMCLIPD" --clipboard --history 10 --debug >>"$LOG" 2>&1 &
	WMCLIPD_PID=$!
	echo "$WMCLIPD_PID" >> "$PIDFILE"
	wait_for 30 log_has "monitoring CLIPBOARD and PRIMARY" ||
		fail "wmclipd did not start"
}

populate_history()
{
	DISPLAY=$DPY "$CLIPTEST" serve-string "hello placement" >>"$LOG" 2>&1 &
	SERVE_PID=$!
	echo "$SERVE_PID" >> "$PIDFILE"
	wait_for 50 log_has "history: added entry" ||
		fail "wmclipd did not record clipboard history"
	stop_pid "$SERVE_PID"
}

start_focus_window()
{
	DISPLAY=$DPY "$CLIPTEST" map-window "$@" >>"$LOG" 2>&1 &
	FOCUS_PID=$!
	echo "$FOCUS_PID" >> "$PIDFILE"
	wait_for 30 log_has "mapped 0x" ||
		fail "focus window did not map"
}

# the warper holds its connection: nested servers resync the pointer
# from their host window when the warping client disconnects
start_warp()
{
	DISPLAY=$DPY "$CLIPTEST" warp "$1" "$2" >>"$LOG" 2>&1 &
	WARP_PID=$!
	echo "$WARP_PID" >> "$PIDFILE"
	wait_for 30 log_has "warped: $1,$2" ||
		fail "pointer did not warp to $1,$2"
}

send_tick()
{
	DISPLAY=$DPY WMAKER_IPC_SOCKET="$RUNTIME/ipc.sock" \
		"$CLIPTEST" fake-tick >>"$LOG" 2>&1 &
	TICK_PID=$!
	echo "$TICK_PID" >> "$PIDFILE"
	wait_for 100 log_has "popup placed at" ||
		fail "wmclipd popup was not placed"
}

# geometry of the last "popup placed at X,Y WxH anchor=A" log line
placed_line()
{
	grep -F "popup placed at" "$LOG" | tail -1
}

placed_fields()
{
	pf_line=$1
	# shellcheck disable=SC2086
	set -- $(printf '%s\n' "$pf_line" |
		sed -n 's/.*popup placed at \(-*[0-9][0-9]*\),\(-*[0-9][0-9]*\) \([0-9][0-9]*\)x\([0-9][0-9]*\).*/\1 \2 \3 \4/p')
	PLACE_X=${1:-}
	PLACE_Y=${2:-}
	PLACE_W=${3:-}
	PLACE_H=${4:-}
	[ -n "$PLACE_X" ] || fail "cannot parse placement line: $pf_line"
}

for tool in Xvfb Xephyr xdpyinfo awk grep sed; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done

[ -x "$WMCLIPD" ] || skip_all "wmclipd not built"
[ -x "$CLIPTEST" ] || skip_all "wmclipd_cliptest not built"

if DISPLAY=$HOSTDPY xdpyinfo >/dev/null 2>&1; then
	skip_all "host display $HOSTDPY is already in use"
fi
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	skip_all "display $DPY is already in use"
fi

Xvfb "$HOSTDPY" -screen 0 640x480x24 -ac >>"$LOG" 2>&1 &
HOST_PID=$!
echo "$HOST_PID" >> "$PIDFILE"

host_ready()
{
	kill -0 "$HOST_PID" 2>/dev/null || fail "Xvfb exited early"
	DISPLAY=$HOSTDPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 host_ready || fail "Xvfb failed to start"

DISPLAY=$HOSTDPY Xephyr "$DPY" +xinerama -screen 400x600 -screen 400x600 \
	>>"$LOG" 2>&1 &
XEPHYR_PID=$!
echo "$XEPHYR_PID" >> "$PIDFILE"

xephyr_ready()
{
	kill -0 "$XEPHYR_PID" 2>/dev/null || fail "Xephyr exited early"
	DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xephyr_ready || fail "Xephyr failed to start"

heads_out=$(DISPLAY=$DPY "$CLIPTEST" heads)
set -- $heads_out
[ "${1:-}" = "2" ] ||
	skip_all "server reports ${1:-no} heads, need 2 for the placement test"

# Case 1: focus on the left head, pointer on the right head. The popup
# must land over the focused window (20,20 200x200 -> center 120,120)
# on the left head, not at the pointer (600,300).
start_wmclipd
populate_history
start_focus_window 20 20 200 200
start_warp 600 300
send_tick

line=$(placed_line)
printf '%s\n' "$line" | grep -qF "anchor=focus" ||
	fail "cross-head popup did not anchor to the focus window: $line"
placed_fields "$line"
center_x=$((PLACE_X + PLACE_W / 2))
center_y=$((PLACE_Y + PLACE_H / 2))
[ "$PLACE_X" -ge 0 ] && [ "$((PLACE_X + PLACE_W))" -le 400 ] ||
	fail "cross-head popup left the focus head: $line"
[ "$center_x" -ge 20 ] && [ "$center_x" -le 220 ] ||
	fail "cross-head popup is not over the focused window: $line"
[ "$center_y" -ge 20 ] && [ "$center_y" -le 220 ] ||
	fail "cross-head popup is not over the focused window: $line"

stop_pid "$WMCLIPD_PID"
stop_pid "$FOCUS_PID"
stop_pid "$WARP_PID"
stop_pid "$TICK_PID"
printf 'PASS: cross-head popup anchored to the focused window (%s)\n' "$line"

# Case 2: pointer on the same head as the focus window keeps the
# pointer-following placement.
start_wmclipd
populate_history
start_focus_window 20 20 200 200
start_warp 100 100
send_tick

line=$(placed_line)
printf '%s\n' "$line" | grep -qF "anchor=pointer" ||
	fail "same-head popup did not stay at the pointer: $line"
placed_fields "$line"
center_x=$((PLACE_X + PLACE_W / 2))
diff_x=$((center_x - 100))
[ "$diff_x" -ge -2 ] && [ "$diff_x" -le 2 ] ||
	fail "same-head popup is not at the pointer: $line"

stop_pid "$WMCLIPD_PID"
stop_pid "$FOCUS_PID"
stop_pid "$WARP_PID"
stop_pid "$TICK_PID"
printf 'PASS: same-head popup stayed at the pointer (%s)\n' "$line"

# Case 3: a focused window too small to contain the popup must not be
# covered by it. The popup flips just below the window (150,150 60x40),
# still centered on it and on the left head, not at the pointer.
start_wmclipd
populate_history
start_focus_window 150 150 60 40
start_warp 600 300
send_tick

line=$(placed_line)
printf '%s\n' "$line" | grep -qF "anchor=focus" ||
	fail "small-window popup did not anchor to the focus window: $line"
placed_fields "$line"
center_x=$((PLACE_X + PLACE_W / 2))
diff_x=$((center_x - 180))
[ "$PLACE_Y" -ge 190 ] ||
	fail "small-window popup covers the focused window: $line"
[ "$diff_x" -ge -2 ] && [ "$diff_x" -le 2 ] ||
	fail "small-window popup is not centered on the focused window: $line"
[ "$PLACE_X" -ge 0 ] && [ "$((PLACE_X + PLACE_W))" -le 400 ] ||
	fail "small-window popup left the focus head: $line"

stop_pid "$WMCLIPD_PID"
stop_pid "$FOCUS_PID"
stop_pid "$WARP_PID"
stop_pid "$TICK_PID"
printf 'PASS: small-window popup flipped below the focused window (%s)\n' "$line"
