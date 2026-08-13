#!/bin/sh
# Iconic-state transitions and the session restore lock.
#
# 1. A client that starts iconic by its own WM_HINTS (initial_state =
#    IconicState) is managed miniaturized. When it later re-maps itself
#    (XMapWindow on its client window -> MapRequest, the ICCCM
#    iconic-to-normal transition) wmaker must deiconify it: the
#    restore_iconified lock is granted by the session paths only
#    (window.c win_state, startup.c startup walk), never by the
#    client's own hint (src/event_client.c handleMapRequest).
#
# 2. A window iconified in a saved session is adopted iconic on the
#    next cold start (wSessionQueueSavedState pre-load plus the startup
#    walk) and its restore_iconified lock keeps client re-maps and
#    non-pager _NET_ACTIVE_WINDOW requests from undoing the iconic
#    state (src/event_client.c, src/wmspec.c) until the user
#    deiconifies.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${ICONIC_DRIVE_DISPLAY:-:114}
RUNTIME=$(mktemp -d /tmp/wmaker-iconic-test.XXXXXX)
WMHOME_A="$RUNTIME/home-a"
WMHOME_B="$RUNTIME/home-b"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/restore-iconic-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
SOCKET=
PROBE="$RUNTIME/probe-iconic.py"
XIDFILE="$RUNTIME/xid"
FIFO="$RUNTIME/cmd"
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
    ri_desc=$1; shift
    if "$@"; then ok "$ri_desc"; else fail "$ri_desc"; fi
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

# one command line to the probe client, bounded so a wedged client
# cannot hang the drive
probe_cmd() {
    if ! timeout 10 sh -c "printf '%s\n' \"\$1\" > \"\$2\"" probe_cmd "$1" "$FIFO"; then
        fail "probe client did not accept command: $1"
        return 1
    fi
    return 0
}

window_state_is() {
    DISPLAY=$DPY xprop -id "$1" WM_STATE 2>/dev/null | grep -q "$2\$"
}

# bounded negative poll: the state must not flip while we watch
state_stays() {
    if wait_for 20 window_state_is "$1" "$2"; then
        return 1
    fi
    return 0
}

start_wmaker() {
    ri_home=$1
    export WMAKER_USER_ROOT="$ri_home"
    unset WMAKER_USER_XDG
    export XDG_RUNTIME_DIR="$RUNTIME"
    unset WMAKER_IPC_SOCKET
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        DISPLAY=$DPY \
        "$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
    WMAKER_PID=$!
    echo "$WMAKER_PID" >> "$PIDFILE"
    SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
    wait_for 80 ipc_ready || fatal "wmaker IPC did not come up"
    sleep 0.3
}

stop_wmaker() {
    msg -q exit >>"$LOG" 2>&1
    wait_for 50 pid_gone "$WMAKER_PID" || fatal "wmaker did not exit"
}

start_probe() {
    ri_fifo=$1
    FIFO="$ri_fifo"
    rm -f "$ri_fifo"
    mkfifo "$ri_fifo"
    : > "$XIDFILE"
    ICONIC_DRIVE_FIFO="$ri_fifo" DISPLAY=$DPY python3 "$PROBE" \
        >"$XIDFILE" 2>>"$LOG" &
    PROBE_PID=$!
    echo "$PROBE_PID" >> "$PIDFILE"
    wait_for 50 sh -c "[ -s '$XIDFILE' ]" || fatal "probe did not report its XID"
    PROBE_XID=$(cat "$XIDFILE")
}

for tool in Xvfb xdpyinfo xprop python3; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
python3 -c 'import Xlib' >/dev/null 2>&1 || skip_all "python3-xlib not installed"
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

cat > "$PROBE" <<'EOF'
import os
from Xlib import X, Xatom, Xutil, display
from Xlib.protocol import event


