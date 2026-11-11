#!/bin/sh
# Drive multi-workspace membership under Xvfb: spawn one xterm, add and
# remove workspace bits over IPC, assert visibility and primary tracking,
# then restart wmaker in place and check the mask survived via
# _WINDOWMAKER_STATE.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${WSMASK_DRIVE_DISPLAY:-:96}
RUNTIME=$(mktemp -d /tmp/wsmask-drive.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/wsmask-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
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
cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
    OmnipresentKey = "Control+Shift+O";
}
EOF
cat >"$WMHOME/Defaults/WMState" <<'EOF'
{
  Workspaces = ( { Name = "wsa"; }, { Name = "wsb"; }, { Name = "wsc"; } );
}
EOF
export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY already in use, set WSMASK_DRIVE_DISPLAY" >&2
    exit 1
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

note "start wmaker"
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

# --- helpers ---------------------------------------------------------------

xt_client() {
    DISPLAY=$DPY xdotool search --name "^$1\$" 2>/dev/null | tail -n 1
}

# get_windows entry for one XID. Proplist output is a single line and
# bare-identifier values are unquoted, so match id = 0x..., and rely on
# the test only ever managing one client at a time.
win_entry() {
    msg get_windows 2>/dev/null | grep -F "id = $1;"
}

win_primary() {
    win_entry "$1" | sed -n 's/.*workspace = \([0-9]*\);.*/\1/p'
}

win_mask() {
    win_entry "$1" | sed -n 's/.*workspaces = (\([^)]*\));.*/\1/p' \
                   | tr -d ' '
}

is_mapped() {
    DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null | grep -q 'Map State: IsViewable'
}

# --- spawn client on workspace 0 -------------------------------------------

note "spawn xterm on wsa"
xtrace -D 19010 -d "$DPY" -c -o /tmp/xterm-trace.log -n --waitforclient >/dev/null 2>&1 &
echo $! >> "$PIDFILE"
sleep 1
DISPLAY=:19010 xterm -title wmx -geometry 60x8+40+40 -e 'sleep 300' >>"$LOG" 2>&1 &
echo "$!" >> "$PIDFILE"
xt_mapped() { [ -n "$(xt_client wmx)" ]; }
wait_for 60 xt_mapped || fail "xterm did not map"
sleep 0.3
XID=$(xt_client wmx)
XIDHEX=$(printf '0x%x' "$XID")
[ -n "$XID" ] || fail "could not resolve xterm XID"

# baseline: primary 0, mask = (0)
[ "$(win_primary "$XIDHEX")" = "0" ] || fail "baseline primary != 0"
[ "$(win_mask "$XIDHEX")" = "0" ]    || fail "baseline mask != (0)"
ok "baseline primary=0 mask=(0)"

# --- reject malformed membership specs ------------------------------------

note "reject malformed membership specs"
msg -q -- window-workspace "$XIDHEX" + && fail "window-workspace + was accepted"
msg -q -- window-workspace "$XIDHEX" - && fail "window-workspace - was accepted"
[ "$(win_mask "$XIDHEX")" = "0" ] || fail "malformed spec changed mask"
ok "malformed membership specs rejected"

# --- add workspace 1 -------------------------------------------------------

note "add membership +1"
msg -q -- window-workspace "$XIDHEX" +1 || fail "window-workspace +1 rejected"
sleep 0.1
[ "$(win_primary "$XIDHEX")" = "0" ] || fail "+1 changed primary"
[ "$(win_mask "$XIDHEX")" = "0,1" ]  || fail "+1 mask != (0,1) got $(win_mask "$XIDHEX")"
ok "+1 primary=0 mask=(0,1)"

# --- visible on both members, hidden elsewhere -----------------------------

note "switch to wsb"
msg -q workspace 1 || fail "workspace 1 rejected"
sleep 0.3
is_mapped "$XID" || fail "not mapped on member workspace 1"
ok "mapped on wsb"

note "switch to wsc"
msg -q workspace 2 || fail "workspace 2 rejected"
sleep 0.3
is_mapped "$XID" && fail "still mapped on non-member workspace 2"
ok "unmapped on wsc"

msg -q workspace 0
sleep 0.3
is_mapped "$XID" || fail "not remapped on wsa"

# --- remove primary: current-if-member else lowest -------------------------

