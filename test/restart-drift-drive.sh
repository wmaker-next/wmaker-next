#!/bin/sh
# Warm-restart window-position drift regression.
#
# Reproduces the bug documented in RESTART_POSITION_DRIFT.md: on a warm
# restart (wmakermsg restart) the in-memory session-state list built by
# wSessionRestoreState() is empty (that path runs only on cold boot), so
# wManageWindow() fell back to wattribs->x/y (the client's current root
# position) as the new frame origin. wWindowConfigure() treats req_y as
# the frame origin, so feeding it the client root y drifted frame_y by
# (top_width + border + inner) every restart. South-gravity clients
# additionally had the gy>0 conversion subtract (top_width + bottom_width)
# from the saved frame y, drifting them upward.
#
# The fix:
#   1. On warm restart, when win_state (session-file channel) is empty
#      but the _WINDOWMAKER_STATE property is present and the window is
#      not maximized, use wstate->x/y (the saved frame origin) as the
#      placement source.
#   2. Skip the gy>0 south-gravity conversion when the position came from
#      a saved-state source (session file or _WINDOWMAKER_STATE), since
#      those are already frame-relative.
#
# The test spawns an xterm (NorthWest gravity) and a synthetic
# ewmh-client (South gravity) at known positions, measures their frame
# origins via _NET_FRAME_EXTENTS, then performs three wmakermsg restart
# cycles and asserts the frame origins stay within 1px of the baseline.
#
# Runs against an isolated WMAKER_USER_ROOT and XDG_RUNTIME_DIR, tracks
# every PID it spawns, the live desktop is untouched.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${RESTART_DRIFT_DRIVE_DISPLAY:-:$(drive_default_display)}
W=1280
H=800
RUNTIME=$(mktemp -d /tmp/restart-drift-drive.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=${RESTART_DRIFT_DRIVE_LOG:-/tmp/restart-drift-drive.log}
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
EWMH_CLIENT="$WMHOME/ewmh-client"
: > "$PIDFILE"
: > "$LOG"

# tolerance for sub-pixel rounding and per-restart dead-space clamps
TOL=1
# how many warm restart cycles to run
RESTARTS=${RESTART_DRIFT_RESTARTS:-3}

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

command -v Xvfb    >/dev/null 2>&1 || skip_all "Xvfb not found"
command -v xdotool >/dev/null 2>&1 || skip_all "xdotool not found"
command -v xterm   >/dev/null 2>&1 || skip_all "xterm not found"
command -v xprop   >/dev/null 2>&1 || skip_all "xprop not found"
command -v xwininfo >/dev/null 2>&1 || skip_all "xwininfo not found"
command -v wmctrl  >/dev/null 2>&1 || skip_all "wmctrl not found"
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

# --- isolated defaults -----------------------------------------------------
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
# SaveSessionOnExit=Yes so the session file is written and would (on cold
# boot) be the restore source. DontMoveOff=Yes both globally and per-app
# for xterm, so the wScreenBringInside() clamp path is on the hot path
# and any frame-border-width delta in the clamp would surface as drift.
cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  SaveSessionOnExit = Yes;
  DontMoveOff = Yes;
  DisableDock = Yes;
  DisableClip = Yes;
  DisableDrawers = Yes;
  NoAutoSnap = Yes;
  IconSize = 32;
}
EOF
cat >"$WMHOME/Defaults/WMWindowAttributes" <<EOF
{
  "xterm.XTerm" = { DontMoveOff = Yes; };
	"notitle.XTerm" = { NoTitlebar = Yes; };
  "EwmhDriveClient.EwmhDriveClient" = { DontMoveOff = Yes; };
  "*" = { NoAppIcon = Yes; };
  DynamicRules = (
    {
      MatchTitle = "^driftnotitle\$";
      MatchClass = "^EwmhDriveClient\$";
      Attributes = { NoTitlebar = YES; };
    }
  );
}
EOF

export WMAKER_USER_ROOT="$WMHOME"
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET WMAKER_USER_XDG

# --- build the synthetic south-gravity client ------------------------------
note "build ewmh-client"
if ! ${CC:-cc} -std=c99 "$ROOT/test/ewmh-client.c" -o "$EWMH_CLIENT" -lX11 >>"$LOG" 2>&1; then
	skip_all "could not build test/ewmh-client.c"
fi

