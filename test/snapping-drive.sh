#!/bin/sh
# Regression drive test for the WindowSnapping-gated drag-to-edge
# window snapping path: get_snap_direction (moveres.c:1095) and
# do_snap (moveres.c:1123) → handleMaximize (actions.c:678) producing
# half/corner/fullscreen maximize on ButtonRelease.
#
# edge-snap-drive.sh covers the edge-snap IPC *actions*
# (MoveToEdge/GrowToEdge/ShrinkToEdge), edge-finder-drive.sh covers
# the old EdgeResistance/Attraction path (with WindowSnapping=NO).
# Neither reaches the WindowSnapping-gated path exercised here.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${SNAPPING_DRIVE_DISPLAY:-:$(drive_default_display)}
W=1280
H=800
RUNTIME=$(mktemp -d /tmp/snapping-drive.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/snapping-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
WDWRITE="$ROOT/util/.libs/wdwrite"
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

check() {
	cd_desc=$1; shift
	if "$@"; then ok "$cd_desc"; else fail "$cd_desc"; fi
}

wait_for() {
	n=$1; shift
	while [ "$n" -gt 0 ]; do "$@" && return 0; sleep 0.1; n=$((n - 1)); done
	return 1
}
pid_gone() { ! kill -0 "$1" 2>/dev/null; }

msg() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" "$@"
}
ipc_ready() { msg -q get_version >/dev/null 2>&1; }

wdwrite() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		DISPLAY=$DPY "$WDWRITE" "$@"
}

# xdotool with a short settle so wmaker dispatches each event before
# the next one arrives (same race guard as moveres-drive.sh)
xdo() { DISPLAY=$DPY xdotool "$@"; sleep 0.05; }

for tool in Xvfb xdpyinfo xprop xwininfo xdotool xterm; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "wmakermsg not built"
[ -x "$WDWRITE" ] || skip_all "wdwrite not built"

export WMAKER_USER_ROOT="$WMHOME"
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_USER_XDG
unset WMAKER_IPC_SOCKET
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"

# EdgeResistance=0 + Attraction=NO isolates the WindowSnapping path
# from the old edge-resistance/attraction snap covered by
# edge-finder-drive.sh. DontLinkWorkspaces=YES keeps left/right/corner
# snaps (moveres.c:1600 clears them when no_autowrap is false).
# DragMaximizedWindow=Move lets a half-maximized window be dragged
# (staying maximized) so §6 can re-snap it to the opposite edge.
cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  OpaqueMove = YES;
  OpaqueResize = YES;
  EdgeResistance = 0;
  Attraction = NO;
  DontLinkWorkspaces = YES;
  WindowSnapping = YES;
  SnapEdgeDetect = 8;
  SnapCornerDetect = 20;
  SnapToTopMaximizesFullscreen = YES;
  AlternativeHalfMaximized = NO;
  MoveHalfMaximizedWindowsBetweenScreens = NO;
  DragMaximizedWindow = Move;
}
EOF

# --- Xvfb ----------------------------------------------------------------
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set SNAPPING_DRIVE_DISPLAY" >&2
	exit 77
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 ${W}x${H}x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
wait_for 40 sh -c "kill -0 $XVFB_PID 2>/dev/null && DISPLAY=$DPY xdpyinfo >/dev/null 2>&1" \
	|| fatal "Xvfb failed to start"

# --- wmaker ---------------------------------------------------------------
note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	ASAN_OPTIONS="abort_on_error=0:halt_on_error=0:detect_leaks=0" \
	DISPLAY=$DPY \
	"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY XDG_RUNTIME_DIR="$RUNTIME" "$WMAKERMSG" --get-socketpath)
wait_for 80 ipc_ready || fatal "wmaker IPC did not come up"
sleep 0.5

