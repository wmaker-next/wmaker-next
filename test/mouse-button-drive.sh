#!/bin/sh
# Drive root button and wheel action mappings: configure
# MouseWheelAction=SwitchWorkspaces and MouseMiddleButtonAction=
# MoveToNextWorkspace via wdwrite + reconfigure, then click on the
# root window and assert the workspace changes via _NET_CURRENT_DESKTOP.
#
# asan-drive.sh already covers the default Button2/Button3 root actions;
# this drive covers the wheel path (executeWheelAction, net-new ASan)
# and reconfigured button actions.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${MOUSE_BUTTON_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-mousebtn-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/mouse-button-drive.log
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

current_desktop() {
    DISPLAY=$DPY xprop -root _NET_CURRENT_DESKTOP 2>/dev/null \
        | sed -n 's/.*_NET_CURRENT_DESKTOP(CARDINAL) = \([0-9]*\).*/\1/p'
}
desktop_is() { [ "$(current_desktop)" = "$1" ]; }

xdo() { DISPLAY=$DPY xdotool "$@"; }

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
    { Name = "ws-1"; },
    { Name = "ws-2"; }
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
    ASAN_OPTIONS="$(drive_asan_options)" \
    DISPLAY=$DPY \
    "$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
wait_for 80 ipc_ready || fatal "wmaker IPC did not come up"
sleep 0.5

check "baseline desktop is 0" desktop_is 0

# --- 1. Default MouseWheelAction=None: wheel does nothing -------------------
note "default wheel: MouseWheelAction=None, scroll does nothing"
xdo mousemove 512 384; sleep 0.1
xdo click 4; sleep 0.3
check "wheel up with default None stays at 0" desktop_is 0
xdo click 5; sleep 0.3
check "wheel down with default None stays at 0" desktop_is 0

# --- 2. MouseWheelAction=SwitchWorkspaces: wheel switches workspace ----------
note "configure MouseWheelAction=SwitchWorkspaces"
wdwrite WindowMaker MouseWheelAction SwitchWorkspaces
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3

xdo click 4; sleep 0.3
check "wheel up (Button4) advances to desktop 1" desktop_is 1
xdo click 4; sleep 0.3
check "wheel up (Button4) advances to desktop 2" desktop_is 2
xdo click 5; sleep 0.3
check "wheel down (Button5) goes back to desktop 1" desktop_is 1
xdo click 5; sleep 0.3
check "wheel down (Button5) goes back to desktop 0" desktop_is 0

# --- 3. MouseMiddleButtonAction=MoveToNextWorkspace --------------------------
note "configure MouseMiddleButtonAction=MoveToNextWorkspace"
wdwrite WindowMaker MouseMiddleButtonAction MoveToNextWorkspace
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3

xdo click 2; sleep 0.3
check "middle click advances to desktop 1" desktop_is 1
xdo click 2; sleep 0.3
check "middle click advances to desktop 2" desktop_is 2

# --- 4. MouseMiddleButtonAction=MoveToPrevWorkspace -------------------------
note "configure MouseMiddleButtonAction=MoveToPrevWorkspace"
wdwrite WindowMaker MouseMiddleButtonAction MoveToPrevWorkspace
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3

xdo click 2; sleep 0.3
check "middle click goes back to desktop 1" desktop_is 1
xdo click 2; sleep 0.3
check "middle click goes back to desktop 0" desktop_is 0

# --- 5. ExtraMouseButtons: Button10 via config ------------------------------
note "configure ExtraMouseButtons for Button10"
wdwrite WindowMaker ExtraMouseButtons '("10 MoveToNextWorkspace")'
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3

xdo click 10; sleep 0.3
check "Button10 advances to desktop 1" desktop_is 1
xdo click 10; sleep 0.3
check "Button10 advances to desktop 2" desktop_is 2

wdwrite WindowMaker ExtraMouseButtons '("10 MoveToPrevWorkspace")'
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3

xdo click 10; sleep 0.3
check "Button10 goes back to desktop 1" desktop_is 1
xdo click 10; sleep 0.3
check "Button10 goes back to desktop 0" desktop_is 0

# --- teardown ----------------------------------------------------------------
note "mouse-button drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
