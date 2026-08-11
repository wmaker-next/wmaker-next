#!/bin/sh
# Drive the window switch panel: flat cycle (Alt+Tab), app-group cycle
# (Mod4+Tab), intra-app cycle (Mod4+grave), letter-key lookup by WM_CLASS
# initial, Escape cancel, skip_switchpanel exclusion via
# _NET_WM_STATE_SKIP_TASKBAR, and SwitchPanelIconSize runtime reconfigure.
#
# Harness conventions follow tabgroup-drive.sh: Xephyr, isolated
# WMAKER_USER_ROOT and XDG_RUNTIME_DIR, PID tracking, no pkill.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${SWITCHPANEL_DRIVE_DISPLAY:-:109}
W=1280
H=800
PIDFILE=$(mktemp /tmp/switchpanel-drive.pids.XXXXXX)
WMHOME=$(mktemp -d /tmp/switchpanel-drive-wmhome.XXXXXX)
RUNTIME=$(mktemp -d /tmp/switchpanel-drive-run.XXXXXX)
LOG=/tmp/switchpanel-drive.log
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
    rm -f "$PIDFILE"
    rm -rf "$WMHOME" "$RUNTIME"
}
trap cleanup EXIT

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
ok()   { PASSED=$((PASSED + 1)); printf 'PASS: %s\n' "$*" | tee -a "$LOG"; }
fail() { FAILED=$((FAILED + 1)); printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; }
fatal() { printf 'FATAL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
skip_all() { printf 'SKIP: %s\n' "$*" | tee -a "$LOG"; exit 77; }

check() {
    cd_desc=$1
    shift
    if "$@"; then
        ok "$cd_desc"
    else
        fail "$cd_desc"
    fi
}

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

xdo() { DISPLAY=$DPY xdotool "$@"; }

msg() {
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$WMAKERMSG" -s "$SOCKET" "$@"
}
ipc_ready() { msg -q get_version >/dev/null 2>&1; }

# _NET_ACTIVE_WINDOW from root, returns the decimal XID or 0x0
active_window() {
    DISPLAY=$DPY xprop -root _NET_ACTIVE_WINDOW 2>/dev/null \
        | sed -n 's/.*_NET_ACTIVE_WINDOW(WINDOW): window id #//p' \
        | tr -d ' '
}
# _NET_WM_STATE_FOCUSED on a client
is_focused() {
    DISPLAY=$DPY xprop -id "$1" _NET_WM_STATE 2>/dev/null \
        | grep -q '_NET_WM_STATE_FOCUSED'
}
wm_class_of() {
    DISPLAY=$DPY xprop -id "$1" WM_CLASS 2>/dev/null \
        | sed -n 's/.*WM_CLASS(STRING) = "[^"]*", "\([^"]*\)".*/\1/p'
}

for tool in Xephyr xdotool xterm xprop xwininfo wmctrl; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not found"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
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
  StrictWindozeCycle = YES;
}
EOF
cat >"$WMHOME/Defaults/WMWindowAttributes" <<'EOF'
{
  "*" = { NoAppIcon = YES; };
}
EOF

# --- Xephyr ----------------------------------------------------------------
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use, set SWITCHPANEL_DRIVE_DISPLAY" >&2
    exit 1
