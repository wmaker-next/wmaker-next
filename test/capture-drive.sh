#!/bin/sh
# Drive ScreenCapture() through the ScreenCaptureKey, WindowCaptureKey and
# PartialCaptureKey bindings. The screenshots are written with the real
# wrlib save path, so this covers the RCreateImageFromDrawable ->
# RSaveTitledImage -> showMiniScreenshot sequence that the other drivers
# never reach.
#
# Runs against an isolated WMAKER_USER_ROOT and XDG_RUNTIME_DIR, tracks
# every PID it spawns, the live desktop is untouched.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${CAPTURE_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-capture-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/capture-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
SHOTDIR="$WMHOME/Library/WindowMaker/Screenshots"
SHOT_EXT=
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

xdo() { DISPLAY=$DPY xdotool "$@"; }

shot_count() {
    find "$SHOTDIR" -maxdepth 1 -name "*.$SHOT_EXT" 2>/dev/null | wc -l
}

shot_at() {
    ls -1 "$SHOTDIR"/*."$SHOT_EXT" 2>/dev/null | LC_ALL=C sort | sed -n "${1}p"
}

shot_at_least() {
    [ "$(shot_count)" -ge "$1" ]
}

is_shot() {
    [ -f "$1" ] || return 1
    if [ "$SHOT_EXT" = png ]; then
        [ "$(od -An -tx1 -N8 "$1" 2>/dev/null | tr -d ' \n')" = "89504e470d0a1a0a" ]
    else
        [ "$(od -An -tx1 -N3 "$1" 2>/dev/null | tr -d ' \n')" = "ffd8ff" ]
    fi
}

shot_size() {
    python3 - "$1" <<'PY'
import struct, sys
data = open(sys.argv[1], "rb").read()
if data[:8] == b"\x89PNG\r\n\x1a\n":
    w, h = struct.unpack(">II", data[16:24])
elif data[:2] == b"\xff\xd8":
    i = 2
    while i + 9 < len(data):
        if data[i] != 0xFF:
            i += 1
            continue
        marker = data[i + 1]
        if marker in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7,
                      0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
            h, w = struct.unpack(">HH", data[i + 5:i + 9])
            break
        if marker in (0xD8, 0xD9) or 0xD0 <= marker <= 0xD7:
            i += 2
            continue
        i += 2 + struct.unpack(">H", data[i + 2:i + 4])[0]
    else:
        sys.exit(1)
else:
    sys.exit(1)
print("%dx%d" % (w, h))
PY
}

for tool in Xvfb xdpyinfo xdotool xprop xterm python3; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
if grep -q '^#define USE_PNG 1' "$ROOT/config.h" 2>/dev/null; then
    SHOT_EXT=png
elif grep -q '^#define USE_JPEG 1' "$ROOT/config.h" 2>/dev/null; then
    SHOT_EXT=jpg
else
    skip_all "wmaker was built without a PNG or JPEG encoder"
fi

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
  ScreenshotFilenameTemplate = "shot";
  ScreenCaptureKey = "Control+Shift+s";
  WindowCaptureKey = "Control+Shift+w";
  PartialCaptureKey = "Control+Shift+p";
}
EOF

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
sleep 0.5

check "baseline has no screenshots" [ "$(shot_count)" -eq 0 ]

note "map an xterm to capture"
DISPLAY=$DPY xterm -geometry 60x18+120+100 -e 'sleep 300' >>"$LOG" 2>&1 &
XTERM_PID=$!
echo "$XTERM_PID" >> "$PIDFILE"
XTERM_WID=
xterm_ready() {
    XTERM_WID=$(xdo search --onlyvisible --class XTerm 2>/dev/null | head -n 1)
    [ -n "$XTERM_WID" ]
}
wait_for 60 xterm_ready || fatal "xterm did not map"
xdo windowactivate "$XTERM_WID" 2>/dev/null
sleep 0.5

# --- 1. full-screen capture ------------------------------------------------
note "full-screen capture through ScreenCaptureKey"
xdo key ctrl+shift+s >>"$LOG" 2>&1
wait_for 50 shot_at_least 1 || fail "full-screen capture wrote no PNG"
FULL=$(shot_at 1)
if [ -n "$FULL" ]; then
    check "full-screen capture is a $SHOT_EXT file" is_shot "$FULL"
    check "full-screen capture is the screen size" \
        [ "$(shot_size "$FULL")" = "1024x768" ]
else
    fail "full-screen capture left no file to inspect"
fi
check "wmaker alive after full-screen capture" ipc_ready

# --- 2. focused-window capture --------------------------------------------
note "window capture through WindowCaptureKey"
BEFORE=$(shot_count)
xdo key ctrl+shift+w >>"$LOG" 2>&1
wait_for 50 shot_at_least $((BEFORE + 1)) || fail "window capture wrote no PNG"
WIN=$(shot_at $((BEFORE + 1)))
if [ -n "$WIN" ]; then
    check "window capture is a $SHOT_EXT file" is_shot "$WIN"
    note "window capture size $(shot_size "$WIN")"
    check "window capture is smaller than the screen" \
        sh -c "case \"$(shot_size "$WIN")\" in 1024x768) exit 1;; *x*) exit 0;; *) exit 1;; esac"
else
    fail "window capture left no file to inspect"
fi
check "wmaker alive after window capture" ipc_ready

# --- 3. partial capture ----------------------------------------------------
note "partial capture through PartialCaptureKey"
BEFORE=$(shot_count)
xdo key ctrl+shift+p >>"$LOG" 2>&1
sleep 0.3
xdo mousemove 150 120 >>"$LOG" 2>&1
xdo mousedown 1 >>"$LOG" 2>&1
xdo mousemove 250 220 >>"$LOG" 2>&1
xdo mousemove 450 320 >>"$LOG" 2>&1
xdo mouseup 1 >>"$LOG" 2>&1
wait_for 50 shot_at_least $((BEFORE + 1)) || fail "partial capture wrote no PNG"
PART=$(shot_at $((BEFORE + 1)))
if [ -n "$PART" ]; then
    check "partial capture is a $SHOT_EXT file" is_shot "$PART"
    SIZE=$(shot_size "$PART")
    note "partial capture size $SIZE"
    check "partial capture is the selected rectangle" [ "$SIZE" = "300x200" ]
else
    fail "partial capture left no file to inspect"
fi
check "wmaker alive after partial capture" ipc_ready

# --- 4. Escape cancels a partial capture ----------------------------------
note "Escape cancels a partial capture"
BEFORE=$(shot_count)
xdo key ctrl+shift+p >>"$LOG" 2>&1
sleep 0.3
xdo mousemove 200 200 >>"$LOG" 2>&1
xdo mousedown 1 >>"$LOG" 2>&1
xdo mousemove 400 400 >>"$LOG" 2>&1
xdo key Escape >>"$LOG" 2>&1
xdo mouseup 1 >>"$LOG" 2>&1
sleep 0.5
check "cancelled partial capture wrote no $SHOT_EXT file" [ "$(shot_count)" -eq "$BEFORE" ]
check "wmaker alive after cancelled partial capture" ipc_ready

# --- teardown --------------------------------------------------------------
note "capture drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

kill "$XTERM_PID" 2>/dev/null
msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
