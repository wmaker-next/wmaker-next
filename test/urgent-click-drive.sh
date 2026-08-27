#!/bin/sh
# Clicking/double-clicking the bouncing icon of an application whose
# window asks for attention (XUrgencyHint / _NET_WM_STATE_DEMANDS_
# ATTENTION) must activate that window: switch to its workspace,
# deiconify/unshade, raise and focus it. Covers the floating appicon
# (src/appicon.c iconDblClick) and a docked running tile (src/dock.c
# iconDblClick). Asserts via _NET_ACTIVE_WINDOW, _NET_CURRENT_DESKTOP
# and WM_STATE/_NET_WM_STATE on the client.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${URGENT_DRIVE_DISPLAY:-:113}
RUNTIME=$(mktemp -d /tmp/wmaker-urgent-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=$(mktemp /tmp/urgent-click-drive.XXXXXX)
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
    uc_desc=$1; shift
    if "$@"; then ok "$uc_desc"; else fail "$uc_desc"; fi
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
current_desktop() {
    DISPLAY=$DPY xprop -root _NET_CURRENT_DESKTOP 2>/dev/null \
        | sed -n 's/.*= \([0-9]*\).*/\1/p'
}
desktop_is() { [ "$(current_desktop)" = "$1" ]; }

has_demands_attention() {
    DISPLAY=$DPY xprop -id "$1" _NET_WM_STATE 2>/dev/null \
        | grep -qF _NET_WM_STATE_DEMANDS_ATTENTION
}
lacks_demands_attention() {
    DISPLAY=$DPY xprop -id "$1" _NET_WM_STATE 2>/dev/null \
        | grep -qvF _NET_WM_STATE_DEMANDS_ATTENTION
}
window_state_is() {
    DISPLAY=$DPY xprop -id "$1" WM_STATE 2>/dev/null | grep -q "$2\$"
}

mapped_named() {
    DISPLAY=$DPY xdotool search --name "^$1\$" 2>/dev/null | grep -q .
}

# window ids of the viewable ICON_SIZE toplevel icons outside the dock
# column; icon core windows carry no WM_NAME so identification is
# positional (a solo app before the focus holder exists)
viewable_icons() {
    for uiw in $(DISPLAY=$DPY xwininfo -root -children 2>/dev/null \
                         | awk '/^     0x/ && $(NF-1) ~ /^64x64\+/ {print $1}'); do
        DISPLAY=$DPY xwininfo -id "$uiw" 2>/dev/null | grep -q 'Map State: IsViewable' \
            || continue
        case "$uiw" in
            "") ;;
            *) echo "$uiw" ;;
        esac
    done
}

