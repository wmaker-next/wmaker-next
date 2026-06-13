#!/bin/sh
# Verify the window tab-group core: attach via GroupTabAttachKey hides
# the joining window and publishes _NET_WM_STATE_HIDDEN, focus moves to
# the visible member, GroupTabNextKey/GroupTabPrevKey cycle the visible
# member in place, GroupTabDetachKey reveals the leaving window at a
# small offset, closing the active member of a two-window group reveals
# the survivor at the group position without remapping the destroyed
# client, and Shift+click on the close button sends WM_DELETE_WINDOW to
# every member.  Runs wmaker under Xephyr on an isolated display and
# WMAKER_USER_ROOT, tracks every PID it spawns and kills only those.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${TABGROUP_DRIVE_DISPLAY:-:93}
W=1280
H=800
PIDFILE=$(mktemp /tmp/tabgroup-drive.pids.XXXXXX)
WMHOME=$(mktemp -d /tmp/tabgroup-drive-wmhome.XXXXXX)
LOG=/tmp/tabgroup-drive.log
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

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
ok()   { printf 'ok: %s\n' "$*" | tee -a "$LOG"; }
fail() { printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
skip() { printf 'SKIP: %s\n' "$*" | tee -a "$LOG"; exit 77; }
xdo()  { DISPLAY=$DPY xdotool "$@"; }

for tool in Xephyr xdotool xterm xprop xwininfo wmctrl; do
    command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done

wait_for() {
    n=$1; shift
    while [ "$n" -gt 0 ]; do
        "$@" && return 0
        sleep 0.1
        n=$((n - 1))
    done
    return 1
}
pid_gone() { ! kill -0 "$1" 2>/dev/null; }

is_viewable() {
    DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null | grep -q 'Map State: IsViewable'
}
not_viewable() { ! is_viewable "$1"; }
has_state() {
    DISPLAY=$DPY xprop -id "$1" _NET_WM_STATE 2>/dev/null | grep -q "$2"
}
no_state() { ! has_state "$1" "$2"; }
abs_x() {
    DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null \
        | sed -n 's/^ *Absolute upper-left X: *\([0-9-]*\).*/\1/p'
}
abs_y() {
    DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null \
        | sed -n 's/^ *Absolute upper-left Y: *\([0-9-]*\).*/\1/p'
}
client_w() {
    DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null \
        | sed -n 's/^ *Width: *\([0-9]*\).*/\1/p'
}

unset WMAKER_IPC_SOCKET
export WMAKER_USER_ROOT="$WMHOME"
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  GroupTabAttachKey = "Control+Shift+A";
  GroupTabDetachKey = "Control+Shift+D";
  GroupTabNextKey = "Control+Shift+N";
  GroupTabPrevKey = "Control+Shift+P";
}
EOF

# --- Xephyr ----------------------------------------------------------------
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use, set TABGROUP_DRIVE_DISPLAY" >&2
    exit 1
