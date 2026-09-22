#!/bin/sh
# Drive the ShowDesktopKey / show-desktop IPC verb: toggle
# _NET_SHOWING_DESKTOP via the new keybind/IPC path, verify windows
# iconify and restore. Asserts via _NET_SHOWING_DESKTOP and
# _NET_WM_STATE_HIDDEN on client windows.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${SHOWDESKTOP_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-showdesktop-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/show-desktop-drive.log
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
    sd_desc=$1; shift
    if "$@"; then ok "$sd_desc"; else fail "$sd_desc"; fi
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

showing_desktop() {
    DISPLAY=$DPY xprop -root _NET_SHOWING_DESKTOP 2>/dev/null \
        | sed -n 's/.*_NET_SHOWING_DESKTOP(CARDINAL) = \([0-9]*\).*/\1/p'
}
showing_is() { [ "$(showing_desktop)" = "$1" ]; }

window_is_iconic() {
    DISPLAY=$DPY xprop -id "$1" WM_STATE 2>/dev/null \
        | grep -q 'Iconic$'
}
window_is_normal() {
    DISPLAY=$DPY xprop -id "$1" WM_STATE 2>/dev/null \
        | grep -q 'Normal$'
}

for tool in Xvfb xdpyinfo xdotool xprop xterm; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
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
}
EOF
cat >"$WMHOME/Defaults/WMState" <<'EOF'
{
  Workspaces = (
    { Name = "ws-0"; },
    { Name = "ws-1"; }
  );
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
    ASAN_OPTIONS="abort_on_error=0:halt_on_error=0:detect_leaks=0" \
    DISPLAY=$DPY \
    "$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
wait_for 80 ipc_ready || fatal "wmaker IPC did not come up"
sleep 0.5

check "baseline showing desktop is 0" showing_is 0

# --- 1. Launch two xterm windows -------------------------------------------
note "launch xterm windows"
DISPLAY=$DPY xterm -title "sd-1" -geometry 100x100+50+50 -e 'sleep 600' >>"$LOG" 2>&1 &
XT1=$!
DISPLAY=$DPY xterm -title "sd-2" -geometry 100x100+300+50 -e 'sleep 600' >>"$LOG" 2>&1 &
XT2=$!
echo "$XT1" >> "$PIDFILE"
echo "$XT2" >> "$PIDFILE"

xt_mapped() { [ -n "$(DISPLAY=$DPY xdotool search --name "^$1\$" 2>/dev/null | tail -n 1)" ]; }
wait_for 60 sh -c 'DISPLAY='"$DPY"' xdotool search --name "^sd-1$" 2>/dev/null | grep -q .' \
    || fatal "xterm sd-1 did not map"
wait_for 60 sh -c 'DISPLAY='"$DPY"' xdotool search --name "^sd-2$" 2>/dev/null | grep -q .' \
    || fatal "xterm sd-2 did not map"

W1=$(DISPLAY=$DPY xdotool search --name "^sd-1$" 2>/dev/null | tail -n 1)
W2=$(DISPLAY=$DPY xdotool search --name "^sd-2$" 2>/dev/null | tail -n 1)
[ -n "$W1" ] && [ -n "$W2" ] || fatal "could not find xterm window ids"
note "xterm windows: $W1 $W2"

sleep 1
check "baseline: windows are not hidden" \
    window_is_normal "$W1"
check "baseline: showing desktop is 0" showing_is 0

# --- 2. show-desktop on: iconify all, set _NET_SHOWING_DESKTOP -------------
note "IPC show-desktop on"
msg -q action ShowDesktop || fail "show-desktop rejected"
wait_for 30 showing_is 1
check "show-desktop sets _NET_SHOWING_DESKTOP to 1" showing_is 1
wait_for 30 sh -c 'DISPLAY='"$DPY"' xprop -id '"$W1"' WM_STATE 2>/dev/null | grep -q "Iconic$"'
check "window 1 is iconic after show-desktop" window_is_iconic "$W1"
wait_for 30 sh -c 'DISPLAY='"$DPY"' xprop -id '"$W2"' WM_STATE 2>/dev/null | grep -q "Iconic$"'
check "window 2 is iconic after show-desktop" window_is_iconic "$W2"

# --- 3. show-desktop off: restore all, clear _NET_SHOWING_DESKTOP ----------
note "IPC show-desktop off (toggle)"
msg -q action ShowDesktop || fail "show-desktop toggle rejected"
wait_for 30 showing_is 0
check "show-desktop toggle sets _NET_SHOWING_DESKTOP to 0" showing_is 0
sleep 1
check "window 1 is restored after toggle" window_is_normal "$W1"
check "window 2 is restored after toggle" window_is_normal "$W2"

# --- 4. the focus-migrated window is swept too ------------------------------
# With three windows focused T3->T2->T1 and the pointer parked over T1,
# iconifying T3 hands focus to T1 under click focus and wSetFocusTo
# moves it to the chain head, unlinking it from the middle; a sweep
# that walks and iconifies in one pass resumes from the stale prev
# snapshot and leaves T1 mapped while _NET_SHOWING_DESKTOP is 1.
if command -v wmctrl >/dev/null 2>&1; then
	note "three-window sweep with the pointer over the oldest-focused window"
	DISPLAY=$DPY xterm -title "sd-3" -geometry 100x100+300+350 -e 'sleep 600' >>"$LOG" 2>&1 &
	XT3=$!
	echo "$XT3" >> "$PIDFILE"
	wait_for 60 sh -c 'DISPLAY='"$DPY"' xdotool search --name "^sd-3$" 2>/dev/null | grep -q .' \
	    || fatal "xterm sd-3 did not map"
	W3=$(DISPLAY=$DPY xdotool search --name "^sd-3$" 2>/dev/null | tail -n 1)
	[ -n "$W3" ] || fatal "could not find the sd-3 window id"
	DISPLAY=$DPY wmctrl -ia "$W1"; sleep 0.3
	DISPLAY=$DPY wmctrl -ia "$W2"; sleep 0.3
	DISPLAY=$DPY wmctrl -ia "$W3"; sleep 0.3
	set -- $(DISPLAY=$DPY xwininfo -id "$W1" | awk \
	    '/Absolute upper-left X/{x=$NF} /Absolute upper-left Y/{y=$NF}
	     /Width:/{w=$NF} /Height:/{h=$NF} END{print x+w/2, y+h/2}')
	DISPLAY=$DPY xdotool mousemove "$1" "$2"
	sleep 0.2
	DISPLAY=$DPY wmctrl -ia "$W3"
	sleep 0.3

	msg -q action ShowDesktop || fail "show-desktop rejected on the three-window leg"
	wait_for 30 showing_is 1
	check "three-window leg sets showing desktop" showing_is 1
	wait_for 30 sh -c 'DISPLAY='"$DPY"' xprop -id '"$W1"' WM_STATE 2>/dev/null | grep -q "Iconic$"'
	check "the focus-migrated window is iconic after show-desktop" window_is_iconic "$W1"
	check "window 2 is iconic" window_is_iconic "$W2"
	check "window 3 is iconic" window_is_iconic "$W3"

	msg -q action ShowDesktop || fail "show-desktop toggle rejected on the three-window leg"
	wait_for 30 showing_is 0
	sleep 1
	check "the focus-migrated window is restored" window_is_normal "$W1"
	check "window 2 is restored" window_is_normal "$W2"
	check "window 3 is restored" window_is_normal "$W3"
else
	note "wmctrl not installed, skipping the focus-migration sweep leg"
	XT3=""
	W3=""
fi

# --- 5. EWMH ClientMessage coexists with IPC path --------------------------
# Use wmctrl -k which sends a real _NET_SHOWING_DESKTOP ClientMessage
# (xprop -set only changes the property, WM needs the ClientMessage)
if command -v wmctrl >/dev/null 2>&1; then
	note "EWMH ClientMessage on via wmctrl"
	DISPLAY=$DPY wmctrl -k on >>"$LOG" 2>&1
	wait_for 30 showing_is 1
	check "EWMH client message sets showing desktop to 1" showing_is 1
	wait_for 30 sh -c 'DISPLAY='"$DPY"' xprop -id '"$W1"' WM_STATE 2>/dev/null | grep -q "Iconic$"'
	check "window 1 iconic via EWMH path" window_is_iconic "$W1"

	note "IPC toggle off after EWMH on"
	msg -q action ShowDesktop || fail "show-desktop toggle after EWMH rejected"
	wait_for 30 showing_is 0
	check "IPC toggle restores after EWMH on" showing_is 0
	sleep 1
	check "window 1 restored after cross-path toggle" window_is_normal "$W1"
else
	note "wmctrl not installed, skipping EWMH cross-path test"
fi

# --- 6. Each press toggles -------------------------------------------------
note "toggle cycle: off -> on -> off -> on"
msg -q action ShowDesktop >/dev/null
wait_for 30 showing_is 1
check "first press turns showing on" showing_is 1
msg -q action ShowDesktop >/dev/null
wait_for 30 showing_is 0
check "second press toggles off" showing_is 0
msg -q action ShowDesktop >/dev/null
wait_for 30 showing_is 1
check "third press toggles on again" showing_is 1
msg -q action ShowDesktop >/dev/null
wait_for 30 showing_is 0
check "fourth press toggles off" showing_is 0

# --- teardown ----------------------------------------------------------------
if [ -n "${XT3:-}" ]; then kill "$XT3" 2>/dev/null; fi
kill "$XT1" "$XT2" 2>/dev/null
note "show-desktop drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
