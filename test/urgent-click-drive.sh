#!/bin/sh
# Clicking/double-clicking the bouncing icon of an application whose
# window asks for attention (XUrgencyHint / _NET_WM_STATE_DEMANDS_
# ATTENTION) must activate that window: switch to its workspace,
# deiconify/unshade, raise and focus it. Covers the floating appicon
# (src/appicon.c iconDblClick) and a docked running tile (src/dock.c
# iconDblClick). Asserts via _NET_ACTIVE_WINDOW, _NET_CURRENT_DESKTOP
# and WM_STATE/_NET_WM_STATE on the client.
#
# A third boot verifies the urgent titlebar blink (BlinkTitlebarWhenUrgent):
# an unfocused urgent window's titlebar must alternate between the
# focused and unfocused appearance, stop on focus, survive an
# iconify/deiconify round-trip, and turn off with the config key. The
# blink phases are proven by pixel-comparing the test window's titlebar
# crop against its own focused/unfocused reference crops (same window,
# hence identical texture-cache pixmaps), a second window holds the
# focus because a focused window rejects the demand.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${URGENT_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-urgent-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=$(mktemp /tmp/urgent-click-drive.XXXXXX)
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
WDWRITE="$ROOT/util/.libs/wdwrite"
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

wdwrite() {
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        DISPLAY=$DPY "$WDWRITE" "$@"
}

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

# --- urgent titlebar blink helpers --------------------------------------------
# frame core of the client (child-of-root ancestor of the frame)
blink_frame() {
    bfcur=$1
    bfroot=$(DISPLAY=$DPY xwininfo -root 2>/dev/null | awk '/Window id:/ {print $4; exit}')
    n=0
    while [ "$n" -lt 5 ]; do
        bfp=$(DISPLAY=$DPY xwininfo -tree -id "$bfcur" 2>/dev/null \
                  | awk '/Parent window id:/ {print $4; exit}')
        [ -n "$bfp" ] || return 1
        [ "$bfp" = "0x0" ] && return 1
        [ "$bfp" = "$bfroot" ] && { printf '%s\n' "$bfcur"; return 0; }
        bfcur=$bfp
        n=$((n + 1))
    done
    return 1
}

# titlebar + top border strip of client $1 to $2: from the frame's outer
# top-left down to the client's top, so the 2px border and the titlebar
# are in-crop but not the content area (a live xterm cursor would not be)
blink_titlebar_crop() {
    bcw=$1
    bout=$2
    bframe=$(blink_frame "$bcw") || return 1
    set -- $(DISPLAY=$DPY xwininfo -id "$bframe" 2>/dev/null \
                 | awk '/Absolute upper-left X:/ {x=$4}
                        /Absolute upper-left Y:/ {y=$4}
                        /Width:/ && !w {w=$2}
                        /Border width:/ {b=$3}
                        END {print x, y, w, b}')
    bfx=$1; bfy=$2; bfw=$3; bfb=$4
    bcy=$(DISPLAY=$DPY xwininfo -id "$bcw" 2>/dev/null \
              | awk '/Absolute upper-left Y:/ {print $4; exit}')
    bht=$((bcy - bfy))
    [ "$bht" -gt 0 ] || return 1
    DISPLAY=$DPY import -window root \
        -crop "$((bfw + 2 * bfb))x${bht}+$bfx+$bfy" \
        +repage "png:$bout" 2>>"$LOG" || return 1
}

# r,g,b of the pixel at $2,$3 inside image $1
blink_px() {
    DISPLAY=$DPY convert "$1" -crop "1x1+$2+$3" +repage txt:- 2>/dev/null \
        | awk -F'[(,) ]+' '/^0,0:/ {print $3","$4","$5}'
}

# window ids of the viewable ICON_SIZE toplevel icons outside the dock
# column, icon core windows carry no WM_NAME so identification is
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

for tool in Xvfb xdpyinfo xdotool xprop xterm wmctrl import convert; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
[ -x "$WDWRITE" ] || skip_all "util/.libs/wdwrite not built"
# wmctrl passes unknown property names through as raw atom names, so the
# demands_attention requests go through this EWMH sender instead
UREQ=$(mktemp /tmp/urgent-req.XXXXXX)
${CC:-cc} -std=gnu11 "$ROOT/test/urgent-req.c" -o "$UREQ" -lX11 >>"$LOG" 2>&1 \
    || skip_all "could not build test/urgent-req.c (needs libX11 headers)"
demand_attention() {
    DISPLAY=$DPY "$UREQ" "$1" "$2"
}

export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
SHOTDIR="$RUNTIME/shots"
mkdir -p "$SHOTDIR"

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use" >&2; exit 77
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
demand_attention "$W1" add 2>>"$LOG"
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
demand_attention "$W1" add 2>>"$LOG"
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
demand_attention "$W1" add 2>>"$LOG"
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
demand_attention "$WD" add 2>>"$LOG"
check "docked uc-dock advertises DEMANDS_ATTENTION" wait_for 30 has_demands_attention "$WD"

dblclick_at "$DOCK_TILE"
check "clicking the dock tile switches workspaces" desktop_is 1
check "clicking the dock tile focuses uc-dock" active_is "$WD"

kill "$XTBG2" 2>/dev/null
msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

# --- boot 3: urgent titlebar blink ---------------------------------------------
note "urgent titlebar blink (BlinkTitlebarWhenUrgent)"
cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  DisableAnimations = YES;
  FrameBorderWidth = 2;
  FrameBorderColor = red;
  FrameFocusedBorderColor = blue;
}
EOF
start_wmaker

