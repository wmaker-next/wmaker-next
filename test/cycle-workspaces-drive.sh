#!/bin/sh
# Drive workspace boundary behavior in wWorkspaceRelativeChange:
# CycleWorkspaces (wrap), AdvanceToNewWorkspace (grow), and the default
# (hard stop at both ends). Asserts via _NET_CURRENT_DESKTOP.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${CYCLE_WS_DRIVE_DISPLAY:-:99}
RUNTIME=$(mktemp -d /tmp/wmaker-cyclews-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/cycle-workspaces-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
WDWRITE="$ROOT/util/.libs/wdwrite"
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

current_desktop() {
    DISPLAY=$DPY xprop -root _NET_CURRENT_DESKTOP 2>/dev/null \
        | sed -n 's/.*_NET_CURRENT_DESKTOP(CARDINAL) = \([0-9]*\).*/\1/p'
}
desktop_is() { [ "$(current_desktop)" = "$1" ]; }
desktop_count() {
    DISPLAY=$DPY xprop -root _NET_NUMBER_OF_DESKTOPS 2>/dev/null \
        | sed -n 's/.*_NET_NUMBER_OF_DESKTOPS(CARDINAL) = \([0-9]*\).*/\1/p'
}

for tool in Xvfb xdpyinfo xdotool xprop xterm; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
[ -x "$WDWRITE" ] || skip_all "util/.libs/wdwrite not built"

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
check "baseline has 3 desktops" [ "$(desktop_count)" = 3 ]

# --- 1. Default (no cycle, no advance): hard stop ---------------------------
note "default: workspace next at top stays"
msg -q workspace 2 || fail "workspace 2 rejected"
wait_for 20 desktop_is 2
check "switched to desktop 2" desktop_is 2

msg -q workspace next || fail "workspace next rejected"
sleep 0.2
check "workspace next at top (2) stays at 2 (no cycle)" desktop_is 2

msg -q workspace 0 || fail "workspace 0 rejected"
wait_for 20 desktop_is 0
check "switched back to desktop 0" desktop_is 0

msg -q workspace prev || fail "workspace prev rejected"
sleep 0.2
check "workspace prev at bottom (0) stays at 0 (no cycle)" desktop_is 0

# --- 2. CycleWorkspaces=YES: wrap at both ends ------------------------------
note "configure CycleWorkspaces=YES"
wdwrite WindowMaker CycleWorkspaces YES
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3

msg -q workspace 2 || fail "workspace 2 rejected"
wait_for 20 desktop_is 2
check "switched to desktop 2" desktop_is 2

msg -q workspace next || fail "workspace next rejected"
sleep 0.2
check "workspace next wraps 2 -> 0 (cycle)" desktop_is 0

msg -q workspace prev || fail "workspace prev rejected"
sleep 0.2
check "workspace prev wraps 0 -> 2 (cycle)" desktop_is 2

msg -q workspace 0 || fail "workspace 0 rejected"
wait_for 20 desktop_is 0
check "switched to desktop 0" desktop_is 0

msg -q workspace prev || fail "workspace prev rejected"
sleep 0.2
check "workspace prev wraps 0 -> 2 (cycle)" desktop_is 2

# --- 3. AdvanceToNewWorkspace=YES (cycle off): grow on next, stop on prev ---
note "configure AdvanceToNewWorkspace=YES, CycleWorkspaces=NO"
wdwrite WindowMaker AdvanceToNewWorkspace YES
wdwrite WindowMaker CycleWorkspaces NO
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3

msg -q workspace 2 || fail "workspace 2 rejected"
wait_for 20 desktop_is 2
check "switched to desktop 2" desktop_is 2

msg -q workspace next || fail "workspace next rejected"
sleep 0.2
check "workspace next at top (2) advances to desktop 3" desktop_is 3
check "desktop count grew to 4" [ "$(desktop_count)" = 4 ]

msg -q workspace prev || fail "workspace prev rejected"
sleep 0.2
check "workspace prev goes back to desktop 2" desktop_is 2

msg -q workspace 0 || fail "workspace 0 rejected"
wait_for 20 desktop_is 0
check "switched to desktop 0" desktop_is 0

msg -q workspace prev || fail "workspace prev rejected"
sleep 0.2
check "workspace prev at bottom (0) stays at 0 (no cycle, no advance below)" desktop_is 0

# --- teardown ----------------------------------------------------------------
note "cycle-workspaces drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
