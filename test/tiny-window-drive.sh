#!/bin/sh
# Drive management of placeholder-size top-levels. test/tinywin-client.c
# maps a 1x1 window and then resizes it to its real geometry, the order
# qiv and similar clients use. A MapRequest for such a window used to be
# discarded, leaving the client unmanaged and unmapped forever, so the
# assertions are that the window is managed at its final size and is
# viewable.
#
# Runs against an isolated WMAKER_USER_ROOT and tracks every PID it
# spawns, the live :0 wmaker is the same binary so no pkill.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${TINY_WINDOW_DRIVE_DISPLAY:-:115}
RUNTIME=$(mktemp -d /tmp/wmaker-tinywin-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/tiny-window-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
SOCKET=
CLIENT="$RUNTIME/tinywin-client"
XIDFILE="$RUNTIME/client.xid"
GEOM="200x150+540+325"
FINAL_W=200
FINAL_H=150
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
	tw_desc=$1; shift
	if "$@"; then ok "$tw_desc"; else fail "$tw_desc"; fi
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

xinfo() { DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null; }
win_width()    { xinfo "$1" | sed -n 's/^  Width: //p'; }
win_height()   { xinfo "$1" | sed -n 's/^  Height: //p'; }
win_width_is()  { [ "$(win_width "$1")" = "$2" ]; }
win_height_is() { [ "$(win_height "$1")" = "$2" ]; }
win_viewable() { xinfo "$1" | grep -q 'Map State: IsViewable'; }
win_managed()  { msg get_windows 2>/dev/null | grep -q "id = $1;"; }
win_framed() {
	DISPLAY=$DPY xprop -id "$1" _NET_FRAME_EXTENTS 2>/dev/null | grep -q '='
}
win_xid_ready() { grep -q '^0x[0-9a-fA-F]*$' "$XIDFILE"; }

for tool in Xvfb xdpyinfo xwininfo xprop; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
command -v "${CC:-cc}" >/dev/null 2>&1 || skip_all "no C compiler"
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "wmaker not built"
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

${CC:-cc} -std=gnu11 "$ROOT/test/tinywin-client.c" -o "$CLIENT" -lX11 >>"$LOG" 2>&1 \
	|| skip_all "could not build test/tinywin-client.c (needs libX11 headers)"

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
	DISPLAY=$DPY \
	"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
wait_for 80 ipc_ready || fatal "wmaker IPC did not come up"
sleep 0.5

note "start placeholder-size client"
DISPLAY=$DPY TINYWIN_GEOM="$GEOM" "$CLIENT" >"$XIDFILE" 2>>"$LOG" &
CLIENT_PID=$!
echo "$CLIENT_PID" >> "$PIDFILE"
wait_for 50 win_xid_ready || fatal "placeholder client did not print its window id"
WID=$(cat "$XIDFILE")

# The window is mapped at 1x1 and resized right after. It must be
# managed once the resize has been applied.
check "MapRequest for the 1x1 placeholder is managed" \
	wait_for 60 win_managed "$WID"
check "managed window is reparented into a frame" win_framed "$WID"

check "resize to the real width took effect" \
	wait_for 40 win_width_is "$WID" "$FINAL_W"
check "resize to the real height took effect" \
	wait_for 40 win_height_is "$WID" "$FINAL_H"
check "managed placeholder is viewable" win_viewable "$WID"
check "wmaker alive after managing the placeholder" ipc_ready

note "tiny-window drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
