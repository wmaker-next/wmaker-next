#!/bin/sh
# Clip Keep on top restore and the shared clip tile.
#
# The clip tile window is shared by every workspace's clip while each
# clip stores its own Keep on top (Lowered) state. Restoring a clip with
# Keep on top must put the tile at the dock level together with its
# attached icons, so an ordinary window overlapping the clip group is
# covered by all of it and not only by the attached icons. Since one
# tile is shared, its level must also follow the clip of the workspace
# that is actually visible after the restore and after every switch.
#
# Seeded WMState files place the clip tile at 200,200 with attached
# launcher icons at 264,200 and 328,200 (IconSize 64); a probe xterm is
# mapped over all of them and the real stacking order is read from the
# root children list (xwininfo prints it top to bottom).
#
# Runs under Xvfb on an isolated display and WMAKER_USER_ROOT, tracks
# every PID it spawns and kills only those.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${CLIPONTOP_DRIVE_DISPLAY:-:$(drive_default_display)}
W=1280
H=800
ISZ=64
RUNTIME=$(mktemp -d /tmp/wmaker-clipontop.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=${CLIPONTOP_DRIVE_LOG:-/tmp/clip-ontop-drive.log}
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
SOCKET=

TILE_GEOM="$ISZ"x"$ISZ"+200+200
ICON1_GEOM="$ISZ"x"$ISZ"+264+200
ICON2_GEOM="$ISZ"x"$ISZ"+328+200

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

ws_current() {
	msg get_workspaces 2>/dev/null | tr '}' '\n' \
		| grep 'current = YES' \
		| sed -n 's/.*index = \([0-9][0-9]*\).*/\1/p'
}

ws_current_is() { [ "$(ws_current)" = "$1" ]; }

# XID of the root child whose listing carries the given "WxH+X+Y" key
win_id_at_geom() {
	DISPLAY=$DPY xwininfo -root -children 2>/dev/null \
		| awk -v g=" $1 " 'index($0, g) { print $1; exit }'
}

# frame XID of a managed client XID: the client is reparented in the
# frame, so walk the root tree up from the client to its parent
client_frame() {
	cf_hex=$(printf '0x%x' "$1")
	DISPLAY=$DPY xwininfo -root -tree 2>/dev/null | awk -v w="$cf_hex" '
		$1 ~ /^0x[0-9a-fA-F]+$/ {
			id = tolower($1)
			ind = match($0, /[^ ]/) - 1
			while (n > 0 && st_ind[n] >= ind) n--
			if (id == w) {
				if (n > 0) print st_id[n]
				exit
			}
			n++
			st_ind[n] = ind
			st_id[n] = id
		}'
}

# 1-based position from the top of the root children list (xwininfo
# lists them top to bottom, so a smaller number is higher in the stack)
stack_pos() {
	sp_i=0
	for sp_id in $(DISPLAY=$DPY xwininfo -root -children 2>/dev/null \
			| sed -n 's/^ *\(0x[0-9a-fA-F]*\) .*/\1/p'); do
		sp_i=$((sp_i + 1))
		if [ "$sp_id" = "$1" ]; then
			echo "$sp_i"
			return 0
		fi
	done
	return 1
}

above() {
	ab_a=$(stack_pos "$1") || return 1
	ab_b=$(stack_pos "$2") || return 1
	[ "$ab_a" -lt "$ab_b" ]
}

# map a probe xterm over the clip group, its frame XID lands in PROBE_FRAME
PROBE_FRAME=
spawn_probe() {
	pr_name=$1
	DISPLAY=$DPY xterm -geometry 40x12+210+210 -title "$pr_name" \
		-e 'sleep 600' >>"$LOG" 2>&1 &
	echo $! >> "$PIDFILE"
	wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^$pr_name\$' 2>/dev/null | grep -q ." \
		|| fatal "probe $pr_name did not map"
	pr_client=$(DISPLAY=$DPY xdotool search --name "^$pr_name$" 2>/dev/null | tail -n 1)
	[ -n "$pr_client" ] || fatal "no client for probe $pr_name"
	PROBE_FRAME=$(client_frame "$pr_client")
	[ -n "$PROBE_FRAME" ] || fatal "no frame for probe $pr_name"
}

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

# one clip tile at 200,200 plus $1 attached launcher icons, Keep on top
# driven by $2 (No keeps the clip on top); $3 optionally names a second
# workspace with the opposite state and $4 the restored workspace name
write_wmstate() {
	w_entries=$1
	w_lowered=$2
	w_extra_ws=$3
	w_last_ws=$4
	w_extra_sep=
	[ -z "$w_extra_ws" ] || w_extra_sep=,
	w_apps=''
	w_i=1
	while [ "$w_i" -le "$w_entries" ]; do
		[ -z "$w_apps" ] || w_apps="$w_apps,"
		w_apps="$w_apps
        { Command = \"/nonexistent-clipontop$w_i\"; Name = clipontop$w_i.CT$w_i;
          AutoLaunch = No; Forced = No; Lock = No; BuggyApplication = No;
          Position = \"$w_i,0\"; }"
		w_i=$((w_i + 1))
	done
	cat > "$WMHOME/Defaults/WMState" <<WMEOD
{
  Clip = {
    Command = "-"; Name = Logo.WMClip;
    AutoLaunch = No; Forced = No; Lock = Yes;
    Position = "200,200";
  };
  Workspace = "$w_last_ws";
  Workspaces = (
    {
      Name = "One";
      Clip = {
        Applications = ($w_apps
        );
        Lowered = $w_lowered;
      };
    }$w_extra_sep$w_extra_ws
  );
}
WMEOD
}

# ----------------------------------------------------------------------

for tool in Xvfb xdpyinfo xwininfo xterm xdotool; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat > "$WMHOME/Defaults/WindowMaker" <<EOF
{
  IconSize = $ISZ;
  DisableDock = Yes;
  DisableDrawers = Yes;
}
EOF

export XDG_RUNTIME_DIR="$RUNTIME"
export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
unset WMAKER_IPC_SOCKET

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
	"$RUNTIME"/*) : ;;
	*) fatal "IPC socket $SOCKET escaped private XDG_RUNTIME_DIR" ;;
esac

# ---- Keep on top restores the whole clip group above normal windows ----

note "restore a Keep on top clip with two attached icons"
write_wmstate 2 No '' One
start_wmaker

TILE=$(win_id_at_geom "$TILE_GEOM")
ICON1=$(win_id_at_geom "$ICON1_GEOM")
ICON2=$(win_id_at_geom "$ICON2_GEOM")
[ -n "$TILE" ]  || fatal "clip tile not at $TILE_GEOM"
[ -n "$ICON1" ] || fatal "clip icon 1 not at $ICON1_GEOM"
[ -n "$ICON2" ] || fatal "clip icon 2 not at $ICON2_GEOM"

spawn_probe clipontop1
sleep 0.5

check "clip tile restored above the overlapping window" \
	above "$TILE" "$PROBE_FRAME"
check "attached icon 1 above the overlapping window" \
	above "$ICON1" "$PROBE_FRAME"
check "attached icon 2 above the overlapping window" \
	above "$ICON2" "$PROBE_FRAME"
stop_wmaker

# ---- and a normal clip is still covered by it --------------------------

note "restore a normal clip (Lowered = Yes)"
write_wmstate 2 Yes '' One
start_wmaker

TILE=$(win_id_at_geom "$TILE_GEOM")
ICON1=$(win_id_at_geom "$ICON1_GEOM")
[ -n "$TILE" ]  || fatal "clip tile not at $TILE_GEOM"
[ -n "$ICON1" ] || fatal "clip icon 1 not at $ICON1_GEOM"

spawn_probe clipontop2
sleep 0.5

check "overlapping window covers a normal clip tile" \
	above "$PROBE_FRAME" "$TILE"
check "overlapping window covers the normal clip icons" \
	above "$PROBE_FRAME" "$ICON1"
stop_wmaker

# ---- the shared tile follows the visible workspace's clip --------------

note "two workspaces with opposite Keep on top states"
write_wmstate 1 No '
    {
      Name = "Two";
      Clip = {
        Applications = (
          { Command = "/nonexistent-clipontop-two"; Name = clipontoptwo.CT;
            AutoLaunch = No; Forced = No; Lock = No; BuggyApplication = No;
            Position = "1,0"; }
        );
        Lowered = Yes;
      };
    }' One
start_wmaker

TILE=$(win_id_at_geom "$TILE_GEOM")
[ -n "$TILE" ] || fatal "clip tile not at $TILE_GEOM"
spawn_probe clipontop3
check "tile starts on the restored workspace's Keep on top clip" \
	above "$TILE" "$PROBE_FRAME"

msg workspace 1 >>"$LOG" 2>&1 || fatal "workspace switch to 1 failed"
wait_for 40 ws_current_is 1 || fatal "workspace 1 did not become current"
spawn_probe clipontop4
check "tile follows the normal clip after the switch" \
	above "$PROBE_FRAME" "$TILE"

msg workspace 0 >>"$LOG" 2>&1 || fatal "workspace switch to 0 failed"
wait_for 40 ws_current_is 0 || fatal "workspace 0 did not become current"
spawn_probe clipontop5
check "tile follows the Keep on top clip back" \
	above "$TILE" "$PROBE_FRAME"

stop_wmaker

# ----------------------------------------------------------------------

printf '\n%d passed, %d failed\n' "$PASSED" "$FAILED" | tee -a "$LOG"
[ "$FAILED" -eq 0 ] || exit 1
exit 0
