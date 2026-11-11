#!/bin/sh
# Drive key-binding actions that the other drivers only reach through IPC
# or the window menu: direct/layer workspace switches, workspace
# membership toggles, window shortcuts, stacking levels, center, shade,
# maximize, layout keys, the session commands and the popup panels.
#
# All bindings use Control+Shift+<letter> so no default binding changes;
# the driver asserts through EWMH properties and the IPC workspace/window
# queries. Runs against an isolated WMAKER_USER_ROOT and XDG_RUNTIME_DIR,
# tracks every PID it spawns, the live desktop is untouched.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${KEYBIND_ACTIONS_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-keybind-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/keybind-actions-drive.log
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

hexid() { printf '0x%x' "$1"; }

win_entry() {
    msg get_windows 2>/dev/null | sed 's/}, *{/}\n{/g' | grep -F "id = $(hexid "$1");"
}
win_workspaces() {
    win_entry "$1" | sed -n 's/.*workspaces = (\([^)]*\));.*/\1/p' | tr -d ' '
}
win_workspace() {
    win_entry "$1" | sed -n 's/.*workspace = \([0-9-]*\);.*/\1/p'
}
workspaces_has() {
    win_workspaces "$1" | grep -qE "(^|,)$2(,|$)"
}
stack_pos() {
    DISPLAY=$DPY xprop -root _NET_CLIENT_LIST_STACKING 2>/dev/null \
        | sed 's/,/\n/g' | grep -n -F "$(hexid "$1")" | cut -d: -f1
}
stacked_above() {
    _pa=$(stack_pos "$1"); _pb=$(stack_pos "$2")
    [ -n "$_pa" ] && [ -n "$_pb" ] && [ "$_pa" -gt "$_pb" ]
}
focus_win() {
    xdo windowactivate "$1" 2>/dev/null
    wait_for 20 active_is "$1"
}
raise_win() {
    focus_win "$1" || return 1
    msg -q action Raise "$1" >/dev/null 2>&1
    sleep 0.2
}

current_desktop() {
    DISPLAY=$DPY xprop -root _NET_CURRENT_DESKTOP 2>/dev/null \
        | sed -n 's/.*= \([0-9]*\).*/\1/p'
}
desktop_is() { [ "$(current_desktop)" = "$1" ]; }

current_layout() {
    msg get_workspaces 2>/dev/null | tr '}' '\n' \
        | grep -F 'current = YES;' \
        | sed -n 's/.*layout = \([A-Za-z_]*\);.*/\1/p'
}
master_count() {
    msg get_workspaces 2>/dev/null | tr '}' '\n' \
        | grep -F 'current = YES;' \
        | sed -n 's/.*master_count = \([0-9]*\);.*/\1/p'
}
master_ratio() {
    msg get_workspaces 2>/dev/null | tr '}' '\n' \
        | grep -F 'current = YES;' \
        | sed -n 's/.*master_ratio = \([0-9.]*\);.*/\1/p'
}

has_state() {
    DISPLAY=$DPY xprop -id "$1" _NET_WM_STATE 2>/dev/null | grep -q -- "$2"
}
no_state() {
    ! has_state "$1" "$2"
}
active_is() {
    [ "$(xdo getactivewindow 2>/dev/null)" = "$1" ]
}
layout_is() { [ "$(current_layout)" = "$1" ]; }
layout_is_not() { [ "$(current_layout)" != "$1" ]; }
master_count_gt() { [ "$(master_count)" -gt "$1" ]; }
master_count_le() { [ "$(master_count)" -le "$1" ]; }
master_ratio_gt() { awk "BEGIN { exit !($(master_ratio) > $1) }"; }
master_ratio_le() { awk "BEGIN { exit !($(master_ratio) <= $1) }"; }
visible_gt() { [ "$(visible_or_count)" -gt "$1" ]; }

visible_or_count() {
    xdo search --onlyvisible --name "" 2>/dev/null | wc -l
}