# --- Xvfb ------------------------------------------------------------------
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set RESTART_DRIFT_DRIVE_DISPLAY" >&2
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
start_wmaker() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	ASAN_OPTIONS="abort_on_error=0:halt_on_error=0:detect_leaks=0" \
	DISPLAY=$DPY \
	"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
	WMAKER_PID=$!
	echo "$WMAKER_PID" >> "$PIDFILE"
}
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
# window stacking level from the get_window_tree reply (JSON, id as 0x%lx)
win_layer() {
	wl_id=$(printf '0x%x' "$1")
	msg get_window_tree 2>/dev/null | sed 's/{"class"/\n{"class"/g' \
		| grep -F "\"id\":\"$wl_id\"" \
		| sed -n 's/.*"layer":"\([0-9-]*\)".*/\1/p'
}
win_layer_is() { [ "$(win_layer "$1")" = "$2" ]; }

note "start wmaker"
start_wmaker
wait_for 120 wm_ready || fatal "wmaker did not come up"
wait_for 80 ipc_ready  || fatal "wmaker IPC did not come up"
sleep 0.5

# --- spawn clients at known positions --------------------------------------
# xterm (NorthWest gravity, the reported down-drift case). Park it near
# the bottom of the screen so the dont_move_off clamp is on the path.
note "spawn xterm at +200+500 (NW gravity)"
DISPLAY=$DPY xterm -geometry 80x24+200+500 -title driftterm \
	-e 'sleep 600' >>"$LOG" 2>&1 &
XTPID=$!
echo "$XTPID" >> "$PIDFILE"
wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^driftterm\$' 2>/dev/null | grep -q ." \
	|| fatal "xterm did not map"
XTW=$(DISPLAY=$DPY xdotool search --name '^driftterm$' 2>/dev/null | tail -n 1)
[ -n "$XTW" ] || fatal "could not find driftterm client window"

# synthetic south-gravity client (the reported up-drift case). Position
# it well inside the screen so the dead-space clamp does not fire and
# confound the gravity-conversion check.
note "spawn ewmh-client (South gravity) at +300+200"
EWMH_CLIENT_NAME=driftsouth \
EWMH_CLIENT_CLASS=EwmhDriveClient \
EWMH_CLIENT_INSTANCE=driftsouth \
EWMH_CLIENT_GRAVITY=South \
EWMH_CLIENT_X=300 \
EWMH_CLIENT_Y=200 \
EWMH_CLIENT_WIDTH=320 \
EWMH_CLIENT_HEIGHT=180 \
DISPLAY=$DPY "$EWMH_CLIENT" >>"$LOG" 2>&1 &
EWMHPID=$!
echo "$EWMHPID" >> "$PIDFILE"
wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^driftsouth\$' 2>/dev/null | grep -q ." \
	|| fatal "ewmh-client did not map"
EWMHW=$(DISPLAY=$DPY xdotool search --name '^driftsouth$' 2>/dev/null | tail -n 1)
[ -n "$EWMHW" ] || fatal "could not find driftsouth client window"

# third client: an xterm with the NoTitlebar attribute applied via
# WMWindowAttributes (instance/class "notitle.XTerm").
# This is the regression case for wWindowConfigureBorders(): the frame
# is created with the default top_width (titlebar) and the rule then
# removes it, and the top_width-compensation must not shift the frame
# on the initial setup path. Without the fix the frame drifts down by
# |old top_width| per restart.
note "spawn xterm (NoTitlebar, NW gravity) at +600+200"
DISPLAY=$DPY xterm -name notitle -geometry 60x8+600+200 -title driftnotitle \
	-e 'sleep 600' >>"$LOG" 2>&1 &
NOTITLEPID=$!
echo "$NOTITLEPID" >> "$PIDFILE"
wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^driftnotitle\$' 2>/dev/null | grep -q ." \
	|| fatal "no-titlebar ewmh-client did not map"
NOTITLEW=$(DISPLAY=$DPY xdotool search --name '^driftnotitle$' 2>/dev/null | tail -n 1)
[ -n "$NOTITLEW" ] || fatal "could not find driftnotitle client window"
EWMH_EXT=$(DISPLAY=$DPY xprop -id "$EWMHW" _NET_FRAME_EXTENTS 2>/dev/null \
	| sed 's/.*= //; s/,//g')
