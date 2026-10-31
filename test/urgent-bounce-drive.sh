#!/bin/sh
# Exercise urgent-bounce timer lifecycle on an isolated X display.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${URGENT_BOUNCE_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-urgent-bounce.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG="$RUNTIME/test.log"
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
UREQ="$RUNTIME/urgent-req"
SOCKET=
WMPID=
: > "$PIDFILE"
: > "$LOG"

cleanup() {
    if [ -n "$SOCKET" ]; then
        msg -q exit >/dev/null 2>&1 || true
    fi
    while read -r pid kind; do
        [ -n "$pid" ] || continue
        case "$kind" in
            xvfb)
                pgrep -a Xvfb | grep -E "^$pid Xvfb $DPY " >/dev/null || continue
                ;;
            wmaker|xterm)
                pgrep -a "$kind" | grep -E "^$pid " >/dev/null || continue
                [ -r "/proc/$pid/environ" ] || continue
                tr '\000' '\n' < "/proc/$pid/environ" | grep -Fx "DISPLAY=$DPY" >/dev/null || continue
                ;;
            *) continue ;;
        esac
        kill "$pid" 2>/dev/null || true
    done < "$PIDFILE"
    rm -rf "$RUNTIME"
}
trap cleanup EXIT

fail() {
    echo "FAIL: $*" >&2
    tail -40 "$LOG" >&2
    exit 1
}

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

msg() {
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$WMAKERMSG" -s "$SOCKET" "$@"
}

set_pref() {
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$ROOT/util/.libs/wdwrite" WindowMaker "$1" "$2" >>"$LOG" 2>&1 \
        || fail "could not set $1"
    msg -q reconfigure >>"$LOG" 2>&1 || fail "could not reload $1"
    sleep 2
}

wakeups() {
    before=$(awk '/^voluntary_ctxt_switches:/{print $2}' "/proc/$WMPID/status")
    sleep "$1"
    after=$(awk '/^voluntary_ctxt_switches:/{print $2}' "/proc/$WMPID/status")
    [ -n "$before" ] && [ -n "$after" ] || fail "wmaker exited during wakeup sample"
    echo $((after - before))
}

quiet() {
    count=$(wakeups 7)
    echo "$1: $count wakeups in 7s"
    [ "$count" -le 1 ] || fail "$1 kept waking the event loop"
}

active() {
    count=$(wakeups 5)
    echo "$1: $count wakeups in 5s"
    [ "$count" -ge 2 ] || fail "$1 did not resume bouncing"
}

has_attention() {
    DISPLAY=$DPY xprop -id "$TARGET" _NET_WM_STATE 2>/dev/null \
        | grep -q _NET_WM_STATE_DEMANDS_ATTENTION
}

for tool in Xvfb xdpyinfo xdotool xprop xterm wmctrl cc pgrep pkg-config; do
    command -v "$tool" >/dev/null 2>&1 || { echo "SKIP: $tool not installed"; exit 77; }
done
[ -r /proc/self/status ] || { echo "SKIP: /proc is unavailable"; exit 77; }
pkg-config --exists x11 || { echo "SKIP: X11 development files unavailable"; exit 77; }
[ -x "$ROOT/src/.libs/wmaker" ] && [ -x "$WMAKERMSG" ] \
    && [ -x "$ROOT/util/.libs/wdwrite" ] || fail "test binaries are not built"
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "SKIP: display $DPY is already in use"
    exit 77
fi

cc -std=gnu11 "$ROOT/test/urgent-req.c" -o "$UREQ" -lX11 >>"$LOG" 2>&1 \
    || fail "could not build urgency request helper"
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
printf '{ DisableDock = YES; DisableClip = YES; DisableDrawers = YES; BounceAppIconsWhenUrgent = NO; BlinkTitlebarWhenUrgent = NO; }\n' \
    > "$WMHOME/Defaults/WindowMaker"
printf '("Test", ("XTerm", EXEC, "xterm"))\n' > "$WMHOME/Defaults/WMRootMenu"

