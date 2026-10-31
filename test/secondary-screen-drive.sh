#!/bin/sh
# Secondary-screen (Zaphod) correctness drive. On a display with two X
# screens this checks that independent wmaker instances keep separate
# state and IPC sockets, that WMState migration works, that a
# secondary-only instance reads and monitors screen 0's Xft.dpi, and
# that XKB, root-menu macros and the cut-buffer fallback use the screen
# they belong to.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${SECONDARY_SCREEN_DRIVE_DISPLAY:-:$(drive_default_display)}
W0=800
H0=600
W1=640
H1=480
RUNTIME=$(mktemp -d /tmp/wmaker-secondary-drive.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG="$RUNTIME/wmaker.log"
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
CLIENT="$RUNTIME/xsettings-client"
MACRO_OUT="$RUNTIME/macro.out"
PASTE_OUT="$RUNTIME/paste.out"
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
    screen=$1; shift
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        XDG_RUNTIME_DIR="$RUNTIME" DISPLAY="$DPY.$screen" "$WMAKERMSG" "$@"
}
xclient() {
    DISPLAY="$DPY.1" "$CLIENT" --screen 1 "$@"
}
root_managed() {
    DISPLAY="$DPY.$1" xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null \
        | grep 'window id' >/dev/null
}
ipc_ready() { msg "$1" -q get_version >/dev/null 2>&1; }
screen_ready() { root_managed "$1" && ipc_ready "$1"; }

start_wm() {
    WM_SCREEN=$1
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        ASAN_OPTIONS="$(drive_asan_options)" \
        DISPLAY="$DPY.$WM_SCREEN" \
        "$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
    WM_PID=$!
    echo "$WM_PID" >> "$PIDFILE"
}
stop_wm() {
    msg "$1" -q exit >>"$LOG" 2>&1
}
root_id() {
    DISPLAY="$DPY.$1" xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null \
        | sed -n 's/.*window id # \(0x[0-9a-fA-F]*\).*/\1/p'
}
dpi_is() {
    xclient 2>/dev/null | grep -Fx "Xft/DPI	integer	$1" >/dev/null
}
set_screen_dpi() {
    screen=$1
    dpi=$2
    DISPLAY="$DPY.$screen" xprop -root -f RESOURCE_MANAGER 8s \
        -set RESOURCE_MANAGER "Xft.dpi: $dpi" >>"$LOG" 2>&1
}
open_menu_select() {
    key=$1
    (
        sleep 5
        DISPLAY="$DPY.1" xdotool key --delay 300 "$key" Return
    ) &
    KEYS_CMD=$!
    DISPLAY="$DPY.1" xdotool mousemove 100 100
    msg 1 action RootMenu >>"$LOG" 2>&1
    wait "$KEYS_CMD"
    sleep 1
}

for tool in Xvfb xdpyinfo xprop xdotool; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
${CC:-cc} -std=gnu11 "$ROOT/test/xsettings-client.c" -o "$CLIENT" -lX11 >>"$LOG" 2>&1 \
    || skip_all "could not build xsettings-client (needs libX11 headers)"

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
  MouseRightButtonAction = OpenApplicationsMenu;
}
EOF
cat >"$RUNTIME/root.menu" <<EOF
"Root" MENU
 "Geometry" SHEXEC echo SCR_WIDTH SCR_HEIGHT DEPTH > $MACRO_OUT
 "Paste" SHEXEC echo %s > $PASTE_OUT
"Root" END
EOF
printf '"%s"\n' "$RUNTIME/root.menu" >"$WMHOME/Defaults/WMRootMenu"

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use" >&2
    exit 77
fi
note "start Xvfb $DPY with two screens"
Xvfb "$DPY" -noreset -screen 0 ${W0}x${H0}x24 -screen 1 ${W1}x${H1}x24 -ac \
    >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
    kill -0 "$XVFB_PID" 2>/dev/null || fail "Xvfb exited early"
    DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fatal "Xvfb failed to start"

# --- 7: XKB rules live only on the canonical root ---------------------------
note "XKB rules property placement"
xprop -display "$DPY.0" -root _XKB_RULES_NAMES >/dev/null 2>&1 \
    && XKB0=yes || XKB0=no
xprop -display "$DPY.1" -root _XKB_RULES_NAMES >/dev/null 2>&1 \
    && XKB1=yes || XKB1=no
check "_XKB_RULES_NAMES is present on screen 0" [ "$XKB0" = yes ]
if [ "$XKB1" = yes ]; then
    note "server also publishes _XKB_RULES_NAMES on screen 1"
fi

# --- 1: two independent instances ------------------------------------------
note "two independent instances"
start_wm 0; P0=$WM_PID
start_wm 1; P1=$WM_PID
both_ready() { screen_ready 0 && screen_ready 1; }
wait_for 120 both_ready || fatal "wmaker did not manage both screens independently"

SOCK0=$(msg 0 --get-socketpath)
SOCK1=$(msg 1 --get-socketpath)
ID0=$(root_id 0)
ID1=$(root_id 1)
check "screen 0 has an IPC socket" [ -n "$SOCK0" ]
check "screen 1 has an IPC socket" [ -n "$SOCK1" ]
check "the two instances use different IPC sockets" [ "$SOCK0" != "$SOCK1" ]
check "each root has its own supporting-WM window" \
    sh -c "test -n \"$ID0\" -a -n \"$ID1\" -a \"$ID0\" != \"$ID1\""
