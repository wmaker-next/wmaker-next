#!/bin/sh
# Drive the dock orientation (old/PLAN-horizontal-dock.md): seed a WMState for
# each edge, flip DockEdge at runtime with wdwrite + SIGUSR2, and assert
# the main tile, dock icons, drawer strip, EWMH strut and icon yard all
# follow the edge. Also covers the upgrade trap: a legacy WMState without
# an Edge key must come back on the right even when the domain says
# otherwise. Runs wmaker under Xvfb on an isolated display and
# WMAKER_USER_ROOT, tracks every PID it spawns and kills only those.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${DOCKORIENT_DRIVE_DISPLAY:-:$(drive_default_display)}
W=1024
H=768
ISZ=64
RUNTIME=$(mktemp -d /tmp/wmaker-dockorient.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=${DOCKORIENT_DRIVE_LOG:-/tmp/dock-orient-drive.log}
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
WDWRITE="$ROOT/util/.libs/wdwrite"
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

msg() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" "$@"
}

ipc_ready() { msg -q get_version >/dev/null 2>&1; }

# ----------------------------------------------------------------------

for tool in Xvfb xdpyinfo xwininfo xprop xdotool; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
[ -x "$WDWRITE" ] || skip_all "util/.libs/wdwrite not built"

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
printf '{\n}\n' > "$WMHOME/Defaults/WindowMaker"

export XDG_RUNTIME_DIR="$RUNTIME"
export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
unset WMAKER_IPC_SOCKET

start_wmaker() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY \
	"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
	WMAKER_PID=$!
	echo "$WMAKER_PID" >> "$PIDFILE"
	wait_for 80 ipc_ready || fatal "wmaker did not answer get_version"
}

stop_wmaker() {
	msg exit >>"$LOG" 2>&1
	wait_for 50 pid_gone "$WMAKER_PID" || fatal "wmaker refused to exit"
}

reload_domain() {
	kill -USR2 "$WMAKER_PID" 2>/dev/null
}

set_edge() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WDWRITE" WindowMaker DockEdge "$1" >>"$LOG" 2>&1 || return 1
	reload_domain
}

tile_at() {
	DISPLAY=$DPY xwininfo -root -children 2>/dev/null \
		| grep -q "64x64+$1+$2 "
}

not_tile_at() {
	! tile_at "$1" "$2"
}

# The appicon popup menu window is created at startup and only mapped
# on the button press, so it is detected as the viewable root child at
# least 100x100. Every xdotool call pins DISPLAY: a bare xdotool would
# follow $DISPLAY and could inject events into the caller's real
# display. The press and release pair leaves the menu closed, so later
# checks start from a clean state.
menu_geom_at() {
	_px=$1
	_py=$2
	menu_xdotool() { DISPLAY=$DPY xdotool "$@"; }
	menu_xdotool mousemove --sync "$_px" "$_py"
	menu_xdotool mousedown 3
	sleep 0.5
	_geom=
	for _id in $(DISPLAY=$DPY xwininfo -root -children 2>/dev/null \
		| sed -n 's/^ *0x\([0-9a-fA-F]*\) .*/\1/p'); do
		_g=$(DISPLAY=$DPY xwininfo -id "0x$_id" -stats 2>/dev/null \
			| awk '/Map State/ { vis=$3 }
			       /Absolute upper-left X/ { x=$4 }
			       /Absolute upper-left Y/ { y=$4 }
			       /Width:/ { w=$2 }
			       /Height:/ { h=$2 }
			       END { if (vis == "IsViewable" && w >= 100 && h >= 100) print w "x" h "+" x "+" y }')
		[ -n "$_g" ] && _geom="$_geom $_g"
	done
	# step clear of the menu before releasing: a release over an item
	# would activate it, and (400, 400) is outside the menu for every
	# edge on this screen
	menu_xdotool mousemove --sync 400 400
	menu_xdotool mouseup 3
	sleep 0.3
	echo "$_geom"
}

menu_field() {
	# WxH+X+Y -> x, y, w or h
	_g=$1
	_f=$2
	_mw=${_g%%x*}
	_rest=${_g#*x}
	_mh=${_rest%%+*}
	_xy=${_rest#*+}
	_mx=${_xy%%+*}
	_my=${_xy#*+}
	case $_f in
	x) echo "$_mx" ;;
	y) echo "$_my" ;;
	w) echo "$_mw" ;;
	h) echo "$_mh" ;;
	esac
}

