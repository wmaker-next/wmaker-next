#!/bin/sh
# Drive getClipboardSelection() through a root-menu SHEXEC %(sel)
# expansion: a selection owner that answers after the requestor's
# one-second timeout must not poison the next request, its late reply
# is dropped instead of being consumed as the new request's result.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${SELREPLY_DRIVE_DISPLAY:-:$(drive_default_display)}
W=800
H=600
RUNTIME=$(mktemp -d /tmp/selreply-drive.XXXXXX)
WMHOME="$RUNTIME/home"
MARK="$RUNTIME/mark"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/selection-reply-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
: > "$PIDFILE"
: > "$LOG"

cleanup() {
    while read -r p; do
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done < "$PIDFILE"
    rm -f "$PIDFILE"
    rm -rf "$RUNTIME"
}
trap cleanup EXIT

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
fail() { printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; exit 1; }

wait_for() {
    _wf=$1; shift
    while [ "$_wf" -gt 0 ]; do
        "$@" && return 0
        sleep 0.1
        _wf=$((_wf - 1))
    done
    return 1
}
pid_gone() { ! kill -0 "$1" 2>/dev/null; }

command -v Xvfb    >/dev/null || { echo "SKIP: Xvfb not found"; exit 77; }
command -v xdotool >/dev/null || { echo "SKIP: xdotool not found"; exit 77; }
command -v xwininfo >/dev/null || { echo "SKIP: xwininfo not found"; exit 77; }
[ -x "$ROOT/src/.libs/wmaker" ] || { echo "SKIP: wmaker not built"; exit 77; }
[ -x "$WMAKERMSG" ] || { echo "SKIP: wmakermsg not built"; exit 77; }
CLIENT="$RUNTIME/selreply-client"
${CC:-cc} -std=gnu11 "$ROOT/test/selreply-client.c" -o "$CLIENT" -lX11 >>"$LOG" 2>&1 \
    || { echo "SKIP: could not build test/selreply-client.c (needs libX11 headers)"; exit 77; }

msg() {
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$WMAKERMSG" -s "$SOCKET" "$@"
}

# --- isolated WMAKER_USER_ROOT ---------------------------------------------
mkdir -p "$WMHOME/Defaults" "$MARK"
cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  MouseRightButtonAction = OpenApplicationsMenu;
}
EOF
cat >"$WMHOME/Defaults/WMRootMenu" <<EOF
("Main",
 ("Read", SHEXEC, "echo -n \"%s\" > $MARK/out"))
EOF
export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET

# --- Xvfb ------------------------------------------------------------------
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use, set SELREPLY_DRIVE_DISPLAY" >&2
    exit 77
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 ${W}x${H}x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
    kill -0 "$XVFB_PID" 2>/dev/null || fail "Xvfb exited early"
    DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fail "Xvfb failed to start"

# --- wmaker ----------------------------------------------------------------
note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
ASAN_OPTIONS="$(drive_asan_options)" \
DISPLAY=$DPY \
"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"

SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DISPLAY=$DPY XDG_RUNTIME_DIR="$RUNTIME" "$WMAKERMSG" --get-socketpath)
ipc_ready() { msg -q get_version >/dev/null 2>&1; }
wait_for 120 ipc_ready || fail "wmaker did not come up"
sleep 0.3