# --- geometry helpers (from moveres-drive.sh + edge-finder-drive.sh) ------
# frame-outer rect for a client XID via window-tree walk
frame_of() {
	DISPLAY=$DPY xwininfo -tree -id "$1" 2>/dev/null \
		| awk '/Parent window id:/ {print $4; exit}'
}
frame_top() {
	ftcur=$1
	ftroot=$(DISPLAY=$DPY xwininfo -root 2>/dev/null \
			| awk '/Window id:/ {print $4; exit}')
	ftn=0
	while [ $ftn -lt 5 ]; do
		ftp=$(frame_of "$ftcur")
		[ -n "$ftp" -a "$ftp" != "0x0" ] || return 1
		[ "$ftp" = "$ftroot" ] && { printf '%s\n' "$ftcur"; return 0; }
		ftcur=$ftp
		ftn=$((ftn + 1))
	done
	return 1
}
# returns "x y w h" for the frame outer rect (border included)
frame_geom() {
	fgframe=$(frame_top "$1") || { echo "0 0 0 0"; return; }
	DISPLAY=$DPY xwininfo -id "$fgframe" 2>/dev/null \
		| awk '/Absolute upper-left X:/ {x=$4}
		       /Absolute upper-left Y:/ {y=$4}
		       /Width:/  && !w {w=$2}
		       /Height:/ && !h {h=$2}
		       /Border width:/ {b=$3}
		       END {print x, y, w+2*b, h+2*b}'
}
# returns "left right top bottom" from _NET_FRAME_EXTENTS
frame_extents() {
	DISPLAY=$DPY xprop -id "$1" _NET_FRAME_EXTENTS \
		| sed 's/.*= //; s/,//g'
}

# _NET_WM_STATE atom presence
has_atom() {
	DISPLAY=$DPY xprop -id "$1" _NET_WM_STATE 2>/dev/null | grep -Fq "$2"
}
lacks_atom() { ! has_atom "$1" "$2"; }

# --- spawn an xterm and return its client XID -----------------------------
XT_PID=
spawn_xt() {
	title=$1; geom=$2
	DISPLAY=$DPY xterm -title "$title" -geometry "$geom" -e 'sleep 300' >>"$LOG" 2>&1 &
	XT_PID=$!
	echo "$XT_PID" >> "$PIDFILE"
	xt_mapped() { [ -n "$(DISPLAY=$DPY xdotool search --name "^$title\$" 2>/dev/null | tail -n 1)" ]; }
	wait_for 60 xt_mapped || fatal "$title did not map"
	sleep 0.2
	DISPLAY=$DPY xdotool search --name "^$title\$" 2>/dev/null | tail -n 1
}
# close an xterm spawned by spawn_xt (WM_DELETE_WINDOW, then SIGTERM)
close_xt() {
	xt_wid=$1; xt_pid=$2
	DISPLAY=$DPY xdotool windowclose "$xt_wid" 2>/dev/null
	sleep 0.2
	kill "$xt_pid" 2>/dev/null
	wait_for 20 pid_gone "$xt_pid" 2>/dev/null
}

# --- drag helper ----------------------------------------------------------
# Grab the titlebar and drag to (tx,ty), ending with several small
# moves at the target so the lagged mouse position is also in the
# snap zone. get_snap_direction reads moveData.mouseX/Y which is one
# event behind: updateWindowPosition (moveres.c:744) runs after the
# snap check (moveres.c:1598), so we need 3+ in-zone motions: 1 to
# arm (exceed MOVE_THRESHOLD=5), 1 to advance the lagged value into
# the zone, 1+ for the snap check to fire on the in-zone value.
drag_snap() {
	ds_xid=$1; ds_tx=$2; ds_ty=$3
	set -- $(frame_geom "$ds_xid"); ds_fx=$1; ds_fy=$2
	set -- $(frame_extents "$ds_xid"); ds_ft=$3
	ds_bx=$((ds_fx + 60))
	ds_by=$((ds_fy + ds_ft/2 - 1))
	xdo mousemove "$ds_bx" "$ds_by"
	xdo mousedown 1
	# big step to arm the drag
	xdo mousemove $((ds_bx + (ds_tx - ds_bx) / 2)) "$ds_ty"
	# move into the zone (advances lagged moveData.mouseX)
	xdo mousemove "$ds_tx" "$ds_ty"
	# stay in zone so snap check fires on the in-zone value
	xdo mousemove "$ds_tx" "$ds_ty"
	xdo mousemove "$ds_tx" "$ds_ty"
	xdo mousemove "$ds_tx" "$ds_ty"
	xdo mouseup 1
	sleep 0.3
}

