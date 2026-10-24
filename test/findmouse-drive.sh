#!/bin/sh
# Drive the FindMouse pointer highlight. The ring window must be an
# override-redirect, click-through (empty ShapeInput) annulus. It must
# follow the cursor for FindMouseDuration, and a runtime config change
# must remove it. Runs against an isolated WMAKER_USER_ROOT and
# XDG_RUNTIME_DIR. Tracks every PID it spawns, the live desktop is
# untouched.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${FINDMOUSE_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-findmouse-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/findmouse-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
WDWRITE="$ROOT/util/.libs/wdwrite"
HELPER="$RUNTIME/findmouse-client"
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
xdo() { DISPLAY=$DPY xdotool "$@"; }

fm() { DISPLAY=$DPY "$HELPER" 2>/dev/null; }
fm_get() { fm | tr ' ' '\n' | sed -n "s/^$1=//p"; }
ring_viewable() {
    [ "$(fm_get found)" = 1 ] && [ "$(fm_get map_state)" = 2 ]
}
ring_hidden() {
    [ "$(fm_get found)" = 0 ] || [ "$(fm_get map_state)" != 2 ]
}

for tool in Xvfb xdpyinfo xdotool xwininfo xterm; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
[ -x "$WDWRITE" ] || skip_all "util/.libs/wdwrite not built"

${CC:-cc} -std=gnu11 "$ROOT/test/findmouse-client.c" -o "$HELPER" \
    -lxcb -lxcb-shape >>"$LOG" 2>&1 \
    || skip_all "could not build test/findmouse-client.c"

export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  DisableSound = YES;
  FindMouseKey = "Control+Shift+f";
  FindMouseDuration = 1000;
  FindMouseRadius = 100;
}
EOF
printf '("Test", ("XTerm", EXEC, "xterm"))\n' > "$WMHOME/Defaults/WMRootMenu"

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

note "map a client so the keybinding grab has a focus window"
DISPLAY=$DPY xterm -geometry 50x14+40+40 -e 'sleep 300' >>"$LOG" 2>&1 &
XTERM_PID=$!
echo "$XTERM_PID" >> "$PIDFILE"
XTERM_WID=
xterm_ready() {
    XTERM_WID=$(xdo search --onlyvisible --class XTerm 2>/dev/null | head -n 1)
    [ -n "$XTERM_WID" ]
}
wait_for 60 xterm_ready || fatal "xterm did not map"
xdo windowactivate "$XTERM_WID" 2>/dev/null
xdo mousemove 300 300 >>"$LOG" 2>&1
sleep 0.4

# --- 1. inactive creates nothing ------------------------------------------
note "baseline"
check "no overlay before the action fires" ring_hidden

# --- 2. IPC action shows a click-through annulus --------------------------
note "trigger through wmakermsg action"
msg -q action FindMouse >/dev/null 2>&1 || fail "action FindMouse rejected"
wait_for 40 ring_viewable || fail "FindMouse did not map the ring"
check "ring window is viewable" ring_viewable
check "ring is override-redirect" [ "$(fm_get override_redirect)" = 1 ]
check "ring is 210x210 for radius 100" [ "$(fm_get width)" = 210 ]
check "ring height matches width" [ "$(fm_get height)" = 210 ]
check "ring shape has an annulus shadow" sh -c "[ \"$(fm_get bounding_rects)\" -gt 1 ] 2>/dev/null"
check "ring input shape is empty" [ "$(fm_get input_rects)" = 0 ]
check "ring is centered on the pointer" sh -c \
    "[ \"$(fm_get x)\" = 195 ] && [ \"$(fm_get y)\" = 195 ]"

# --- 3. expiry ------------------------------------------------------------
note "wait for the effect to expire"
wait_for 50 ring_hidden || fail "ring stayed visible past FindMouseDuration"
check "ring hidden after the duration" ring_hidden
check "wmaker alive after expiry" ipc_ready

# --- 4. the configured keybinding triggers it -----------------------------
note "trigger through FindMouseKey"
xdo key ctrl+shift+f >>"$LOG" 2>&1
wait_for 40 ring_viewable || fail "FindMouseKey did not show the ring"
check "keybinding shows the ring" ring_viewable
wait_for 50 ring_hidden || fail "keybinding ring did not expire"

# --- 5. config change while active removes it -----------------------------
note "change the radius while the ring is active"
msg -q action FindMouse >/dev/null 2>&1
wait_for 40 ring_viewable || fail "ring did not map before the reload"
wdwrite WindowMaker FindMouseRadius 150 >>"$LOG" 2>&1 || fail "wdwrite failed"
msg -q reconfigure || fail "reconfigure rejected"
wait_for 30 ring_hidden || fail "radius change did not remove the ring"
check "radius change while active hides the ring" ring_hidden

# --- 6. the new radius applies on the next trigger ------------------------
note "new radius applies on the next trigger"
msg -q action FindMouse >/dev/null 2>&1
wait_for 40 ring_viewable || fail "ring did not return after the reload"
check "ring is 310x310 for radius 150" [ "$(fm_get width)" = 310 ]
wait_for 50 ring_hidden || fail "resized ring did not expire"

# --- 7. automatic radius --------------------------------------------------
note "automatic radius from the monitor"
wdwrite WindowMaker FindMouseRadius 0 >>"$LOG" 2>&1 || fail "wdwrite failed"
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3
msg -q action FindMouse >/dev/null 2>&1
wait_for 40 ring_viewable || fail "ring did not show with auto radius"
check "auto radius is 64 on 1024x768" [ "$(fm_get width)" = 138 ]
wait_for 50 ring_hidden || fail "auto-radius ring did not expire"

note "findmouse drive summary: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ] || exit 1
exit 0
