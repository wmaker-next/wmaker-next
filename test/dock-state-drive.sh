#!/bin/sh
# Drive the dock state across resolution changes
# (PLAN-dock-state-resolution.md): seed WMState with per-resolution icon
# lists and assert which one wins on restore, that the keyed duplicates
# are dropped or preserved as DockPerResolution says, and that icon lists
# longer than the screen capacity survive a save/restore round trip for
# the dock, a drawer and a workspace clip. Runs wmaker under Xvfb on an
# isolated display and WMAKER_USER_ROOT, tracks every PID it spawns and
# kills only those.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${DOCKSTATE_DRIVE_DISPLAY:-:$(drive_default_display)}
W=1024
H=768
ISZ=64
RUNTIME=$(mktemp -d /tmp/wmaker-dockstate.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=${DOCKSTATE_DRIVE_LOG:-/tmp/dock-state-drive.log}
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
SOCKET=

export LANG=en_US.UTF-8

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

for tool in Xvfb xdpyinfo; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"

export XDG_RUNTIME_DIR="$RUNTIME"
export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
unset WMAKER_IPC_SOCKET

start_wmaker() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	ASAN_OPTIONS="$(drive_asan_options)" \
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

# $1 icon entries named $2.NN on their own lines, joined with commas so
# the proplist parser accepts the array. $3 and $4 wrap the entry index
# into the Position string: "0," and "" stack icons down a vertical dock
# edge, "" and ",0" spread them into a drawer on that edge.
emit_icons() {
	ei_n=$1
	ei_base=$2
	ei_pre=$3
	ei_post=$4
	ei_i=1
	while [ "$ei_i" -le "$ei_n" ]; do
		[ "$ei_i" -gt 1 ] && printf ',\n'
		printf '      { Command = "/bin/true"; Name = %s.%02d; AutoLaunch = No; Forced = No; Lock = No; BuggyApplication = No; Position = "%s%d%s"; }' \
			"$ei_base" "$ei_i" "$ei_pre" "$ei_i" "$ei_post"
		ei_i=$((ei_i + 1))
	done
	printf '\n'
}

state_count() {
	grep -c "$1" "$WMHOME/Defaults/WMState" 2>/dev/null || true
}

state_has() { grep -q "$1" "$WMHOME/Defaults/WMState"; }
state_lacks() { ! grep -q "$1" "$WMHOME/Defaults/WMState"; }
log_lacks() { ! grep -q "$1" "$LOG"; }

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

# 1024x768 at icon size 64: dock_capacity() is 16, so 20-icon lists are
# the overflow every grow case below uses.

# ---- case 1: default mode drops the keyed duplicate ------------------

note "case 1: default mode, Applications wins and the keyed copy dies"
printf '{\n}\n' > "$WMHOME/Defaults/WindowMaker"
cat > "$WMHOME/Defaults/WMState" <<WMEOD
{
  Dock = {
    Applications = (
$(emit_icons 2 keyedA "0," "")
    );
    Applications768 = (
$(emit_icons 2 keyedB "0," "")
    );
    Position = "$((W - ISZ)),0";
    Edge = Right;
    Lowered = No;
  };
  Workspaces = ( { Name = Main; } );
}
WMEOD
start_wmaker
stop_wmaker
check "default mode: Applications kept the A list" \
	[ "$(state_count 'Name = keyedA\.')" -eq 2 ]
check "default mode: the keyed B list did not win the restore" \
	[ "$(state_count 'Name = keyedB\.')" -eq 0 ]
check "default mode: Applications768 dropped from WMState" \
	state_lacks 'Applications768'

# ---- case 2: opt-in keyed mode --------------------------------------

note "case 2: DockPerResolution, the keyed list wins"
printf '{\n  DockPerResolution = YES;\n}\n' > "$WMHOME/Defaults/WindowMaker"
cat > "$WMHOME/Defaults/WMState" <<WMEOD
{
  Dock = {
    Applications = (
$(emit_icons 2 keyedA "0," "")
    );
    Applications768 = (
$(emit_icons 2 keyedB "0," "")
    );
    Position = "$((W - ISZ)),0";
    Edge = Right;
    Lowered = No;
  };
  Workspaces = ( { Name = Main; } );
}
WMEOD
start_wmaker
stop_wmaker
check "keyed mode: the B list won the restore" \
	[ "$(state_count 'Name = keyedB\.')" -eq 4 ]
check "keyed mode: the A list no longer anywhere" \
	[ "$(state_count 'Name = keyedA\.')" -eq 0 ]
check "keyed mode: Applications768 kept in WMState" \
	state_has 'Applications768'

# ---- case 3: opt-in mode preserves other resolutions ----------------

note "case 3: DockPerResolution, other resolution lists preserved"
cat > "$WMHOME/Defaults/WMState" <<WMEOD
{
  Dock = {
    Applications = (
$(emit_icons 2 keyedA "0," "")
    );
    Applications1080 = (
$(emit_icons 2 keyedC "0," "")
    );
    Position = "$((W - ISZ)),0";
    Edge = Right;
    Lowered = No;
  };
  Workspaces = ( { Name = Main; } );
}
WMEOD
start_wmaker
stop_wmaker
check "keyed merge: Applications and Applications768 hold the A list" \
	[ "$(state_count 'Name = keyedA\.')" -eq 4 ]
check "keyed merge: Applications1080 kept the C list" \
	[ "$(state_count 'Name = keyedC\.')" -eq 2 ]
check "keyed merge: Applications768 written" \
	state_has 'Applications768'
check "keyed merge: Applications1080 written" \
	state_has 'Applications1080'

# ---- case 4: dock overflow ------------------------------------------

note "case 4: 20 dock icons on a 16 slot screen"
printf '{\n}\n' > "$WMHOME/Defaults/WindowMaker"
cat > "$WMHOME/Defaults/WMState" <<WMEOD
{
  Dock = {
    Applications = (
$(emit_icons 20 ovf "0," "")
    );
    Position = "$((W - ISZ)),0";
    Edge = Right;
    Lowered = No;
  };
  Workspaces = ( { Name = Main; } );
}
WMEOD
start_wmaker
stop_wmaker
check "dock overflow: all 20 entries saved" \
	[ "$(state_count 'Name = ovf\.')" -eq 20 ]
check "dock overflow: no truncation warning" \
	log_lacks 'too many icons stored'

# ---- case 5: drawer overflow ----------------------------------------

note "case 5: 20 drawer icons on a 16 slot screen"
cat > "$WMHOME/Defaults/WMState" <<WMEOD
{
  Dock = {
    Applications = ();
    Position = "$((W - ISZ)),0";
    Edge = Right;
    Lowered = No;
  };
  Drawers = (
    {
      Name = Drawer1;
      Position = "$((W - ISZ)),$ISZ";
      Dock = {
        Applications = (
$(emit_icons 20 dwrovf "" ",0")
        );
        Collapsed = No;
        AutoCollapse = No;
        AutoAttractIcons = No;
      };
    }
  );
  Workspaces = ( { Name = Main; } );
}
WMEOD
start_wmaker
stop_wmaker
check "drawer overflow: all 20 entries saved" \
	[ "$(state_count 'Name = dwrovf\.')" -eq 20 ]
check "drawer overflow: no truncation warning" \
	log_lacks 'too many icons stored'

# ---- case 6: workspace clip overflow --------------------------------

note "case 6: 20 clip icons on a 16 slot screen"
cat > "$WMHOME/Defaults/WMState" <<WMEOD
{
  Dock = {
    Applications = ();
    Position = "$((W - ISZ)),0";
    Edge = Right;
    Lowered = No;
  };
  Workspaces = (
    {
      Name = Main;
      Clip = {
        Applications = (
$(emit_icons 20 clipovf "0," "")
        );
      };
    }
  );
}
WMEOD
start_wmaker
stop_wmaker
check "clip overflow: all 20 entries saved" \
	[ "$(state_count 'Name = clipovf\.')" -eq 20 ]

# ---- case 7: default mode migrates a keyed-only state file -----------

note "case 7: default mode restores a keyed-only state file"
cat > "$WMHOME/Defaults/WMState" <<WMEOD
{
  Dock = {
    Applications768 = (
$(emit_icons 2 mig "0," "")
    );
    Position = "$((W - ISZ)),0";
    Edge = Right;
    Lowered = No;
  };
  Workspaces = ( { Name = Main; } );
}
WMEOD
start_wmaker
stop_wmaker
check "migration: the keyed list was restored" \
	[ "$(state_count 'Name = mig\.')" -eq 2 ]
check "migration: saved under Applications alone" \
	state_has 'Applications = ($'
check "migration: Applications768 dropped from WMState" \
	state_lacks 'Applications768'

note "dock state drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
[ "$FAILED" -eq 0 ] && [ "$PASSED" -gt 0 ]