drag_from_to() {
	_x0=$1; _y0=$2; _x1=$3; _y1=$4
	DISPLAY=$DPY xdotool mousemove --sync "$_x0" "$_y0"
	DISPLAY=$DPY xdotool mousedown 1
	DISPLAY=$DPY xdotool mousemove --sync $(((_x0 + _x1) / 2)) $(((_y0 + _y1) / 2))
	DISPLAY=$DPY xdotool mousemove --sync "$_x1" "$_y1"
	sleep 0.3
	DISPLAY=$DPY xdotool mouseup 1
	sleep 0.5
}

# a minimal seeded WMState for one edge: main tile, one app icon, one
# drawer with a single icon; positions are the screen-axis indices and
# pixel anchor for that edge
write_wmstate() {
	_edge=$1
	_dw=$2
	_dpos=$3
	_main=$4
	_app=$5
	_danchor=$6
	_dicon=$7
	_dcontent=$8
	_extra=$9
	cat > "$WMHOME/Defaults/WMState" <<WMEOD
{
  Dock = {
    Applications = (
      { Command = "-"; Name = Logo.WMDock;
        AutoLaunch = No; Forced = No; Lock = Yes;
        Position = "$_main"; },
      { Command = "/nonexistent-dockorient"; Name = dockorient.DO;
        AutoLaunch = No; Forced = No; Lock = No; BuggyApplication = No;
        Position = "$_app"; }
    );
    Position = "$_dpos";
    Edge = $_edge;
    Lowered = No;
  };
  Drawers = (
    {
      Name = Drawer1;
      Position = "$_danchor";
      Dock = {
        Applications = (
          { Command = "/bin/true"; Name = drawerapp.DO;
            AutoLaunch = No; Forced = No; Lock = No; BuggyApplication = No;
            Position = "$_dcontent"; }
        );
        Collapsed = No;
        AutoCollapse = No;
        AutoAttractIcons = No;
      };
    }
  );
  Workspaces = ( { Name = "Main"; } );
$_extra}
WMEOD
}

# ----------------------------------------------------------------------

note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 ${W}x${H}x24 -ac -noreset >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
	kill -0 "$XVFB_PID" 2>/dev/null || fatal "Xvfb exited early"
	DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fatal "Xvfb failed to start"

SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
case "$SOCKET" in
	"$RUNTIME"/wmaker/ipc.*) ;;
	*) fatal "--get-socketpath returned $SOCKET, refusing" ;;
esac

# ---- assertion 1 and 2: cold start on the right, icon below the tile --

note "cold start on the right edge"
# no Edge key anywhere: the domain default is Right
printf '{\n}\n' > "$WMHOME/Defaults/WindowMaker"
cat > "$WMHOME/Defaults/WMState" <<WMEOD
{
  Dock = {
    Applications = (
      { Command = "-"; Name = Logo.WMDock;
        AutoLaunch = No; Forced = No; Lock = Yes;
        Position = "0,0"; },
      { Command = "/nonexistent-dockorient"; Name = dockorient.DO;
        AutoLaunch = No; Forced = No; Lock = No; BuggyApplication = No;
        Position = "0,1"; }
    );
    Position = "$((W - ISZ)),0";
    Lowered = No;
  };
  Workspaces = ( { Name = "Main"; } );
}
WMEOD

start_wmaker

check "cold start: main tile at the right edge, y = 0" \
	tile_at "$((W - ISZ))" 0
check "cold start: docked icon below the main tile, not beside it" \
	tile_at "$((W - ISZ))" "$ISZ"

# ---- assertion 2b: the appicon popup menu follows the clicked icon -----

note "right-click the docked icon at ($((W - ISZ + 32)), $((ISZ + 32)))"
_g=$(menu_geom_at "$((W - ISZ + 32))" "$((ISZ + 32))")
if [ -z "$_g" ]; then
	record_fail "right edge: no menu mapped after button3 press"
else
	note "menu geometry: $_g"
	check "right edge: menu y tracks the clicked icon" \
		[ "$(menu_field "$_g" y)" -eq $((ISZ + 34)) ]
	check "right edge: menu opens left of the screen edge" \
		[ $(( $(menu_field "$_g" x) + $(menu_field "$_g" w) )) -eq $((W - 3)) ]
fi

# ---- assertion 3: flip to Top at runtime ------------------------------

note "wdwrite DockEdge Top + SIGUSR2"
set_edge Top || fatal "wdwrite DockEdge Top failed"
top_moved() { tile_at 0 0 && tile_at "$ISZ" 0; }
check "top shelf: main tile at (0, 0), along offset kept and clamped" \
	wait_for 50 top_moved
check "top shelf: second icon to the right of the main tile" \
	tile_at "$ISZ" 0

note "right-click the docked icon at ($((ISZ + 32)), 32)"
_g=$(menu_geom_at "$((ISZ + 32))" 32)
if [ -z "$_g" ]; then
	record_fail "top shelf: no menu mapped after button3 press"
