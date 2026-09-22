#!/bin/sh
# Drive the _NET_WM_NAME / WM_NAME title fallback under Xvfb: a
# python-xlib probe client maps with _NET_WM_NAME set, then walks the
# rename / delete / legacy-fallback / re-add matrix over one X
# connection. Every phase is asserted through the IPC get_windows title
# field and wmaker must survive each one.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${TITLE_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/title-drive.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=${TITLE_DRIVE_LOG:-/tmp/title-fallback-drive.log}
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
PROBE="$RUNTIME/probe.py"
FIFO="$RUNTIME/probe.fifo"
XIDFILE="$RUNTIME/probe.xid"

PASSED=0
FAILED=0

: > "$PIDFILE"
: > "$LOG"

cleanup() {
	while read -r p; do
		[ -n "$p" ] && kill "$p" 2>/dev/null
	done < "$PIDFILE"
	rm -rf "$RUNTIME"
}
trap cleanup EXIT

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
fail() { printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
record_pass() { PASSED=$((PASSED + 1)); printf 'PASS: %s\n' "$*" | tee -a "$LOG"; }
record_fail() { FAILED=$((FAILED + 1)); printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; }

check() {
	check_desc=$1
	shift
	if "$@"; then
		record_pass "$check_desc"
	else
		record_fail "$check_desc"
	fi
}

finish() {
	printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
	[ "$FAILED" -eq 0 ]
}

wait_for() {
	_wf=$1; shift
	while [ "$_wf" -gt 0 ]; do
		"$@" && return 0
		sleep 0.1
		_wf=$((_wf - 1))
	done
	return 1
}

command -v Xvfb    >/dev/null 2>&1 || { echo "SKIP: Xvfb not found"; exit 77; }
command -v python3 >/dev/null 2>&1 || { echo "SKIP: python3 not found"; exit 77; }
python3 -c 'import Xlib' >/dev/null 2>&1 || { echo "SKIP: python3-xlib not installed"; exit 77; }
[ -x "$ROOT/src/.libs/wmaker" ] || { echo "SKIP: wmaker not built"; exit 77; }
[ -x "$WMAKERMSG" ] || { echo "SKIP: wmakermsg not built"; exit 77; }

msg() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" "$@"
}

# one command line to the probe client, bounded so a wedged client
# cannot hang the drive
probe_cmd() {
	if ! timeout 10 sh -c "printf '%s\n' \"\$1\" > \"\$2\"" probe_cmd "$1" "$FIFO"; then
		fail "probe client did not accept command: $1"
	fi
}

# win_title XID : the IPC title field of one managed window, quotes
# stripped so bare-identifier and quoted proplist values compare equal
win_entry() {
	msg get_windows 2>/dev/null | sed 's/}, *{/}\n{/g' | grep -F "id = $1;"
}
win_title() {
	win_entry "$1" | sed -n 's/.*title = \([^;]*\);.*/\1/p' | sed 's/^"\(.*\)"$/\1/'
}
win_title_is() {
	[ "$(win_title "$1")" = "$2" ]
}

wmaker_alive() {
	kill -0 "$WMAKER_PID" 2>/dev/null
}

# phase DESC CMD XID EXPECT : run one probe command and poll the IPC
# title until it equals EXPECT
phase() {
	phase_desc=$1
	phase_cmd=$2
	phase_xid=$3
	phase_expect=$4

	if [ -n "$phase_cmd" ]; then
		probe_cmd "$phase_cmd" || return 1
	fi
	if wait_for 40 win_title_is "$phase_xid" "$phase_expect"; then
		record_pass "$phase_desc"
	else
		record_fail "$phase_desc: expected title $phase_expect, got $(win_title "$phase_xid")"
	fi
	check "wmaker alive after $phase_desc" wmaker_alive
}

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
}
EOF
export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY already in use, set TITLE_DRIVE_DISPLAY" >&2
	exit 77
fi

cat > "$PROBE" <<'EOF'
import os
from Xlib import X, Xatom, display

def main():
	fifo = os.environ['TITLE_DRIVE_FIFO']
	d = display.Display()
	s = d.screen()
	net_wm_name = d.intern_atom('_NET_WM_NAME')
	utf8_string = d.intern_atom('UTF8_STRING')
	w = s.root.create_window(10, 10, 320, 200, 0, s.root_depth,
				 X.InputOutput, X.CopyFromParent)
	w.set_wm_class('TitleDrive', 'TitleDrive')
	w.set_wm_name('LegacyPre')
	w.change_property(net_wm_name, utf8_string, 8, b'NetA')
	w.map()
	d.flush()
	print('0x%x' % w.id, flush=True)
	while True:
		with open(fifo) as f:
			for line in f:
				line = line.strip()
				if not line:
					continue
				op, _, val = line.partition(' ')
				if op == 'netwm':
					w.change_property(net_wm_name, utf8_string, 8, val.encode())
				elif op == 'wmname':
					w.change_property(Xatom.WM_NAME, Xatom.STRING, 8, val.encode())
				elif op == 'legacydel':
					w.change_property(Xatom.WM_NAME, Xatom.STRING, 8, val.encode())
					w.delete_property(net_wm_name)
				elif op == 'emptynetwm':
					w.change_property(net_wm_name, utf8_string, 8, b'')
				d.sync()

main()
EOF
export TITLE_DRIVE_FIFO="$FIFO"

note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 1024x768x24 -ac -noreset >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
	kill -0 "$XVFB_PID" 2>/dev/null || fail "Xvfb exited early"
	DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fail "Xvfb failed to start"

note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY \
	"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY XDG_RUNTIME_DIR="$RUNTIME" "$WMAKERMSG" --get-socketpath)
ipc_ready() { msg -q get_version >/dev/null 2>&1; }
wait_for 120 ipc_ready || fail "wmaker did not come up"

note "start probe client"
mkfifo "$FIFO" || fail "mkfifo failed"
DISPLAY=$DPY python3 "$PROBE" >"$XIDFILE" 2>>"$LOG" &
PROBE_PID=$!
echo "$PROBE_PID" >> "$PIDFILE"
probe_xid_ready() {
	grep -q '^0x[0-9a-fA-F]*$' "$XIDFILE"
}
wait_for 50 probe_xid_ready || fail "probe client did not map"
PROBE_XID=$(cat "$XIDFILE")
wait_for 40 win_entry "$PROBE_XID" >/dev/null || fail "probe window never got managed"

note "title fallback matrix"
check "map with _NET_WM_NAME: title is the UTF-8 value" \
	win_title_is "$PROBE_XID" "NetA"
check "wmaker alive after manage" wmaker_alive

phase "post-manage _NET_WM_NAME rename" "netwm NetB" "$PROBE_XID" "NetB"
phase "delete _NET_WM_NAME falls back to WM_NAME" "legacydel LegacyC" "$PROBE_XID" "LegacyC"
phase "WM_NAME update after the delete" "wmname LegacyD" "$PROBE_XID" "LegacyD"
phase "re-added _NET_WM_NAME wins again" "netwm NetE" "$PROBE_XID" "NetE"

probe_cmd "wmname LegacyF"
sleep 1
check "latched again: WM_NAME update loses" win_title_is "$PROBE_XID" "NetE"
check "wmaker alive after the latched update" wmaker_alive

phase "empty _NET_WM_NAME counts as absent" "emptynetwm" "$PROBE_XID" "LegacyF"

note "summary"
finish