# --- trigger the %(sel) read through the root menu -------------------------
read_sel() {
    : > "$MARK/out"
    DISPLAY=$DPY xdotool mousemove $((W / 2)) $((H / 2)) >>"$LOG" 2>&1
    DISPLAY=$DPY xdotool mousedown 3 >>"$LOG" 2>&1
    sleep 0.4
    # the entry list is the only child of the menu frame below its title
    ENTRY_WIN=$(DISPLAY=$DPY xwininfo -root -tree 2>/dev/null \
        | awk -v cx=$((W / 2)) -v cy=$((H / 2)) '
            /^     0x/ { fx=""; fy=""; fw="";
                if (match($0, /[0-9]+x[0-9]+\+-?[0-9]+\+-?[0-9]+/)) {
                    split(substr($0, RSTART, RLENGTH), g, /[x+]/);
                    fw=g[1]; fx=g[3]; fy=g[4];
                }
                inframe = (fx!="" && fy<=cy && cy<=fy+40 && fx<=cx && cx<=fx+fw);
                next; }
            inframe && /^        0x/ {
                if (match($0, /[0-9]+x[0-9]+\+-?[0-9]+\+-?[0-9]+  \+-?[0-9]+\+-?[0-9]+$/)) {
                    split(substr($0, RSTART, RLENGTH), g, /[x+ ]+/);
                    if (g[6] > cy) { print g[5] " " g[6]; exit; }
                } }')
    [ -n "$ENTRY_WIN" ] || { DISPLAY=$DPY xdotool mouseup 3 >>"$LOG" 2>&1; return 1; }
    set -- $ENTRY_WIN
    DISPLAY=$DPY xdotool mousemove $(($1 + 20)) $(($2 + 4)) >>"$LOG" 2>&1
    DISPLAY=$DPY xdotool mouseup 3 >>"$LOG" 2>&1
    return 0
}

out_is() {
    [ "$(cat "$MARK/out" 2>/dev/null)" = "$1" ]
}

# --- 1. a slow owner times out, its late reply lands in the next read -----
note "slow owner: read must time out with empty output"
DISPLAY=$DPY "$CLIENT" STALE_REPLY 4800 >>"$LOG" 2>&1 &
echo "$!" >> "$PIDFILE"
sleep 0.5
read_sel || fail "could not trigger the first selection read"
# the entry runs inside the click and its read gives up after one
# second, a fixed settle window keeps the phases from overlapping
sleep 2.5
if [ -n "$(cat "$MARK/out" 2>/dev/null)" ]; then
    fail "timed-out read produced '$(cat "$MARK/out")' instead of empty"
fi
note "ok: timed-out read is empty"

# --- 2. the late reply must be dropped, the fresh owner's text read -------
# The slow owner's reply to the first read lands about 0.4s into this
# read's one-second window, the fresh owner answers 0.8s in, so the
# stale notify is on the queue first when the wait starts
note "fresh owner: read must see its text, not the late stale reply"
DISPLAY=$DPY "$CLIENT" FRESH_TEXT 800 >"$RUNTIME/fresh.log" 2>>"$LOG" &
echo "$!" >> "$PIDFILE"
read_sel || fail "could not trigger the second selection read"
wait_for 40 out_is FRESH_TEXT || fail "late reply consumed: got '$(cat "$MARK/out")' instead of FRESH_TEXT"
note "ok: late reply dropped, fresh owner read"

# --- 3. a later read still sees the fresh owner ----------------------------
note "later read: must still see the fresh owner"
read_sel || fail "could not trigger the third selection read"
wait_for 40 out_is FRESH_TEXT || fail "third read got '$(cat "$MARK/out")' instead of FRESH_TEXT"
note "ok: later read stable"

note "requestors remain distinct after more than four reads"
i=0
while [ "$i" -lt 4 ]; do
    read_sel || fail "could not trigger repeated selection read"
    wait_for 40 out_is FRESH_TEXT || fail "repeated selection read failed"
    i=$((i + 1))
done
[ "$(grep -c '^requestor ' "$RUNTIME/fresh.log")" -eq 6 ] \
    || fail "fresh owner did not receive all six requests"
[ "$(grep '^requestor ' "$RUNTIME/fresh.log" | sort -u | wc -l)" -eq 6 ] \
    || fail "selection requestors were reused"
note "ok: six distinct requestors"

kill -TERM "$WMAKER_PID" 2>/dev/null
wait_for 50 pid_gone "$WMAKER_PID"

note "PASS"
exit 0