HALF_X=$((W / 2))    # 640
HALF_Y=$((H / 2))    # 400
SLACK=16            # xterm char-cell rounding + border adj

# =================================================================
# Section 1: drag to left edge → left-half maximize (SNAP_LEFT)
# =================================================================
note "Section 1: left edge → left-half"
W1=$(spawn_xt snap-left 80x12+300+300)
[ -n "$W1" ] || fatal "could not spawn snap-left"

drag_snap "$W1" 3 $((H / 2))

set -- $(frame_geom "$W1"); NX=$1; NY=$2; NW=$3; NH=$4
note "after left-snap: ($NX,$NY ${NW}x${NH})"
check "left-half: x near 0"      [ "$((NX - 0))" -ge $((0 - SLACK)) -a "$((NX - 0))" -le "$SLACK" ]
check "left-half: width near $HALF_X" \
	[ "$((NW - HALF_X))" -ge $((0 - SLACK)) -a "$((NW - HALF_X))" -le "$SLACK" ]
check "left-half: has _NET_WM_STATE_MAXIMIZED_VERT"   has_atom "$W1" _NET_WM_STATE_MAXIMIZED_VERT
check "left-half: no _NET_WM_STATE_MAXIMIZED_HORZ"    lacks_atom "$W1" _NET_WM_STATE_MAXIMIZED_HORZ
check "wmaker alive after left-snap" ipc_ready
close_xt "$W1" "$XT_PID"

# =================================================================
# Section 2: drag to right edge → right-half maximize (SNAP_RIGHT)
# =================================================================
note "Section 2: right edge → right-half"
W2=$(spawn_xt snap-right 80x12+300+300)
[ -n "$W2" ] || fatal "could not spawn snap-right"

drag_snap "$W2" $((W - 3)) $((H / 2))

set -- $(frame_geom "$W2"); NX=$1; NY=$2; NW=$3; NH=$4
note "after right-snap: ($NX,$NY ${NW}x${NH})"
RIGHT_EDGE=$((NX + NW))
check "right-half: x near $HALF_X" \
	[ "$((NX - HALF_X))" -ge $((0 - SLACK)) -a "$((NX - HALF_X))" -le "$SLACK" ]
check "right-half: right edge near $W" \
	[ "$((RIGHT_EDGE - W))" -ge $((0 - SLACK)) -a "$((RIGHT_EDGE - W))" -le "$SLACK" ]
check "right-half: has _NET_WM_STATE_MAXIMIZED_VERT"   has_atom "$W2" _NET_WM_STATE_MAXIMIZED_VERT
check "right-half: no _NET_WM_STATE_MAXIMIZED_HORZ"    lacks_atom "$W2" _NET_WM_STATE_MAXIMIZED_HORZ
check "wmaker alive after right-snap" ipc_ready
close_xt "$W2" "$XT_PID"

# =================================================================
# Section 3: drag to top + SnapToTopMaximizesFullscreen=YES
#            → fullscreen (SNAP_TOP)
# =================================================================
note "Section 3: top edge + fullscreen flag → fullscreen"
W3=$(spawn_xt snap-top-fs 80x12+300+300)
[ -n "$W3" ] || fatal "could not spawn snap-top-fs"

drag_snap "$W3" $((W / 2)) 3

