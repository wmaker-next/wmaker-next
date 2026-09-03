#!/bin/sh
# Verify that the first Button3 click on a normal managed titlebar opens the
# window menu without terminating Window Maker. Runs the manager and xterm
# under Xephyr on an isolated display.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${WINDOW_MENU_DRIVE_DISPLAY:-:97}
PIDFILE=$(mktemp /tmp/window-menu-drive.pids.XXXXXX)
WMHOME=$(mktemp -d /tmp/window-menu-drive-wmhome.XXXXXX)
LOG=/tmp/window-menu-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
: > "$PIDFILE"
: > "$LOG"

cleanup() {
    while read -r p; do
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done < "$PIDFILE"
    rm -f "$PIDFILE"
    rm -rf "$WMHOME"
}
trap cleanup EXIT

fail() { printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
skip() { printf 'SKIP: %s\n' "$*" | tee -a "$LOG"; exit 77; }

for tool in Xvfb Xephyr xdpyinfo xdotool xterm xprop xwininfo; do
    command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done

wait_for() {
    n=$1
    shift
    while [ "$n" -gt 0 ]; do
        "$@" && return 0
        sleep 0.1
        n=$((n - 1))
    done
    return 1
}

pid_alive() { kill -0 "$1" 2>/dev/null; }
wm_ready() {
    DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null \
        | grep 'window id' >/dev/null
}

frame_of() {
    DISPLAY=$DPY xwininfo -tree -id "$1" 2>/dev/null \
        | awk '/Parent window id:/ {print $4; exit}'
}

frame_top() {
    current=$1
    root_window=$(DISPLAY=$DPY xwininfo -root 2>/dev/null \
        | awk '/Window id:/ {print $4; exit}')
    count=0
    while [ "$count" -lt 5 ]; do
        parent=$(frame_of "$current")
        [ -n "$parent" ] && [ "$parent" != "0x0" ] || return 1
        if [ "$parent" = "$root_window" ]; then
            printf '%s\n' "$current"
            return 0
        fi
        current=$parent
        count=$((count + 1))
    done
    return 1
}

xterm_frame() {
    root_window=$(DISPLAY=$DPY xwininfo -root 2>/dev/null \
        | awk '/Window id:/ {print $4; exit}')
    for client in $(DISPLAY=$DPY xdotool search --class XTerm 2>/dev/null); do
        parent=$(frame_of "$client")
        [ -n "$parent" ] && [ "$parent" != "$root_window" ] && [ "$parent" != "0x0" ] || continue
        CLIENT=$client
        frame_top "$client"
        return $?
    done
    return 1
}

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use, set WINDOW_MENU_DRIVE_DISPLAY to a free one" >&2
    exit 1
fi

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
}
EOF

# Xephyr is a nested server: on the session display its host pointer
# tracks the live mouse and can override the XTEST warps the drive
# sends. Run it on a private headless parent whose virtual pointer
# only moves when Xephyr mirrors a warp.
PARENT_DISPLAY=${WINDOW_MENU_DRIVE_PARENT_DISPLAY:-":$(( ${DPY#:} + 1 ))"}
Xvfb "$PARENT_DISPLAY" -screen 0 2400x1600x24 -ac >>"$LOG" 2>&1 &
XVFB_PARENT_PID=$!
echo "$XVFB_PARENT_PID" >> "$PIDFILE"
wait_for 40 sh -c "kill -0 $XVFB_PARENT_PID 2>/dev/null && DISPLAY=$PARENT_DISPLAY xdpyinfo >/dev/null 2>&1" \
    || fail "parent Xvfb failed to start"

Xephyr "$DPY" -display "$PARENT_DISPLAY" -screen 1280x800 -ac >>"$LOG" 2>&1 &
XEPHYR_PID=$!
echo "$XEPHYR_PID" >> "$PIDFILE"
wait_for 40 sh -c "DISPLAY=$DPY xdpyinfo >/dev/null 2>&1" \
    || fail "Xephyr failed to start"

WMAKER_USER_ROOT="$WMHOME" \
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
ASAN_OPTIONS="abort_on_error=0:halt_on_error=0:detect_leaks=0" \
DISPLAY=$DPY "$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
wait_for 120 wm_ready || fail "wmaker did not come up"

WMAKER_USER_ROOT="$WMHOME" DISPLAY=$DPY xterm \
    -geometry 80x24+100+100 -e 'sleep 600' >>"$LOG" 2>&1 &
XTERM_PID=$!
echo "$XTERM_PID" >> "$PIDFILE"
wait_for 50 xterm_frame || fail "xterm frame did not appear"
FRAME=$(xterm_frame)

set -- $(DISPLAY=$DPY xwininfo -id "$FRAME" 2>/dev/null \
    | awk '/Absolute upper-left X:/ {x=$4}
           /Absolute upper-left Y:/ {y=$4}
           /Width:/ && !w {w=$2}
           /Border width:/ {b=$3}
           END {print x, y, w, b}')
FRAME_X=$1
FRAME_Y=$2
FRAME_WIDTH=$3
FRAME_BORDER=$4
CLIENT_Y=$(DISPLAY=$DPY xwininfo -id "$CLIENT" 2>/dev/null \
    | awk '/Absolute upper-left Y:/ {print $4; exit}')
TITLEBAR_HEIGHT=$((CLIENT_Y - FRAME_Y - FRAME_BORDER))
[ "$TITLEBAR_HEIGHT" -gt 0 ] || fail "invalid titlebar height $TITLEBAR_HEIGHT"

# This is the first construction of scr->window_menu. The old ownership bug
# freed menu entry text here and crashed during the following update.
CLICK_X=$((FRAME_X + FRAME_BORDER + FRAME_WIDTH / 2))
CLICK_Y=$((FRAME_Y + FRAME_BORDER + TITLEBAR_HEIGHT / 2))
DISPLAY=$DPY xdotool mousemove --sync "$CLICK_X" "$CLICK_Y"
DISPLAY=$DPY xdotool click 3
sleep 0.5

pid_alive "$WMAKER_PID" || fail "wmaker exited after titlebar Button3"
DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null \
    | grep 'window id' >/dev/null \
    || fail "wmaker no longer owns the display after titlebar Button3"
printf 'PASS: first titlebar Button3 kept wmaker alive\n'