else
	note "menu geometry: $_g"
	check "top shelf: menu x tracks the clicked icon" \
		[ "$(menu_field "$_g" x)" -eq $((ISZ + 34)) ]
	check "top shelf: menu top sits below the shelf" \
		[ "$(menu_field "$_g" y)" -eq $((ISZ + 3)) ]
fi

# ---- assertion 4: drawer opens below the shelf ------------------------

note "drawer below the top shelf"
msg exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID" || fatal "wmaker refused to exit"
grep -q 'Edge = Top;' "$WMHOME/Defaults/WMState" \
	&& record_pass "WMState remembers Edge = Top" \
	|| record_fail "WMState remembers Edge = Top"

# along slot 2, content one cell into the screen (below the shelf)
write_wmstate Top "$((W - ISZ))" "0,0" "0,0" "1,0" "128,0" "128,0" "0,1" " "
start_wmaker
check "drawer main tile on the shelf at x = 128" \
	wait_for 50 tile_at 128 0
check "drawer contents below the shelf, x = drawer anchor" \
	tile_at 128 "$ISZ"

# ---- assertion 5: Bottom ----------------------------------------------

note "DockEdge Bottom"
set_edge Bottom || fatal "wdwrite DockEdge Bottom failed"
bottom_ready() { tile_at 0 "$((H - ISZ))" && tile_at "$ISZ" "$((H - ISZ))"; }
check "bottom shelf: main tile at (0, H - ICON), second icon beside it" \
	wait_for 50 bottom_ready
check "bottom shelf: drawer contents above the row" \
	tile_at 128 "$((H - 2 * ISZ))"

note "right-click the docked icon at ($((ISZ + 32)), $((H - ISZ + 32)))"
_g=$(menu_geom_at "$((ISZ + 32))" "$((H - ISZ + 32))")
if [ -z "$_g" ]; then
	record_fail "bottom shelf: no menu mapped after button3 press"
else
	note "menu geometry: $_g"
	check "bottom shelf: menu x tracks the clicked icon" \
		[ "$(menu_field "$_g" x)" -eq $((ISZ + 34)) ]
	check "bottom shelf: menu bottom sits above the shelf" \
		[ $(( $(menu_field "$_g" y) + $(menu_field "$_g" h) )) -eq $((H - ISZ - 3)) ]
fi

# ---- assertion 5b: a drawer slide keeps the vertical column ------------

note "bottom drawer: dragging an icon out keeps the column vertical"
msg exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID" || fatal "wmaker refused to exit"
# drawer at along slot 2 with three contents stacked into the screen;
# the restore re-signs the stored magnitudes against the current edge
cat > "$WMHOME/Defaults/WMState" <<WMEOD
{
  Dock = {
    Applications = (
      { Command = "-"; Name = Logo.WMDock;
        AutoLaunch = No; Forced = No; Lock = Yes;
        Position = "0,0"; },
      { Command = "/nonexistent-dockorient"; Name = dockorient.DO;
        AutoLaunch = No; Forced = No; Lock = No; BuggyApplication = No;
        Position = "1,0"; }
    );
    Position = "0,$((H - ISZ))";
    Edge = Bottom;
    Lowered = No;
  };
  Drawers = (
    {
      Name = Drawer1;
      Position = "128,$((H - ISZ))";
      Dock = {
        Applications = (
          { Command = "/bin/true"; Name = drawerapp1.DO;
            AutoLaunch = No; Forced = No; Lock = No; BuggyApplication = No;
            Position = "0,-1"; },
          { Command = "/bin/true"; Name = drawerapp2.DO;
            AutoLaunch = No; Forced = No; Lock = No; BuggyApplication = No;
            Position = "0,-2"; },
          { Command = "/bin/true"; Name = drawerapp3.DO;
            AutoLaunch = No; Forced = No; Lock = No; BuggyApplication = No;
            Position = "0,-3"; }
        );
        Collapsed = No;
        AutoCollapse = No;
        AutoAttractIcons = No;
      };
    }
  );
  Workspaces = ( { Name = "Main"; } );
}
WMEOD
start_wmaker
drawer_column_ready() {
	tile_at 128 "$((H - ISZ))" &&
	tile_at 128 "$((H - 2 * ISZ))" &&
	tile_at 128 "$((H - 3 * ISZ))" &&
	tile_at 128 "$((H - 4 * ISZ))"
}
check "drawer column: main tile on the shelf, three contents above" \
	wait_for 50 drawer_column_ready

# drag the innermost content icon onto the desktop; leaving the drawer
# must slide the two remaining icons down one row each, along y
drag_from_to 160 "$((H - 2 * ISZ + 32))" 500 300
check "drawer gap filled: second content slid down to the first slot" \
	wait_for 50 tile_at 128 "$((H - 2 * ISZ))"