set -- $(frame_geom "$W3"); NX=$1; NY=$2; NW=$3; NH=$4
note "after top-snap (fullscreen): ($NX,$NY ${NW}x${NH})"
RIGHT_EDGE=$((NX + NW))
BOTTOM_EDGE=$((NY + NH))
check "fullscreen: x near 0"     [ "$((NX - 0))" -ge $((0 - SLACK)) -a "$((NX - 0))" -le "$SLACK" ]
check "fullscreen: right edge near $W" \
	[ "$((RIGHT_EDGE - W))" -ge $((0 - SLACK)) -a "$((RIGHT_EDGE - W))" -le "$SLACK" ]
check "fullscreen: has _NET_WM_STATE_MAXIMIZED_VERT"  has_atom "$W3" _NET_WM_STATE_MAXIMIZED_VERT
check "fullscreen: has _NET_WM_STATE_MAXIMIZED_HORZ"  has_atom "$W3" _NET_WM_STATE_MAXIMIZED_HORZ
check "wmaker alive after fullscreen-snap" ipc_ready
close_xt "$W3" "$XT_PID"

# =================================================================
# Section 4: drag to top + SnapToTopMaximizesFullscreen=NO
#            → top-half (SNAP_TOP)
# =================================================================
note "Section 4: top edge + no-fullscreen flag → top-half"
wdwrite WindowMaker SnapToTopMaximizesFullscreen NO
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3

W4=$(spawn_xt snap-top-half 80x12+300+300)
[ -n "$W4" ] || fatal "could not spawn snap-top-half"

drag_snap "$W4" $((W / 2)) 3

set -- $(frame_geom "$W4"); NX=$1; NY=$2; NW=$3; NH=$4
note "after top-snap (half): ($NX,$NY ${NW}x${NH})"
check "top-half: y near 0"  [ "$((NY - 0))" -ge $((0 - SLACK)) -a "$((NY - 0))" -le "$SLACK" ]
check "top-half: has _NET_WM_STATE_MAXIMIZED_HORZ"  has_atom "$W4" _NET_WM_STATE_MAXIMIZED_HORZ
check "top-half: no _NET_WM_STATE_MAXIMIZED_VERT"   lacks_atom "$W4" _NET_WM_STATE_MAXIMIZED_VERT
check "wmaker alive after top-half-snap" ipc_ready
close_xt "$W4" "$XT_PID"

# restore the fullscreen flag for later sections
wdwrite WindowMaker SnapToTopMaximizesFullscreen YES
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3

# =================================================================
# Section 5: corner drags → quarter maximize (SNAP_TOPLEFT / SNAP_BOTTOMRIGHT)
# =================================================================
note "Section 5: corners → quarter maximize"

# top-left corner → (10, 10) is inside SnapCornerDetect=20
W5a=$(spawn_xt snap-tl 80x12+300+300)
[ -n "$W5a" ] || fatal "could not spawn snap-tl"

drag_snap "$W5a" 10 10

set -- $(frame_geom "$W5a"); NX=$1; NY=$2; NW=$3; NH=$4
note "after top-left corner-snap: ($NX,$NY ${NW}x${NH})"
RIGHT_EDGE=$((NX + NW))
BOTTOM_EDGE=$((NY + NH))
check "top-left quarter: x near 0" \
	[ "$((NX - 0))" -ge $((0 - SLACK)) -a "$((NX - 0))" -le "$SLACK" ]
check "top-left quarter: y near 0" \
	[ "$((NY - 0))" -ge $((0 - SLACK)) -a "$((NY - 0))" -le "$SLACK" ]
check "top-left quarter: right edge near $HALF_X" \
	[ "$((RIGHT_EDGE - HALF_X))" -ge $((0 - SLACK)) -a "$((RIGHT_EDGE - HALF_X))" -le "$SLACK" ]
check "top-left quarter: no MAXIMIZED_VERT" lacks_atom "$W5a" _NET_WM_STATE_MAXIMIZED_VERT
check "top-left quarter: no MAXIMIZED_HORZ" lacks_atom "$W5a" _NET_WM_STATE_MAXIMIZED_HORZ
check "wmaker alive after top-left corner-snap" ipc_ready
close_xt "$W5a" "$XT_PID"

