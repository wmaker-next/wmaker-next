#!/bin/sh
# Drive the dynamic-workspaces auto-destroy under Xvfb: spawn xterms,
# move/close them over IPC, and assert that empty trailing workspaces
# are reaped (and that the active workspace, MinWorkspaceCount and
# non-empty members block the sweep). Uses IPC exclusively.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${WORKSPACE_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/workspace-drive.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=${WORKSPACE_DRIVE_LOG:-/tmp/workspace-drive.log}
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
: > "$PIDFILE"
: > "$RUNTIME/xtpids"
: > "$LOG"

cleanup() {
	while read -r p; do
		[ -n "$p" ] && kill "$p" 2>/dev/null
	done < "$PIDFILE"
	rm -rf "$RUNTIME"
}
trap cleanup EXIT

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
fail() { printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
ok()   { note "ok: $*"; }

wait_for() {
	_wf=$1; shift
	while [ "$_wf" -gt 0 ]; do
		"$@" && return 0
		sleep 0.1
		_wf=$((_wf - 1))
	done
	return 1
}

command -v Xvfb    >/dev/null || { echo "SKIP: Xvfb not found"; exit 77; }
command -v xdotool >/dev/null || { echo "SKIP: xdotool not found"; exit 77; }
command -v xterm   >/dev/null || { echo "SKIP: xterm not found"; exit 77; }
[ -x "$ROOT/src/.libs/wmaker" ] || { echo "SKIP: wmaker not built"; exit 77; }
[ -x "$WMAKERMSG" ] || { echo "SKIP: wmakermsg not built"; exit 77; }

msg() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" "$@"
}

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY already in use, set WORKSPACE_DRIVE_DISPLAY" >&2
	exit 77
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 1024x768x24 -ac -noreset >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
	kill -0 "$XVFB_PID" 2>/dev/null || fail "Xvfb exited early"
	DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fail "Xvfb failed to start"

# --- wmaker boot helpers ----------------------------------------------------

write_config() {
	# $1 = AutoRemove (YES/NO), $2 = MinWorkspaceCount
	cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  AutoRemoveEmptyWorkspaces = $1;
  MinWorkspaceCount = $2;
  AdvanceToNewWorkspace = NO;
}
EOF
}

write_wmstate() {
	# seed a single workspace, scenarios grow with workspace-new
	cat >"$WMHOME/Defaults/WMState" <<'EOF'
{
  Workspaces = ( { Name = "ws0"; } );
}
EOF
}

boot() {
	# $1 = AutoRemove, $2 = MinWorkspaceCount
	if [ -n "${WMAKER_PID:-}" ] && kill -0 "$WMAKER_PID" 2>/dev/null; then
		kill "$WMAKER_PID" 2>/dev/null
		wait_for 30 pid_gone "$WMAKER_PID" || true
	fi
	# kill any leftover xterms from the previous scenario so their
	# titles do not collide with freshly spawned ones
	while read -r p; do
		[ -n "$p" ] && kill "$p" 2>/dev/null
	done < "$RUNTIME/xtpids"
	: > "$RUNTIME/xtpids"
	wait_for 20 no_xterms || true
	write_config "$1" "$2"
	write_wmstate
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		DISPLAY=$DPY \
		"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
	WMAKER_PID=$!
	echo "$WMAKER_PID" >> "$PIDFILE"
	SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		DISPLAY=$DPY XDG_RUNTIME_DIR="$RUNTIME" "$WMAKERMSG" --get-socketpath)
	ipc_ready() { msg -q get_version >/dev/null 2>&1; }
	wait_for 120 ipc_ready || fail "wmaker did not come up"
	sleep 0.3
}

pid_gone() { ! kill -0 "$1" 2>/dev/null; }
no_xterms() { ! DISPLAY=$DPY xdotool search --class xterm >/dev/null 2>&1; }

# --- query helpers ---------------------------------------------------------

ws_count() {
	msg get_workspaces 2>/dev/null | grep -o 'index = ' | wc -l | tr -d ' '
}

ws_count_is() { [ "$(ws_count)" = "$1" ]; }
wait_count() { wait_for 40 ws_count_is "$1"; }

xt_client() {
	DISPLAY=$DPY xdotool search --name "^$1\$" 2>/dev/null | tail -n 1
}

win_entry() {
	msg get_windows 2>/dev/null | sed 's/}, *{/}\n{/g' | grep -F "id = $1;"
}