check "drawer gap filled: third content slid down one row" \
	tile_at 128 "$((H - 3 * ISZ))"
check "drawer gap filled: no tile left in the outermost slot" \
	not_tile_at 128 "$((H - 4 * ISZ))"
check "drawer slide stayed on the y axis: no tile beside the column" \
	not_tile_at 192 "$((H - 3 * ISZ))"

# a click on the drawer tile runs the release slide with zero distance;
# the column must stay where it is
click_kept_column() {
	tile_at 128 "$((H - 2 * ISZ))" && tile_at 128 "$((H - 3 * ISZ))"
}
DISPLAY=$DPY xdotool mousemove --sync 160 "$((H - ISZ + 32))"
DISPLAY=$DPY xdotool click 1
sleep 0.5
check "click on the drawer tile keeps the column in place" \
	wait_for 30 click_kept_column

# ---- assertion 6: Left -------------------------------------------------

note "DockEdge Left"
set_edge Left || fatal "wdwrite DockEdge Left failed"
left_ready() { tile_at 0 0 && tile_at 0 "$ISZ"; }
check "left column: main tile at (0, 0), second icon below it" \
	wait_for 50 left_ready
check "left column: drawer contents to the right of the column" \
	tile_at "$ISZ" "$((2 * ISZ))"

note "right-click the docked icon at (32, $((ISZ + 32)))"
_g=$(menu_geom_at 32 "$((ISZ + 32))")
if [ -z "$_g" ]; then
	record_fail "left column: no menu mapped after button3 press"
else
	note "menu geometry: $_g"
	check "left column: menu y tracks the clicked icon" \
		[ "$(menu_field "$_g" y)" -eq $((ISZ + 34)) ]
	check "left column: menu opens right of the column" \
		[ "$(menu_field "$_g" x)" -eq $((ISZ + 3)) ]
fi

# ---- assertion 7: the edge survives restarts ---------------------------

note "restart on Left; the domain must not pull the dock back"
msg exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID" || fatal "wmaker refused to exit"
grep -q 'Edge = Left;' "$WMHOME/Defaults/WMState" \
	&& record_pass "WMState remembers Edge = Left" \
	|| record_fail "WMState remembers Edge = Left"
# the domain still says Left from the wdwrite above; rewrite it to Right
# so the restart proves WMState wins over the domain default
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	"$WDWRITE" WindowMaker DockEdge Right >>"$LOG" 2>&1
start_wmaker
check "restart on Left with domain Right: dock stays left" \
	wait_for 50 left_ready

# ---- assertion 8: the EWMH strut ---------------------------------------

note "NoWindowOverDock publishes the strut on the info window"
set_edge Top || fatal "wdwrite DockEdge Top failed"
check "back on the top shelf" wait_for 50 top_moved

info_win() {
	DISPLAY=$DPY xwininfo -root -children 2>/dev/null \
		| awk '/10x10\+0\+0/ {print $1}' | head -1
}
INFOWIN=$(info_win)
[ -n "$INFOWIN" ] || fatal "could not find the info window"

strut_is() {
	propname=$1
	want=$2
	DISPLAY=$DPY xprop -id "$INFOWIN" "$propname" 2>/dev/null | grep -qF -- "$want"
}

strut_absent() {
	DISPLAY=$DPY xprop -id "$INFOWIN" "$1" 2>/dev/null | grep -qE 'not found|no such atom'
}

LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	"$WDWRITE" WindowMaker NoWindowOverDock YES >>"$LOG" 2>&1
reload_domain
# Top on a 1024x768 root: top thickness 64, the top start/end span
# covers the head; the unused left/right pairs carry no span
check "_NET_WM_STRUT_PARTIAL published with top thickness ICON_SIZE" \
	wait_for 50 strut_is _NET_WM_STRUT_PARTIAL \
		  '0, 0, 64, 0, 0, 0, 0, 0, 0, 1023, 0, 0'
check "legacy _NET_WM_STRUT carries the same thicknesses" \
	strut_is _NET_WM_STRUT '0, 0, 64, 0'

LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	"$WDWRITE" WindowMaker NoWindowOverDock NO >>"$LOG" 2>&1
reload_domain
check "NoWindowOverDock NO deletes the strut properties" \
	wait_for 50 strut_absent _NET_WM_STRUT_PARTIAL

# ---- assertion 9: dock-launch uses the along slot ----------------------

