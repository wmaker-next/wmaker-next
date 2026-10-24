#!/bin/sh
# Drive the RandR head rebuild on a live display. Xephyr exposes the
# parent server modes and supports RandR 1.5 user monitors, so splitting
# the one output into a left and a right monitor makes wRandRHandleNotify
# -> wRandR_Update() rebuild xine_info from GetMonitors while the window
# manager is running. Moving a window between the new heads and removing
# one again exercises moveBetweenHeads(), warpPointerToNextMonitor() and
# wRandR_BringAllWindowsInside().
#
# Xvfb alone ignores RRSetMonitor, hence the nested Xephyr. Runs against
# an isolated WMAKER_USER_ROOT and XDG_RUNTIME_DIR, tracks every PID it
# spawns, the live desktop is untouched.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${RANDR_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-randr-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/randr-drive.log
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

xdo() { DISPLAY=$DPY xdotool "$@"; }
xr() { DISPLAY=$DPY xrandr "$@"; }

win_x() {
    DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null \
        | awk '/Absolute upper-left X/ {print $4}'
}
win_y() {
    DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null \
        | awk '/Absolute upper-left Y/ {print $4}'
}
win_w() {
    DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null \
        | awk '/Width:/ {print $2}'
}
pointer_x() {
    xdo getmouselocation --shell 2>/dev/null | sed -n 's/^X=//p'
}
desktop_geometry() {
    DISPLAY=$DPY xprop -root _NET_DESKTOP_GEOMETRY 2>/dev/null \
        | sed 's/.*= //'
}
desktop_geometry_is() {
    [ "$(desktop_geometry)" = "$1" ]
}

for tool in Xvfb Xephyr xrandr xdpyinfo xdotool xprop xwininfo xterm; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  DisableSound = YES;
  HotplugMonitor = YES;
  MoveTo12to6Head = "Control+Shift+l";
  MoveTo6to12Head = "Control+Shift+h";
  ScreenSwitchKey = "Control+Shift+m";
  LockScreenCommand = "touch $RUNTIME/key-dispatch";
  LockScreenKey = "Control+Shift+z";
}
EOF

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use" >&2; exit 77
fi
# Xephyr's own RandR is what changes, the parent only supplies its modes
PARENT_DISPLAY=${RANDR_DRIVE_PARENT_DISPLAY:-":$(( ${DPY#:} + 1 ))"}
if DISPLAY=$PARENT_DISPLAY xdpyinfo >/dev/null 2>&1; then
    echo "parent display $PARENT_DISPLAY is already in use" >&2; exit 77
fi
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
sleep 0.5

note "map an xterm on the left head"
DISPLAY=$DPY xterm -geometry 40x12+40+100 -e 'sleep 300' >>"$LOG" 2>&1 &
XTERM_PID=$!
echo "$XTERM_PID" >> "$PIDFILE"
XTERM_WID=
xterm_ready() {
    XTERM_WID=$(xdo search --onlyvisible --class XTerm 2>/dev/null | head -n 1)
    [ -n "$XTERM_WID" ]
}
wait_for 60 xterm_ready || fatal "xterm did not map"
xdo windowactivate "$XTERM_WID" 2>/dev/null
sleep 0.3

BASE_X=$(win_x "$XTERM_WID")
check "xterm starts on the left half" sh -c "[ -n '$BASE_X' ] && [ $BASE_X -lt 512 ]"

# --- 1. split the output into two virtual monitors -------------------------
note "split $DPY into left/right monitors"
xr --setmonitor left 512/135x768/203+0+0 default >>"$LOG" 2>&1 \
    || fail "could not create left monitor"
xr --setmonitor right 512/135x768/203+512+0 none >>"$LOG" 2>&1 \
    || fail "could not create right monitor"
# RRSetMonitor alone does not reliably emit the CRTC/output notifies the
# WM selected, and a same-mode SetCrtcConfig is a no-op. A real mode
# round-trip makes the server publish the new GetMonitors view.
xr --output default --mode 800x600 >>"$LOG" 2>&1 \
    || fail "could not nudge the mode after adding heads"
sleep 0.4
xr --output default --mode 1024x768 >>"$LOG" 2>&1 \
    || fail "could not restore the mode after adding heads"
sleep 0.6
check "wmaker alive after adding a head" ipc_ready
check "left monitor is advertised" \
    sh -c "DISPLAY=$DPY xrandr --listmonitors | grep -q ' left '"
check "right monitor is advertised" \
    sh -c "DISPLAY=$DPY xrandr --listmonitors | grep -q ' right '"

