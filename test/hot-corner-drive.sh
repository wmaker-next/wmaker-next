#!/bin/sh
# Verify hot corners fire even when a client (windowed or fullscreen)
# covers the corner. Runs wmaker under Xephyr on an isolated display
# and WMAKER_USER_ROOT, tracks every PID it spawns and kills only those.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${HOTCORNER_DRIVE_DISPLAY:-:94}
W=1280
H=800
PIDFILE=$(mktemp /tmp/hotcorner-drive.pids.XXXXXX)
WMHOME=$(mktemp -d /tmp/hotcorner-drive-wmhome.XXXXXX)
MARK=$(mktemp -d /tmp/hotcorner-drive-mark.XXXXXX)
LOG=/tmp/hotcorner-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
: > "$PIDFILE"
: > "$LOG"

cleanup() {
    while read -r p; do
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done < "$PIDFILE"
    rm -f "$PIDFILE"
    rm -rf "$WMHOME" "$MARK"
}
trap cleanup EXIT

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
fail() { printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
xdo()  {
    { DISPLAY=$DPY xdotool "$@" 2>&1 1>&3 \
        | grep -v '^XGetWindowProperty\[_NET_WM_DESKTOP\] failed' >&2; } 3>&1
}

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

# move pointer well away from any corner, then into the target corner,
# wait past HotCornerDelay and check the action ran
hit_corner() {
    local x=$1 y=$2 marker=$3 what=$4
    rm -f "$MARK/$marker"
    xdo mousemove $((W/2)) $((H/2)); sleep 0.1
    xdo mousemove "$x" "$y"; sleep 0.3
    [ -f "$MARK/$marker" ] || fail "$what: corner action did not fire"
    note "ok: $what"
}

# --- isolated WMAKER_USER_ROOT with HotCorners enabled ---------------------
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  HotCorners = YES;
  HotCornerDelay = 100;
  HotCornerEdge = 10;
  HotCornerActions = ("touch $MARK/tl", "touch $MARK/tr",
                      "touch $MARK/bl", "touch $MARK/br");
}
EOF
export WMAKER_USER_ROOT="$WMHOME"

# --- Xephyr -----------------------------------------------------------------
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use, set HOTCORNER_DRIVE_DISPLAY to a free one" >&2
    exit 1
fi
note "start Xephyr $DPY"
Xephyr "$DPY" -screen ${W}x${H} -ac >>"$LOG" 2>&1 &
XEPHYR_PID=$!
echo "$XEPHYR_PID" >> "$PIDFILE"
xephyr_ready() {
    kill -0 "$XEPHYR_PID" 2>/dev/null || fail "Xephyr exited early"
    DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xephyr_ready || fail "Xephyr failed to start"

# --- wmaker -----------------------------------------------------------------
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
sleep 0.5

# --- 1. baseline: bare root -------------------------------------------------
hit_corner 1 1 tl "baseline top-left over root"

# --- 2. regression: window covers the corner --------------------------------
note "map xterm at +0+0 covering top-left"
DISPLAY=$DPY xterm -geometry 200x60+0+0 -e 'sleep 300' >>"$LOG" 2>&1 &
XTPID=$!
echo "$XTPID" >> "$PIDFILE"
XTW=""
xterm_mapped() {
    XTW=$(DISPLAY=$DPY xdotool search --class XTerm 2>/dev/null | head -n 1)
    [ -n "$XTW" ]
}
wait_for 40 xterm_mapped || fail "xterm did not map"
xdo windowactivate "$XTW"; sleep 0.3

hit_corner 1 1 tl "top-left over windowed client"

# --- 3. fullscreen covers the corner ----------------------------------------
note "fullscreen the xterm"
DISPLAY=$DPY wmctrl -i -r "$XTW" -b add,fullscreen 2>/dev/null; sleep 0.4

hit_corner 1 1 tl "top-left over fullscreen client"

# --- 4. second corner under fullscreen --------------------------------------
hit_corner $((W-2)) $((H-2)) br "bottom-right over fullscreen client"

# --- teardown ---------------------------------------------------------------
DISPLAY=$DPY wmctrl -i -c "$XTW" 2>/dev/null
wait_for 30 pid_gone "$XTPID"
kill -TERM "$WMAKER_PID" 2>/dev/null
wait_for 50 pid_gone "$WMAKER_PID"

note "PASS"
exit 0
