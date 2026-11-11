#!/bin/sh
# Per-window geometry locks from the window-menu Options sub-menu.
#
# Covers the cwm-style "Lock position"/"Lock size" toggles:
#   1. Control: xdotool windowmove/windowsize move and resize a plain
#      managed window (proves the assertions can detect change).
#   2. "Lock size": client ConfigureRequests stop changing the size,
#      while moves still work.
#   3. "Lock position" additionally stops moves.
#   4. A wmakermsg restart re-applies the locks from the
#      _WINDOWMAKER_STATE data[14]/data[15] tri-state slots.
#   5. Unlocking through the same menu restores both.
#   6. A full session save (SaveSessionOnExit = Yes, wmakermsg exit) must
#      NOT write a lock key: the toggles are per-window, and the per-app
#      WMState Applications entry cannot tell same-class siblings apart.
#      After killing the client, the cold-start re-launch comes back
#      unlocked.
#
# The menu is opened with WindowMenuKey (Control+Escape, keyboard mode),
# which keyboard-grabs, so xdotool key navigates. A freshly mapped menu
# has no preselection, so Down x12 lands on "Options" (rows 0..11, all
# always visible), Right enters the cascade with "Auto-hide on unfocus"
# (row 0) preselected, then Down x4/x5 reaches "Lock position"/"Lock size",
# Return executes.
#
# Runs against an isolated WMAKER_USER_ROOT and XDG_RUNTIME_DIR, tracks
# every PID it spawns, the live desktop is untouched.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${LOCK_GEOMETRY_DRIVE_DISPLAY:-:$(drive_default_display)}
W=1280
H=800
RUNTIME=$(mktemp -d /tmp/lock-geometry-drive.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=${LOCK_GEOMETRY_DRIVE_LOG:-/tmp/lock-geometry-drive.log}
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
WMSTATE="$WMHOME/Defaults/WMState"
: > "$PIDFILE"
: > "$LOG"

PASSED=0
FAILED=0

record_pass() { PASSED=$((PASSED + 1)); printf 'PASS: %s\n' "$*" | tee -a "$LOG"; }
record_fail() { FAILED=$((FAILED + 1)); printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; }
note()       { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
fatal()      { printf 'FATAL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
skip_all()   { printf 'SKIP: %s\n' "$*" | tee -a "$LOG"; exit 77; }

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

command -v Xvfb     >/dev/null 2>&1 || skip_all "Xvfb not found"
command -v xdotool  >/dev/null 2>&1 || skip_all "xdotool not found"
command -v xterm    >/dev/null 2>&1 || skip_all "xterm not found"
command -v xprop    >/dev/null 2>&1 || skip_all "xprop not found"
command -v xwininfo >/dev/null 2>&1 || skip_all "xwininfo not found"
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

# --- isolated defaults -----------------------------------------------------
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  DisableDock = Yes;
  DisableClip = Yes;
  DisableDrawers = Yes;
  NoAutoSnap = Yes;
  SaveSessionOnExit = No;
}
EOF
cat >"$WMHOME/Defaults/WMWindowAttributes" <<EOF
{
  "*" = { NoAppIcon = Yes; };
}
EOF

export WMAKER_USER_ROOT="$WMHOME"
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET WMAKER_USER_XDG

# --- Xvfb ------------------------------------------------------------------
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set LOCK_GEOMETRY_DRIVE_DISPLAY" >&2
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

# --- wmaker ----------------------------------------------------------------
wm_ready() {
	DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null \
		| grep -q 'window id'
}
ipc_ready() {
	SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		DISPLAY=$DPY "$WMAKERMSG" --get-socketpath 2>/dev/null)
	[ -n "$SOCKET" ] || return 1
	case "$SOCKET" in
		"$RUNTIME"/*) : ;;
		*) fatal "IPC socket $SOCKET escaped private XDG_RUNTIME_DIR" ;;
	esac
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		DISPLAY=$DPY "$WMAKERMSG" -s "$SOCKET" get_version >/dev/null 2>&1
}
msg() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		DISPLAY=$DPY "$WMAKERMSG" -s "$SOCKET" "$@"
}

start_wmaker() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	ASAN_OPTIONS="$(drive_asan_options)" \
	DISPLAY=$DPY \
	"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
	WMAKER_PID=$!
	echo "$WMAKER_PID" >> "$PIDFILE"
	wait_for 120 wm_ready || fatal "wmaker did not come up"
	wait_for 80 ipc_ready || fatal "wmaker IPC did not come up"
	sleep 0.5
}

note "start wmaker"
start_wmaker

# --- client ----------------------------------------------------------------
note "spawn xterm at +150+120"
DISPLAY=$DPY xterm -name lockterm -title lockterm -geometry 60x12+150+120 \
	-e 'sleep 600' >>"$LOG" 2>&1 &
XTPID=$!
echo "$XTPID" >> "$PIDFILE"
wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^lockterm\$' 2>/dev/null | grep -q ." \
	|| fatal "xterm did not map"
XTW=$(DISPLAY=$DPY xdotool search --name '^lockterm$' 2>/dev/null | tail -n 1)
[ -n "$XTW" ] || fatal "could not find lockterm client window"

# measure the client window, not the frame: locking size removes the
# resizebar and would otherwise show up as a frame change
geom() {
	DISPLAY=$DPY xwininfo -id "$XTW" 2>/dev/null | awk '
		$1 == "Width:"  { w = $2 }
		$1 == "Height:" { h = $2 }
		$1 == "Absolute" && $3 == "X:" { x = $4 }
		$1 == "Absolute" && $3 == "Y:" { y = $4 }
		END { if (w != "") print w "x" h "+" x "+" y }'
}

xdo() {
	DISPLAY=$DPY xdotool "$@" >>"$LOG" 2>&1
}

focus_term() {
	xdo windowactivate "$XTW"
	sleep 0.3
}

lockterm_mapped() {
	DISPLAY=$DPY xdotool search --name '^lockterm$' 2>/dev/null | grep -q .
}

find_lockterm() {
	DISPLAY=$DPY xdotool search --name '^lockterm$' 2>/dev/null | tail -n 1
}

# open the window menu from the keyboard and execute the Options submenu
# entry at index $1 (2 = Lock position, 3 = Lock size). A freshly mapped
# menu has no preselection (Down x1 -> row 0) and Options is main-menu
# row 9 while the Restore geometry entry is hidden, so 10 Downs.
# Entering the cascade preselects row 0 (Keep at
# bottom), so submenu index N needs Down xN.
toggle_option() {
	focus_term
	xdo key ctrl+Escape
	sleep 0.4
	i=0
	while [ "$i" -lt 10 ]; do
		xdo key Down
		i=$((i + 1))
		sleep 0.05
	done
	xdo key Right
	sleep 0.2
	i=0
	while [ "$i" -lt "$1" ]; do
		xdo key Down
		i=$((i + 1))
		sleep 0.05
	done
	xdo key Return
	sleep 0.5
}

expect_geom() {
	want=$1
	have=$(geom)
	if [ "$have" = "$want" ]; then
		return 0
	fi
	printf 'geometry %s, want %s\n' "$have" "$want" >>"$LOG"
	return 1
}

note "control: unlocked window resizes and moves"
focus_term
BASE=$(geom)
xdo windowsize "$XTW" 420 260
sleep 0.6
RESIZED=$(geom)
[ "$RESIZED" != "$BASE" ] || { printf 'geom %s\n' "$BASE" >>"$LOG"; fatal "control windowsize did not change the window"; }
xdo windowmove "$XTW" 300 300
sleep 0.6
MOVED=$(geom)
[ "$MOVED" != "$RESIZED" ] || fatal "control windowmove did not move the window"
record_pass "control: free window moves and resizes"

note "toggle Lock size (Options index 5)"
toggle_option 5 || fatal "could not toggle Lock size"
G0=$(geom)
xdo windowsize "$XTW" 300 200
sleep 0.6
if expect_geom "$G0"; then
	record_pass "lock size: ConfigureRequest width/height ignored"
else
	record_fail "lock size: geometry changed to $(geom)"
fi
xdo windowmove "$XTW" 150 250
sleep 0.6
G1=$(geom)
if [ "$G1" != "$G0" ]; then
	record_pass "lock size: position still free"
else
	record_fail "lock size: move was wrongly blocked"
fi

note "toggle Lock position (Options index 4)"
toggle_option 4 || fatal "could not toggle Lock position"
G1=$(geom)
xdo windowmove "$XTW" 500 400
xdo windowsize "$XTW" 640 480
sleep 0.6
if expect_geom "$G1"; then
	record_pass "lock position: move and resize both ignored"
else
	record_fail "lock position: geometry changed to $(geom)"
fi

note "warm restart, locks must come back via _WINDOWMAKER_STATE"
msg -q restart >>"$LOG" 2>&1 || fatal "wmakermsg restart failed"
wait_for 120 ipc_ready || fatal "IPC did not re-bind after restart"
wait_for 120 wm_ready || fatal "wmaker did not come back after restart"
wait_for 60 sh -c "DISPLAY=$DPY xwininfo -id $XTW >/dev/null 2>&1" \
	|| fatal "client vanished across restart"
sleep 0.8
G2=$(geom)
xdo windowmove "$XTW" 500 400
xdo windowsize "$XTW" 640 480
sleep 0.6
if expect_geom "$G2"; then
	record_pass "restart: locks persisted and still enforced"
else
	record_fail "restart: geometry changed to $(geom)"
fi

note "unlock both through the menu"
toggle_option 4 || fatal "could not unlock Lock position"
toggle_option 5 || fatal "could not unlock Lock size"
focus_term
xdo windowmove "$XTW" 200 160
sleep 0.6
G3=$(geom)
if [ "$G3" != "$G2" ]; then
	record_pass "unlock: move works again"
else
	record_fail "unlock: move still blocked at $(geom)"
fi
xdo windowsize "$XTW" 380 240
sleep 0.6
G4=$(geom)
if [ "$G4" != "$G3" ]; then
	record_pass "unlock: resize works again"
else
	record_fail "unlock: resize still blocked at $(geom)"
fi

note "session-file round-trip: re-lock both"
toggle_option 4 || fatal "could not re-lock Lock position"
toggle_option 5 || fatal "could not re-lock Lock size"
G5=$(geom)
xdo windowmove "$XTW" 300 300
xdo windowsize "$XTW" 520 360
sleep 0.6
if expect_geom "$G5"; then
	record_pass "re-lock: both locks enforced before the save"
else
	record_fail "re-lock: geometry changed to $(geom)"
fi

# The toggles are per-window, like Keep on top. The WMState Applications
# entry is per application and matched by (instance, class, command), so a
# per-window lock must not ride it: two same-class siblings would share the
# value. Save the session on exit and confirm no lock key is written.
note "save the session on exit (SaveSessionOnExit = Yes)"
cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  DisableDock = Yes;
  DisableClip = Yes;
  DisableDrawers = Yes;
  NoAutoSnap = Yes;
  SaveSessionOnExit = Yes;
}
EOF
msg -q reconfigure >>"$LOG" 2>&1 || fatal "reconfigure failed"
msg -q exit >>"$LOG" 2>&1
wait_for 80 pid_gone "$WMAKER_PID" || fatal "wmaker did not exit after the save"
if grep -q 'NoMovable\|NoResizable' "$WMSTATE" 2>/dev/null; then
	record_fail "session file carries no lock key"
	grep -n 'NoMovable\|NoResizable' "$WMSTATE" >>"$LOG" 2>&1
else
	record_pass "session file carries no lock key"
fi

note "kill the client so the cold start re-launches it from WMState"
kill -TERM "$XTPID" 2>/dev/null
wait_for 40 pid_gone "$XTPID" || fatal "the original xterm did not go away"

note "cold start: the re-launched window must come back unlocked"
start_wmaker
wait_for 100 lockterm_mapped || fatal "session restore did not re-launch the xterm"
XTW=$(find_lockterm)
[ -n "$XTW" ] || fatal "could not find the relaunched lockterm client window"
RTPID=$(DISPLAY=$DPY xdotool getwindowpid "$XTW" 2>/dev/null)
[ -n "$RTPID" ] && echo "$RTPID" >> "$PIDFILE"
sleep 0.8
G6=$(geom)
xdo windowmove "$XTW" 500 400
sleep 0.6
if [ "$(geom)" != "$G6" ]; then
	record_pass "cold session restore drops the position lock"
else
	record_fail "cold session restore restored the position lock at $(geom)"
fi
G7=$(geom)
xdo windowsize "$XTW" 500 320
sleep 0.6
if [ "$(geom)" != "$G7" ]; then
	record_pass "cold session restore drops the size lock"
else
	record_fail "cold session restore restored the size lock at $(geom)"
fi

note "teardown"
kill -TERM "$WMAKER_PID" 2>/dev/null
wait_for 50 pid_gone "$WMAKER_PID"

printf '\n%d passed, %d failed\n' "$PASSED" "$FAILED" | tee -a "$LOG"
[ "$FAILED" -eq 0 ] || exit 1
echo PASS
exit 0