dblclick_at() {
    # centre of the given icon window
    set -- $(DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null \
                 | awk '/Absolute upper-left X:/ {x=$4}
                        /Absolute upper-left Y:/ {y=$4}
                        END {print x + 32, y + 32}')
    DISPLAY=$DPY xdotool mousemove --sync "$1" "$2" >>"$LOG" 2>&1
    DISPLAY=$DPY xdotool click --repeat 2 --delay 120 1 >>"$LOG" 2>&1
    sleep 0.4
}

for tool in Xvfb xdpyinfo xdotool xprop xterm wmctrl; do
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
    echo "display $DPY is already in use" >&2; exit 1
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 ${SCR_W}x${SCR_H}x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
wait_for 40 sh -c "kill -0 $XVFB_PID 2>/dev/null && DISPLAY=$DPY xdpyinfo >/dev/null 2>&1" \
    || fatal "Xvfb failed to start"

# --- boot 1: dock disabled, floating appicons only ---------------------------
note "boot wmaker without dock"
cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  DisableAnimations = YES;
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

start_wmaker

note "launch the urgent client"
DISPLAY=$DPY xterm -title uc-1 -geometry 60x4+100+100 -e 'sleep 600' >>"$LOG" 2>&1 &
XT1=$!
echo "$XT1" >> "$PIDFILE"
wait_for 60 mapped_named uc-1 || fatal "uc-1 did not map"
W1=$(DISPLAY=$DPY xdotool search --name '^uc-1$' 2>/dev/null | tail -n 1)
[ -n "$W1" ] || fatal "could not find uc-1 window id"
note "client: uc-1=$W1"
sleep 0.5

# exactly one floating appicon exists at this point
ICON1=$(viewable_icons | head -n 1)
[ -n "$ICON1" ] || fatal "could not locate the uc-1 appicon"
[ "$(viewable_icons | wc -l)" = "1" ] || fatal "expected a single appicon"
note "uc-1 appicon: $ICON1"

# --- 1. urgent window on another workspace -----------------------------------
note "uc-1 moves to ws-1 and demands attention"
DISPLAY=$DPY wmctrl -i -r "$W1" -t 1 2>>"$LOG"
msg -q workspace 0 >/dev/null
sleep 0.5
check "baseline: current desktop is 0" desktop_is 0
DISPLAY=$DPY wmctrl -i -b add,demands_attention -r "$W1" 2>>"$LOG"
check "uc-1 advertises DEMANDS_ATTENTION" wait_for 30 has_demands_attention "$W1"

dblclick_at "$ICON1"
check "clicking the appicon switches to uc-1 workspace" desktop_is 1
check "clicking the appicon focuses uc-1" active_is "$W1"
check "focus cleared DEMANDS_ATTENTION" lacks_demands_attention "$W1"

# --- 2. miniaturized urgent window --------------------------------------------
note "uc-1 is iconified while demanding attention"
msg -q workspace 0 >/dev/null
sleep 0.5
DISPLAY=$DPY xdotool windowminimize "$W1" >>"$LOG" 2>&1
check "uc-1 is iconic" wait_for 30 window_state_is "$W1" Iconic
DISPLAY=$DPY wmctrl -i -b add,demands_attention -r "$W1" 2>>"$LOG"
check "iconic uc-1 advertises DEMANDS_ATTENTION" wait_for 30 has_demands_attention "$W1"

dblclick_at "$ICON1"
check "clicking the appicon deiconifies uc-1" wait_for 30 window_state_is "$W1" Normal
check "deiconified uc-1 holds focus" active_is "$W1"
check "activation followed the window to its workspace" desktop_is 1

# --- 3. same-workspace raise/focus ---------------------------------------------
note "focus holder mapped, uc-1 demands attention on the same workspace"
msg -q workspace 1 >/dev/null
sleep 0.5
DISPLAY=$DPY xterm -title uc-bg -geometry 60x4+300+300 -e 'sleep 600' >>"$LOG" 2>&1 &
XTBG=$!
echo "$XTBG" >> "$PIDFILE"
wait_for 60 mapped_named uc-bg || fatal "uc-bg did not map"
WBG=$(DISPLAY=$DPY xdotool search --name '^uc-bg$' 2>/dev/null | tail -n 1)
sleep 0.3
DISPLAY=$DPY wmctrl -i -a "$WBG" 2>>"$LOG"
sleep 0.3
check "baseline: uc-bg holds focus" active_is "$WBG"
WS_BEFORE=$(current_desktop)
DISPLAY=$DPY wmctrl -i -b add,demands_attention -r "$W1" 2>>"$LOG"
check "uc-1 advertises DEMANDS_ATTENTION again" wait_for 30 has_demands_attention "$W1"

dblclick_at "$ICON1"
check "clicking the appicon focuses uc-1 in place" active_is "$W1"
check "workspace unchanged" desktop_is "$WS_BEFORE"

kill "$XTBG" "$XT1" 2>/dev/null

# --- boot 2: dock enabled with an auto-launched running tile -------------------
note "restart wmaker with a docked auto-launched xterm tile"
msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"
sleep 0.5

cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = NO;
  DisableClip = YES;
  DisableDrawers = YES;
  DisableAnimations = YES;
}
EOF
cat >"$WMHOME/Defaults/WMState" <<EOF
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
        Command = "/usr/bin/xterm -title uc-dock -geometry 60x4+200+200 -e sleep 600";
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
    { Name = "ws-0"; },
    { Name = "ws-1"; }
  );
}
EOF

start_wmaker
wait_for 80 mapped_named uc-dock || fatal "auto-launched uc-dock did not map"
WD=$(DISPLAY=$DPY xdotool search --name '^uc-dock$' 2>/dev/null | tail -n 1)
[ -n "$WD" ] || fatal "could not find uc-dock window id"
DISPLAY=$DPY xterm -title uc-bg2 -geometry 60x4+400+400 -e 'sleep 600' >>"$LOG" 2>&1 &
XTBG2=$!
echo "$XTBG2" >> "$PIDFILE"
wait_for 60 mapped_named uc-bg2 || fatal "uc-bg2 did not map"
WBG2=$(DISPLAY=$DPY xdotool search --name '^uc-bg2$' 2>/dev/null | tail -n 1)

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

DISPLAY=$DPY wmctrl -i -a "$WBG2" 2>>"$LOG"
sleep 0.3
DISPLAY=$DPY wmctrl -i -r "$WD" -t 1 2>>"$LOG"
msg -q workspace 0 >/dev/null
sleep 0.5
DISPLAY=$DPY wmctrl -i -b add,demands_attention -r "$WD" 2>>"$LOG"
check "docked uc-dock advertises DEMANDS_ATTENTION" wait_for 30 has_demands_attention "$WD"

dblclick_at "$DOCK_TILE"
check "clicking the dock tile switches workspaces" desktop_is 1
check "clicking the dock tile focuses uc-dock" active_is "$WD"

kill "$XTBG2" 2>/dev/null
msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

note "urgent-click drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"

[ "$FAILED" -eq 0 ] && [ "$PASSED" -gt 0 ]
