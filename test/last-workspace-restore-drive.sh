#!/bin/sh
# Drive last-workspace restore on warm restart: switch to workspace 2,
# wmakermsg restart, assert _NET_CURRENT_DESKTOP = 2 after restart.
# On warm restart wmaker reads _NET_CURRENT_DESKTOP from the root
# window (which persists across the re-exec since the X server stays
# up) and restores to that workspace via wWorkspaceForceChange
# (startup.c:776).
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${LAST_WS_RESTORE_DRIVE_DISPLAY:-:101}
RUNTIME=$(mktemp -d /tmp/wmaker-lastws-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/last-workspace-restore-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
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

current_desktop() {
    DISPLAY=$DPY xprop -root _NET_CURRENT_DESKTOP 2>/dev/null \
        | sed -n 's/.*_NET_CURRENT_DESKTOP(CARDINAL) = \([0-9]*\).*/\1/p'
}
desktop_is() { [ "$(current_desktop)" = "$1" ]; }

for tool in Xvfb xdpyinfo xprop xterm; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

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
  SaveSessionOnExit = YES;
}
EOF
cat >"$WMHOME/Defaults/WMState" <<'EOF'
{
  Workspaces = (
    { Name = "ws-0"; },
    { Name = "ws-1"; },
    { Name = "ws-2"; }
  );
}
EOF

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use" >&2; exit 1
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 1024x768x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
wait_for 40 sh -c "kill -0 $XVFB_PID 2>/dev/null && DISPLAY=$DPY xdpyinfo >/dev/null 2>&1" \
    || fatal "Xvfb failed to start"

note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    ASAN_OPTIONS="abort_on_error=0:halt_on_error=0:detect_leaks=0" \
    DISPLAY=$DPY \
    "$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
wait_for 80 ipc_ready || fatal "wmaker IPC did not come up"
sleep 0.5

check "baseline desktop is 0" desktop_is 0

# --- 1. Switch to workspace 2, restart, assert restore ----------------------
note "switch to workspace 2"
msg -q workspace 2 || fail "workspace 2 rejected"
wait_for 20 desktop_is 2
check "switched to desktop 2" desktop_is 2

note "wmakermsg restart"
msg -q restart >>"$LOG" 2>&1
sleep 0.5
wait_for 120 ipc_ready || fatal "wmaker IPC did not re-bind after restart"
WMAKER_PID=$(msg get_version 2>/dev/null \
    | sed -n 's/.*pid = \([0-9]*\);.*/\1/p')
[ -n "$WMAKER_PID" ] && echo "$WMAKER_PID" >> "$PIDFILE"
if wait_for 200 desktop_is 2; then
    ok "_NET_CURRENT_DESKTOP = 2 after restart"
else
    fail "_NET_CURRENT_DESKTOP = 2 after restart (got $(current_desktop))"
fi

# --- 2. Switch to workspace 0, restart, assert restore ----------------------
note "switch to workspace 0, restart again"
msg -q workspace 0 || fail "workspace 0 rejected"
wait_for 20 desktop_is 0
check "switched to desktop 0" desktop_is 0

msg -q restart >>"$LOG" 2>&1
sleep 0.5
wait_for 120 ipc_ready || fatal "wmaker IPC did not re-bind after second restart"
WMAKER_PID=$(msg get_version 2>/dev/null \
    | sed -n 's/.*pid = \([0-9]*\);.*/\1/p')
[ -n "$WMAKER_PID" ] && echo "$WMAKER_PID" >> "$PIDFILE"
if wait_for 120 desktop_is 0; then
    ok "_NET_CURRENT_DESKTOP = 0 after second restart"
else
    fail "_NET_CURRENT_DESKTOP = 0 after second restart"
fi
check "wmaker alive after second restart" ipc_ready

# --- teardown ----------------------------------------------------------------
note "last-workspace-restore drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