def main():
    mode = os.environ['ICONIC_DRIVE_MODE']
    fifo = os.environ['ICONIC_DRIVE_FIFO']
    d = display.Display()
    s = d.screen()
    w = s.root.create_window(10, 10, 320, 200, 0, s.root_depth,
                             X.InputOutput, X.CopyFromParent)
    w.set_wm_class('IconicDrive', 'IconicDrive')
    w.set_wm_name('IconicDrive')
    if mode == 'initial':
        w.set_wm_hints(flags=Xutil.StateHint | Xutil.InputHint,
                       input=1, initial_state=Xutil.IconicState)
    w.change_property(d.intern_atom('WM_COMMAND'), Xatom.STRING, 8,
                      b'probe-iconic\x00')
    w.map()
    d.flush()
    print('0x%x' % w.id, flush=True)
    while True:
        with open(fifo) as f:
            for line in f:
                op = line.strip()
                if op == 'remap':
                    w.map()
                elif op == 'wmstate-iconic':
                    t = d.intern_atom('WM_CHANGE_STATE')
                    ev = event.ClientMessage(window=w, client_type=t,
                                             data=(32, [Xutil.IconicState, 0, 0, 0, 0]))
                    s.root.send_event(ev, event_mask=X.SubstructureRedirectMask |
                                      X.SubstructureNotifyMask)
                elif op == 'activate':
                    t = d.intern_atom('_NET_ACTIVE_WINDOW')
                    ev = event.ClientMessage(window=w, client_type=t,
                                             data=(32, [0, 0, 0, 0, 0]))
                    s.root.send_event(ev, event_mask=X.SubstructureRedirectMask |
                                      X.SubstructureNotifyMask)
                elif op == 'unhide':
                    t = d.intern_atom('_NET_WM_STATE')
                    h = d.intern_atom('_NET_WM_STATE_HIDDEN')
                    ev = event.ClientMessage(window=w, client_type=t,
                                             data=(32, [0, h, 0, 0, 0]))
                    s.root.send_event(ev, event_mask=X.SubstructureRedirectMask |
                                      X.SubstructureNotifyMask)
                elif op == 'newwin':
                    w2 = s.root.create_window(90, 90, 320, 200, 0,
                                              s.root_depth, X.InputOutput,
                                              X.CopyFromParent)
                    w2.set_wm_class('IconicDrive', 'IconicDrive')
                    w2.set_wm_name('IconicDrive')
                    w2.change_property(d.intern_atom('WM_COMMAND'),
                                       Xatom.STRING, 8, b'probe-iconic\x00')
                    w2.map()
                    d.flush()
                    print('0x%x' % w2.id, flush=True)
                elif op == 'quit':
                    return
                d.sync()


main()
EOF

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use" >&2
    exit 1
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 1024x768x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
wait_for 40 sh -c "kill -0 $XVFB_PID 2>/dev/null && DISPLAY=$DPY xdpyinfo >/dev/null 2>&1" \
    || fatal "Xvfb failed to start"
mkdir -p "$WMHOME_A/Defaults" "$WMHOME_B/Defaults"
cat >"$WMHOME_A/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
}
EOF
cat >"$WMHOME_B/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  SaveSessionOnExit = YES;
}
EOF

# --- 1. initially-iconic client can map itself out of iconic state ----------
note "leg 1: WM_HINTS iconic client re-map"
start_wmaker "$WMHOME_A"
ICONIC_DRIVE_MODE=initial start_probe "$FIFO"

check "initially-iconic client is iconic" \
    wait_for 50 window_state_is "$PROBE_XID" Iconic

probe_cmd remap || true
check "client re-map deiconifies (ICCCM iconic->normal)" \
    wait_for 50 window_state_is "$PROBE_XID" Normal
check "wmaker alive after leg 1" ipc_ready

stop_wmaker
kill "$PROBE_PID" 2>/dev/null
wait_for 20 pid_gone "$PROBE_PID" || kill -9 "$PROBE_PID" 2>/dev/null

# --- 2. session-restored iconic window keeps its lock ------------------------
note "leg 2: session-adopted iconic window"
start_wmaker "$WMHOME_B"
ICONIC_DRIVE_MODE=normal start_probe "$FIFO"

check "normal client maps normal" \
    wait_for 50 window_state_is "$PROBE_XID" Normal

probe_cmd wmstate-iconic || true
check "WM_CHANGE_STATE iconifies the client" \
    wait_for 50 window_state_is "$PROBE_XID" Iconic

note "save the session and exit"
stop_wmaker "$WMHOME_B"
check "session file records the iconic window" \
    wait_for 50 sh -c "tr -d ' \\n;' < '$WMHOME_B/Defaults/WMState' | grep -q Miniaturized=Yes"

note "cold start with the same client still running"
start_wmaker "$WMHOME_B"
check "adopted window is iconic" \
    wait_for 50 window_state_is "$PROBE_XID" Iconic

probe_cmd remap || true
check "client re-map stays locked" state_stays "$PROBE_XID" Normal
check "still iconic after re-map" window_state_is "$PROBE_XID" Iconic

probe_cmd activate || true
check "_NET_ACTIVE_WINDOW stays locked" state_stays "$PROBE_XID" Normal
check "still iconic after activation attempt" window_state_is "$PROBE_XID" Iconic

note "finding 53: pager-style HIDDEN removal on the locked window"
probe_cmd unhide || true
check "HIDDEN removal deiconifies the locked window" \
    wait_for 50 window_state_is "$PROBE_XID" Normal
check "wmaker alive after leg 2" ipc_ready

note "finding 55: close the adopted window, launch the same command"
DISPLAY=$DPY wmctrl -i -c "$PROBE_XID" >>"$LOG" 2>&1
check "adopted window unmanaged" \
    wait_for 50 sh -c "! DISPLAY=$DPY wmctrl -l 2>/dev/null | grep -qi '$PROBE_XID'"
probe_cmd newwin || true
wait_for 50 sh -c "[ \$(wc -l < '$XIDFILE') -ge 2 ]" \
    || fatal "probe did not report the second window"
NEW_XID=$(tail -1 "$XIDFILE")
check "relaunched window maps normal" \
    wait_for 50 window_state_is "$NEW_XID" Normal
check "wmaker alive after leg 3" ipc_ready

# --- teardown ----------------------------------------------------------------
note "restore-iconic drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