EWMH_TOP=$(echo "$EWMH_EXT" | awk '{print $3}')
NOTITLE_EXT=$(DISPLAY=$DPY xprop -id "$NOTITLEW" _NET_FRAME_EXTENTS 2>/dev/null \
	| sed 's/.*= //; s/,//g')
NOTITLE_TOP=$(echo "$NOTITLE_EXT" | awk '{print $3}')
[ -n "${EWMH_TOP:-}" ] && [ -n "${NOTITLE_TOP:-}" ] \
	&& [ "$NOTITLE_TOP" -lt "$EWMH_TOP" ] \
	|| fatal "NoTitlebar rule did not apply (notitle-top=${NOTITLE_TOP:-?} south-top=${EWMH_TOP:-?})"

# --- frame-origin measurement ---------------------------------------------
# frame_geom CLIENT_XID -> prints "FX FY" on stdout, derived from the
# client's absolute root position (xwininfo) minus _NET_FRAME_EXTENTS
# (left, top). xdotool getwindowgeometry double-counts the frame offset
# for a reparented client, so do not use it here.
frame_geom() {
	fg_xid=$1
	fg_client=$(DISPLAY=$DPY xwininfo -id "$fg_xid" 2>/dev/null \
		| awk '/Absolute upper-left X:/ {print $4}
		       /Absolute upper-left Y:/ {print $4}')
	fg_ext=$(DISPLAY=$DPY xprop -id "$fg_xid" _NET_FRAME_EXTENTS 2>/dev/null \
		| sed 's/.*= //; s/,//g')
	[ -n "$fg_client" ] && [ -n "$fg_ext" ] || return 1
	# fg_client is two lines (CX\nCY), split on IFS so $1=CX $2=CY
	set -- $fg_client
	fg_cx=$1
	fg_cy=$2
	# fg_ext is "LEFT RIGHT TOP BOTTOM" on one line
	set -- $fg_ext
	fg_left=$1
	fg_top=$3
	[ -n "$fg_cx" ] && [ -n "$fg_cy" ] && [ -n "$fg_left" ] && [ -n "$fg_top" ] || return 1
	echo $((fg_cx - fg_left)) $((fg_cy - fg_top))
}

measure() {
	XT_FX=""; XT_FY=""; EW_FX=""; EW_FY=""; NT_FX=""; NT_FY=""
	XT_GEOM=$(frame_geom "$XTW")   || return 1
	[ -n "$XT_GEOM" ] || return 1
	set -- $XT_GEOM
	XT_FX=$1; XT_FY=$2
	EW_GEOM=$(frame_geom "$EWMHW") || return 1
	[ -n "$EW_GEOM" ] || return 1
	set -- $EW_GEOM
	EW_FX=$1; EW_FY=$2
	NT_GEOM=$(frame_geom "$NOTITLEW") || return 1
	[ -n "$NT_GEOM" ] || return 1
	set -- $NT_GEOM
	NT_FX=$1; NT_FY=$2
	return 0
}

note "baseline measurement"
wait_for 30 measure || fatal "could not measure baseline frame geometry"
BASE_XT_FX=$XT_FX; BASE_XT_FY=$XT_FY
BASE_EW_FX=$EW_FX; BASE_EW_FY=$EW_FY
BASE_NT_FX=$NT_FX; BASE_NT_FY=$NT_FY
note "baseline: xterm frame=($BASE_XT_FX,$BASE_XT_FY) south-grav frame=($BASE_EW_FX,$BASE_EW_FY) no-title frame=($BASE_NT_FX,$BASE_NT_FY)"

# A helper that asserts the current frame position is within TOL of the
# baseline, separately for X and Y, separately for each client.
assert_no_drift() {
	ad_label=$1; ad_bfx=$2; ad_bfy=$3; ad_cfx=$4; ad_cfy=$5
	ad_dx=$((ad_cfx - ad_bfx))
	ad_dy=$((ad_cfy - ad_bfy))
	[ $((ad_dx < 0 ? -ad_dx : ad_dx)) -le $TOL ] \
		|| { record_fail "$ad_label X drifted by $ad_dx (baseline=$ad_bfx now=$ad_cfx)"; return; }
	[ $((ad_dy < 0 ? -ad_dy : ad_dy)) -le $TOL ] \
		|| { record_fail "$ad_label Y drifted by $ad_dy (baseline=$ad_bfy now=$ad_cfy)"; return; }
	record_pass "$ad_label frame stable at ($ad_cfx,$ad_cfy) (delta $ad_dx,$ad_dy)"
}

# --- per-window stacking level survives a warm restart ---------------------
# Keep on top changes only the frame's stacking level and the checkmark
# derives from that level, so the level is what must round-trip.
note "toggle Keep on top on the NW xterm"
DISPLAY=$DPY wmctrl -i -a "$(printf '0x%x' "$XTW")" >>"$LOG" 2>&1
sleep 0.3
msg action KeepOnTop "$XTW" >>"$LOG" 2>&1 || fatal "Keep on top action failed"
wait_for 30 win_layer_is "$XTW" 3 \
	|| fatal "Keep on top did not set the floating level (got $(win_layer "$XTW"))"
wait_for 30 win_layer_is "$EWMHW" 0 \
	|| fatal "Keep on top changed an unrelated window's level (got $(win_layer "$EWMHW"))"
record_pass "Keep on top sets the floating level on the target only"

# --- warm restart cycles ---------------------------------------------------
cycle=0
while [ "$cycle" -lt "$RESTARTS" ]; do
	cycle=$((cycle + 1))
	note "warm restart cycle $cycle/$RESTARTS"
	msg -q restart >>"$LOG" 2>&1; rc=$?
	if [ "$rc" -ne 0 ]; then
		fatal "wmakermsg restart failed (rc=$rc)"
	fi
	# restart re-execs in place, the old PID file entry is stale, refresh
	# the recorded pid from get_version after IPC re-binds
	wait_for 120 ipc_ready || fatal "IPC did not re-bind after restart cycle $cycle"
	WMAKER_PID=$(msg get_version 2>/dev/null \
		| sed -n 's/.*pid = \([0-9]*\);.*/\1/p')
	[ -n "$WMAKER_PID" ] && echo "$WMAKER_PID" >> "$PIDFILE"
	# give wmaker a moment to re-manage the already-mapped clients and
	# settle their ConfigureNotify round-trips before re-measuring
	sleep 0.5
	wait_for 30 measure || fatal "could not re-measure after restart cycle $cycle"
	note "cycle $cycle: xterm frame=($XT_FX,$XT_FY) south-grav frame=($EW_FX,$EW_FY) no-title frame=($NT_FX,$NT_FY)"
	assert_no_drift "xterm cycle $cycle"      "$BASE_XT_FX" "$BASE_XT_FY" "$XT_FX" "$XT_FY"
	assert_no_drift "south-grav cycle $cycle" "$BASE_EW_FX" "$BASE_EW_FY" "$EW_FX" "$EW_FY"
	assert_no_drift "no-title cycle $cycle"  "$BASE_NT_FX" "$BASE_NT_FY" "$NT_FX" "$NT_FY"
	if win_layer_is "$XTW" 3; then
		record_pass "xterm keeps the floating level across cycle $cycle"
	else
		record_fail "xterm floating level lost in cycle $cycle (got $(win_layer "$XTW"))"
	fi
	if win_layer_is "$EWMHW" 0; then
		record_pass "south-grav level unchanged across cycle $cycle"
	else
		record_fail "south-grav level changed in cycle $cycle (got $(win_layer "$EWMHW"))"
	fi
done

# --- per-window decoration survives a warm restart -------------------------
# The no-title client is live, so dropping the domain rule and warm
# restarting must keep its NoTitlebar via the hint.
note "delete the notitle domain rule, then warm restart"
sed -i '/notitle\.XTerm/d' "$WMHOME/Defaults/WMWindowAttributes"
msg -q restart >>"$LOG" 2>&1; rc=$?
[ "$rc" -eq 0 ] || fatal "wmakermsg restart failed (rc=$rc)"
wait_for 120 ipc_ready || fatal "IPC did not re-bind after the decoration restart"
WMAKER_PID=$(msg get_version 2>/dev/null \
	| sed -n 's/.*pid = \([0-9]*\);.*/\1/p')
[ -n "$WMAKER_PID" ] && echo "$WMAKER_PID" >> "$PIDFILE"
sleep 0.5
NTRW=$(DISPLAY=$DPY xdotool search --name '^driftnotitle$' 2>/dev/null | tail -n 1)
[ -n "$NTRW" ] || fatal "driftnotitle was lost across the decoration restart"
XTRW=$(DISPLAY=$DPY xdotool search --name '^driftterm$' 2>/dev/null | tail -n 1)
[ -n "$XTRW" ] || fatal "driftterm was lost across the decoration restart"
NT_EXT=$(DISPLAY=$DPY xprop -id "$NTRW" _NET_FRAME_EXTENTS 2>/dev/null \
	| sed 's/.*= //; s/,//g')
XT_EXT=$(DISPLAY=$DPY xprop -id "$XTRW" _NET_FRAME_EXTENTS 2>/dev/null \
	| sed 's/.*= //; s/,//g')
set -- $NT_EXT
NT_TOP=$3
set -- $XT_EXT
XT_TOP=$3
[ -n "${NT_TOP:-}" ] && [ -n "${XT_TOP:-}" ] \
	|| fatal "could not read frame extents after the decoration restart"
[ "$NT_TOP" -lt "$XT_TOP" ] \
	|| fatal "NoTitlebar reverted on warm restart (notitle-top=$NT_TOP xterm-top=$XT_TOP)"
record_pass "NoTitlebar survived a warm restart with the domain rule removed"

# --- cold session restore --------------------------------------------------
note "cold session restore"
msg -q exit >>"$LOG" 2>&1 || fatal "wmaker session save failed"
wait_for 50 pid_gone "$WMAKER_PID" || fatal "wmaker did not exit"
kill "$EWMHPID" "$NOTITLEPID" "$XTPID" 2>/dev/null
wait_for 30 pid_gone "$EWMHPID"
wait_for 30 pid_gone "$NOTITLEPID"
wait_for 30 pid_gone "$XTPID"

# The clients are killed and relaunched from the session (the domain rule
# was dropped above), so the relaunched no-title client has no hint and
# re-derives its decoration: the cold-relaunch loss is accepted.
start_wmaker
wait_for 120 wm_ready || fatal "wmaker did not come up for cold restore"
wait_for 80 ipc_ready || fatal "wmaker IPC did not come up for cold restore"
wait_for 120 sh -c "DISPLAY=$DPY xdotool search --name '^driftterm\$' 2>/dev/null | grep -q ." \
	|| fatal "xterm was not restored from the session"
XTW=$(DISPLAY=$DPY xdotool search --name '^driftterm$' 2>/dev/null | tail -n 1)
wait_for 120 sh -c "DISPLAY=$DPY xdotool search --name '^driftnotitle\$' 2>/dev/null | grep -q ." \
	|| fatal "no-titlebar window was not restored from the session"
NOTITLEW=$(DISPLAY=$DPY xdotool search --name '^driftnotitle$' 2>/dev/null | tail -n 1)
XT_GEOM=$(frame_geom "$XTW") || fatal "could not measure cold-restored xterm"
set -- $XT_GEOM
XT_FX=$1; XT_FY=$2
NT_GEOM=$(frame_geom "$NOTITLEW") || fatal "could not measure cold-restored no-titlebar xterm"
set -- $NT_GEOM
NT_FX=$1; NT_FY=$2
XT_EXT=$(DISPLAY=$DPY xprop -id "$XTW" _NET_FRAME_EXTENTS 2>/dev/null \
	| sed 's/.*= //; s/,//g')
NT_EXT=$(DISPLAY=$DPY xprop -id "$NOTITLEW" _NET_FRAME_EXTENTS 2>/dev/null \
	| sed 's/.*= //; s/,//g')
set -- $XT_EXT
XT_TOP=$3
set -- $NT_EXT
NT_TOP=$3
[ "$NT_TOP" -eq "$XT_TOP" ] \
	|| fatal "NoTitlebar unexpectedly restored for a relaunched client (notitle-top=$NT_TOP xterm-top=$XT_TOP)"
record_pass "relaunched client re-derives its decoration (per-window loss accepted)"
assert_no_drift "xterm cold restore" "$BASE_XT_FX" "$BASE_XT_FY" "$XT_FX" "$XT_FY"
assert_no_drift "no-title cold restore" "$BASE_NT_FX" "$BASE_NT_FY" "$NT_FX" "$NT_FY"

# --- teardown --------------------------------------------------------------
note "summary: $PASSED passed, $FAILED failed"
kill "$EWMHPID" "$NOTITLEPID" "$XTPID" 2>/dev/null
wait_for 30 pid_gone "$EWMHPID"
wait_for 30 pid_gone "$NOTITLEPID"
wait_for 30 pid_gone "$XTPID"
msg -q exit >>"$LOG" 2>&1 || true
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] || exit 1
note "PASS"
exit 0