unset WMAKER_USER_XDG WMAKER_IPC_SOCKET
export WMAKER_USER_ROOT="$WMHOME" XDG_RUNTIME_DIR="$RUNTIME" DISPLAY="$DPY"
Xvfb "$DPY" -screen 0 1024x768x24 -ac >>"$LOG" 2>&1 &
echo "$! xvfb" >> "$PIDFILE"
wait_for 40 xdpyinfo >/dev/null 2>&1 || fail "Xvfb did not start"

LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
ASAN_OPTIONS="$(drive_asan_options)" \
    "$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMPID=$!
echo "$WMPID wmaker" >> "$PIDFILE"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$WMAKERMSG" --get-socketpath)
wait_for 80 msg -q get_version >/dev/null 2>&1 || fail "wmaker did not start"
grep -q '^voluntary_ctxt_switches:' "/proc/$WMPID/status" \
    || { echo "SKIP: voluntary context switch counts unavailable"; exit 77; }

xterm -title urgent-bounce-target -geometry 50x8+50+50 -e sleep 600 >>"$LOG" 2>&1 &
echo "$! xterm" >> "$PIDFILE"
xterm -title urgent-bounce-focus -geometry 50x8+400+200 -e sleep 600 >>"$LOG" 2>&1 &
echo "$! xterm" >> "$PIDFILE"
wait_for 60 xdotool search --name '^urgent-bounce-target$' >/dev/null 2>&1 \
    || fail "urgent target did not map"
wait_for 60 xdotool search --name '^urgent-bounce-focus$' >/dev/null 2>&1 \
    || fail "focus holder did not map"
TARGET=$(xdotool search --name '^urgent-bounce-target$' | tail -n 1)
FOCUS=$(xdotool search --name '^urgent-bounce-focus$' | tail -n 1)
wmctrl -i -a "$FOCUS" >>"$LOG" 2>&1 || fail "could not focus holder"
xdotool mousemove 250 700
sleep 2

"$UREQ" "$TARGET" add || fail "could not request attention"
wait_for 30 has_attention || fail "attention request was not applied"
sleep 2
quiet "urgent with bouncing disabled"

set_pref BounceAppIconsWhenUrgent YES
active "bouncing enabled while urgent"
set_pref BounceAppIconsWhenUrgent NO
quiet "bouncing disabled while urgent"
set_pref BounceAppIconsWhenUrgent YES
active "bouncing re-enabled while urgent"

set_pref DoNotMakeAppIconsBounce YES
quiet "app icon bouncing disabled"
set_pref DoNotMakeAppIconsBounce NO
active "app icon bouncing re-enabled"
set_pref DisableAnimations YES
quiet "animations disabled"
set_pref DisableAnimations NO
active "animations re-enabled"

msg -q display-power off >>"$LOG" 2>&1 || fail "could not turn display off"
sleep 2
quiet "display off while urgent"
set_pref BounceAppIconsWhenUrgent NO
msg -q display-power on >>"$LOG" 2>&1 || fail "could not turn display on"
sleep 2
quiet "display on while bouncing is disabled"
set_pref BounceAppIconsWhenUrgent YES
active "bouncing enabled after display resume"
msg -q display-power off >>"$LOG" 2>&1 || fail "could not turn display off"
"$UREQ" "$TARGET" remove || fail "could not clear attention"
"$UREQ" "$TARGET" add || fail "could not request attention while display is off"
wait_for 30 has_attention || fail "attention request while display is off was not applied"
sleep 2
quiet "new urgency while display is off"
msg -q display-power on >>"$LOG" 2>&1 || fail "could not turn display on"
sleep 2
active "display on while urgent"
"$UREQ" "$TARGET" remove || fail "could not clear attention"
sleep 2
quiet "attention cleared"

set_pref BounceAppIconsWhenUrgent NO
xdotool set_window --urgency 1 "$TARGET"
sleep 2
xprop -id "$TARGET" WM_HINTS | grep -qi urgency || fail "ICCCM urgency was not set"
quiet "ICCCM urgency with bouncing disabled"
set_pref BounceAppIconsWhenUrgent YES
active "ICCCM urgency after re-enabling bouncing"
xdotool set_window --urgency 0 "$TARGET"
sleep 2
quiet "ICCCM urgency cleared"

echo "PASS: urgent bounce timer lifecycle"
