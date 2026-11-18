#!/bin/sh
# Repeated activation of an application icon walks through the windows
# of the application: while one of its windows holds focus the next
# activation moves to a different window of the same application, in
# manage order with wrap (src/application.c wApplicationNextWindow,
# reached from the icon double-click paths in src/dock.c iconDblClick
# and src/appicon.c iconDblClick). A double-click and the two fast
# wheel ticks that IsDoubleClick() folds into the same gesture take
# this path, an activation from another application keeps landing on a
# window of the application without cycling. Covers:
#
#   1. docked tile: two windows of one application (two same-class
#      xterms sharing the application icon) alternate on repeated
#      double-clicks, a single wheel tick is inert and a wheel
#      double-tick cycles like the double-click
#   2. floating appicon: the same alternation as on the docked tile
#
# Runs wmaker under Xvfb on an isolated display and WMAKER_USER_ROOT,
# tracks every PID it spawns and kills only those.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${APPICON_CYCLE_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-appicon-cycle.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=${APPICON_CYCLE_DRIVE_LOG:-/tmp/appicon-cycle-drive.log}
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
SOCKET=
SCR_W=1024
SCR_H=768
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
	ac_desc=$1; shift
	if "$@"; then ok "$ac_desc"; else fail "$ac_desc"; fi
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

start_wmaker() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		DISPLAY=$DPY \
		"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
	WMAKER_PID=$!
	echo "$WMAKER_PID" >> "$PIDFILE"
	SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
	wait_for 80 ipc_ready || fatal "wmaker IPC did not come up"
	sleep 0.3
}

active_win() {
	DISPLAY=$DPY xprop -root _NET_ACTIVE_WINDOW 2>/dev/null \
		| awk '/_NET_ACTIVE_WINDOW/ {print $5}' | tr 'A-F' 'a-f'
}
active_is() {
	[ "$(active_win)" = "$(printf '0x%x' "$1")" ]
}
active_is_ac() {
	active_is "$W1" || active_is "$W2"
}

mapped_named() {
	DISPLAY=$DPY xdotool search --name "^$1\$" 2>/dev/null | grep -q .
}

client_win() {
	DISPLAY=$DPY xdotool search --name "^$1\$" 2>/dev/null | tail -n 1
}

# centre of the given icon window, button 1 double-click
dblclick_at() {
	set -- $(DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null \
			| awk '/Absolute upper-left X:/ {x=$4}
			       /Absolute upper-left Y:/ {y=$4}
			       END {print x + 32, y + 32}')
	DISPLAY=$DPY xdotool mousemove --sync "$1" "$2" >>"$LOG" 2>&1
	DISPLAY=$DPY xdotool click --repeat 2 --delay 120 1 >>"$LOG" 2>&1
	sleep 0.4
}

# same, $2 wheel ticks of button 5 in one gesture (one tick is inert,
# two ticks inside DoubleClickTime act as the double-click)
wheel_at() {
	wa_ticks=$2
	set -- $(DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null \
			| awk '/Absolute upper-left X:/ {x=$4}
			       /Absolute upper-left Y:/ {y=$4}
			       END {print x + 32, y + 32}')
	DISPLAY=$DPY xdotool mousemove --sync "$1" "$2" >>"$LOG" 2>&1
	DISPLAY=$DPY xdotool click --repeat "$wa_ticks" --delay 120 5 >>"$LOG" 2>&1
	sleep 0.4
}

# viewable ICON_SIZE toplevel icons outside the dock column, icon core
# windows carry no WM_NAME so identification is positional
viewable_icons() {
	for uiw in $(DISPLAY=$DPY xwininfo -root -children 2>/dev/null \
			| awk '/^     0x/ && $(NF-1) ~ /^64x64\+/ {print $1}'); do
		DISPLAY=$DPY xwininfo -id "$uiw" 2>/dev/null \
			| grep -q 'Map State: IsViewable' || continue
		echo "$uiw"
	done
}

for tool in Xvfb xdpyinfo xdotool xprop xterm xmessage wmctrl; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use" >&2; exit 77
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 ${SCR_W}x${SCR_H}x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
wait_for 40 sh -c "kill -0 $XVFB_PID 2>/dev/null && DISPLAY=$DPY xdpyinfo >/dev/null 2>&1" \
	|| fatal "Xvfb failed to start"

# --- boot 1: docked tile of a two-window application -------------------------
note "boot wmaker with an auto-launched docked xterm tile"
cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableClip = YES;
  DisableDrawers = YES;
  DisableAnimations = YES;
}
EOF
cat >"$WMHOME/Defaults/WMState" <<'EOF'
{
  Dock = {
    Applications = (
      {
        Command = "#wprefs#";
        Name = Logo.WMDock;
        AutoLaunch = No;
        Forced = No;
        Position = "0,0";
        Lock = Yes;
      },
      {
        Command = "/usr/bin/xterm -title ac-1 -geometry 60x4+200+200 -e sleep 600";
        Name = xterm.XTerm;
        AutoLaunch = Yes;
        Forced = No;
        Position = "0,1";
      }
    );
    Position = "-64,0";
    Lowered = No;
  };
  Workspaces = (
    { Name = "ws-0"; }
  );
}
EOF

start_wmaker
wait_for 80 mapped_named ac-1 || fatal "auto-launched ac-1 did not map"
W1=$(client_win ac-1)
[ -n "$W1" ] || fatal "could not find ac-1 window id"
# the auto-launched client is spawned by wmaker, record its pid so the
# teardown below kills it and it cannot leak into boot 2 as a third
# window of the same application
AC1_PID=$(DISPLAY=$DPY xdotool getwindowpid "$W1" 2>/dev/null)
[ -n "$AC1_PID" ] || fatal "could not resolve the ac-1 client pid"
case "$(ps -o args= -p "$AC1_PID" 2>/dev/null)" in
	*ac-1*) echo "$AC1_PID" >> "$PIDFILE" ;;
	*) fatal "pid $AC1_PID is not the ac-1 test client" ;;