note "dock-launch on the along axis"
EVLOG="$RUNTIME/events"
: > "$EVLOG"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	"$WMAKERMSG" -s "$SOCKET" -m child_exit tick > "$EVLOG" 2>>"$LOG" &
MON_PID=$!
echo "$MON_PID" >> "$PIDFILE"
wait_for 50 grep -q "first = YES" "$EVLOG" \
	|| fatal "monitor did not see first tick"

msg -q dock-launch 1 >>"$LOG" 2>&1
check "dock-launch 1 accepted on the top shelf" [ $? -eq 0 ]
check "child_exit reports the along slot 1" \
	wait_for 100 grep -q "slot = 1" "$EVLOG"
check "child_exit source is the dock" \
	grep -q "source = dock" "$EVLOG"
kill "$MON_PID" 2>/dev/null
wait_for 30 pid_gone "$MON_PID"

# ---- assertion 10: the upgrade trap ------------------------------------

note "legacy WMState without Edge comes back on the right"
msg exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID" || fatal "wmaker refused to exit"
# domain says Left from an earlier wdwrite in the worst case
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	"$WDWRITE" WindowMaker DockEdge Left >>"$LOG" 2>&1
cat > "$WMHOME/Defaults/WMState" <<WMEOD
{
  Dock = {
    Applications = (
      { Command = "-"; Name = Logo.WMDock;
        AutoLaunch = No; Forced = No; Lock = Yes;
        Position = "0,0"; },
      { Command = "/bin/true"; Name = dockorient.DO;
        AutoLaunch = No; Forced = No; Lock = No; BuggyApplication = No;
        Position = "0,1"; }
    );
    Position = "$((W - ISZ)),0";
    Lowered = No;
  };
  Workspaces = ( { Name = "Main"; } );
}
WMEOD
start_wmaker
check "no Edge key: the X-midpoint test keeps the dock on the right" \
	wait_for 50 tile_at "$((W - ISZ))" 0
check "no Edge key: the second icon is below the main tile" \
	tile_at "$((W - ISZ))" "$ISZ"

# ---- assertion 11: the icon yard follows the dock edge -----------------

note "icon yard under Bottom and Top"
CLIENT="$RUNTIME/ewmh-client"
${CC:-cc} -std=gnu11 "$ROOT/test/ewmh-client.c" -o "$CLIENT" -lX11 >>"$LOG" 2>&1 \
	|| skip_all "could not build test/ewmh-client.c"

set_edge Bottom || fatal "wdwrite DockEdge Bottom failed"
bottom_moved() { tile_at 0 "$((H - ISZ))"; }
check "bottom shelf ready for the yard check" wait_for 50 bottom_moved

window_managed() {
	msg get_windows 2>/dev/null | grep -q "class = $1"
}

iconify_client() {
	CLS=$1
	EWMH_CLIENT_NAME="$CLS-window" EWMH_CLIENT_CLASS="$CLS" \
		EWMH_CLIENT_INSTANCE="$CLS" \
		LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		DISPLAY=$DPY "$CLIENT" </dev/null >>"$LOG" 2>&1 &
	CPID=$!
	echo "$CPID" >> "$PIDFILE"
	wait_for 50 window_managed "$CLS" || return 1
	CLIENT_XID=$(msg get_windows 2>/dev/null \
		| sed -n "/class = $CLS/{s/.*id = \(0x[0-9a-f]*\).*/\1/p;q;}")
	[ -n "$CLIENT_XID" ] || return 1
	msg -q action Miniaturize "$CLIENT_XID" >>"$LOG" 2>&1
}

iconify_client yardone || fatal "yard client did not map"
# the miniwindow rides one row above the bottom dock strip (the only
# free 64x64 cell in the bottom-left yard is (0, H - 2 * ICON))
check "miniwindow placed one row above the bottom dock" \
	wait_for 50 tile_at 0 "$((H - 2 * ISZ))"

set_edge Top || fatal "wdwrite DockEdge Top failed"
check "back on the top shelf for the yard check" wait_for 50 top_moved
iconify_client yardtwo || fatal "second yard client did not map"
# under a top dock the default bottom-left yard keeps the plain bottom
# row, which is exactly where the new miniwindow lands
check "miniwindow lands on the plain bottom row under a top dock" \
	wait_for 50 tile_at 0 "$((H - ISZ))"

# ---- dragging the dock along a side edge --------------------------------

