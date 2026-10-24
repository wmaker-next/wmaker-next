#!/bin/sh
# Drive the SkipEmptyWorkspaces option: the Next/Prev/Left/Right/Up/Down
# view-switch paths and the Clip arrow buttons must skip workspaces
# holding no non-omnipresent windows while the flag is on, explicit jumps
# and window moves must not be filtered, and ws_advance must be
# suppressed. Asserts via _NET_CURRENT_DESKTOP / _NET_NUMBER_OF_DESKTOPS,
# occupancy seeded with xterm windows moved by the window-workspace verb.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${SKIP_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-skiptest.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/skip-empty-workspaces-drive.log
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
reconfigure() {
    msg -q reconfigure || fail "reconfigure rejected"
    sleep 0.3
}

current_desktop() {
    DISPLAY=$DPY xprop -root _NET_CURRENT_DESKTOP 2>/dev/null \
        | sed -n 's/.*_NET_CURRENT_DESKTOP(CARDINAL) = \([0-9]*\).*/\1/p'
}
desktop_is() { [ "$(current_desktop)" = "$1" ]; }
desktop_count() {
    DISPLAY=$DPY xprop -root _NET_NUMBER_OF_DESKTOPS 2>/dev/null \
        | sed -n 's/.*_NET_NUMBER_OF_DESKTOPS(CARDINAL) = \([0-9]*\).*/\1/p'
}

xt_client() {
    DISPLAY=$DPY xdotool search --name "^$1\$" 2>/dev/null | tail -n 1
}

# get_windows replies with all entries on a single line. Split at the
# "}, {" boundaries so the id match isolates one window's entry
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

is_sticky() {
    DISPLAY=$DPY xprop -id "$1" _NET_WM_STATE 2>/dev/null \
        | grep -q '_NET_WM_STATE_STICKY'
}
not_sticky() { ! is_sticky "$1"; }

set_layout() {
    DISPLAY=$DPY xprop -root -f _NET_DESKTOP_LAYOUT 32c \
        -set _NET_DESKTOP_LAYOUT "$1" >>"$LOG" 2>&1 || return 1
    wait_for 20 sh -c \
        "DISPLAY=$DPY xprop -root _NET_DESKTOP_LAYOUT 2>/dev/null | grep -F '= $1' >/dev/null"
}

set_skip() {
    wdwrite WindowMaker SkipEmptyWorkspaces "$1"
    reconfigure
}

for tool in Xvfb xdpyinfo xdotool xprop xterm; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
[ -x "$WDWRITE" ] || skip_all "util/.libs/wdwrite not built"
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"

export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = NO;
  DisableDrawers = YES;
  OmnipresentKey = "Control+Shift+O";
  LeftWorkspaceKey = "Control+Left";
  RightWorkspaceKey = "Control+Right";
  UpWorkspaceKey = "Control+Up";
  DownWorkspaceKey = "Control+Down";
}
EOF
cat >"$WMHOME/Defaults/WMState" <<'EOF'
{
  Workspaces = (
    { Name = "ws-0"; },
    { Name = "ws-1"; },
    { Name = "ws-2"; },
    { Name = "ws-3"; }
  );
}
EOF

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use, set SKIP_DRIVE_DISPLAY" >&2; exit 77
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
check "baseline has 4 desktops" [ "$(desktop_count)" = 4 ]

# --- 1. Skip ON, all workspaces empty: every direction no-ops -----------------
note "skip ON, no windows at all"
set_skip YES

msg -q workspace next || fail "workspace next rejected"
sleep 0.3
check "next with nothing occupied stays at 0" desktop_is 0
msg -q workspace prev || fail "workspace prev rejected"
sleep 0.3
check "prev with nothing occupied stays at 0" desktop_is 0

