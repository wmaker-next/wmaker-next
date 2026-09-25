#!/bin/sh
# Drive wmtouchd through the generic `action` IPC verb using event-level
# replay, so no libinput device or /dev/uinput access is needed.
#
# Asserts that a recognised right-swipe fires NextWorkspace, a spread and a
# pinch fire the workspace actions mapped to PinchOut/PinchIn, a None-mapped
# swipe and a sub-threshold pinch fire nothing, a two-finger swipe is
# inert because libinput never reports one, a typo'd name is logged
# once, and `--check` reports the bad name without executing any action.
# A four-finger swipe bound to an action chain fires both actions in
# order as one IPC batch.
#
# Skips (77) when wmtouchd was not built, i.e. libinput is unavailable.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${TOUCH_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-touch-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=${TOUCH_DRIVE_LOG:-$(mktemp /tmp/touch-drive.XXXXXX)}
EVLOG="$RUNTIME/events"
DAEMONLOG="$RUNTIME/wmtouchd.log"
CHECKLOG="$RUNTIME/check.log"
GEST="$RUNTIME/gestures.txt"
GEST2="$RUNTIME/gestures-chain.txt"
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
WDWRITE="$ROOT/util/.libs/wdwrite"
WTOUCHD="$ROOT/util/.libs/wmtouchd"
SOCKET=
MON_PID=

: > "$PIDFILE"
: > "$LOG"
: > "$EVLOG"
: > "$DAEMONLOG"
: > "$CHECKLOG"

PASSED=0
FAILED=0

cleanup() {
	[ -n "$MON_PID" ] && kill "$MON_PID" 2>/dev/null
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
	td_desc=$1; shift
	if "$@"; then ok "$td_desc"; else fail "$td_desc"; fi
}

wait_for() {
	n=$1; shift
	while [ "$n" -gt 0 ]; do "$@" && return 0; sleep 0.1; n=$((n-1)); done
	return 1
}
pid_gone() { ! kill -0 "$1" 2>/dev/null; }

msg() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" "$@"
}
ipc_ready() { msg -q get_version >/dev/null 2>&1; }

evlog_has() { grep -qF "$1" "$EVLOG" 2>/dev/null; }
evlog_count() { grep -cF "$1" "$EVLOG" 2>/dev/null || true; }
evlog_count_is() { [ "$(evlog_count "$1")" = "$2" ]; }
evlog_line() { grep -n -m1 -F "$1" "$EVLOG" 2>/dev/null | cut -d: -f1; }

wait_tick() {
	wt_mark=$1
	msg send_tick "$wt_mark" >/dev/null 2>&1 || return 1
	wait_for 50 evlog_has "payload = \"$wt_mark\""
}

start_monitor() {
	: > "$EVLOG"
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" -m "$@" tick > "$EVLOG" 2>>"$LOG" &
	MON_PID=$!
	echo "$MON_PID" >> "$PIDFILE"
	wait_for 50 evlog_has "first = YES"
}

stop_monitor() {
	[ -n "$MON_PID" ] && kill "$MON_PID" 2>/dev/null
	wait_for 30 pid_gone "$MON_PID"
	MON_PID=
}

for tool in Xvfb xdpyinfo; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
[ -x "$WDWRITE" ] || skip_all "util/.libs/wdwrite not built"
[ -x "$WTOUCHD" ] || skip_all "wmtouchd not built (configure --with-libinput)"

export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"

cat >"$WMHOME/Defaults/WindowMaker" <<'DOMAIN'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
}
DOMAIN
cat >"$WMHOME/Defaults/WMState" <<'DOMAIN'
{
  Workspaces = (
    { Name = "touch-0"; },
    { Name = "touch-1"; },
    { Name = "touch-2"; }
  );
}
DOMAIN

# Seed the gesture mapping through wdwrite so the daemon reads a real
# user-domain file. ThreeFingerLeft is deliberately a typo and
# TwoFingerLeft is never reported by libinput.
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$WDWRITE" WMTouch ThreeFingerRight NextWorkspace >>"$LOG" 2>&1
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$WDWRITE" WMTouch ThreeFingerDown None >>"$LOG" 2>&1
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$WDWRITE" WMTouch ThreeFingerLeft NoSuchThing >>"$LOG" 2>&1
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$WDWRITE" WMTouch ThreeFingerPinchOut NextWorkspace >>"$LOG" 2>&1
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$WDWRITE" WMTouch ThreeFingerPinchIn PrevWorkspace >>"$LOG" 2>&1
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$WDWRITE" WMTouch TwoFingerLeft PrevWorkspace >>"$LOG" 2>&1
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$WDWRITE" WMTouch FourFingerRight \
	"(NextWorkspace, PrevWorkspace)" >>"$LOG" 2>&1
[ -f "$WMHOME/Defaults/WMTouch" ] || fatal "wdwrite did not create the WMTouch domain"