# --- 2. move the window between the new heads -----------------------------
# the RandR rebuild may leave the root focused, and the frame key grabs
# only exist while a managed window owns the focus, so re-focus the client
xdo windowactivate "$XTERM_WID" 2>/dev/null
sleep 0.2
xdo key ctrl+shift+z >>"$LOG" 2>&1
wait_for 20 [ -f "$RUNTIME/key-dispatch" ] \
    || fail "key dispatch stopped working after the RandR rebuild"
check "key dispatch still works after the RandR rebuild" [ -f "$RUNTIME/key-dispatch" ]
note "move the xterm to the right head with MoveTo12to6Head"
xdo key ctrl+shift+l >>"$LOG" 2>&1
wait_for 40 sh -c "[ $(win_x "$XTERM_WID") -ge 512 ]" \
    || fail "window did not move to the right head (x=$(win_x "$XTERM_WID"))"
check "window is on the right head" [ "$(win_x "$XTERM_WID")" -ge 512 ]

note "move the xterm back to the left head with MoveTo6to12Head"
xdo windowactivate "$XTERM_WID" 2>/dev/null
xdo key ctrl+shift+h >>"$LOG" 2>&1
wait_for 40 sh -c "[ $(win_x "$XTERM_WID") -lt 512 ]" \
    || fail "window did not move back to the left head (x=$(win_x "$XTERM_WID"))"
check "window is back on the left head" [ "$(win_x "$XTERM_WID")" -lt 512 ]

# --- 3. ScreenSwitchKey warps the pointer to the next head -----------------
note "warp the pointer to the next head with ScreenSwitchKey"
xdo windowactivate "$XTERM_WID" 2>/dev/null
xdo mousemove 100 300 >>"$LOG" 2>&1
sleep 0.2
PX0=$(pointer_x)
xdo key ctrl+shift+m >>"$LOG" 2>&1
wait_for 40 sh -c "[ $(pointer_x) -ge 512 ]" \
    || fail "pointer did not warp to the right head (x=$(pointer_x))"
check "pointer started on the left head" [ "$PX0" -lt 512 ]
check "pointer is on the right head" [ "$(pointer_x)" -ge 512 ]

# --- 4. removing the occupied head brings the window back ------------------
note "remove the right head while the window is on it"
xdo windowactivate "$XTERM_WID" 2>/dev/null
xdo key ctrl+shift+l >>"$LOG" 2>&1
wait_for 40 sh -c "[ $(win_x "$XTERM_WID") -ge 512 ]" \
    || fail "window is not on the right head before removal"
xr --delmonitor right >>"$LOG" 2>&1 || fail "could not remove right monitor"
sleep 0.6
check "wmaker alive after removing a head" ipc_ready
WX=$(win_x "$XTERM_WID")
WW=$(win_w "$XTERM_WID")
check "window was brought back inside the remaining head" \
    sh -c "[ -n '$WX' ] && [ -n '$WW' ] && [ $WX -ge 0 ] && [ $((WX + WW)) -le 1024 ]"

# --- 5. restore the default monitor and change the mode --------------------
note "restore the default monitor"
xr --delmonitor left >>"$LOG" 2>&1 || fail "could not remove left monitor"
sleep 0.5
check "wmaker alive after restoring the default monitor" ipc_ready

note "shrink the framebuffer from 1024x768 to 800x600"
xr --output default --mode 800x600 >>"$LOG" 2>&1 || fail "could not switch mode"
sleep 0.6
check "server reports 800x600" \
    sh -c "DISPLAY=$DPY xdpyinfo | grep -q 'dimensions:.*800x600'"
wait_for 30 desktop_geometry_is '800, 600' \
    || fail "_NET_DESKTOP_GEOMETRY did not follow the mode change (got $(desktop_geometry))"
check "_NET_DESKTOP_GEOMETRY follows the mode change" \
    desktop_geometry_is '800, 600'
check "wmaker alive after the mode change" ipc_ready
WX=$(win_x "$XTERM_WID")
WY=$(win_y "$XTERM_WID")
WW=$(win_w "$XTERM_WID")
check "window stays inside the shrunken framebuffer" \
    sh -c "[ -n '$WX' ] && [ -n '$WY' ] && [ -n '$WW' ] && [ $WX -ge 0 ] && [ $WY -ge 0 ] && [ $((WX + WW)) -le 800 ]"

note "grow the framebuffer back to 1024x768"
xr --output default --mode 1024x768 >>"$LOG" 2>&1 || fail "could not restore the mode"
sleep 0.5
check "_NET_DESKTOP_GEOMETRY follows the grow" \
    desktop_geometry_is '1024, 768'
check "wmaker alive after growing the framebuffer" ipc_ready

# --- teardown --------------------------------------------------------------
note "randr drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

kill "$XTERM_PID" 2>/dev/null
msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