fi
note "start Xephyr $DPY"
Xephyr "$DPY" -screen ${W}x${H} -ac >>"$LOG" 2>&1 &
XEPHYR_PID=$!
echo "$XEPHYR_PID" >> "$PIDFILE"
xephyr_ready() {
    kill -0 "$XEPHYR_PID" 2>/dev/null || fail "Xephyr exited early"
    DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xephyr_ready || fail "Xephyr failed to start"

# --- wmaker ----------------------------------------------------------------
note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
ASAN_OPTIONS="abort_on_error=0:halt_on_error=0:detect_leaks=0" \
DISPLAY=$DPY \
"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
wm_ready() {
    DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null \
        | grep -q 'window id'
}
wait_for 120 wm_ready || fail "wmaker did not come up"
sleep 0.5

# --- spawn three xterms ----------------------------------------------------
spawn_term() {
    DISPLAY=$DPY xterm -title "$1" -name "$1" -geometry "$2" \
        -e 'sleep 600' >>"$LOG" 2>&1 &
    p=$!
    echo "$p" >> "$PIDFILE"
    eval "${1}PID=$p"
}
find_client() {
    DISPLAY=$DPY xdotool search --name "^$1\$" 2>/dev/null | tail -n 1
}

spawn_term termA 80x24+50+50
spawn_term termB 80x24+400+200
spawn_term termC 80x24+200+350
for t in termA termB termC; do
    wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^$t\$' 2>/dev/null | grep -q ." \
        || fail "$t did not map"
done
TA=$(find_client termA)
TB=$(find_client termB)
TC=$(find_client termC)
note "termA=$TA termB=$TB termC=$TC"

# wGroupFindAttachTarget() walks the focus MRU list, so make the order
# deterministic regardless of xterm spawn races: A then B then C, leaving
# termB as termC->prev so the keybind attach picks termB as the target
xdo windowactivate "$TA"; sleep 0.2
xdo windowactivate "$TB"; sleep 0.2

# --- 1. attach termC to termB's group -------------------------------------
note "attach: focus termC, GroupTabAttachKey"
xdo windowactivate "$TC"; sleep 0.3
xdo key --clearmodifiers ctrl+shift+a
wait_for 30 not_viewable "$TC" || fail "termC was not hidden after attach"
is_viewable "$TB" || fail "termB is not visible after attach"
ok "termC hidden, termB visible"

wait_for 20 has_state "$TC" _NET_WM_STATE_HIDDEN \
    || fail "termC missing _NET_WM_STATE_HIDDEN"
has_state "$TC" _NET_WM_STATE_SKIP_PAGER \
    && fail "termC has _NET_WM_STATE_SKIP_PAGER, only HIDDEN expected"
ok "termC has HIDDEN, no SKIP_PAGER"

wait_for 20 no_state "$TC" _NET_WM_STATE_FOCUSED \
    || fail "termC still FOCUSED after being hidden"
wait_for 20 has_state "$TB" _NET_WM_STATE_FOCUSED \
    || fail "termB did not receive focus after attach"
ok "focus transferred to active member"

# --- 2. attach termA into the same group ----------------------------------
note "attach: focus termA, GroupTabAttachKey"
xdo windowactivate "$TA"; sleep 0.3
xdo key --clearmodifiers ctrl+shift+a
wait_for 30 not_viewable "$TA" || fail "termA was not hidden after attach"
is_viewable "$TB" || fail "termB is not visible after second attach"
not_viewable "$TC" || fail "termC became visible after second attach"
ok "three-member group, only termB visible"

# --- 3. cycle next/prev ---------------------------------------------------
note "cycle: GroupTabNextKey twice, then GroupTabPrevKey"
xdo windowactivate "$TB"; sleep 0.3
xdo key --clearmodifiers ctrl+shift+n
wait_for 30 is_viewable "$TC" || fail "next did not reveal termC"
not_viewable "$TB" || fail "termB still visible after next"
ok "next: termB -> termC"

xdo key --clearmodifiers ctrl+shift+n
wait_for 30 is_viewable "$TA" || fail "next did not reveal termA"
not_viewable "$TC" || fail "termC still visible after second next"
ok "next: termC -> termA"

xdo key --clearmodifiers ctrl+shift+p
wait_for 30 is_viewable "$TC" || fail "prev did not reveal termC"
not_viewable "$TA" || fail "termA still visible after prev"
ok "prev: termA -> termC"

# --- 4. detach termC ------------------------------------------------------
note "detach: GroupTabDetachKey on termC"
GX=$(abs_x "$TC"); GY=$(abs_y "$TC")
xdo key --clearmodifiers ctrl+shift+d
wait_for 30 is_viewable "$TC" || fail "detached termC is not visible"
wait_for 30 is_viewable "$TA" || fail "next group member not revealed after detach"
not_viewable "$TB" || fail "termB became visible after detach"
DX=$(abs_x "$TC"); DY=$(abs_y "$TC")
[ "$DX" -gt "$GX" ] && [ "$DY" -gt "$GY" ] \
    || fail "detached termC was not offset from group ($GX,$GY -> $DX,$DY)"
ok "termC detached with offset ($GX,$GY -> $DX,$DY)"
wait_for 20 no_state "$TC" _NET_WM_STATE_HIDDEN \
    || fail "detached termC still has HIDDEN"
ok "termC HIDDEN cleared"

# --- 5. close active member reveals survivor ------------------------------
note "close: WM_DELETE_WINDOW to active member termA"
SX=$(abs_x "$TB")
xdo windowactivate "$TA"; sleep 0.3
DISPLAY=$DPY wmctrl -i -c "$TA"
wait_for 40 pid_gone "$termAPID" || fail "termA did not exit"
wait_for 30 is_viewable "$TB" || fail "survivor termB was not revealed"
[ "$(abs_x "$TB")" = "$SX" ] \
    || fail "survivor termB jumped from x=$SX to x=$(abs_x "$TB")"
wait_for 20 no_state "$TB" _NET_WM_STATE_HIDDEN \
    || fail "survivor termB still has HIDDEN"
kill -0 "$WMAKER_PID" 2>/dev/null || fail "wmaker died handling member close"
ok "survivor revealed at group position, group dissolved"

# --- 6. Shift+close closes whole group ------------------------------------
note "shift+close: re-group termC into termB, click close button"
xdo windowactivate "$TC"; sleep 0.3
xdo key --clearmodifiers ctrl+shift+a
wait_for 30 not_viewable "$TC" || fail "termC was not hidden after re-attach"
xdo windowactivate "$TB"; sleep 0.3
BX=$(abs_x "$TB"); BY=$(abs_y "$TB"); BW=$(client_w "$TB")
# close button sits at the right edge of the titlebar, roughly half a
# titlebar height in and above the client origin
CX=$((BX + BW - 10)); CY=$((BY - 10))
xdo mousemove "$CX" "$CY"; sleep 0.2
xdo keydown shift click 1 keyup shift
wait_for 40 pid_gone "$termBPID" || fail "termB did not exit on Shift+close"
wait_for 40 pid_gone "$termCPID" || fail "termC did not exit on Shift+close"
kill -0 "$WMAKER_PID" 2>/dev/null || fail "wmaker died on Shift+close"
ok "Shift+close closed every group member"

# --- 7. state propagation: move, workspace, iconify -----------------------
note "state: re-group two terms"
spawn_term termD 80x24+100+100
spawn_term termE 80x24+450+250
for t in termD termE; do
    wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^$t\$' 2>/dev/null | grep -q ." \
        || fail "$t did not map"
done
TD=$(find_client termD)
TE=$(find_client termE)
sleep 0.3
xdo windowactivate "$TE"; sleep 0.3
xdo key --clearmodifiers ctrl+shift+a
wait_for 30 not_viewable "$TE" || fail "termE was not hidden after attach"

note "state: move active, switch tab"
DISPLAY=$DPY wmctrl -i -r "$TD" -e 0,600,400,-1,-1
wait_for 30 sh -c "[ \"\$(DISPLAY=$DPY xwininfo -id $TD 2>/dev/null \
    | sed -n 's/^ *Absolute upper-left X: *\\([0-9-]*\\).*/\\1/p')\" -ge 590 ]" \
    || fail "termD did not move"
NX=$(abs_x "$TD"); NY=$(abs_y "$TD")
xdo windowactivate "$TD"; sleep 0.2
xdo key --clearmodifiers ctrl+shift+n
wait_for 30 is_viewable "$TE" || fail "next did not reveal termE"
[ "$(abs_x "$TE")" = "$NX" ] && [ "$(abs_y "$TE")" = "$NY" ] \
    || fail "termE at $(abs_x "$TE"),$(abs_y "$TE"), expected $NX,$NY"
ok "peer position follows active member ($NX,$NY)"

note "state: send group to workspace 2 and back"
DISPLAY=$DPY wmctrl -n 2
wait_for 20 sh -c "DISPLAY=$DPY wmctrl -d 2>/dev/null | wc -l | grep -q '^2\$'" \
    || fail "could not create second workspace"
DISPLAY=$DPY wmctrl -i -r "$TE" -t 1
wait_for 30 not_viewable "$TE" || fail "termE still visible after workspace move"
DISPLAY=$DPY wmctrl -i -r "$TE" -t 0
wait_for 30 is_viewable "$TE" || fail "termE not visible after returning to ws 0"
not_viewable "$TD" \
    || fail "inactive termD became visible after workspace round-trip"
ok "workspace round-trip keeps inactive member hidden"

note "state: iconify group, deiconify"
xdo windowactivate "$TE"; sleep 0.2
DISPLAY=$DPY xdotool windowminimize "$TE"
wait_for 30 not_viewable "$TE" || fail "termE still visible after iconify"
wait_for 20 has_state "$TE" _NET_WM_STATE_HIDDEN \
    || fail "termE missing HIDDEN after iconify"
wait_for 20 has_state "$TD" _NET_WM_STATE_HIDDEN \
    || fail "inactive termD missing HIDDEN after group iconify"
xdo windowactivate "$TE"
wait_for 30 is_viewable "$TE" || fail "termE not visible after deiconify"
not_viewable "$TD" \
    || fail "inactive termD became visible after deiconify"
ok "iconify+deiconify keeps one member visible"

DISPLAY=$DPY wmctrl -i -c "$TE"; DISPLAY=$DPY wmctrl -i -c "$TD"
wait_for 40 pid_gone "$termDPID"
wait_for 40 pid_gone "$termEPID"
kill -0 "$WMAKER_PID" 2>/dev/null || fail "wmaker died during state propagation"

# --- teardown --------------------------------------------------------------
kill -TERM "$WMAKER_PID" 2>/dev/null
wait_for 50 pid_gone "$WMAKER_PID"

note "PASS"
exit 0