# one test window and one focus holder, the test window's own focused
# and unfocused frames are the references (identical width, so the
# blink phases match them pixel for pixel)
DISPLAY=$DPY xterm -title uc-blk -geometry 50x10+100+100 -e 'sleep 600' >>"$LOG" 2>&1 &
XTB=$!
echo "$XTB" >> "$PIDFILE"
DISPLAY=$DPY xterm -title uc-foc -geometry 50x10+700+100 -e 'sleep 600' >>"$LOG" 2>&1 &
XTF=$!
echo "$XTF" >> "$PIDFILE"
n=0
until [ -n "$(DISPLAY=$DPY xdotool search --name '^uc-blk$' 2>/dev/null)" ] \
      && [ -n "$(DISPLAY=$DPY xdotool search --name '^uc-foc$' 2>/dev/null)" ]; do
    sleep 0.1
    n=$((n + 1))
    [ "$n" -ge 60 ] && fatal "blink test windows did not map"
done
WT=$(DISPLAY=$DPY xdotool search --name '^uc-blk$' 2>/dev/null | tail -n 1)
WF=$(DISPLAY=$DPY xdotool search --name '^uc-foc$' 2>/dev/null | tail -n 1)
sleep 0.5

# references from the test window itself, urgency is still off so the
# crops capture the plain focused/unfocused frames
DISPLAY=$DPY wmctrl -i -a "$WT" 2>>"$LOG"
check "test window holds focus" wait_for 30 active_is "$WT"
blink_titlebar_crop "$WT" "$SHOTDIR/ref-focused.png" || fatal "focused reference crop failed"
DISPLAY=$DPY wmctrl -i -a "$WF" 2>>"$LOG"
check "focus holder takes the focus" wait_for 30 active_is "$WF"
blink_titlebar_crop "$WT" "$SHOTDIR/ref-unfocused.png" || fatal "unfocused reference crop failed"
cmp -s "$SHOTDIR/ref-focused.png" "$SHOTDIR/ref-unfocused.png" \
    && fatal "focused/unfocused reference crops are identical, cannot distinguish states"
note "references captured"

# a focused window rejects the demand, so the holder keeps the focus
# while the test window advertises urgency
demand_attention "$WT" add 2>>"$LOG"
check "test window advertises DEMANDS_ATTENTION" wait_for 30 has_demands_attention "$WT"
BLINK_SAW_FOCUSED=0
BLINK_SAW_UNFOCUSED=0
i=0
while [ "$i" -lt 13 ]; do
    blink_titlebar_crop "$WT" "$SHOTDIR/blink-$i.png" || fatal "blink sample crop failed"
    cmp -s "$SHOTDIR/blink-$i.png" "$SHOTDIR/ref-focused.png" \
        && BLINK_SAW_FOCUSED=1
    cmp -s "$SHOTDIR/blink-$i.png" "$SHOTDIR/ref-unfocused.png" \
        && BLINK_SAW_UNFOCUSED=1
    sleep 0.15
    i=$((i + 1))