# --- 2. Baseline (skip OFF): next lands on the empty next workspace ---------
note "spawn xterm A on ws0, xterm B on ws2"
set_skip NO
DISPLAY=$DPY xterm -title wmskip-a -geometry 60x8+40+40 -e 'sleep 600' >>"$LOG" 2>&1 &
echo "$!" >> "$PIDFILE"
wait_for 60 sh -c '[ -n "$(DISPLAY='"$DPY"' xdotool search --name "^wmskip-a$" 2>/dev/null)" ]' \
    || fatal "xterm A did not map"
DISPLAY=$DPY xterm -title wmskip-b -geometry 60x8+120+120 -e 'sleep 600' >>"$LOG" 2>&1 &
echo "$!" >> "$PIDFILE"
wait_for 60 sh -c '[ -n "$(DISPLAY='"$DPY"' xdotool search --name "^wmskip-b$" 2>/dev/null)" ]' \
    || fatal "xterm B did not map"
sleep 0.3
AXID=$(xt_client wmskip-a)
BXID=$(xt_client wmskip-b)
AXIDHEX=$(printf '0x%x' "$AXID")
BXIDHEX=$(printf '0x%x' "$BXID")
[ -n "$AXID" ] && [ -n "$BXID" ] || fatal "could not resolve xterm XIDs"

msg -q -- window-workspace "$BXIDHEX" 2 || fatal "window-workspace B 2 rejected"
sleep 0.2
[ "$(win_primary "$BXIDHEX")" = "2" ] || fatal "B did not move to ws2"
check "setup: A on ws0, B on ws2, ws1/ws3 empty" desktop_is 0

msg -q workspace next || fail "workspace next rejected"
wait_for 20 desktop_is 1
check "skip OFF: next from 0 lands on empty ws1" desktop_is 1
msg -q workspace 0 || fail "workspace 0 rejected"
wait_for 20 desktop_is 0

# --- 3. Skip ON, linear next/prev --------------------------------------------
note "skip ON: next/prev skip empty ws1"
set_skip YES

msg -q workspace next || fail "workspace next rejected"
wait_for 20 desktop_is 2
check "skip ON: next from 0 lands on occupied ws2" desktop_is 2
msg -q workspace prev || fail "workspace prev rejected"
wait_for 20 desktop_is 0
check "skip ON: prev from 2 lands on occupied ws0" desktop_is 0

# --- 4. Skip ON, only the current workspace occupied: no-ops -----------------
note "skip ON: only ws0 occupied"
msg -q -- window-workspace "$BXIDHEX" 0 || fatal "window-workspace B 0 rejected"
sleep 0.2

msg -q workspace next || fail "workspace next rejected"
sleep 0.3
check "next from the only occupied ws stays at 0" desktop_is 0
msg -q workspace prev || fail "workspace prev rejected"
sleep 0.3
check "prev from the only occupied ws stays at 0" desktop_is 0

# --- 5. Skip ON + CycleWorkspaces ON: wrap and skip ---------------------------
note "skip ON + cycle ON: wrap skipping empty cells"
msg -q -- window-workspace "$BXIDHEX" 2 || fatal "window-workspace B 2 rejected"
sleep 0.2
wdwrite WindowMaker CycleWorkspaces YES
reconfigure

msg -q workspace 2 || fail "workspace 2 rejected"
wait_for 20 desktop_is 2
msg -q workspace next || fail "workspace next rejected"
wait_for 20 desktop_is 0
check "cycle+skip: next from 2 wraps past empty 3 to occupied 0" desktop_is 0
msg -q workspace prev || fail "workspace prev rejected"
wait_for 20 desktop_is 2
check "cycle+skip: prev from 0 wraps past empty 3 to occupied 2" desktop_is 2
check "cycle+skip: desktop count unchanged" [ "$(desktop_count)" = 4 ]

# --- 6. Skip ON + AdvanceToNewWorkspace ON: advance suppressed ----------------
note "skip ON + advance ON: advance suppressed"
wdwrite WindowMaker CycleWorkspaces NO
wdwrite WindowMaker AdvanceToNewWorkspace YES
reconfigure

