#!/bin/sh
# Drive the play-sound IPC verb and the sound event stream.
# Asserts that play-sound emits a sound event frame on the IPC stream
# and that a real (gated) window-manager action also emits one.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${SOUND_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-sound-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=${SOUND_DRIVE_LOG:-$(mktemp /tmp/sound-drive.XXXXXX)}
EVLOG="$RUNTIME/events"
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
SOCKET=
MON_PID=

: > "$PIDFILE"
: > "$LOG"
: > "$EVLOG"

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
	sd_desc=$1; shift
	if "$@"; then ok "$sd_desc"; else fail "$sd_desc"; fi
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

evlog_has() {
	grep -qF "$1" "$EVLOG" 2>/dev/null
}

# start_monitor EVENT... : subscribe (always with tick for the first=YES
# fence), truncate the event log, and block until the post-subscribe
# tick lands so later emits cannot be missed
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

export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
# DisableSound = NO so the gated wSoundPlay sites emit.
cat >"$WMHOME/Defaults/WindowMaker" <<'DOMAIN'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  DisableSound = NO;
}
DOMAIN

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
sleep 0.3

note "play-sound verb emits a sound event"
start_monitor sound || fatal "could not subscribe to sound events"
msg play-sound shade >>"$LOG" 2>&1
check "play-sound shade exits 0" [ $? -eq 0 ]
wait_for 50 evlog_has "event = shade" \
	|| fail "no sound event frame for play-sound shade"
check "monitor received sound event 'shade'" evlog_has "event = shade"
stop_monitor

note "gated emit on workspace switch"
# wmaker starts with a single workspace, make a second one to switch to
msg -q workspace-new 1 >>"$LOG" 2>&1
check "workspace-new 1 exits 0" [ $? -eq 0 ]
start_monitor sound || fatal "could not re-subscribe to sound events"
msg -q workspace 1 >>"$LOG" 2>&1
check "workspace 1 exits 0" [ $? -eq 0 ]
wait_for 50 evlog_has "event = workspace" \
	|| fail "no sound event frame on workspace switch"
check "monitor received sound event 'workspace'" evlog_has "event = workspace"
stop_monitor

# return to workspace 0 for a clean state
msg -q workspace 0 >>"$LOG" 2>&1

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"
kill "$XVFB_PID" 2>/dev/null

note "sound drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))
[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