esac

# second window of the same application: same instance.class so both
# windows share the application icon the dock tile is bound to
DISPLAY=$DPY xterm -title ac-2 -geometry 60x4+400+300 -e 'sleep 600' >>"$LOG" 2>&1 &
XT2=$!
echo "$XT2" >> "$PIDFILE"
wait_for 60 mapped_named ac-2 || fatal "ac-2 did not map"
W2=$(client_win ac-2)
[ -n "$W2" ] || fatal "could not find ac-2 window id"
note "ac-1=$W1 ac-2=$W2"

DISPLAY=$DPY xmessage -title ac-bg -geometry 200x80+500+450 'focus holder' >>"$LOG" 2>&1 &
XBG=$!
echo "$XBG" >> "$PIDFILE"
wait_for 60 mapped_named ac-bg || fatal "ac-bg did not map"
WBG=$(client_win ac-bg)
[ -n "$WBG" ] || fatal "could not find ac-bg window id"

# the docked tile is the ICON_SIZE child on row 1 of the dock (absolute
# y 64, the Position "-64,0" template value clamps to the left edge)
DOCK_TILE=
for uiw in $(DISPLAY=$DPY xwininfo -root -children 2>/dev/null \
		| awk '/^     0x/ && $(NF-1) ~ /^64x64\+/ {print $1}'); do
	uy=$(DISPLAY=$DPY xwininfo -id "$uiw" 2>/dev/null \
		| awk '/Absolute upper-left Y:/ {print $NF}')
	[ "$uy" = "64" ] && DOCK_TILE=$uiw && break
done
[ -n "$DOCK_TILE" ] || fatal "could not locate the docked xterm tile"
note "docked tile: $DOCK_TILE"

DISPLAY=$DPY wmctrl -i -a "$W2" >>"$LOG" 2>&1
sleep 0.3
check "baseline: ac-2 holds focus" active_is "$W2"

dblclick_at "$DOCK_TILE"
check "double-click focuses another window of the app" active_is "$W1"

dblclick_at "$DOCK_TILE"
check "double-click again focuses the other window" active_is "$W2"

wheel_at "$DOCK_TILE" 1
check "a single wheel tick does not move focus" active_is "$W2"

wheel_at "$DOCK_TILE" 2
check "wheel double-tick cycles like the double-click" active_is "$W1"

DISPLAY=$DPY wmctrl -i -a "$WBG" >>"$LOG" 2>&1
sleep 0.3
check "baseline: ac-bg holds focus" active_is "$WBG"

dblclick_at "$DOCK_TILE"
check "activation from another app lands on an ac window" active_is_ac

kill "$AC1_PID" "$XT2" "$XBG" 2>/dev/null
wait_for 30 pid_gone "$AC1_PID" || fatal "ac-1 did not exit"
msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"
sleep 0.5

# --- boot 2: floating appicon of a two-window application --------------------
note "boot wmaker without dock, floating application icon"
cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  DisableAnimations = YES;
}
EOF

start_wmaker

DISPLAY=$DPY xterm -title ac-1 -geometry 60x4+200+200 -e 'sleep 600' >>"$LOG" 2>&1 &
XT1=$!
echo "$XT1" >> "$PIDFILE"
wait_for 60 mapped_named ac-1 || fatal "ac-1 did not map"
W1=$(client_win ac-1)

DISPLAY=$DPY xterm -title ac-2 -geometry 60x4+400+300 -e 'sleep 600' >>"$LOG" 2>&1 &
XT2=$!
echo "$XT2" >> "$PIDFILE"
wait_for 60 mapped_named ac-2 || fatal "ac-2 did not map"
W2=$(client_win ac-2)
sleep 0.5

ICON1=$(viewable_icons | head -n 1)
[ -n "$ICON1" ] || fatal "could not locate the shared appicon"
[ "$(viewable_icons | wc -l)" = "1" ] || fatal "expected a single shared appicon"
note "shared appicon: $ICON1"

DISPLAY=$DPY wmctrl -i -a "$W2" >>"$LOG" 2>&1
sleep 0.3
check "baseline: ac-2 holds focus" active_is "$W2"

dblclick_at "$ICON1"
check "double-click on the appicon focuses another window" active_is "$W1"

dblclick_at "$ICON1"
check "double-click again focuses the other window" active_is "$W2"

kill "$XT1" "$XT2" 2>/dev/null
msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

echo
printf 'TOTAL: %d passed, %d failed\n' "$PASSED" "$FAILED" | tee -a "$LOG"
[ "$FAILED" = 0 ]
