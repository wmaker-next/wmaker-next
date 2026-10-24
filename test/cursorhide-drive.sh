#!/bin/sh
# Drive the auto-hide cursor feature through xtrace. The window manager
# talks to a fake display served by xtrace so the XFixes HideCursor and
# ShowCursor requests it emits can be asserted directly, independent of
# where the sprite happens to be or what another client reports. Runs
# against an isolated WMAKER_USER_ROOT and XDG_RUNTIME_DIR. Tracks every
# PID it spawns, the live desktop is untouched.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${CURSORHIDE_DRIVE_DISPLAY:-:$(drive_default_display)}
FAKE=${CURSORHIDE_DRIVE_FAKE_DISPLAY:-:$(drive_default_display 1)}
RUNTIME=$(mktemp -d /tmp/wmaker-cursorhide-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
TRACE="$RUNTIME/xtrace.log"
LOG=/tmp/cursorhide-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
WDWRITE="$ROOT/util/.libs/wdwrite"
HELPER="$RUNTIME/cursorhide-client"
SOCKET=
: > "$PIDFILE"
: > "$LOG"
: > "$TRACE"

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
ipc_ready() { msg -q get_version >/dev/null 2>&1; }

msg() {
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$WMAKERMSG" -s "$SOCKET" "$@"
}
wdwrite() {
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        DISPLAY=$FAKE "$WDWRITE" "$@"
}
xdo() { DISPLAY=$FAKE xdotool "$@"; }
move_xtest() { DISPLAY=$FAKE "$HELPER" "$@"; }

trace_count() {
    c=$(grep -c -i "$1" "$TRACE" 2>/dev/null)
    echo "${c:-0}"
}
wait_trace_above() {
    wt_pattern=$1
    wt_before=$2
    wt_n=60
    while [ "$wt_n" -gt 0 ]; do
        [ "$(trace_count "$wt_pattern")" -gt "$wt_before" ] && return 0
        sleep 0.1
        wt_n=$((wt_n - 1))
    done
    return 1
}

for tool in Xvfb xdpyinfo xdotool xterm xtrace; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
[ -x "$WDWRITE" ] || skip_all "util/.libs/wdwrite not built"

${CC:-cc} -std=gnu11 "$ROOT/test/cursorhide-client.c" -o "$HELPER" \
    -lxcb -lxcb-xtest >>"$LOG" 2>&1 \
    || skip_all "could not build test/cursorhide-client.c"

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use" >&2; exit 77
fi
if DISPLAY=$FAKE xdpyinfo >/dev/null 2>&1; then
    echo "display $FAKE is already in use" >&2; exit 77
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
  AutoHideCursor = NO;
  HideCursorAfterKeyboard = YES;
  HideCursorAfterIdle = NO;
  HideCursorIdleDelay = 1000;
  FindMouseKey = "Mod4+f";
}
EOF
printf '("Test", ("XTerm", EXEC, "xterm"))\n' > "$WMHOME/Defaults/WMRootMenu"

note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 1024x768x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
wait_for 40 sh -c "kill -0 $XVFB_PID 2>/dev/null && DISPLAY=$DPY xdpyinfo >/dev/null 2>&1" \
    || fatal "Xvfb failed to start"

note "start xtrace $DPY -> $FAKE"
xtrace -n -d "$DPY" -D "$FAKE" -o "$TRACE" -k >>"$LOG" 2>&1 &
XTRACE_PID=$!
echo "$XTRACE_PID" >> "$PIDFILE"
wait_for 60 sh -c "kill -0 $XTRACE_PID 2>/dev/null && DISPLAY=$FAKE xdpyinfo >/dev/null 2>&1" \
    || fatal "xtrace fake display did not come up"

note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    ASAN_OPTIONS="$(drive_asan_options)" \
    DISPLAY=$FAKE \
    "$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DISPLAY=$FAKE "$WMAKERMSG" --get-socketpath)
wait_for 80 ipc_ready || fatal "wmaker IPC did not come up"

grep -qi 'XFixes' "$TRACE" \
    || skip_all "this xtrace build does not decode XFixes"

note "map a client so keyboard input has a focus window"
DISPLAY=$FAKE xterm -geometry 50x14+40+40 -e 'sleep 300' >>"$LOG" 2>&1 &
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
sleep 0.5

# --- 1. disabled feature emits no hide request ----------------------------
note "baseline with AutoHideCursor off"
sleep 1.5
check "no HideCursor request while disabled" [ "$(trace_count HideCursor)" = 0 ]