fi
note "start Xephyr $DPY"
Xephyr "$DPY" -screen ${W}x${H} -ac >>"$LOG" 2>&1 &
XEPHYR_PID=$!
echo "$XEPHYR_PID" >> "$PIDFILE"
xephyr_ready() {
    kill -0 "$XEPHYR_PID" 2>/dev/null || fatal "Xephyr exited early"
    DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xephyr_ready || fatal "Xephyr failed to start"

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
wait_for 120 wm_ready || fatal "wmaker did not come up"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
case "$SOCKET" in
    "$RUNTIME"/*) : ;;
    *) fatal "IPC socket $SOCKET escaped private XDG_RUNTIME_DIR" ;;
esac
wait_for 80 ipc_ready || fatal "wmaker IPC did not come up at $SOCKET"
sleep 0.5

# --- spawn xterms -----------------------------------------------------------
# Two plain xterms share WM_CLASS "XTerm" for intra-app cycle (CYCLE_CLASS_ONLY).
# Three with distinct -class (Alpha, Beta, Gamma) for app-group cycle and
# letter-key lookup. Distinct -class per xterm so each forms its own
# WApplication (SharedAppIcon coalesces same-instance.class windows).
spawn_term() {
    DISPLAY=$DPY xterm -title "$1" ${2:+-class "$2"} \
        -geometry "$3" -e 'sleep 600' >>"$LOG" 2>&1 &
    p=$!
    echo "$p" >> "$PIDFILE"
}
find_client() {
    DISPLAY=$DPY xdotool search --name "^$1\$" 2>/dev/null | tail -n 1
}

note "spawn five xterms"
spawn_term xtA "" 80x24+50+50
spawn_term xtB "" 80x24+200+50
spawn_term Alpha Alpha 80x24+400+100
spawn_term Beta  Beta  80x24+600+200
spawn_term Gamma Gamma 80x24+800+100
for t in xtA xtB Alpha Beta Gamma; do
    wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^$t\$' 2>/dev/null | grep -q ." \
        || fatal "$t did not map"
done
XA=$(find_client xtA)
XB=$(find_client xtB)
AC=$(find_client Alpha)
BT=$(find_client Beta)
GM=$(find_client Gamma)
GM_HEX=$(printf '0x%x' "$GM")
note "xtA=$XA xtB=$XB Alpha=$AC Beta=$BT Gamma=$GM"

# Establish a deterministic MRU order: activate xtA, xtB, Alpha, Beta, Gamma
# so Gamma is most-recently-focused and the panel list walks Gamma->Beta->
# Alpha->xtB->xtA.
for w in "$XA" "$XB" "$AC" "$BT" "$GM"; do
    xdo windowactivate "$w"; sleep 0.2
done
sleep 0.3

# --- 1. CYCLE_FLAT forward (Alt+Tab) ---------------------------------------
note "CYCLE_FLAT: Alt+Tab changes focus"
before=$(active_window)
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.2
xdo keyup alt; sleep 0.3
after=$(active_window)
check "Alt+Tab moved focus away from $before" [ "$before" != "$after" ]
check "Alt+Tab target is focused" is_focused "$after"
# cycle again: Alt+Tab from the new window
before2=$after
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.2
xdo keyup alt; sleep 0.3
after2=$(active_window)
check "second Alt+Tab moved focus away from $before2" [ "$before2" != "$after2" ]
check "wmaker alive after flat cycle" ipc_ready

# --- 2. CYCLE_FLAT backward (Alt+Shift+Tab) -------------------------------
note "CYCLE_FLAT: Alt+Shift+Tab backward"
before=$after2
xdo keydown alt; sleep 0.1
xdo keydown shift; sleep 0.1
xdo key Tab; sleep 0.2
xdo keyup shift; sleep 0.1
xdo keyup alt; sleep 0.3
after=$(active_window)
check "Alt+Shift+Tab moved focus away from $before" [ "$before" != "$after" ]
check "wmaker alive after backward cycle" ipc_ready

# --- 3. CYCLE_GROUP (Mod4+Tab) ----------------------------------------------
note "CYCLE_GROUP: Mod4+Tab cycles app groups"
# Re-focus Gamma so the MRU is deterministic.
xdo windowactivate "$GM"; sleep 0.3
before=$(active_window)
before_class=$(wm_class_of "$before")
xdo keydown super; sleep 0.1
xdo key Tab; sleep 0.2
xdo keyup super; sleep 0.3
after=$(active_window)
after_class=$(wm_class_of "$after")
check "Mod4+Tab moved focus away from $before" [ "$before" != "$after" ]
check "Mod4+Tab changed WM_CLASS ($before_class -> $after_class)" \
    [ "$before_class" != "$after_class" ]
check "wmaker alive after app-group cycle" ipc_ready

# --- 4. Letter-key lookup on app-group panel --------------------------------
note "letter-key: Mod4+Tab then 'b' selects a Beta-class window"
# Focus xtA (class XTerm) so the app-group panel starts from a non-Beta class.
xdo windowactivate "$XA"; sleep 0.3
xdo keydown super; sleep 0.1
xdo key Tab; sleep 0.2
xdo key b; sleep 0.2
xdo keyup super; sleep 0.3
after=$(active_window)
after_class=$(wm_class_of "$after")
class_starts_with() { case "$2" in "$1"*) true;; *) false;; esac; }
check "letter 'b' focused a window" [ -n "$after" ]
check "letter 'b' selected class starting with B (got $after_class)" \
    class_starts_with B "$after_class"
check "wmaker alive after letter lookup" ipc_ready

# --- 5. CYCLE_CLASS_ONLY (Mod4+grave) --------------------------------------
note "CYCLE_CLASS_ONLY: Mod4+grave cycles within same WM_CLASS"
# Focus xtA (class XTerm). The intra-app panel filters to same-WM_CLASS, so
# it should cycle between xtA and xtB (both XTerm) and skip Alpha/Beta/Gamma.
xdo windowactivate "$XA"; sleep 0.3
before=$(active_window)
xdo keydown super; sleep 0.1
xdo key grave; sleep 0.2
xdo keyup super; sleep 0.3
after=$(active_window)
check "Mod4+grave moved focus away from $before" [ "$before" != "$after" ]
check "Mod4+grave stayed within XTerm class" \
    [ "$(wm_class_of "$after")" = "XTerm" ]
check "wmaker alive after intra-app cycle" ipc_ready

# --- 6. Escape cancels cycle ------------------------------------------------
note "Escape cancels Alt+Tab cycle"
xdo windowactivate "$GM"; sleep 0.3
before=$(active_window)
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.2
xdo key Escape; sleep 0.3
xdo keyup alt; sleep 0.2
after=$(active_window)
check "Escape restored focus to $before (got $after)" [ "$before" = "$after" ]
check "wmaker alive after Escape cancel" ipc_ready

# --- 7. skip_switchpanel exclusion -----------------------------------------
note "skip_switchpanel: SKIP_TASKBAR excludes from cycle"
# Mark Gamma with _NET_WM_STATE_SKIP_TASKBAR, which mirrors skip_switchpanel
# (wmspec.c:1429-1434). Then Alt+Tab from Beta should never land on Gamma.
DISPLAY=$DPY wmctrl -i -r "$GM" -b add,skip_taskbar
sleep 0.3
# verify the property is set
DISPLAY=$DPY xprop -id "$GM" _NET_WM_STATE 2>/dev/null | grep -q SKIP_TASKBAR \
    || fatal "could not set skip_taskbar on Gamma"
# focus Beta, then Alt+Tab multiple times — Gamma must never get focus
xdo windowactivate "$BT"; sleep 0.3
hit_gamma=0
i=0
while [ "$i" -lt 6 ]; do
    xdo keydown alt; sleep 0.1
    xdo key Tab; sleep 0.2
    xdo keyup alt; sleep 0.3
    cur=$(active_window)
    [ "$cur" = "$GM_HEX" ] && hit_gamma=1
    i=$((i + 1))
done
check "Gamma with skip_taskbar never focused during 6-cycle" [ "$hit_gamma" = "0" ]
check "wmaker alive after skip_switchpanel cycle" ipc_ready
# clean up the flag
DISPLAY=$DPY wmctrl -i -r "$GM" -b remove,skip_taskbar
sleep 0.2

# --- 8. SwitchPanelIconSize runtime reconfigure -----------------------------
note "SwitchPanelIconSize runtime reconfigure"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" DISPLAY=$DPY \
    "$WDWRITE" WindowMaker SwitchPanelIconSize 96
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3
# open the panel — wmaker must survive the size change
xdo windowactivate "$AC"; sleep 0.3
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.3
xdo keyup alt; sleep 0.3
check "wmaker alive after SwitchPanelIconSize reconfigure + cycle" ipc_ready
# restore default
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" DISPLAY=$DPY \
    "$WDWRITE" WindowMaker SwitchPanelIconSize 64
msg -q reconfigure || true
sleep 0.2

# --- teardown --------------------------------------------------------------
note "switchpanel drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