win_primary() {
	win_entry "$1" | sed -n 's/.*workspace = \([0-9]*\);.*/\1/p'
}

win_mask() {
	win_entry "$1" | sed -n 's/.*workspaces = (\([^)]*\));.*/\1/p' \
	               | tr -d ' '
}

no_windows() {
	[ -z "$(msg get_windows 2>/dev/null | grep -o 'id = ')" ];
}

# spawn an xterm with title $1 on the current workspace, return its hex XID
spawn_xt() {
	DISPLAY=$DPY xterm -title "$1" -geometry 60x8+40+40 -e 'sleep 300' >>"$LOG" 2>&1 &
	echo $! >> "$PIDFILE"
	echo $! >> "$RUNTIME/xtpids"
	xt_mapped() { [ -n "$(xt_client "$1")" ]; }
	wait_for 60 xt_mapped "$1" || fail "xterm $1 did not map"
	sleep 0.2
	XID=$(xt_client "$1")
	[ -n "$XID" ] || fail "could not resolve xterm $1 XID"
	printf '0x%x' "$XID"
}

# --- scenarios -------------------------------------------------------------

note "scenario 1: close reaps all trailing empties"
boot YES 1
msg -q workspace-new 2 || fail "workspace-new 2 rejected"
wait_count 3 || fail "expected 3 workspaces after create"
A=$(spawn_xt xa)
msg -q workspace 2 || fail "switch to ws 2 rejected"
sleep 0.2
# move xterm to ws 2 (sole occupant of the trailing workspace)
msg -q -- window-workspace "$A" 2 || fail "move A to ws 2 rejected"
sleep 0.2
[ "$(win_primary "$A")" = "2" ] || fail "A primary != 2"
msg -q workspace 0 || fail "switch to ws 0 rejected"
sleep 0.2
msg -q -- action Close "$A" || fail "action Close A rejected"
wait_for 30 no_windows || fail "A did not close"
# ws 2 and ws 1 both empty trailing -> reaped, ws 0 active kept -> count 1
wait_count 1 || fail "close reaps: count $(ws_count) != 1"
ok "close reaps all trailing empties -> count 1"

note "scenario 2: move reaps trailing"
boot YES 1
msg -q workspace-new 1 || fail "workspace-new 1 rejected"
wait_count 2 || fail "expected 2 workspaces"
A=$(spawn_xt xa)
msg -q -- window-workspace "$A" 1 || fail "move A to ws 1 rejected"
sleep 0.2
msg -q workspace 0 || fail "switch to ws 0"
sleep 0.2
msg -q -- window-workspace "$A" 0 || fail "move A to ws 0 rejected"
sleep 0.2
# ws 1 now empty trailing -> reaped -> count 1
wait_count 1 || fail "move reaps: count $(ws_count) != 1"
ok "move reaps trailing -> count 1"

note "scenario 3: membership removal reaps trailing + intermediate"
boot YES 1
msg -q workspace-new 2 || fail "workspace-new 2 rejected"
wait_count 3 || fail "expected 3 workspaces"
A=$(spawn_xt xa)
# A on {0,2}: add ws 2, stays on ws 0 as primary
msg -q -- window-workspace "$A" +2 || fail "add A to ws 2 rejected"
sleep 0.2
[ "$(win_mask "$A")" = "0,2" ] || fail "A mask != (0,2) got $(win_mask "$A")"
msg -q workspace 0 || fail "switch to ws 0"
sleep 0.2
# remove ws 2 -> ws 2 empty, ws 1 already empty -> both reaped -> count 1
msg -q -- window-workspace "$A" -2 || fail "remove A from ws 2 rejected"
sleep 0.2
wait_count 1 || fail "membership reaps: count $(ws_count) != 1"
[ "$(win_mask "$A")" = "0" ] || fail "A mask != (0) got $(win_mask "$A")"
ok "membership removal reaps trailing + intermediate -> count 1"