msg -q workspace 3 || fail "workspace 3 rejected"
wait_for 20 desktop_is 3
msg -q workspace next || fail "workspace next rejected"
sleep 0.3
check "skip+advance: next at top stays at 3" desktop_is 3
check "skip+advance: desktop count did not grow" [ "$(desktop_count)" = 4 ]

wdwrite WindowMaker CycleWorkspaces YES
reconfigure
msg -q workspace next || fail "workspace next rejected"
wait_for 20 desktop_is 0
check "skip+advance+cycle: next at top wraps to occupied 0" desktop_is 0
check "skip+advance+cycle: desktop count still 4" [ "$(desktop_count)" = 4 ]

wdwrite WindowMaker CycleWorkspaces NO
wdwrite WindowMaker AdvanceToNewWorkspace NO
reconfigure

# --- 7. Omnipresent window does not count as occupied ------------------------
note "skip ON: omnipresent-only workspace is treated as empty"
msg -q -- window-workspace "$AXIDHEX" 1 || fatal "window-workspace A 1 rejected"
sleep 0.2
msg -q workspace 1 || fail "workspace 1 rejected"
wait_for 20 desktop_is 1
DISPLAY=$DPY xdotool windowactivate "$AXID" >>"$LOG" 2>&1
DISPLAY=$DPY xdotool key --clearmodifiers ctrl+shift+o >>"$LOG" 2>&1
wait_for 20 is_sticky "$AXID" || fatal "A did not become omnipresent"

msg -q workspace 0 || fail "workspace 0 rejected"
wait_for 20 desktop_is 0
msg -q workspace next || fail "workspace next rejected"
wait_for 20 desktop_is 2
check "omnipresent-only ws1 is skipped on next (0 -> 2)" desktop_is 2

# anchor B on ws0 so prev from 2 lands on 0 only if omni-only ws1 is skipped
msg -q -- window-workspace "$BXIDHEX" 0 || fatal "window-workspace B 0 rejected"
sleep 0.2
msg -q workspace 2 || fail "workspace 2 rejected"
wait_for 20 desktop_is 2
msg -q workspace prev || fail "workspace prev rejected"
wait_for 20 desktop_is 0
check "omnipresent-only ws1 is skipped on prev (2 -> 0)" desktop_is 0

DISPLAY=$DPY xdotool windowactivate "$AXID" >>"$LOG" 2>&1
DISPLAY=$DPY xdotool key --clearmodifiers ctrl+shift+o >>"$LOG" 2>&1
wait_for 20 not_sticky "$AXID" || fatal "A did not leave omnipresent state"

# --- 8. Explicit index / last not filtered ------------------------------------
note "explicit jumps never filtered"
msg -q workspace 2 || fail "workspace 2 rejected"
wait_for 20 desktop_is 2
check "workspace 2 lands on empty ws2" desktop_is 2
msg -q workspace 3 || fail "workspace 3 rejected"
wait_for 20 desktop_is 3
check "workspace 3 lands on empty ws3" desktop_is 3
msg -q workspace last || fail "workspace last rejected"
wait_for 20 desktop_is 2
check "workspace last returns to 2" desktop_is 2

# --- 9. Moving a window is not filtered --------------------------------------
note "move-to-workspace never filtered"
msg -q -- window-workspace "$BXIDHEX" 3 || fatal "window-workspace B 3 rejected"
sleep 0.2
check "window moved onto empty ws3 (primary 3)" [ "$(win_primary "$BXIDHEX")" = "3" ]
check "window mask replaced by ws3" [ "$(win_mask "$BXIDHEX")" = "3" ]

# --- 10. Directional skip (1x4 and 4x1 grid) ----------------------------------
note "directional skip"
msg -q -- window-workspace "$AXIDHEX" 0 || fatal "window-workspace A 0 rejected"
sleep 0.2

