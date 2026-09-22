#!/bin/sh
# Drive DisableWSMouseActions (suppresses all root button/wheel actions)
# and CloseRootMenuByLeftOrRightMouseClick (Button1/Button3 on root
# closes an open non-sticked root menu).
#
# DisableWSMouseActions is asserted by configuring a workspace-changing
# button action and verifying it does NOT fire when the flag is on.
# CloseRootMenu is asserted by counting visible top-level windows.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${DISABLE_WS_MOUSE_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-disws-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/disable-ws-mouse-drive.log
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

xdo() { DISPLAY=$DPY xdotool "$@"; }

current_desktop() {
    DISPLAY=$DPY xprop -root _NET_CURRENT_DESKTOP 2>/dev/null \
        | sed -n 's/.*_NET_CURRENT_DESKTOP(CARDINAL) = \([0-9]*\).*/\1/p'
}
desktop_is() { [ "$(current_desktop)" = "$1" ]; }

# Count visible override-redirect top-level windows. Menus are OR windows;
# at idle there are none (dock/clip disabled). When a menu is mapped the
# count increases by at least 1.
visible_or_count() {
    DISPLAY=$DPY xdotool search --onlyvisible --name "" 2>/dev/null \
        | while read -r wid; do
            DISPLAY=$DPY xprop -id "$wid" _NET_WM_STATE 2>/dev/null | head -1
        done | wc -l
}

for tool in Xvfb xdpyinfo xdotool xprop xterm; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
[ -x "$WDWRITE" ] || skip_all "util/.libs/wdwrite not built"

export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
}
EOF
cat >"$WMHOME/Defaults/WMState" <<'EOF'
{
  Workspaces = (
    { Name = "ws-0"; },
    { Name = "ws-1"; }
  );
}
EOF

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use" >&2; exit 77
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 1024x768x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
wait_for 40 sh -c "kill -0 $XVFB_PID 2>/dev/null && DISPLAY=$DPY xdpyinfo >/dev/null 2>&1" \
    || fatal "Xvfb failed to start"

note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    ASAN_OPTIONS="abort_on_error=0:halt_on_error=0:detect_leaks=0" \
    DISPLAY=$DPY \
    "$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
wait_for 80 ipc_ready || fatal "wmaker IPC did not come up"
sleep 0.5

check "baseline desktop is 0" desktop_is 0

# --- 1. DisableWSMouseActions: button action suppressed ---------------------
# Configure MouseMiddleButtonAction=MoveToNextWorkspace so Button2
# changes workspace. With DisableWSMouseActions=YES, Button2 must NOT
# change the workspace.
note "configure MouseMiddleButtonAction=MoveToNextWorkspace"
wdwrite WindowMaker MouseMiddleButtonAction MoveToNextWorkspace
wdwrite WindowMaker DisableWSMouseActions YES
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3

xdo mousemove 512 384; sleep 0.1
xdo click 2; sleep 0.3
check "Button2 with DisableWSMouseActions does not change workspace" desktop_is 0

# Restore: DisableWSMouseActions=NO, Button2 should change workspace
note "restore DisableWSMouseActions=NO"
wdwrite WindowMaker DisableWSMouseActions NO
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3

xdo click 2; sleep 0.3
check "Button2 without DisableWSMouseActions changes workspace" desktop_is 1
check "wmaker alive after workspace change via Button2" ipc_ready

# Go back to workspace 0
msg -q workspace 0 || true
wait_for 20 desktop_is 0

# --- 2. CloseRootMenuByLeftOrRightMouseClick -------------------------------
# The default MouseLeftButtonAction=SelectWindows (Button1 on root)
# calls wUnselectWindows + wSelectWindows. With
# CloseRootMenuByLeftOrRightMouseClick=YES, it also destroys an open
# root menu. We test this by opening the root menu (Button3) and then
# clicking Button1 to close it.
note "configure CloseRootMenuByLeftOrRightMouseClick=YES"
wdwrite WindowMaker CloseRootMenuByLeftOrRightMouseClick YES
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3

# Open the root menu with Button3
xdo mousemove 512 384; sleep 0.1
xdo click 3; sleep 0.5
before=$(visible_or_count)
note "visible OR count with menu open: $before"
check "root menu opened (visible OR count > 0)" [ "$before" -gt 0 ]

# Button1 on root should close the menu with the flag
xdo mousemove 900 600; sleep 0.1
xdo click 1; sleep 0.5
after=$(visible_or_count)
note "visible OR count after Button1: $after"
check "Button1 closes root menu with CloseRootMenu flag" [ "$after" -lt "$before" ]

# --- 3. Without the flag, Button1 does not close ---------------------------
note "restore CloseRootMenuByLeftOrRightMouseClick=NO"
wdwrite WindowMaker CloseRootMenuByLeftOrRightMouseClick NO
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3

# Reopen the root menu
xdo mousemove 512 384; sleep 0.1
xdo click 3; sleep 0.5
before2=$(visible_or_count)
note "visible OR count with menu re-opened: $before2"
check "root menu re-opened" [ "$before2" -gt 0 ]

# Button1 on root should NOT close it without the flag
xdo mousemove 900 600; sleep 0.1
xdo click 1; sleep 0.3
after2=$(visible_or_count)
note "visible OR count after Button1 (no flag): $after2"
check "Button1 without CloseRootMenu does not close menu" [ "$after2" -ge "$before2" ]
check "wmaker alive after CloseRootMenu test" ipc_ready

# --- teardown ----------------------------------------------------------------
note "disable-ws-mouse drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