for tool in Xvfb xdpyinfo xdotool xprop xterm; do
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
  EnableWorkspacePager = YES;
  LockScreenCommand = "touch $RUNTIME/lock-sentinel";
  ClipboardCommand = "touch $RUNTIME/clip-sentinel";
  Workspace1Key = "Control+Shift+q";
  Workspace2Key = "Control+Shift+w";
  Workspace3Key = "Control+Shift+e";
  NextWorkspaceKey = "Control+Shift+n";
  PrevWorkspaceKey = "Control+Shift+p";
  NextWorkspaceLayerKey = "Control+Shift+bracketright";
  PrevWorkspaceLayerKey = "Control+Shift+bracketleft";
  MoveToWorkspace2Key = "Control+Shift+m";
  ToggleWorkspace3Key = "Control+Shift+t";
  WindowShortcut1Key = "Control+Shift+a";
  KeepOnTopKey = "Control+Shift+o";
  KeepAtBottomKey = "Control+Shift+b";
  CenterKey = "Control+Shift+c";
  ShadeKey = "Control+Shift+h";
  MaximizeKey = "Control+Shift+x";
  RestoreGeometryKey = "Control+Shift+z";
  ToggleFullscreenKey = "Control+Shift+k";
  LayoutCycleKey = "Control+Shift+f";
  LayoutFloatKey = "Control+Shift+d";
  MasterCountIncKey = "Control+Shift+1";
  MasterCountDecKey = "Control+Shift+2";
  MasterRatioIncKey = "Control+Shift+3";
  MasterRatioDecKey = "Control+Shift+4";
  LockScreenKey = "Control+Shift+y";
  ClipboardKey = "Control+Shift+u";
  ShowKeybindsKey = "Control+Shift+i";
  WorkspaceMapKey = "Control+Shift+g";
  RunKey = "Control+Shift+r";
  WindowListKey = "Control+Shift+j";
}
EOF
cat >"$WMHOME/Defaults/WMState" <<'EOF'
{
  Workspaces = (
    { Name = "ws-0"; }, { Name = "ws-1"; }, { Name = "ws-2"; },
    { Name = "ws-3"; }, { Name = "ws-4"; }, { Name = "ws-5"; },
    { Name = "ws-6"; }, { Name = "ws-7"; }, { Name = "ws-8"; },
    { Name = "ws-9"; }, { Name = "ws-10"; }, { Name = "ws-11"; }
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

note "map two xterms"
DISPLAY=$DPY xterm -geometry 40x12+80+80 -T keybind-a -e 'sleep 300' >>"$LOG" 2>&1 &
A_PID=$!
echo "$A_PID" >> "$PIDFILE"
DISPLAY=$DPY xterm -geometry 40x12+500+320 -T keybind-b -e 'sleep 300' >>"$LOG" 2>&1 &
B_PID=$!
echo "$B_PID" >> "$PIDFILE"
A_WID=
B_WID=
wait_clients() {
    A_WID=$(xdo search --onlyvisible --name '^keybind-a$' 2>/dev/null | head -n 1)
    B_WID=$(xdo search --onlyvisible --name '^keybind-b$' 2>/dev/null | head -n 1)
    [ -n "$A_WID" ] && [ -n "$B_WID" ]
}
wait_for 80 wait_clients || fatal "xterms did not map"
xdo windowactivate "$A_WID" 2>/dev/null
sleep 0.3
check "two clients managed" sh -c "[ -n \"$(win_entry "$A_WID")\" ] && [ -n \"$(win_entry "$B_WID")\" ]"

# --- 1. direct and step workspace bindings --------------------------------
note "direct workspace keys"
xdo key ctrl+shift+w >>"$LOG" 2>&1
wait_for 30 desktop_is 1 || fail "Workspace2Key did not switch to workspace 1"
check "Workspace2Key switches to workspace 1" desktop_is 1
xdo key ctrl+shift+q >>"$LOG" 2>&1
wait_for 30 desktop_is 0 || fail "Workspace1Key did not switch back to workspace 0"
check "Workspace1Key switches back to workspace 0" desktop_is 0

note "next/previous workspace keys"
xdo key ctrl+shift+n >>"$LOG" 2>&1
wait_for 30 desktop_is 1 || fail "NextWorkspaceKey did not advance"
check "NextWorkspaceKey advances to workspace 1" desktop_is 1
xdo key ctrl+shift+p >>"$LOG" 2>&1
wait_for 30 desktop_is 0 || fail "PrevWorkspaceKey did not go back"
check "PrevWorkspaceKey returns to workspace 0" desktop_is 0

note "workspace layer keys (12 workspaces)"
xdo key ctrl+shift+bracketright >>"$LOG" 2>&1
wait_for 30 desktop_is 10 || fail "NextWorkspaceLayerKey did not jump a layer"
check "NextWorkspaceLayerKey jumps to workspace 10" desktop_is 10
xdo key ctrl+shift+bracketleft >>"$LOG" 2>&1
wait_for 30 desktop_is 0 || fail "PrevWorkspaceLayerKey did not return"
check "PrevWorkspaceLayerKey returns to workspace 0" desktop_is 0

# --- 2. membership toggles and window shortcuts ---------------------------
note "move window to workspace 2 by key"
xdo windowactivate "$A_WID" 2>/dev/null
xdo key ctrl+shift+m >>"$LOG" 2>&1
wait_for 30 workspaces_has "$A_WID" 1 \
    || fail "MoveToWorkspace2Key did not add workspace 1 (got $(win_workspaces "$A_WID"))"
check "MoveToWorkspace2Key moved the window to workspace 1" \
    workspaces_has "$A_WID" 1
xdo key ctrl+shift+m >>"$LOG" 2>&1
sleep 0.3

note "toggle workspace membership by key"
xdo windowactivate "$A_WID" 2>/dev/null
xdo key ctrl+shift+t >>"$LOG" 2>&1
wait_for 30 workspaces_has "$A_WID" 2 \
    || fail "ToggleWorkspace3Key did not add workspace 2 (got $(win_workspaces "$A_WID"))"
check "ToggleWorkspace3Key added workspace 2" workspaces_has "$A_WID" 2
xdo key ctrl+shift+t >>"$LOG" 2>&1
sleep 0.3
check "ToggleWorkspace3Key removed workspace 2" \
    sh -c "! printf '%s' \"$(win_workspaces "$A_WID")\" | grep -qE '(^|,)2(,|$)'"

note "window shortcut key"
xdo windowactivate "$A_WID" 2>/dev/null
xdo key ctrl+shift+a >>"$LOG" 2>&1
sleep 0.3
xdo windowactivate "$B_WID" 2>/dev/null
wait_for 30 active_is "$B_WID" || fail "could not focus the second client"
xdo key ctrl+shift+a >>"$LOG" 2>&1
wait_for 30 active_is "$A_WID" || fail "WindowShortcut1Key did not focus the stored window"
check "WindowShortcut1Key focuses the stored window" active_is "$A_WID"

# --- 3. stacking, geometry and state bindings -----------------------------
# A was moved to workspace 1 above; park the view there so every later
# activation keeps the same workspace and focus.
note "park both clients on workspace 0"
msg -q action MoveToWorkspace1 "$A_WID" >>"$LOG" 2>&1 \
    || fail "could not move client A back to workspace 0"
wait_for 20 workspaces_has "$A_WID" 0 || fail "client A did not return to workspace 0"
msg -q workspace 0 >>"$LOG" 2>&1 || fail "workspace 0 rejected"
wait_for 20 desktop_is 0 || fail "could not switch to workspace 0"
focus_win "$A_WID" || fail "could not focus client A"
check "client A is focused before the window-state keys" active_is "$A_WID"

note "keep on top / keep at bottom keys"
# the keyboard toggle changes the stacking level rather than the client
# flag, so raise a sibling explicitly and assert the visible stack order
raise_win "$A_WID"
focus_win "$A_WID"
check "client A is focused for the KeepOnTop off leg" active_is "$A_WID"
xdo key ctrl+shift+o >>"$LOG" 2>&1
raise_win "$B_WID"
wait_for 30 stacked_above "$A_WID" "$B_WID" \
    || fail "KeepOnTopKey did not keep the window above a raised sibling"
check "KeepOnTopKey keeps the window above a raised sibling" \
    stacked_above "$A_WID" "$B_WID"
wait_for 30 has_state "$A_WID" _NET_WM_STATE_ABOVE \
    || fail "KeepOnTopKey did not publish _NET_WM_STATE_ABOVE"
check "KeepOnTopKey publishes _NET_WM_STATE_ABOVE" \
    has_state "$A_WID" _NET_WM_STATE_ABOVE
focus_win "$A_WID"
xdo key ctrl+shift+o >>"$LOG" 2>&1
raise_win "$B_WID"
wait_for 30 stacked_above "$B_WID" "$A_WID" \
    || fail "KeepOnTopKey did not return the window to the normal level"
check "KeepOnTopKey returns the window to the normal level" \
    stacked_above "$B_WID" "$A_WID"
wait_for 30 no_state "$A_WID" _NET_WM_STATE_ABOVE \
    || fail "KeepOnTopKey left _NET_WM_STATE_ABOVE set"
check "KeepOnTopKey clears _NET_WM_STATE_ABOVE" \
    no_state "$A_WID" _NET_WM_STATE_ABOVE

focus_win "$A_WID"
xdo key ctrl+shift+b >>"$LOG" 2>&1
raise_win "$B_WID"
wait_for 30 stacked_above "$B_WID" "$A_WID" \
    || fail "KeepAtBottomKey did not sink the window below a raised sibling"
check "KeepAtBottomKey sinks the window below a raised sibling" \
    stacked_above "$B_WID" "$A_WID"
wait_for 30 has_state "$A_WID" _NET_WM_STATE_BELOW \
    || fail "KeepAtBottomKey did not publish _NET_WM_STATE_BELOW"
check "KeepAtBottomKey publishes _NET_WM_STATE_BELOW" \
    has_state "$A_WID" _NET_WM_STATE_BELOW
focus_win "$A_WID"
xdo key ctrl+shift+b >>"$LOG" 2>&1
raise_win "$A_WID"
wait_for 30 stacked_above "$A_WID" "$B_WID" \
    || fail "KeepAtBottomKey did not return the window to the normal level"
check "KeepAtBottomKey returns the window to the normal level" \
    stacked_above "$A_WID" "$B_WID"
wait_for 30 no_state "$A_WID" _NET_WM_STATE_BELOW \
    || fail "KeepAtBottomKey left _NET_WM_STATE_BELOW set"
check "KeepAtBottomKey clears _NET_WM_STATE_BELOW" \
    no_state "$A_WID" _NET_WM_STATE_BELOW

note "center key"
focus_win "$A_WID" || fail "could not focus client A for CenterKey"
DISPLAY=$DPY xdotool windowmove "$A_WID" 700 500 >>"$LOG" 2>&1
sleep 0.2
xdo key ctrl+shift+c >>"$LOG" 2>&1
sleep 0.4
AX=$(DISPLAY=$DPY xwininfo -id "$A_WID" | awk '/Absolute upper-left X/{print $4}')
AY=$(DISPLAY=$DPY xwininfo -id "$A_WID" | awk '/Absolute upper-left Y/{print $4}')
AW=$(DISPLAY=$DPY xwininfo -id "$A_WID" | awk '/Width:/{print $2}')
AH=$(DISPLAY=$DPY xwininfo -id "$A_WID" | awk '/Height:/{print $2}')
note "centered geometry ${AX:-?}+${AY:-?} ${AW:-?}x${AH:-?}"
check "CenterKey centers the window horizontally" \
    sh -c "[ -n '$AX' ] && [ $AX -ge 150 ] && [ $((AX + AW)) -le 900 ]"
check "CenterKey centers the window vertically" \
    sh -c "[ -n '$AY' ] && [ $AY -ge 100 ] && [ $((AY + AH)) -le 700 ]"

note "shade key"
focus_win "$A_WID" || fail "could not focus client A for ShadeKey"
xdo key ctrl+shift+h >>"$LOG" 2>&1
wait_for 30 has_state "$A_WID" _NET_WM_STATE_SHADED \
    || fail "ShadeKey did not set _NET_WM_STATE_SHADED"
check "ShadeKey shades the window" has_state "$A_WID" _NET_WM_STATE_SHADED
xdo key ctrl+shift+h >>"$LOG" 2>&1
wait_for 30 sh -c "! DISPLAY=$DPY xprop -id $A_WID _NET_WM_STATE 2>/dev/null | grep -q _NET_WM_STATE_SHADED" \
    || fail "ShadeKey did not unshade the window"
check "ShadeKey unshades the window" \
    sh -c "! DISPLAY=$DPY xprop -id $A_WID _NET_WM_STATE 2>/dev/null | grep -q _NET_WM_STATE_SHADED"

note "maximize key"
focus_win "$A_WID" || fail "could not focus client A for MaximizeKey"
xdo key ctrl+shift+x >>"$LOG" 2>&1
wait_for 40 has_state "$A_WID" _NET_WM_STATE_MAXIMIZED_VERT \
    || fail "MaximizeKey did not set the vertical maximize state"
check "MaximizeKey sets the vertical maximize state" \
    has_state "$A_WID" _NET_WM_STATE_MAXIMIZED_VERT
check "MaximizeKey sets the horizontal maximize state" \
    has_state "$A_WID" _NET_WM_STATE_MAXIMIZED_HORZ
xdo key ctrl+shift+x >>"$LOG" 2>&1
wait_for 40 sh -c "! DISPLAY=$DPY xprop -id $A_WID _NET_WM_STATE 2>/dev/null | grep -q _NET_WM_STATE_MAXIMIZED_VERT" \
    || fail "MaximizeKey did not unmaximize the window"
check "MaximizeKey unmaximizes the window" \
    sh -c "! DISPLAY=$DPY xprop -id $A_WID _NET_WM_STATE 2>/dev/null | grep -q _NET_WM_STATE_MAXIMIZED_VERT"

note "restore geometry key"
focus_win "$A_WID" || fail "could not focus client A for RestoreGeometryKey"
xdo key ctrl+shift+x >>"$LOG" 2>&1
wait_for 40 has_state "$A_WID" _NET_WM_STATE_MAXIMIZED_VERT \
    || fail "setup for RestoreGeometryKey did not maximize the window"
xdo key ctrl+shift+z >>"$LOG" 2>&1
wait_for 40 sh -c "! DISPLAY=$DPY xprop -id $A_WID _NET_WM_STATE 2>/dev/null | grep -q _NET_WM_STATE_MAXIMIZED_VERT" \
    || fail "RestoreGeometryKey did not unmaximize the window"
check "RestoreGeometryKey unmaximizes the window" \
    sh -c "! DISPLAY=$DPY xprop -id $A_WID _NET_WM_STATE 2>/dev/null | grep -q _NET_WM_STATE_MAXIMIZED_VERT"

note "restore geometry key, fullscreen then maximize"
focus_win "$A_WID" || fail "could not focus client A for RestoreGeometryKey"
xdo key ctrl+shift+x >>"$LOG" 2>&1
wait_for 40 has_state "$A_WID" _NET_WM_STATE_MAXIMIZED_VERT \
    || fail "setup for RestoreGeometryKey did not maximize the window"
xdo key ctrl+shift+k >>"$LOG" 2>&1
wait_for 40 has_state "$A_WID" _NET_WM_STATE_FULLSCREEN \
    || fail "setup for RestoreGeometryKey did not fullscreen the window"
xdo key ctrl+shift+z >>"$LOG" 2>&1
wait_for 40 no_state "$A_WID" _NET_WM_STATE_FULLSCREEN \
    || fail "RestoreGeometryKey did not exit fullscreen"
check "RestoreGeometryKey exits fullscreen first" \
    no_state "$A_WID" _NET_WM_STATE_FULLSCREEN
check "RestoreGeometryKey keeps the pre-fullscreen maximize state" \
    has_state "$A_WID" _NET_WM_STATE_MAXIMIZED_VERT
xdo key ctrl+shift+z >>"$LOG" 2>&1
wait_for 40 sh -c "! DISPLAY=$DPY xprop -id $A_WID _NET_WM_STATE 2>/dev/null | grep -q _NET_WM_STATE_MAXIMIZED_VERT" \
    || fail "RestoreGeometryKey did not unmaximize after fullscreen"
check "RestoreGeometryKey then unmaximizes the window" \
    sh -c "! DISPLAY=$DPY xprop -id $A_WID _NET_WM_STATE 2>/dev/null | grep -q _NET_WM_STATE_MAXIMIZED_VERT"

# --- 4. layout keys --------------------------------------------------------
note "layout cycle and float keys"
focus_win "$A_WID" || fail "could not focus client A for the layout keys"
check "client A is focused for the layout keys" active_is "$A_WID"
LAYOUT0=$(current_layout)
xdo key ctrl+shift+f >>"$LOG" 2>&1
wait_for 30 layout_is_not "$LAYOUT0" \
    || fail "LayoutCycleKey did not change the layout (still $LAYOUT0)"
check "LayoutCycleKey changes the workspace layout" \
    layout_is_not "$LAYOUT0"
xdo key ctrl+shift+d >>"$LOG" 2>&1
wait_for 30 layout_is float \
    || fail "LayoutFloatKey did not restore the float layout"
check "LayoutFloatKey restores the float layout" layout_is float

note "master count and ratio keys"
# tile is the first cycle target, set it again and adjust the master knobs
xdo key ctrl+shift+f >>"$LOG" 2>&1
sleep 0.3
MC0=$(master_count)
MR0=$(master_ratio)
xdo key ctrl+shift+1 >>"$LOG" 2>&1
wait_for 30 master_count_gt "$MC0" \
    || fail "MasterCountIncKey did not increase master_count"
check "MasterCountIncKey increases master_count" master_count_gt "$MC0"
xdo key ctrl+shift+2 >>"$LOG" 2>&1
sleep 0.3
check "MasterCountDecKey restores master_count" master_count_le "$MC0"
xdo key ctrl+shift+3 >>"$LOG" 2>&1
wait_for 30 master_ratio_gt "$MR0" \
    || fail "MasterRatioIncKey did not increase master_ratio"
check "MasterRatioIncKey increases master_ratio" master_ratio_gt "$MR0"
xdo key ctrl+shift+4 >>"$LOG" 2>&1
sleep 0.3
check "MasterRatioDecKey restores master_ratio" master_ratio_le "$MR0"
xdo key ctrl+shift+d >>"$LOG" 2>&1

# --- 5. session command and popup keys ------------------------------------
note "lock screen and clipboard command keys"
rm -f "$RUNTIME/lock-sentinel" "$RUNTIME/clip-sentinel"
xdo key ctrl+shift+y >>"$LOG" 2>&1
wait_for 30 [ -f "$RUNTIME/lock-sentinel" ] \
    || fail "LockScreenKey did not run LockScreenCommand"
check "LockScreenKey runs LockScreenCommand" [ -f "$RUNTIME/lock-sentinel" ]
xdo key ctrl+shift+u >>"$LOG" 2>&1
wait_for 30 [ -f "$RUNTIME/clip-sentinel" ] \
    || fail "ClipboardKey did not run ClipboardCommand"
check "ClipboardKey runs ClipboardCommand" [ -f "$RUNTIME/clip-sentinel" ]

note "popup keys"
popup_key() {
    _key=$1
    _what=$2
    _before=$(visible_or_count)
    xdo key "$_key" >>"$LOG" 2>&1
    wait_for 30 visible_gt "$_before" \
        || fail "$_what key did not map a panel"
    check "$_what key maps a panel" visible_gt "$_before"
    xdo key Escape >>"$LOG" 2>&1
    sleep 0.3
}
popup_key ctrl+shift+i ShowKeybinds
popup_key ctrl+shift+g WorkspaceMap
popup_key ctrl+shift+r Run
popup_key ctrl+shift+j WindowList

check "wmaker alive at the end" ipc_ready

# --- teardown --------------------------------------------------------------
note "keybind-actions drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

kill "$A_PID" "$B_PID" 2>/dev/null
msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