set_layout "0, 4, 1, 0" || fatal "could not set 1x4 layout"
msg -q workspace 0 || fail "workspace 0 rejected"
wait_for 20 desktop_is 0
DISPLAY=$DPY xdotool key ctrl+Right >>"$LOG" 2>&1
wait_for 20 desktop_is 3
check "Right from ws0 skips 1,2 and lands on ws3" desktop_is 3
DISPLAY=$DPY xdotool key ctrl+Left >>"$LOG" 2>&1
wait_for 20 desktop_is 0
check "Left from ws3 skips 2,1 and lands on ws0" desktop_is 0

set_layout "0, 1, 4, 0" || fatal "could not set 4x1 layout"
msg -q workspace 0 || fail "workspace 0 rejected"
wait_for 20 desktop_is 0
DISPLAY=$DPY xdotool key ctrl+Down >>"$LOG" 2>&1
wait_for 20 desktop_is 3
check "Down from ws0 skips 1,2 and lands on ws3" desktop_is 3
DISPLAY=$DPY xdotool key ctrl+Up >>"$LOG" 2>&1
wait_for 20 desktop_is 0
check "Up from ws3 skips 2,1 and lands on ws0" desktop_is 0

# --- 11. Runtime toggle via reconfigure ---------------------------------------
note "runtime toggling of SkipEmptyWorkspaces"
set_skip NO
msg -q workspace 0 || fail "workspace 0 rejected"
wait_for 20 desktop_is 0
msg -q workspace next || fail "workspace next rejected"
wait_for 20 desktop_is 1
check "skip flipped OFF at runtime: next from 0 lands on 1" desktop_is 1

set_skip YES
msg -q workspace 0 || fail "workspace 0 rejected"
wait_for 20 desktop_is 0
msg -q workspace next || fail "workspace next rejected"
wait_for 20 desktop_is 3
check "skip flipped ON at runtime: next from 0 lands on 3" desktop_is 3

# --- 12. Clip arrow buttons honor skip ----------------------------------------
# arrow triangles for ICON_SIZE=64 (pt=25): FORWARD py <= px-38, REWIND px <= py-38
note "clip arrows honor skip"
msg -q workspace 0 || fail "workspace 0 rejected"
wait_for 20 desktop_is 0
DISPLAY=$DPY xdotool mousemove 60 10 click 1 >>"$LOG" 2>&1
wait_for 20 desktop_is 3
check "clip FORWARD from ws0 skips 1,2 and lands on ws3" desktop_is 3
DISPLAY=$DPY xdotool mousemove 10 60 click 1 >>"$LOG" 2>&1
wait_for 20 desktop_is 0
check "clip REWIND from ws3 skips 2,1 and lands on ws0" desktop_is 0

msg -q workspace 3 || fail "workspace 3 rejected"
wait_for 20 desktop_is 3
DISPLAY=$DPY xdotool mousemove 60 10 keydown ctrl click 1 keyup ctrl >>"$LOG" 2>&1
sleep 0.3
check "ctrl+clip FORWARD at top stays at 3 (advance suppressed)" desktop_is 3
check "desktop count did not grow on ctrl+click" [ "$(desktop_count)" = 4 ]

# --- 13. Negative control: advance works again once skip is off ----------------
note "advance still live when skip is off"
wdwrite WindowMaker AdvanceToNewWorkspace YES
set_skip NO
msg -q workspace 3 || fail "workspace 3 rejected"
wait_for 20 desktop_is 3
msg -q workspace next || fail "workspace next rejected"
wait_for 20 desktop_is 4
check "skip OFF again: next at top advances to ws4" desktop_is 4
check "desktop count grew to 5" [ "$(desktop_count)" = 5 ]

# --- teardown ----------------------------------------------------------------
note "skip-empty-workspaces drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