check "screen 0 instance answers on its own root" root_managed 0
check "screen 1 instance answers on its own root" root_managed 1

# --- 2: independent WMState files ------------------------------------------
note "WMState independence"
msg 1 -q workspace-new 1 >>"$LOG" 2>&1 || fail "workspace-new on screen 1"
msg 1 -q workspace 1 >>"$LOG" 2>&1 || fail "workspace change on screen 1"
stop_wm 0
stop_wm 1
wait_for 100 pid_gone "$P0" || fail "screen 0 instance did not exit"
wait_for 100 pid_gone "$P1" || fail "screen 1 instance did not exit"

check "WMState.0 exists" test -f "$WMHOME/Defaults/WMState.0"
check "WMState.1 exists" test -f "$WMHOME/Defaults/WMState.1"
check "no bare WMState after two instances" \
    test ! -e "$WMHOME/Defaults/WMState"
check "the two WMState files differ" \
    sh -c "! cmp -s \"$WMHOME/Defaults/WMState.0\" \"$WMHOME/Defaults/WMState.1\""

# --- 3: migration from the legacy bare file --------------------------------
note "WMState migration fallback"
cp "$WMHOME/Defaults/WMState.0" "$WMHOME/Defaults/WMState"
cp "$WMHOME/Defaults/WMState" "$RUNTIME/WMState.seed"
rm -f "$WMHOME/Defaults/WMState.0" "$WMHOME/Defaults/WMState.1"

start_wm 1; P1=$WM_PID
wait_for 120 root_managed 1 || fatal "secondary-only instance did not start"
wait_for 120 ipc_ready 1 || fatal "secondary-only instance did not answer IPC"

# --- 8: root-menu macros use the menu's screen -----------------------------
note "root-menu macros"
rm -f "$MACRO_OUT"
open_menu_select g
wait_for 200 test -f "$MACRO_OUT" || fail "root-menu macro entry did not run"
check "root-menu macros describe screen 1" \
    sh -c "test \"\$(cat '$MACRO_OUT')\" = '$W1 $H1 24'"

# --- 9: cut-buffer fallback uses the action's screen -----------------------
note "cut-buffer fallback"
DISPLAY="$DPY.0" xprop -root -f CUT_BUFFER0 8s -set CUT_BUFFER0 screen-zero \
    >>"$LOG" 2>&1
DISPLAY="$DPY.1" xprop -root -f CUT_BUFFER0 8s -set CUT_BUFFER0 screen-one \
    >>"$LOG" 2>&1
rm -f "$PASTE_OUT"
open_menu_select p
wait_for 200 test -f "$PASTE_OUT" || fail "cut-buffer menu entry did not run"
check "screen 1 paste reads screen 1's CUT_BUFFER0" \
    sh -c "test \"\$(cat '$PASTE_OUT')\" = 'screen-one'"

stop_wm 1
wait_for 100 pid_gone "$P1" || fail "secondary-only instance did not exit"

check "migration writes WMState.1" test -f "$WMHOME/Defaults/WMState.1"
check "migration leaves the bare WMState unchanged" \
    cmp -s "$RUNTIME/WMState.seed" "$WMHOME/Defaults/WMState"
check "migration does not write WMState.0" \
    test ! -e "$WMHOME/Defaults/WMState.0"

# --- 4: DPI cold boot with only screen 0 configured ------------------------
note "DPI cold boot fallback"
DISPLAY="$DPY.1" xprop -root -remove RESOURCE_MANAGER >/dev/null 2>&1
set_screen_dpi 0 144
check "screen 0 carries Xft.dpi 144" \
    sh -c "DISPLAY=$DPY.0 xprop -root RESOURCE_MANAGER | grep -q 'Xft.dpi: 144'"

start_wm 1; P1=$WM_PID
wait_for 120 screen_ready 1 || fatal "secondary-only instance did not come up"
wait_for 100 dpi_is 147456 || fail "screen 1 did not fall back to screen 0's Xft.dpi"
check "secondary-only instance publishes 147456" dpi_is 147456

# --- 5: per-screen override wins over the fallback -------------------------
note "DPI per-screen override"
set_screen_dpi 1 96
wait_for 100 dpi_is 98304 || fail "screen 1 did not publish its own Xft.dpi"
check "screen 1 override publishes 98304" dpi_is 98304
wait_for 100 ipc_ready 1 || fail "instance did not return after the override restart"

# --- 6: runtime change on the canonical root ------------------------------
note "DPI runtime change on screen 0 while managing screen 1"
DISPLAY="$DPY.1" xprop -root -remove RESOURCE_MANAGER >/dev/null 2>&1
wait_for 100 dpi_is 147456 || fail "screen 1 did not return to the screen 0 fallback"
wait_for 100 ipc_ready 1 || fail "instance did not return after removing the override"

set_screen_dpi 0 120
wait_for 120 grep -q 'Xft.dpi 120' "$LOG" \
    || fail "wmaker did not restart for the screen 0 Xft.dpi change"
wait_for 100 dpi_is 122880 \
    || fail "screen 1 did not republish the new screen 0 Xft.dpi"
check "runtime change republished 122880" dpi_is 122880
wait_for 100 ipc_ready 1 || fail "instance did not come back after the runtime restart"

stop_wm 1
wait_for 100 pid_gone "$P1" || fail "secondary-only instance did not exit at teardown"

note "secondary-screen drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"

[ "$FAILED" -eq 0 ] && [ "$PASSED" -gt 0 ]