note "drag the dock to the middle of the side edge"
# the main tile sits at (0, 0) on the top shelf; grab it at its centre
# and drop it half way down the left edge. Before the along-axis slide
# this either snapped back to a corner or refused to move. Every call
# pins DISPLAY: a bare xdotool would follow $DISPLAY and could inject
# events into the caller's real display.
drag_xdotool() { DISPLAY=$DPY xdotool "$@"; }
drag_xdotool mousemove --sync 32 32
sleep 0.3
drag_xdotool mousedown 1
# the first motion past the threshold only arms the pointer grab, the
# dock starts following the pointer from a later motion on; spread the
# moves out so the server cannot coalesce them into the arming event
drag_xdotool mousemove --sync 32 96
sleep 0.5
drag_xdotool mousemove --sync 32 160
sleep 0.5
drag_xdotool mousemove --sync 16 384
wait_for 50 tile_at 0 352 || true
drag_xdotool mouseup 1
check "dock dragged to the middle of the left edge" \
	wait_for 50 tile_at 0 352

msg exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID" || fatal "wmaker refused to exit"
check "dragged edge remembered in WMState" \
	grep -q 'Edge = Left;' "$WMHOME/Defaults/WMState"
check "mid-side position saved in WMState" \
	grep -qF 'Position = "0,352";' "$WMHOME/Defaults/WMState"

# ---- the along offset rescales to the new side ---------------------------

note "the along offset scales with the new side"
# park the dock 320 px down the left edge with a drawer two cells
# along. Switching to the bottom edge maps the offset onto the longer
# side proportionally: 320 of 576 usable pixels becomes 462 of 832, so
# the whole run stays on screen instead of hanging off the corner.
write_wmstate Left 960 "0,320" "0,0" "0,1" "0,448" "0,448" "1,0" " "
start_wmaker
check "parked dock loads 320 px down the left edge" \
	wait_for 50 tile_at 0 320
check "drawer anchor two cells below the main tile" \
	wait_for 50 tile_at 0 448
set_edge Bottom || fatal "wdwrite DockEdge Bottom failed"
check "offset rescaled onto the bottom edge" \
	wait_for 50 tile_at 462 704
check "second icon beside the rescaled main tile" \
	wait_for 50 tile_at 526 704
check "drawer kept its offset from the dock" \
	wait_for 50 tile_at 590 704
msg exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID" || fatal "wmaker refused to exit"

# ---- a layout wider than the new side compresses -------------------------

note "a far-flung icon compresses onto the shorter side"
# the dock sits at the left end of the bottom edge with an app icon
# parked at the far right (cell 15 of 16). Moving everything to the
# left edge (12 cells) must compress the along offsets, not push the
# far icon off screen: it lands on the last cell of the side.
write_wmstate Bottom 960 "0,704" "0,0" "15,0" "896,0" "896,0" "0,1" " "
start_wmaker
check "far icon parked at the right end of the bottom edge" \
	wait_for 50 tile_at 960 704
check "drawer parked next to the far icon" \
	wait_for 50 tile_at 896 704
set_edge Left || fatal "wdwrite DockEdge Left failed"
check "main tile back at the top of the left edge" \
	wait_for 50 tile_at 0 0
check "far icon compressed onto the left edge" \
	wait_for 50 tile_at 0 704
check "drawer compressed with the layout" \
	wait_for 50 tile_at 0 640
msg exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID" || fatal "wmaker refused to exit"

# ---- an edge move with wrap on keeps the slot order ----------------------

note "a bottom to right drag does not let the wrap pass reorder icons"
# With WrapAppiconsInDock on (the default), the drag clamps the along
# axis to the live run so the transposed layout cannot overflow and
# let the wrap pass cycle icons to the far end. DisableClip keeps the
# tile census free of the workspace clips at (0, 0).
LD_LIBRARY_PATH="$WMLIBS" "$WDWRITE" WindowMaker DisableClip YES >>"$LOG" 2>&1
cat > "$WMHOME/Defaults/WMState" <<WMEOD
{
  Dock = {
    Applications = (
      { Command = "-"; Name = Logo.WMDock;
        AutoLaunch = No; Forced = No; Lock = Yes;
        Position = "0,0"; },
      { Command = "/bin/true"; Name = wrapone.DO;
        AutoLaunch = No; Forced = No; Lock = No; BuggyApplication = No;
        Position = "2,0"; },
      { Command = "/bin/true"; Name = wraptwo.DO;
        AutoLaunch = No; Forced = No; Lock = No; BuggyApplication = No;
        Position = "4,0"; }
    );
    Position = "0,$((H - ISZ))";
    Edge = Bottom;
    Lowered = No;
  };
  Workspaces = ( { Name = "Main"; } );
}
WMEOD
start_wmaker
check "wrap drag: seeded on the bottom shelf" \
	wait_for 50 tile_at "$((2 * ISZ))" "$((H - ISZ))"
drag_from_to 32 "$((H - ISZ + 32))" 980 620
# the transposed run is 5 cells: the main tile clamps at H - 5 * ISZ and
# the two apps keep slots 2 and 4 below it, blanks at 1 and 3
check "wrap drag: main tile clamped with the whole run on screen" \
	wait_for 50 tile_at "$((W - ISZ))" "$((H - 5 * ISZ))"
