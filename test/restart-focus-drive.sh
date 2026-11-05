#!/bin/sh
# Warm-restart focus regression: the focused window must still be the
# focused window after wmaker re-execs, not fall back to the bottom of
# the stack because a stale queued FocusIn was applied, and not lose a
# WM_TAKE_FOCUS race against globally-active clients that answer the
# protocol asynchronously.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${RESTART_FOCUS_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/restart-focus-drive.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/restart-focus-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
GAFocus_CLIENT="$WMHOME/gafocus-client"
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

wait_for() {
	wf_n=$1; shift
	while [ "$wf_n" -gt 0 ]; do
		"$@" && return 0
		sleep 0.1
		wf_n=$((wf_n - 1))
	done
	return 1
}

msg() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" "$@"
}
ipc_ready() { msg -q get_version >/dev/null 2>&1; }
pid_rebind() {
	WMAKER_PID=$(msg get_version 2>/dev/null \
		| sed -n 's/.*pid = \([0-9]*\);.*/\1/p')
	[ -n "$WMAKER_PID" ]
}
active_is() {
	[ "$(DISPLAY=$DPY xdotool getactivewindow 2>/dev/null)" = "$1" ]
}
window_id() {
	DISPLAY=$DPY wmctrl -l 2>/dev/null | awk -v t="$1" '$NF == t { print $1; exit }'
}

for tool in Xvfb xdpyinfo xterm xdotool wmctrl; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"

export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG WMAKER_IPC_SOCKET
export XDG_RUNTIME_DIR="$RUNTIME"
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<'WMCONF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  NoAutoSnap = YES;
}
WMCONF
cat >"$WMHOME/Defaults/WMWindowAttributes" <<'WMATTR'
{
  "*" = { NoAppIcon = Yes; };
}
WMATTR

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use" >&2
	exit 77
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 1024x768x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
wait_for 40 sh -c "kill -0 $XVFB_PID 2>/dev/null && DISPLAY=$DPY xdpyinfo >/dev/null 2>&1" \
	|| fatal "Xvfb failed to start"

note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	ASAN_OPTIONS="$(drive_asan_options)" \
	DISPLAY=$DPY \
	"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
wait_for 80 ipc_ready || fatal "wmaker IPC did not come up"

note "spawn three same-class xterms"
for t in focus-a focus-b focus-c; do
	DISPLAY=$DPY xterm -geometry 40x10+40+40 -title "$t" \
		-e 'sleep 600' >>"$LOG" 2>&1 &
	echo "$!" >> "$PIDFILE"
done
wait_for 40 sh -c "DISPLAY=$DPY wmctrl -l | grep -q ' focus-c\$'" \
	|| fatal "focus-c did not map"
sleep 0.5
FOCUS_C=$(window_id focus-c)
[ -n "$FOCUS_C" ] || fatal "could not find focus-c"
FOCUS_C_DEC=$(printf '%d' "$FOCUS_C")

note "focus the top xterm"
DISPLAY=$DPY wmctrl -i -a "$FOCUS_C" >>"$LOG" 2>&1
wait_for 30 active_is "$FOCUS_C_DEC" || fatal "could not focus focus-c before restart"

note "wmakermsg restart"
msg -q restart >>"$LOG" 2>&1
wait_for 120 ipc_ready || fatal "wmaker IPC did not re-bind after restart"
wait_for 50 pid_rebind || fatal "wmaker pid not reported after restart"
echo "$WMAKER_PID" >> "$PIDFILE"
if wait_for 50 active_is "$FOCUS_C_DEC"; then
	ok "focus stays on the top xterm across a warm restart"
else
	fail "focus left the top xterm after warm restart (active=$(DISPLAY=$DPY xdotool getactivewindow 2>/dev/null))"
fi

note "globally-active clients with staggered take-focus replies"
if ! ${CC:-cc} -std=c99 "$ROOT/test/gafocus-client.c" -o "$GAFocus_CLIENT" -lX11 >>"$LOG" 2>&1; then
	skip_all "could not build test/gafocus-client.c"
else
	GA_TOP=
	ga_spawn() {
		gs_name=$1
		gs_geom=$2
		gs_utime=$3
		gs_delay=$4
		GAFOCUS_NAME=$gs_name GAFOCUS_GEOM=$gs_geom \
		GAFOCUS_USER_TIME=$gs_utime GAFOCUS_REPLY_DELAY=$gs_delay \
		GAFOCUS_ARM_AFTER_MS=4000 \
		DISPLAY=$DPY "$GAFocus_CLIENT" >>"$LOG" 2>&1 &
		ga_pid=$!
		echo "$ga_pid" >> "$PIDFILE"
	}

	ga_spawn ga-bottom "200x120+40+40" 1000 2000
	ga_spawn ga-middle "200x120+120+120" 2000 1000
	ga_spawn ga-top "200x120+200+200" 3000 20

	# let the map-time focus requests pass unanswered before arming
	sleep 5

	wait_for 40 sh -c "DISPLAY=$DPY wmctrl -l | grep -q ' ga-top\$'" \
		|| fatal "ga-top did not map"
	sleep 0.5
	GA_TOP=$(window_id ga-top)
	GA_TOP_DEC=$(printf '%d' "$GA_TOP")
	DISPLAY=$DPY wmctrl -i -a "$GA_TOP" >>"$LOG" 2>&1
	wait_for 30 active_is "$GA_TOP_DEC" || fatal "could not focus ga-top before restart"
	# quiesce the staggered replies of the map-time focus requests, the
	# restart race under test is the one the re-adopt burst creates
	sleep 3

	note "wmakermsg restart with globally-active clients"
	msg -q restart >>"$LOG" 2>&1
	wait_for 120 ipc_ready || fatal "wmaker IPC did not re-bind after restart"
	wait_for 50 pid_rebind || fatal "wmaker pid not reported after restart"
	echo "$WMAKER_PID" >> "$PIDFILE"
	# let any pending protocol reply land before reading the focus back
	sleep 3
	if active_is "$GA_TOP_DEC"; then
		ok "focus stays on the top globally-active window"
	else
		fail "focus left the top globally-active window (active=$(DISPLAY=$DPY xdotool getactivewindow 2>/dev/null))"
	fi
fi

if [ "$FAILED" -eq 0 ]; then
	printf '\n=== summary: %d passed, %d failed ===\n' "$PASSED" "$FAILED"
	echo "=== PASS ==="
	exit 0
fi
printf '\n=== summary: %d passed, %d failed ===\n' "$PASSED" "$FAILED"
echo "=== FAIL ==="
exit 1