note "scenario 4: multi-membership blocks sweep"
boot YES 1
msg -q workspace-new 2 || fail "workspace-new 2 rejected"
wait_count 3 || fail "expected 3 workspaces"
A=$(spawn_xt xa)
B=$(spawn_xt xb)
# view ws 2 so the sweep cannot reap it while we build A's membership
msg -q workspace 2 || fail "switch to ws 2"
sleep 0.2
msg -q -- window-workspace "$A" +1 || fail "add A to ws 1"
msg -q -- window-workspace "$A" +2 || fail "add A to ws 2"
sleep 0.2
[ "$(win_mask "$A")" = "0,1,2" ] || fail "A mask != (0,1,2) got $(win_mask "$A")"
# drop ws 0 so A lives on {1,2}, ws 2 has A so the sweep stops there
msg -q -- window-workspace "$A" -0 || fail "remove A from ws 0"
sleep 0.2
[ "$(win_mask "$A")" = "1,2" ] || fail "A mask != (1,2) got $(win_mask "$A")"
msg -q workspace 0 || fail "switch to ws 0"
sleep 0.2
# close B (on ws 0): A still holds ws 1 and ws 2 -> nothing reaped -> count 3
msg -q -- action Close "$B" || fail "close B rejected"
xb_gone() { ! msg get_windows 2>/dev/null | grep -q -F "id = $B;"; }
wait_for 30 xb_gone || fail "B did not close"
wait_count 3 || fail "multi-membership: count $(ws_count) != 3 (B close should not reap)"
ok "multi-membership blocks sweep on B close -> count 3"
# now remove ws 2 from A -> ws 2 empty reaped, A still on ws 1 -> count 2
msg -q -- window-workspace "$A" -2 || fail "remove A from ws 2"
sleep 0.2
wait_count 2 || fail "after -2: count $(ws_count) != 2"
[ "$(win_mask "$A")" = "1" ] || fail "A mask != (1) got $(win_mask "$A")"
ok "remove ws 2 reaps only trailing -> count 2"

note "scenario 5: active-workspace protection"
boot YES 1
msg -q workspace-new 1 || fail "workspace-new 1 rejected"
wait_count 2 || fail "expected 2 workspaces"
A=$(spawn_xt xa)
msg -q -- window-workspace "$A" 1 || fail "move A to ws 1"
sleep 0.2
msg -q workspace 1 || fail "switch to ws 1"
sleep 0.2
msg -q -- action Close "$A" || fail "close A rejected"
wait_for 30 no_windows || fail "A did not close"
# ws 1 empty but it is the active workspace -> never destroyed -> count 2
wait_count 2 || fail "active protection: count $(ws_count) != 2"
ok "active workspace protected -> count 2"
# navigation never reaps: switch to ws 0, count stays 2
msg -q workspace 0 || fail "switch to ws 0"
sleep 0.2
wait_count 2 || fail "navigation reaped: count $(ws_count) != 2"
ok "navigation never reaps -> count 2"

note "scenario 6: MinWorkspaceCount floor"
boot YES 2
msg -q workspace-new 2 || fail "workspace-new 2 rejected"
wait_count 3 || fail "expected 3 workspaces"
A=$(spawn_xt xa)
msg -q -- window-workspace "$A" 2 || fail "move A to ws 2"
sleep 0.2
msg -q workspace 0 || fail "switch to ws 0"
sleep 0.2
msg -q -- action Close "$A" || fail "close A rejected"
wait_for 30 no_windows || fail "A did not close"
# ws 2 empty reaped, ws 1 empty but at min(2) -> kept -> count 2
wait_count 2 || fail "min floor: count $(ws_count) != 2"
ok "MinWorkspaceCount floor -> count 2"

note "scenario 7: disabled"
boot NO 1
msg -q workspace-new 2 || fail "workspace-new 2 rejected"
wait_count 3 || fail "expected 3 workspaces"
A=$(spawn_xt xa)
msg -q -- window-workspace "$A" 2 || fail "move A to ws 2"
sleep 0.2
msg -q workspace 0 || fail "switch to ws 0"
sleep 0.2
msg -q -- action Close "$A" || fail "close A rejected"
wait_for 30 no_windows || fail "A did not close"
# disabled -> nothing reaped -> count 3
wait_count 3 || fail "disabled: count $(ws_count) != 3"
ok "disabled -> count 3"