# bottom-right corner → (1270, 790) inside SnapCornerDetect=20
W5b=$(spawn_xt snap-br 80x12+300+300)
[ -n "$W5b" ] || fatal "could not spawn snap-br"

drag_snap "$W5b" $((W - 10)) $((H - 10))

set -- $(frame_geom "$W5b"); NX=$1; NY=$2; NW=$3; NH=$4
note "after bottom-right corner-snap: ($NX,$NY ${NW}x${NH})"
RIGHT_EDGE=$((NX + NW))
BOTTOM_EDGE=$((NY + NH))
check "bottom-right quarter: x near $HALF_X" \
	[ "$((NX - HALF_X))" -ge $((0 - SLACK)) -a "$((NX - HALF_X))" -le "$SLACK" ]
check "bottom-right quarter: y near $HALF_Y" \
	[ "$((NY - HALF_Y))" -ge $((0 - SLACK)) -a "$((NY - HALF_Y))" -le "$SLACK" ]
check "bottom-right quarter: right edge near $W" \
	[ "$((RIGHT_EDGE - W))" -ge $((0 - SLACK)) -a "$((RIGHT_EDGE - W))" -le "$SLACK" ]
check "bottom-right quarter: no MAXIMIZED_VERT" lacks_atom "$W5b" _NET_WM_STATE_MAXIMIZED_VERT
check "bottom-right quarter: no MAXIMIZED_HORZ" lacks_atom "$W5b" _NET_WM_STATE_MAXIMIZED_HORZ
check "wmaker alive after bottom-right corner-snap" ipc_ready
close_xt "$W5b" "$XT_PID"

# =================================================================
# Section 6: AlternativeHalfMaximized toggle
#            alt=NO:  left-half → drag right → right-half (normal opposite)
#            alt=YES: left-half → drag right → fullscreen (alt transition)
# =================================================================
note "Section 6: AlternativeHalfMaximized"

# --- 6a: alt=NO → opposite half ---
note "6a: alt=NO → left-half then drag right → right-half"
W6a=$(spawn_xt alt-no 80x12+300+300)
[ -n "$W6a" ] || fatal "could not spawn alt-no"

drag_snap "$W6a" 3 $((H / 2))
set -- $(frame_geom "$W6a"); NX=$1; NY=$2; NW=$3; NH=$4
note "6a after left-snap: ($NX,$NY ${NW}x${NH})"
check "6a left-half: x near 0" \
	[ "$((NX - 0))" -ge $((0 - SLACK)) -a "$((NX - 0))" -le "$SLACK" ]
check "6a left-half: has MAXIMIZED_VERT"  has_atom "$W6a" _NET_WM_STATE_MAXIMIZED_VERT
check "6a left-half: no MAXIMIZED_HORZ"   lacks_atom "$W6a" _NET_WM_STATE_MAXIMIZED_HORZ

# re-grab the (now half-maximized) titlebar and drag to the right edge
drag_snap "$W6a" $((W - 3)) $((H / 2))
set -- $(frame_geom "$W6a"); NX=$1; NY=$2; NW=$3; NH=$4
note "6a after re-snap right: ($NX,$NY ${NW}x${NH})"
RIGHT_EDGE=$((NX + NW))
check "6a right-half: x near $HALF_X" \
	[ "$((NX - HALF_X))" -ge $((0 - SLACK)) -a "$((NX - HALF_X))" -le "$SLACK" ]
check "6a right-half: right edge near $W" \
	[ "$((RIGHT_EDGE - W))" -ge $((0 - SLACK)) -a "$((RIGHT_EDGE - W))" -le "$SLACK" ]
check "6a right-half: has MAXIMIZED_VERT"  has_atom "$W6a" _NET_WM_STATE_MAXIMIZED_VERT
check "6a right-half: no MAXIMIZED_HORZ"   lacks_atom "$W6a" _NET_WM_STATE_MAXIMIZED_HORZ
check "wmaker alive after 6a" ipc_ready
close_xt "$W6a" "$XT_PID"