done
check "blink reached the focused phase" [ "$BLINK_SAW_FOCUSED" = 1 ]
check "blink reached the unfocused phase" [ "$BLINK_SAW_UNFOCUSED" = 1 ]

# border colour follows the phase, sampled from the same frame image
# (pixel (1,1) is the top-left corner of the 2px outer border)
bp_focus=$(blink_px "$SHOTDIR/ref-focused.png" 1 1)
bp_unfocus=$(blink_px "$SHOTDIR/ref-unfocused.png" 1 1)
check "focused border is blue" [ "$bp_focus" = "0,0,255" ]
check "unfocused border is red" [ "$bp_unfocus" = "255,0,0" ]

# focus the urgent window: the blink stops, the window keeps the
# focused look
DISPLAY=$DPY wmctrl -i -a "$WT" 2>>"$LOG"
check "focus cleared DEMANDS_ATTENTION" wait_for 30 lacks_demands_attention "$WT"
sleep 0.5
BLINK_FOCUS_STABLE=1
i=0
while [ "$i" -lt 6 ]; do
    blink_titlebar_crop "$WT" "$SHOTDIR/blink-focus-$i.png" || BLINK_FOCUS_STABLE=0
    cmp -s "$SHOTDIR/blink-focus-$i.png" "$SHOTDIR/ref-focused.png" || BLINK_FOCUS_STABLE=0
    sleep 0.2
    i=$((i + 1))
done
check "blink stopped after focus (frame stays focused)" [ "$BLINK_FOCUS_STABLE" = 1 ]

# iconify while urgent then deiconify: the blink resumes
DISPLAY=$DPY wmctrl -i -a "$WF" 2>>"$LOG"
sleep 0.3
demand_attention "$WT" add 2>>"$LOG"
wait_for 30 has_demands_attention "$WT"
DISPLAY=$DPY xdotool windowminimize "$WT" 2>>"$LOG"
wait_for 30 window_state_is "$WT" Iconic
DISPLAY=$DPY wmctrl -i -a "$WT" 2>>"$LOG"
wait_for 30 window_state_is "$WT" Normal
DISPLAY=$DPY wmctrl -i -a "$WF" 2>>"$LOG"
sleep 0.3
demand_attention "$WT" add 2>>"$LOG"
wait_for 30 has_demands_attention "$WT"
BLINK_RESUMED=0
i=0
while [ "$i" -lt 13 ]; do
    blink_titlebar_crop "$WT" "$SHOTDIR/blink-resume-$i.png" || i=$((i + 1))
    cmp -s "$SHOTDIR/blink-resume-$i.png" "$SHOTDIR/ref-focused.png" || BLINK_RESUMED=1
    sleep 0.15
    i=$((i + 1))
done
check "blink resumed after deiconify" [ "$BLINK_RESUMED" = 1 ]

# negative: disable the key via wdwrite + reconfigure, the urgent window
# must stay on the unfocused appearance
wdwrite WindowMaker BlinkTitlebarWhenUrgent NO >>"$LOG" 2>&1
msg -q reconfigure >>"$LOG" 2>&1
sleep 0.5
BLINK_OFF_STABLE=1
i=0
while [ "$i" -lt 6 ]; do
    blink_titlebar_crop "$WT" "$SHOTDIR/blink-off-$i.png" || BLINK_OFF_STABLE=0
    cmp -s "$SHOTDIR/blink-off-$i.png" "$SHOTDIR/ref-unfocused.png" || BLINK_OFF_STABLE=0
    sleep 0.2
    i=$((i + 1))
done
check "BlinkTitlebarWhenUrgent=NO stops the blink" [ "$BLINK_OFF_STABLE" = 1 ]

kill "$XTB" "$XTF" 2>/dev/null
msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

note "urgent-click drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"

[ "$FAILED" -eq 0 ] && [ "$PASSED" -gt 0 ]
