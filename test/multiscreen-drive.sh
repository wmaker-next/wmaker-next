#!/bin/sh
# Multi-screen (multi-WScreen on one Display) cold-boot smoke for the
# typed config schema: StartUp() parses the WindowMaker domain once and
# every wScreenInit() applies it, so all screens must come up fully
# managed with their own IPC socket, and a runtime reload must fan out
# to every screen. A deliberately broken texture spec in the domain
# exercises the startup realisation-failure fallback on each screen.
#
# Xvfb provides two screens on one display (:N.0 and :N.1), wmaker is
# started with a screen-less DISPLAY so it manages both.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${ZAPHOD_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-multiscr-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=$(mktemp /tmp/multiscreen-drive.XXXXXX)
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
WDWRITE="$ROOT/util/.libs/wdwrite"
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

# wmakermsg scoped to one of the two screens via DISPLAY
msg() {
    screen=$1; shift
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        DISPLAY="$DPY.$screen" "$WMAKERMSG" "$@"
}

wdwrite() {
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$WDWRITE" "$@"
}

root_managed() {
    DISPLAY="$DPY.$1" xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null \
        | grep 'window id' >/dev/null
}

both_roots_managed() { root_managed 0 && root_managed 1; }

log_count() { grep -c "$1" "$LOG" 2>/dev/null; }

for tool in Xvfb xdpyinfo xprop; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
[ -x "$WDWRITE" ] || skip_all "util/.libs/wdwrite not built"

export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  FTitleBack = (totally, bogus, spec);
  WorkspaceBorder = "BogusBorder";
}
EOF

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use" >&2; exit 77
fi
note "start Xvfb $DPY with two screens (Zaphod)"
Xvfb "$DPY" -screen 0 1024x768x24 -screen 1 640x480x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
wait_for 40 sh -c "kill -0 $XVFB_PID 2>/dev/null && DISPLAY=$DPY xdpyinfo >/dev/null 2>&1" \
    || fatal "Xvfb failed to start"

note "start wmaker on both screens"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    ASAN_OPTIONS="$(drive_asan_options)" \
    DISPLAY=$DPY \
    "$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
wait_for 120 both_roots_managed \
    || fatal "wmaker did not manage both screens"

# --- cold-boot single parse, per-screen apply --------------------------------
# the hoisted parse emits its schema-validation warning exactly once,
# while the per-screen resource apply hits the broken texture spec once
# per managed screen and falls back to the schema default
check "domain parsed once (one schema warning for two screens)" \
    [ "$(log_count 'wrong option value for key "WorkspaceBorder"')" -eq 1 ]
check "texture fallback fired on screen 0" \
    grep -q 'Error in texture specification for key "FTitleBack"' "$LOG"
check "apply ran on both screens (two texture failures)" \
    [ "$(log_count 'Error in texture specification for key \"FTitleBack\"')" -eq 2 ]
check "fallback used the schema default" \
    [ "$(log_count 'using default \"(solid, black)\" instead')" -eq 2 ]

# --- per-screen IPC sockets ---------------------------------------------------
note "per-screen IPC sockets"
SOCK0=$(msg 0 --get-socketpath)
SOCK1=$(msg 1 --get-socketpath)
note "screen 0: $SOCK0"
note "screen 1: $SOCK1"
check "both socket paths resolved and differ" \
    sh -c "test -n \"$SOCK0\" -a -n \"$SOCK1\" -a \"$SOCK0\" != \"$SOCK1\""
check "IPC answers on screen 0" msg 0 -q get_workspaces
check "IPC answers on screen 1" msg 1 -q get_workspaces

# --- runtime reload fans out to every screen ---------------------------------
# wConfigApplyRuntime() recreates the per-head InputOnly corner windows
# when HotCorners/HotCornerEdge change, so enabling them with a
# distinctive edge must materialise four E x E children on BOTH roots
corner_count() {
    DISPLAY="$DPY.$1" xwininfo -root -tree 2>/dev/null \
        | grep -c "${CORNER}x${CORNER}+" || true
}
corners_up() { [ "$(corner_count "$1")" -eq "$2" ]; }
CORNER=37
C0=$(corner_count 0); C1=$(corner_count 1)
check "no corner windows before reload" \
    sh -c "test \"$C0\" -eq 0 -a \"$C1\" -eq 0"
wdwrite WindowMaker HotCorners YES
wdwrite WindowMaker HotCornerEdge "$CORNER"
msg 0 -q reconfigure || fail "reconfigure rejected"
wait_for 50 corners_up 0 4 || fail "corner windows did not appear on screen 0"
wait_for 50 corners_up 1 4 || fail "corner windows did not appear on screen 1"
C0=$(corner_count 0); C1=$(corner_count 1)
check "four corner windows created on screen 0" [ "$C0" -eq 4 ]
check "four corner windows created on screen 1" [ "$C1" -eq 4 ]
check "wmaker alive after reload fan-out" kill -0 "$WMAKER_PID"

# --- teardown ------------------------------------------------------------------
msg 0 -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"
kill -0 "$WMAKER_PID" 2>/dev/null && fail "wmaker did not exit cleanly"

# a display with more than one X screen keys WMState on the X screen
check "WMState.0 written" test -f "$WMHOME/Defaults/WMState.0"
check "WMState.1 written" test -f "$WMHOME/Defaults/WMState.1"
check "no bare WMState on a multi-screen display" \
    test ! -e "$WMHOME/Defaults/WMState"

note "multi-screen drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"

[ "$FAILED" -eq 0 ] && [ "$PASSED" -gt 0 ]
