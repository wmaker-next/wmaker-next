#!/bin/bash
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
xdo()  { DISPLAY=$DPY xdotool "$@" 2> >(grep -v '^XGetWindowProperty\[_NET_WM_DESKTOP\] failed' >&2); }

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
for i in $(seq 1 40); do
    kill -0 "$XEPHYR_PID" 2>/dev/null || fail "Xephyr exited early"
    DISPLAY=$DPY xdpyinfo >/dev/null 2>&1 && break
    sleep 0.1
done
DISPLAY=$DPY xdpyinfo >/dev/null 2>&1 || fail "Xephyr failed to start"

# --- wmaker -----------------------------------------------------------------
note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
ASAN_OPTIONS="abort_on_error=0:halt_on_error=0:detect_leaks=0" \
DISPLAY=$DPY \
"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
for i in $(seq 1 60); do
    DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id' && break
    sleep 0.2
done
DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id' \
    || fail "wmaker did not come up"
sleep 0.5

# --- 1. baseline: bare root -------------------------------------------------
hit_corner 1 1 tl "baseline top-left over root"

# --- 2. regression: window covers the corner --------------------------------
note "map xterm at +0+0 covering top-left"
DISPLAY=$DPY xterm -geometry 200x60+0+0 -e 'sleep 300' >>"$LOG" 2>&1 &
XTPID=$!
echo "$XTPID" >> "$PIDFILE"
for i in $(seq 1 40); do
    XTW=$(DISPLAY=$DPY xdotool search --class XTerm 2>/dev/null | head -1)
    [ -n "$XTW" ] && break
    sleep 0.1
done
[ -n "$XTW" ] || fail "xterm did not map"
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
for i in $(seq 1 30); do kill -0 "$XTPID" 2>/dev/null || break; sleep 0.1; done
kill -TERM "$WMAKER_PID" 2>/dev/null
for i in $(seq 1 50); do kill -0 "$WMAKER_PID" 2>/dev/null || break; sleep 0.1; done

note "PASS"
exit 0