note "scenario 8: runtime reload"
boot NO 1
msg -q workspace-new 2 || fail "workspace-new 2 rejected"
wait_count 3 || fail "expected 3 workspaces"
A=$(spawn_xt xa)
msg -q -- window-workspace "$A" 2 || fail "move A to ws 2"
sleep 0.2
msg -q workspace 0 || fail "switch to ws 0"
sleep 0.2
msg -q -- action Close "$A" || fail "close A rejected"
wait_for 30 no_windows || fail "A did not close"
wait_count 3 || fail "disabled close: count $(ws_count) != 3"
# enable at runtime via reconfigure
write_config YES 1
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3
A=$(spawn_xt xb)
msg -q -- window-workspace "$A" 2 || fail "move A to ws 2"
sleep 0.2
msg -q workspace 0 || fail "switch to ws 0"
sleep 0.2
msg -q -- action Close "$A" || fail "close A rejected"
wait_for 30 no_windows || fail "A did not close"
# auto-remove now ON with min 1: ws 2 and ws 1 both empty trailing -> reaped,
# ws 0 active -> kept -> count 1
wait_count 1 || fail "runtime reload: count $(ws_count) != 1"
ok "runtime reload enables sweep -> count 1"

note "scenario 9: ws_advance + auto-destroy cycle"
boot YES 1
# enable ws_advance for this session by writing the config and reconfiguring
write_config_with_advance() {
	cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  AutoRemoveEmptyWorkspaces = YES;
  MinWorkspaceCount = 1;
  AdvanceToNewWorkspace = YES;
}
EOF
}
write_config_with_advance
	msg -q reconfigure || fail "reconfigure rejected"
	sleep 0.3
A=$(spawn_xt xa)
[ "$(ws_count)" = "1" ] || fail "expected 1 workspace at start"
msg -q workspace next || fail "workspace next rejected"
sleep 0.2
# ws_advance creates ws 1 -> count 2
wait_count 2 || fail "advance: count $(ws_count) != 2"
ok "ws_advance creates -> count 2"
msg -q -- window-workspace "$A" 1 || fail "move A to ws 1"
sleep 0.2
msg -q workspace 0 || fail "switch to ws 0"
sleep 0.2
msg -q -- action Close "$A" || fail "close A rejected"
wait_for 30 no_windows || fail "A did not close"
# ws 1 empty trailing reaped -> count 1
wait_count 1 || fail "after close: count $(ws_count) != 1"
ok "auto-destroy reaps -> count 1"
msg -q workspace next || fail "workspace next rejected"
sleep 0.2
wait_count 2 || fail "advance again: count $(ws_count) != 2"
ok "ws_advance recreates -> count 2"

note "scenario 10: explicit delete + compaction"
boot NO 1
msg -q workspace-new 3 || fail "workspace-new 3 rejected"
wait_count 4 || fail "expected 4 workspaces"
A=$(spawn_xt xa)
msg -q -- window-workspace "$A" 2 || fail "move A to ws 2"
sleep 0.2
[ "$(win_primary "$A")" = "2" ] || fail "A primary != 2"
# delete empty ws 3 -> count 3, A unaffected (still ws 2)
msg -q workspace-delete 3 || fail "workspace-delete 3 rejected"
sleep 0.2
wait_count 3 || fail "delete empty: count $(ws_count) != 3"
[ "$(win_primary "$A")" = "2" ] || fail "A primary changed after delete 3"
ok "explicit delete empty ws 3 -> count 3, A stays ws 2"
# A on ws 2, ws 1 empty middle, delete ws 1 -> A compacted 2 -> 1, count 2
msg -q workspace-delete 1 || fail "workspace-delete 1 rejected"
sleep 0.2
wait_count 2 || fail "delete ws 1: count $(ws_count) != 2"
[ "$(win_primary "$A")" = "1" ] || fail "compacted primary != 1 got $(win_primary "$A")"
[ "$(win_mask "$A")" = "1" ] || fail "compacted mask != (1) got $(win_mask "$A")"
ok "explicit delete compacts membership 2 -> 1"

note "scenario 11: explicit delete refuses sole-membership"
boot YES 1
msg -q workspace-new 1 || fail "workspace-new 1 rejected"
wait_count 2 || fail "expected 2 workspaces"
A=$(spawn_xt xa)
msg -q -- window-workspace "$A" 1 || fail "move A to ws 1"
sleep 0.2
# A is the sole member of ws 1 -> workspace-delete must fail (rc=2)
if msg -q -- workspace-delete 1 >>"$LOG" 2>&1; then
	fail "workspace-delete 1 succeeded on sole-membership workspace"
fi
wait_count 2 || fail "refused delete: count $(ws_count) != 2"
ok "explicit delete refuses sole-membership -> count 2"

note "PASS"
exit 0