cat >"$GEST" <<'GESTURES'
# three-finger right: NextWorkspace
begin 3
update 3 20 0
update 3 30 1
update 3 25 2
end 3
# three-finger down: None, must fire nothing
begin 3
update 3 2 80
end 3
# three-finger left: typo'd name, logged once
begin 3
update 3 -90 0
end 3
# cancelled swipe is dropped
begin 3
update 3 200 0
cancel 3
end 3
# two-finger swipe: libinput never reports one, must fire nothing
begin 2
update 2 90 0
end 2
# three-finger spread: NextWorkspace
pinch-begin 3
pinch-update 3 1.2
pinch-update 3 1.8
pinch-end 3
# three-finger pinch: PrevWorkspace
pinch-begin 3
pinch-update 3 0.8
pinch-update 3 0.55
pinch-end 3
# sub-threshold pinch fires nothing
pinch-begin 3
pinch-update 3 1.1
pinch-end 3
GESTURES

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
	DISPLAY=$DPY "$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
wait_for 80 ipc_ready || fatal "wmaker IPC did not come up"
sleep 0.3

note "replay gestures through wmtouchd"
start_monitor workspace || fatal "could not subscribe to workspace events"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$WTOUCHD" -v --config "$WMHOME/Defaults/WMTouch" \
	--source-events "$GEST" --socket "$SOCKET" >>"$DAEMONLOG" 2>&1 &
TOUCHD_PID=$!
echo "$TOUCHD_PID" >> "$PIDFILE"

wait_for 50 evlog_count_is "change = focus" 3 \
	|| fail "expected three workspace events for the right-swipe and both pinches"
check "right-swipe and pinch-out fired NextWorkspace, pinch-in PrevWorkspace" \
	[ "$(evlog_count 'change = focus')" = "3" ]
check "None-mapped swipe and sub-threshold pinch fired no workspace event" \
	[ "$(evlog_count 'change = focus')" = "3" ]
check "typo'd action logged exactly once" \
	[ "$(grep -c "action 'NoSuchThing' failed" "$DAEMONLOG")" = "1" ]
check "two-finger swipe fired nothing" \
	[ "$(grep -c '2-finger' "$DAEMONLOG")" = "0" ]
kill "$TOUCHD_PID" 2>/dev/null
wait_for 30 pid_gone "$TOUCHD_PID"

note "--check validates without executing"
WS_BEFORE=$(evlog_count 'change = focus')
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$WTOUCHD" --check --config "$WMHOME/Defaults/WMTouch" \
	--socket "$SOCKET" >"$CHECKLOG" 2>&1
CHECK_RC=$?
check "--check exits non-zero on a bad name" [ "$CHECK_RC" -ne 0 ]
check "--check reports the bad name" grep -qF "unknown action 'NoSuchThing'" "$CHECKLOG"
check "--check accepts NextWorkspace" grep -qF "OK NextWorkspace" "$CHECKLOG"
check "--check validates both names of a chain" grep -qF "OK PrevWorkspace" "$CHECKLOG"
sleep 0.5
check "--check fired no action" \
	[ "$(evlog_count 'change = focus')" = "$WS_BEFORE" ]
stop_monitor

# ---- chained swipe: one gesture, two actions, in order -------------------

note "chained swipe fires two actions in order"
cat >"$GEST2" <<'GESTURES'
# four-finger right is bound to the (NextWorkspace, PrevWorkspace) chain
begin 4
update 4 20 0
update 4 200 0
end 4
GESTURES
# start the chain from a known workspace: next lands on 1, prev back on 0
msg -q workspace 0 >>"$LOG" 2>&1
sleep 0.3
start_monitor workspace || fatal "could not subscribe to workspace events"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$WTOUCHD" -v --config "$WMHOME/Defaults/WMTouch" \
	--source-events "$GEST2" --socket "$SOCKET" >>"$DAEMONLOG" 2>&1 &
TOUCHD_PID=$!
echo "$TOUCHD_PID" >> "$PIDFILE"
wait_for 50 evlog_count_is 'change = focus' 2 \
	|| fail "expected two workspace events for the chained swipe"
wait_tick "chain-done" || fail "tick fence after the chained swipe never arrived"
WS1_LINE=$(evlog_line 'index = 1')
WS2_LINE=$(evlog_line 'index = 0')
TICK_LINE=$(evlog_line 'payload = "chain-done"')
check "chained swipe fired exactly two workspace switches" \
	[ "$(evlog_count 'change = focus')" = "2" ]
check "chain ran NextWorkspace before PrevWorkspace" \
	sh -c "[ '$WS1_LINE' -gt 0 ] && [ '$WS1_LINE' -lt '$WS2_LINE' ] && [ '$WS2_LINE' -lt '$TICK_LINE' ]"
check "daemon logged the chain" \
	grep -qF '4-finger Right -> NextWorkspace, PrevWorkspace' "$DAEMONLOG"
kill "$TOUCHD_PID" 2>/dev/null
wait_for 30 pid_gone "$TOUCHD_PID"
stop_monitor

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"
kill "$XVFB_PID" 2>/dev/null

note "touch drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))
[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