check "wrap drag: first app kept slot 2 below the main tile" \
	tile_at "$((W - ISZ))" "$((H - 3 * ISZ))"
check "wrap drag: second app kept slot 4 below the main tile" \
	tile_at "$((W - ISZ))" "$((H - ISZ))"
check "wrap drag: the blanks survived, no tile at slot 1" \
	not_tile_at "$((W - ISZ))" "$((H - 4 * ISZ))"
check "wrap drag: the blanks survived, no tile at slot 3" \
	not_tile_at "$((W - ISZ))" "$((H - 2 * ISZ))"
msg exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID" || fatal "wmaker refused to exit"

# ---- wrap off restores the free slide along the edge ---------------------

note "wrap off lets the drag push the tail past the screen edge"
# WrapAppiconsInDock = NO means leave icons off screen, so the drag is
# not clamped to the live run. Same gesture as the wrap drag above, the
# release at (980, 620) lands the main tile at 620 - 32 (the grab
# offset) and the apps at slots 2 and 4 below it, slot 4 past the edge.
LD_LIBRARY_PATH="$WMLIBS" "$WDWRITE" WindowMaker WrapAppiconsInDock NO >>"$LOG" 2>&1
cat > "$WMHOME/Defaults/WMState" <<WMEOD
{
  Dock = {
    Applications = (
      { Command = "-"; Name = Logo.WMDock;
        AutoLaunch = No; Forced = No; Lock = Yes;
        Position = "0,0"; },
      { Command = "/bin/true"; Name = freeone.DO;
        AutoLaunch = No; Forced = No; Lock = No; BuggyApplication = No;
        Position = "2,0"; },
      { Command = "/bin/true"; Name = freetwo.DO;
        AutoLaunch = No; Forced = No; Lock = No; BuggyApplication = No;
        Position = "4,0"; }
    );
    Position = "0,$((H - ISZ))";
    Edge = Bottom;
    Lowered = No;
  };
  Workspaces = ( { Name = "Main"; } );
}
WMEOD
start_wmaker
check "free slide: seeded on the bottom shelf" \
	wait_for 50 tile_at "$((2 * ISZ))" "$((H - ISZ))"
drag_from_to 32 "$((H - ISZ + 32))" 980 620
check "free slide: main tile follows the pointer below the clamp spot" \
	wait_for 50 tile_at "$((W - ISZ))" 588
check "free slide: the clamp spot 5 slots up is empty" \
	not_tile_at "$((W - ISZ))" "$((H - 5 * ISZ))"
check "free slide: first app kept slot 2 below the main tile" \
	tile_at "$((W - ISZ))" 716
check "free slide: second app slid past the edge at slot 4" \
	tile_at "$((W - ISZ))" 844
check "free slide: the blanks survived, no tile at slot 1" \
	not_tile_at "$((W - ISZ))" 652
check "free slide: the blanks survived, no tile at slot 3" \
	not_tile_at "$((W - ISZ))" 780
msg exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID" || fatal "wmaker refused to exit"
LD_LIBRARY_PATH="$WMLIBS" "$WDWRITE" WindowMaker WrapAppiconsInDock YES >>"$LOG" 2>&1

# ---- a same-edge slide with wrap on cycles the icons back ----------------

note "wrap on wraps a slid-off tail back into the visible run"
# A same-edge slide is free in both wrap modes. With wrap on the icons
# that pass the end of the edge wrap to the far end of the run instead
# of staying off screen. DisableClip is still YES.
cat > "$WMHOME/Defaults/WMState" <<WMEOD
{
  Dock = {
    Applications = (
      { Command = "-"; Name = Logo.WMDock;
        AutoLaunch = No; Forced = No; Lock = Yes;
        Position = "0,0"; },
      { Command = "/bin/true"; Name = cycleone.DO;
        AutoLaunch = No; Forced = No; Lock = No; BuggyApplication = No;
        Position = "0,2"; },
      { Command = "/bin/true"; Name = cycletwo.DO;
        AutoLaunch = No; Forced = No; Lock = No; BuggyApplication = No;
        Position = "0,4"; }
    );
    Position = "$((W - ISZ)),0";
    Edge = Right;
    Lowered = No;
  };
  Workspaces = ( { Name = "Main"; } );
}
WMEOD
start_wmaker
check "cycle: seeded down the right edge" \
	wait_for 50 tile_at "$((W - ISZ))" "$((2 * ISZ))"
