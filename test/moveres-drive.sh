#!/bin/sh
# Drive a real titlebar drag and a resizebar drag through the
# wMouseMoveWindow() and wMouseResizeWindow() modal WMMaskEvent loops
# under Xvfb, those are the highest-traffic modal loops and the ones
# the WINGs internal event queue must keep behaviourally identical, so
# this is the regression gate for the queue intake change.
#
# Runs against an isolated WMAKER_USER_ROOT and tracks every PID it
# spawns, the live :0 wmaker is the same binary so no pkill.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${MOVERES_DRIVE_DISPLAY:-:93}
W=1280
H=800
RUNTIME=$(mktemp -d /tmp/moveres-drive.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/moveres-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
: > "$PIDFILE"
: > "$LOG"

cleanup() {
    while read -r p; do
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done < "$PIDFILE"
    rm -f "$PIDFILE"
    rm -rf "$RUNTIME"
}
trap cleanup EXIT

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
fail() { printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
# every fake event gets a short settle so wmaker has dispatched the
# previous one before the next XTest connection opens, without it the
# whole press-move-release burst can land before handleButtonPress has
# entered the modal loop and the drag is lost (verified to also fail on
# next without the wxevq change, so this is a driver-side race not a
# regression guard)
xdo()  { DISPLAY=$DPY xdotool "$@"; sleep 0.05; }

wait_for() {
    local n=$1; shift
    while [ "$n" -gt 0 ]; do
        "$@" && return 0
        sleep 0.1
        n=$((n - 1))
    done
    return 1
}
pid_gone() { ! kill -0 "$1" 2>/dev/null; }

command -v Xvfb    >/dev/null || { echo "SKIP: Xvfb not found"; exit 77; }
command -v xdotool >/dev/null || { echo "SKIP: xdotool not found"; exit 77; }
command -v xterm   >/dev/null || { echo "SKIP: xterm not found"; exit 77; }

# --- isolated WMAKER_USER_ROOT --------------------------------------------
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
# OpaqueMove/OpaqueResize on so the loop dispatches a frame configure per
# step instead of just the wireframe outline, that is the path the sync
# resize and the motion-compress drain both sit on
cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  OpaqueMove = YES;
  OpaqueResize = YES;
  EdgeResistance = 0;
}
EOF
export WMAKER_USER_ROOT="$WMHOME"
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET

# --- Xvfb ------------------------------------------------------------------
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use, set MOVERES_DRIVE_DISPLAY" >&2
    exit 1
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 ${W}x${H}x24 -bs -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
    kill -0 "$XVFB_PID" 2>/dev/null || fail "Xvfb exited early"
    DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fail "Xvfb failed to start"

# --- wmaker ----------------------------------------------------------------
note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
ASAN_OPTIONS="abort_on_error=0:halt_on_error=0:detect_leaks=0" \
DISPLAY=$DPY \
"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
wm_ready() {
    DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null \
        | grep 'window id' >/dev/null
}
wait_for 120 wm_ready || fail "wmaker did not come up"
sleep 0.3

# --- spawn a client and find its frame geometry ----------------------------
note "spawn xterm"
DISPLAY=$DPY xterm -geometry 80x12+200+200 -e 'sleep 300' >>"$LOG" 2>&1 &
XTPID=$!
echo "$XTPID" >> "$PIDFILE"
XTW=""
xterm_mapped() {
    XTW=$(DISPLAY=$DPY xdotool search --class XTerm 2>/dev/null | tail -n 1)
    [ -n "$XTW" ]
}
wait_for 40 xterm_mapped || fail "xterm did not map"
sleep 0.3

# absolute client origin and frame extents straight from xwininfo and
# _NET_FRAME_EXTENTS, xdotool getwindowgeometry double-counts the frame
# offset for a reparented client so do not use it here
client_geom() {
    DISPLAY=$DPY xwininfo -id "$XTW" | awk \
        '/Absolute upper-left X/{x=$NF} /Absolute upper-left Y/{y=$NF}
         /Width:/{w=$NF} /Height:/{h=$NF} END{print x,y,w,h}'
}
set -- $(client_geom)
CX=$1; CY=$2; CW=$3; CH=$4
EXT=$(DISPLAY=$DPY xprop -id "$XTW" _NET_FRAME_EXTENTS \
        | sed 's/.*= //; s/,//g')
set -- $EXT
FT=${3:-20}; FB=${4:-8}

# --- 1. titlebar drag ------------------------------------------------------
note "titlebar drag through wMouseMoveWindow"
TBX=$((CX + 60))
TBY=$((CY - FT/2 - 1))
DX=147; DY=93
xdo mousemove "$TBX" "$TBY"
xdo mousedown 1
# the first MotionNotify only arms started=1, the second runs
# updateWindowPosition(), step through a couple of intermediates so the
# motion-compress drain in the modal loop has something to compress
xdo mousemove $((TBX + 30))  $((TBY + 20))
xdo mousemove $((TBX + 90))  $((TBY + 60))
xdo mousemove $((TBX + DX))  $((TBY + DY))
xdo mouseup 1
sleep 0.3

set -- $(client_geom)
NX=$1; NY=$2
[ "$((NX - CX))" = "$DX" ] && [ "$((NY - CY))" = "$DY" ] \
    || fail "move: expected delta ($DX,$DY) got ($((NX - CX)),$((NY - CY)))"
note "ok: titlebar drag moved client by ($DX,$DY)"
CX=$NX; CY=$NY

# --- 2. resizebar drag -----------------------------------------------------
note "resizebar drag through wMouseResizeWindow"
RBX=$((CX + CW/2))
RBY=$((CY + CH + FB/2))
RDX=0; RDY=64
xdo mousemove "$RBX" "$RBY"
xdo mousedown 1
xdo mousemove $((RBX + RDX)) $((RBY + 20))
xdo mousemove $((RBX + RDX)) $((RBY + 40))
xdo mousemove $((RBX + RDX)) $((RBY + RDY))
xdo mouseup 1
sleep 0.3

set -- $(client_geom)
NW=$3; NH=$4
# xterm rounds to its character-cell increment so accept any growth
# within one cell of the requested delta, the point is the modal loop
# returned and applied a resize, not pixel-exactness
DH=$((NH - CH))
[ "$DH" -ge $((RDY - 20)) ] && [ "$DH" -le $((RDY + 20)) ] \
    || fail "resize: expected height delta ~$RDY got $DH"
[ "$NW" = "$CW" ] \
    || fail "resize: width changed on a middle-segment drag ($CW -> $NW)"
note "ok: resizebar drag grew client height by $DH"

# --- teardown --------------------------------------------------------------
kill "$XTPID" 2>/dev/null
wait_for 30 pid_gone "$XTPID"
kill -TERM "$WMAKER_PID" 2>/dev/null
wait_for 50 pid_gone "$WMAKER_PID"

note "PASS"
exit 0