# --- 2. pointer idle hides, movement shows -------------------------------
note "enable the pointer idle mode with a 1 second delay"
wdwrite WindowMaker AutoHideCursor YES
wdwrite WindowMaker HideCursorAfterIdle YES
wdwrite WindowMaker HideCursorIdleDelay 1000
msg -q reconfigure || fail "reconfigure rejected"
xdo mousemove 300 300 >>"$LOG" 2>&1

HIDE_BEFORE=$(trace_count HideCursor)
wait_trace_above HideCursor "$HIDE_BEFORE" \
    || fail "idle did not emit HideCursor"
check "pointer idle emits HideCursor" \
    [ "$(trace_count HideCursor)" -gt "$HIDE_BEFORE" ]

SHOW_BEFORE=$(trace_count ShowCursor)
move_xtest 700 600 >>"$LOG" 2>&1
wait_trace_above ShowCursor "$SHOW_BEFORE" \
    || fail "pointer movement did not emit ShowCursor"
check "pointer movement emits ShowCursor" \
    [ "$(trace_count ShowCursor)" -gt "$SHOW_BEFORE" ]

# --- 3. keyboard hides, button press shows -------------------------------
note "switch to keyboard mode"
wdwrite WindowMaker HideCursorAfterIdle NO
msg -q reconfigure || fail "reconfigure rejected"
xdo windowactivate "$XTERM_WID" 2>/dev/null
sleep 0.3

HIDE_BEFORE=$(trace_count HideCursor)
xdo key a >>"$LOG" 2>&1
wait_trace_above HideCursor "$HIDE_BEFORE" \
    || fail "non-modifier key press did not emit HideCursor"
check "keyboard input emits HideCursor" \
    [ "$(trace_count HideCursor)" -gt "$HIDE_BEFORE" ]

SHOW_BEFORE=$(trace_count ShowCursor)
xdo click 1 >>"$LOG" 2>&1
wait_trace_above ShowCursor "$SHOW_BEFORE" \
    || fail "button press did not emit ShowCursor"
check "button press emits ShowCursor" \
    [ "$(trace_count ShowCursor)" -gt "$SHOW_BEFORE" ]

# --- 4. FindMouse reveals a hidden pointer -------------------------------
note "hide again, then invoke FindMouse"
HIDE_BEFORE=$(trace_count HideCursor)
xdo key a >>"$LOG" 2>&1
wait_trace_above HideCursor "$HIDE_BEFORE" \
    || fail "could not hide before the FindMouse test"

SHOW_BEFORE=$(trace_count ShowCursor)
msg -q action FindMouse || fail "FindMouse action rejected"
wait_trace_above ShowCursor "$SHOW_BEFORE" \
    || fail "FindMouse did not reveal the hidden pointer"
check "FindMouse reveals the hidden pointer" \
    [ "$(trace_count ShowCursor)" -gt "$SHOW_BEFORE" ]

# --- 5. the FindMouse keybinding also reveals a hidden pointer -----------
note "hide again, then press FindMouseKey"
HIDE_BEFORE=$(trace_count HideCursor)
xdo key a >>"$LOG" 2>&1
wait_trace_above HideCursor "$HIDE_BEFORE" \
    || fail "could not hide before the FindMouseKey test"

SHOW_BEFORE=$(trace_count ShowCursor)
xdo key super+f >>"$LOG" 2>&1
wait_trace_above ShowCursor "$SHOW_BEFORE" \
    || fail "FindMouseKey did not reveal the hidden pointer"
check "FindMouseKey reveals the hidden pointer" \
    [ "$(trace_count ShowCursor)" -gt "$SHOW_BEFORE" ]

# --- 6. disabling the master switch shows a hidden pointer ---------------
note "hide again, then disable the master switch"
HIDE_BEFORE=$(trace_count HideCursor)
xdo key a >>"$LOG" 2>&1
wait_trace_above HideCursor "$HIDE_BEFORE" \
    || fail "could not hide again for the disable test"

SHOW_BEFORE=$(trace_count ShowCursor)
wdwrite WindowMaker AutoHideCursor NO
msg -q reconfigure || fail "reconfigure rejected"
wait_trace_above ShowCursor "$SHOW_BEFORE" \
    || fail "disabling AutoHideCursor did not emit ShowCursor"
check "disabling the feature shows the pointer" \
    [ "$(trace_count ShowCursor)" -gt "$SHOW_BEFORE" ]

check "wmaker alive at the end" ipc_ready

note "results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ] || exit 1
exit 0