drag_from_to "$((W - ISZ + 32))" 32 "$((W - 8))" 736
check "cycle: main tile slid to the bottom of the right edge" \
	wait_for 50 tile_at "$((W - ISZ))" "$((H - ISZ))"
check "cycle: first app wrapped back to the top of the run" \
	tile_at "$((W - ISZ))" "$ISZ"
check "cycle: second app wrapped back below it" \
	tile_at "$((W - ISZ))" "$((3 * ISZ))"
check "cycle: nothing left past the bottom edge" \
	not_tile_at "$((W - ISZ))" "$((H + ISZ))"
msg exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID" || fatal "wmaker refused to exit"

# ---- wrap on pulls a half-hidden icon back into the run ------------------

note "wrap on recovers an icon stuck half past the edge"
# The wrap target of an icon in the partial band at the edge is itself,
# so it snaps to the far end of the run. The same seed and a shorter
# slide leaves the first app half past the bottom edge at 588 + 2 * ISZ.
cat > "$WMHOME/Defaults/WMState" <<WMEOD
{
  Dock = {
    Applications = (
      { Command = "-"; Name = Logo.WMDock;
        AutoLaunch = No; Forced = No; Lock = Yes;
        Position = "0,0"; },
      { Command = "/bin/true"; Name = partialone.DO;
        AutoLaunch = No; Forced = No; Lock = No; BuggyApplication = No;
        Position = "0,2"; },
      { Command = "/bin/true"; Name = partialtwo.DO;
        AutoLaunch = No; Forced = No; Lock = No; BuggyApplication = No;
        Position = "0,4"; }
    );
    Position = "$((W - ISZ)),0";
    Edge = Right;
    Lowered = No;
  };
  Workspaces = ( { Name = "Main"; } );
}
WMEOD
start_wmaker
check "partial: seeded down the right edge" \
	wait_for 50 tile_at "$((W - ISZ))" "$((2 * ISZ))"
drag_from_to "$((W - ISZ + 32))" 32 "$((W - 8))" 620
check "partial: main tile stopped at 620 - 32" \
	wait_for 50 tile_at "$((W - ISZ))" 588
check "partial: half-hidden app wrapped to the top of the run" \
	tile_at "$((W - ISZ))" 12
check "partial: second app wrapped back at 76" \
	tile_at "$((W - ISZ))" 76
check "partial: nothing left past the bottom edge" \
	not_tile_at "$((W - ISZ))" 716
msg exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID" || fatal "wmaker refused to exit"

# ---- compression never stacks two icons on one cell ----------------------

note "a compressed layout keeps one cell per icon"
# 15 icons on a 16-cell bottom edge moving to the 12-cell left edge:
# the layout compresses, but two icons must never share a cell, the
# tail ends past the visible run. DisableClip is still YES from the
# wrap drag section, so the tile census sees dock icons only.
APPS=""
i=1
while [ "$i" -le 14 ]; do
	[ "$i" -gt 1 ] && APPS="$APPS,"
	APPS="$APPS
      { Command = \"/bin/true\"; Name = pack$i.DO;
        AutoLaunch = No; Forced = No; Lock = No; BuggyApplication = No;
        Position = \"$i,0\"; }"
	i=$((i + 1))
done
# the domain already says Left from the compression section above, a
# same-value wdwrite would not register as a config change
LD_LIBRARY_PATH="$WMLIBS" "$WDWRITE" WindowMaker DockEdge Bottom >>"$LOG" 2>&1
cat > "$WMHOME/Defaults/WMState" <<WMEOD
{
  Dock = {
    Applications = (
      { Command = "-"; Name = Logo.WMDock;
        AutoLaunch = No; Forced = No; Lock = Yes;
        Position = "0,0"; },
$APPS
    );
    Position = "0,$((H - ISZ))";
    Edge = Bottom;
    Lowered = No;
  };
  Workspaces = ( { Name = "Main"; } );
}
WMEOD
start_wmaker
check "pack: seeded on the bottom shelf" \
	wait_for 50 tile_at "$((14 * ISZ))" "$((H - ISZ))"
set_edge Left || fatal "wdwrite DockEdge Left failed"
check "pack: main tile at the top of the left edge" \
	wait_for 50 tile_at 0 0
check "pack: icons keep their order down the column" \
	wait_for 50 tile_at 0 "$((11 * ISZ))"
check "pack: the tail sits past the visible run, not stacked on it" \
	wait_for 50 tile_at 0 "$((13 * ISZ))"
check "pack: nothing beyond the last icon" \
	not_tile_at 0 "$((15 * ISZ))"
msg exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID" || fatal "wmaker refused to exit"

note "dock orient drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
[ "$FAILED" -eq 0 ] && [ "$PASSED" -gt 0 ]