note "remove primary -0 while viewing wsa"
msg -q -- window-workspace "$XIDHEX" -0 || fail "window-workspace -0 rejected"
sleep 0.3
# mask was (0,1), removed 0, current=0 not in remaining mask, so
# primary should fall back to lowest bit = 1
[ "$(win_primary "$XIDHEX")" = "1" ] || fail "-0 primary != 1 got $(win_primary "$XIDHEX")"
[ "$(win_mask "$XIDHEX")" = "1" ]    || fail "-0 mask != (1)"
is_mapped "$XID" && fail "still mapped after removing current membership"
ok "-0 primary=1 mask=(1) unmapped"

# --- last-bit refusal ------------------------------------------------------

note "remove last bit -1"
msg -q -- window-workspace "$XIDHEX" -1 || fail "window-workspace -1 errored"
sleep 0.1
[ "$(win_mask "$XIDHEX")" = "1" ] || fail "last-bit removal was not refused"
ok "-1 refused, mask still (1)"

# --- current-if-member primary pick ---------------------------------------

note "add +0 +2 then remove primary while viewing a member"
msg -q -- window-workspace "$XIDHEX" +0
msg -q -- window-workspace "$XIDHEX" +2
sleep 0.1
[ "$(win_mask "$XIDHEX")" = "0,1,2" ] || fail "mask != (0,1,2) got $(win_mask "$XIDHEX")"
msg -q workspace 2
sleep 0.3
# primary is still 1, remove it, current=2 is a member so primary should
# become 2 not lowest(0)
msg -q -- window-workspace "$XIDHEX" -1 || fail "window-workspace -1 rejected"
sleep 0.1
[ "$(win_primary "$XIDHEX")" = "2" ] || fail "-1 primary != current(2) got $(win_primary "$XIDHEX")"
[ "$(win_mask "$XIDHEX")" = "0,2" ]  || fail "-1 mask != (0,2)"
is_mapped "$XID" || fail "unmapped despite still being a member of current"
ok "-1 primary=current(2) mask=(0,2) mapped"

# --- restart survival ------------------------------------------------------

note "restart wmaker"
msg -q restart || fail "restart rejected"
sleep 0.5
wait_for 120 ipc_ready || fail "wmaker did not come back after restart"
WMAKER_PID=$(msg get_version 2>/dev/null | sed -n 's/.*pid = \([0-9]*\);.*/\1/p')
[ -n "$WMAKER_PID" ] && echo "$WMAKER_PID" >> "$PIDFILE"
sleep 0.3

[ "$(win_primary "$XIDHEX")" = "2" ] || fail "restart lost primary got $(win_primary "$XIDHEX")"
[ "$(win_mask "$XIDHEX")" = "0,2" ]  || fail "restart lost mask got $(win_mask "$XIDHEX")"
ok "restart preserved primary=2 mask=(0,2)"

# --- runtime omnipresent survives restart ----------------------------------

note "make window omnipresent through its shortcut"
DISPLAY=$DPY xdotool windowactivate "$XID"
DISPLAY=$DPY xdotool key --clearmodifiers ctrl+shift+o
is_sticky() {
    DISPLAY=$DPY xprop -id "$XID" _NET_WM_STATE 2>/dev/null \
        | grep -q '_NET_WM_STATE_STICKY'
}
wait_for 20 is_sticky || fail "window did not become omnipresent"

note "restart omnipresent window"
msg -q restart || fail "restart rejected"
sleep 0.5
wait_for 120 ipc_ready || fail "wmaker did not come back after omnipresent restart"
wait_for 20 is_sticky || fail "restart lost omnipresent state"
ok "restart preserved omnipresent state"

note "turn off omnipresent state"
DISPLAY=$DPY xdotool windowactivate "$XID"
DISPLAY=$DPY xdotool key --clearmodifiers ctrl+shift+o
not_sticky() { ! is_sticky; }
wait_for 20 not_sticky || fail "window did not leave omnipresent state"

# --- plain N replaces the mask --------------------------------------------

note "plain move to 1 replaces mask"
msg -q -- window-workspace "$XIDHEX" 1 || fail "window-workspace 1 rejected"
sleep 0.1
[ "$(win_primary "$XIDHEX")" = "1" ] || fail "move primary != 1"
[ "$(win_mask "$XIDHEX")" = "1" ]    || fail "move mask != (1)"
ok "move-to-1 primary=1 mask=(1)"

note "PASS"
exit 0