# --- 6b: alt=YES → fullscreen ---
note "6b: alt=YES → left-half then drag right → fullscreen"
wdwrite WindowMaker AlternativeHalfMaximized YES
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3

W6b=$(spawn_xt alt-yes 80x12+300+300)
[ -n "$W6b" ] || fatal "could not spawn alt-yes"

drag_snap "$W6b" 3 $((H / 2))
set -- $(frame_geom "$W6b"); NX=$1; NY=$2; NW=$3; NH=$4
note "6b after left-snap: ($NX,$NY ${NW}x${NH})"
check "6b left-half: x near 0" \
	[ "$((NX - 0))" -ge $((0 - SLACK)) -a "$((NX - 0))" -le "$SLACK" ]
check "6b left-half: has MAXIMIZED_VERT"  has_atom "$W6b" _NET_WM_STATE_MAXIMIZED_VERT
check "6b left-half: no MAXIMIZED_HORZ"   lacks_atom "$W6b" _NET_WM_STATE_MAXIMIZED_HORZ

# re-snap right → fullscreen (alt transition, actions.c:799-809)
drag_snap "$W6b" $((W - 3)) $((H / 2))
set -- $(frame_geom "$W6b"); NX=$1; NY=$2; NW=$3; NH=$4
note "6b after re-snap right (alt): ($NX,$NY ${NW}x${NH})"
RIGHT_EDGE=$((NX + NW))
check "6b fullscreen: x near 0" \
	[ "$((NX - 0))" -ge $((0 - SLACK)) -a "$((NX - 0))" -le "$SLACK" ]
check "6b fullscreen: right edge near $W" \
	[ "$((RIGHT_EDGE - W))" -ge $((0 - SLACK)) -a "$((RIGHT_EDGE - W))" -le "$SLACK" ]
check "6b fullscreen: has MAXIMIZED_VERT" has_atom "$W6b" _NET_WM_STATE_MAXIMIZED_VERT
check "6b fullscreen: has MAXIMIZED_HORZ" has_atom "$W6b" _NET_WM_STATE_MAXIMIZED_HORZ
check "wmaker alive after 6b" ipc_ready
close_xt "$W6b" "$XT_PID"

# restore alt=NO
wdwrite WindowMaker AlternativeHalfMaximized NO
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3

# =================================================================
# Section 7: WindowSnapping=NO control (no snap fires)
# =================================================================
note "Section 7: WindowSnapping=NO control (no snap)"
wdwrite WindowMaker WindowSnapping NO
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3

W7=$(spawn_xt no-snap 80x12+300+300)
[ -n "$W7" ] || fatal "could not spawn no-snap"

set -- $(frame_geom "$W7"); FX7=$1; FY7=$2; FW7=$3; FH7=$4
drag_snap "$W7" 3 $((H / 2))
set -- $(frame_geom "$W7"); NX=$1; NY=$2; NW=$3; NH=$4
note "after drag-left (snapping off): ($NX,$NY ${NW}x${NH})"
# window should NOT be maximized: width stays at the original size,
# not half-screen, and no maximize atoms present
check "no-snap: width unchanged"      [ "$NW" -eq "$FW7" ]
check "no-snap: not half-width"        [ "$NW" -lt "$HALF_X" ]
check "no-snap: no MAXIMIZED_VERT"  lacks_atom "$W7" _NET_WM_STATE_MAXIMIZED_VERT
check "no-snap: no MAXIMIZED_HORZ"  lacks_atom "$W7" _NET_WM_STATE_MAXIMIZED_HORZ
check "wmaker alive after no-snap" ipc_ready
close_xt "$W7" "$XT_PID"

# re-enable snapping
wdwrite WindowMaker WindowSnapping YES
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3

# --- teardown -------------------------------------------------------------
note "snapping drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 -a "$total" -gt 0 ]
