#!/bin/sh
# Regression test for the plain xcb clipboard clients: wxcopy stores
# cut buffers (creation, slot select, rotate direction, NUL bytes) and
# wxpaste reads them back, including the STRING-only selection path
# served live by wmclipd_cliptest's serve-string mode.

set -u

top_builddir=${top_builddir:-../..}
builddir=${builddir:-.}

DPY=${WXCOPY_TEST_DISPLAY:-:94}
RUNTIME=$(mktemp -d /tmp/wxcopy-test.XXXXXX) || exit 99
PIDFILE="$RUNTIME/pids"
LOG="$RUNTIME/wxpaste.log"

WXCOPY="$top_builddir/util/wxcopy"
WXPASTE="$top_builddir/util/wxpaste"
CLIPTEST="$builddir/wmclipd_cliptest"

: > "$PIDFILE"
: > "$LOG"

cleanup()
{
	while read -r pid; do
		[ -n "$pid" ] && kill "$pid" 2>/dev/null
	done < "$PIDFILE"
	rm -rf "$RUNTIME"
}

trap cleanup EXIT

skip_all()
{
	printf 'SKIP: %s\n' "$*"
	exit 77
}

fail()
{
	printf 'FAIL: %s\n' "$*"
	printf '%s\n' '--- log ---'
	cat "$LOG"
	exit 1
}

wait_for()
{
	wf_count=$1
	shift
	while [ "$wf_count" -gt 0 ]; do
		"$@" && return 0
		sleep 0.1
		wf_count=$((wf_count - 1))
	done
	return 1
}

pid_gone()
{
	! kill -0 "$1" 2>/dev/null
}

stop_pid()
{
	spid=$1
	kill "$spid" 2>/dev/null || return 0
	wait_for 30 pid_gone "$spid" && return 0
	kill -9 "$spid" 2>/dev/null || true
	wait_for 10 pid_gone "$spid"
}

for tool in Xvfb xdpyinfo; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done

[ -x "$WXCOPY" ] || skip_all "wxcopy not built"
[ -x "$WXPASTE" ] || skip_all "wxpaste not built"
[ -x "$CLIPTEST" ] || skip_all "wmclipd_cliptest not built"

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	skip_all "display $DPY is already in use"
fi

Xvfb "$DPY" -screen 0 800x600x24 -ac -noreset >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"

xvfb_ready()
{
	kill -0 "$XVFB_PID" 2>/dev/null || fail "Xvfb exited early"
	DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}

wait_for 40 xvfb_ready || fail "Xvfb failed to start"
export DISPLAY=$DPY

# wxcopy's exit contract: 0 on a successful copy even when the display
# open leaves errno dirty, 1 when nothing was read (nothing stored), 1
# on a bad display. wxpaste's 0-on-success is asserted throughout.

# no-owner selection fallback on a virgin server: empty cut buffers and
# nobody owning PRIMARY must leave wxpaste with nothing to serve
out=$("$WXPASTE" --selection PRIMARY 2>>"$LOG")
[ -z "$out" ] || fail "wxpaste returned data with no owner and no cut buffer"

# basic round trip through cut buffer 0
printf 'hello xcb' | "$WXCOPY" >>"$LOG" 2>&1
[ $? -eq 0 ] || fail "wxcopy basic copy exit code"
out=$("$WXPASTE" 2>>"$LOG") || fail "wxpaste basic read failed"
[ "$out" = "hello xcb" ] || fail "wxpaste basic round trip"

# explicit slot
printf 'slot3' | "$WXCOPY" --cutbuffer 3 >>"$LOG" 2>&1
[ $? -eq 0 ] || fail "wxcopy slot copy exit code"
out=$("$WXPASTE" --cutbuffer 3 2>>"$LOG") || fail "wxpaste slot 3 read failed"
[ "$out" = "slot3" ] || fail "wxpaste slot 3 round trip"

# rotate direction: wxcopy with no --cutbuffer rotates FIRST (the old
# slot 0 value moves to slot 1) and then stores the fresh data in slot 0
printf 'z' | "$WXCOPY" --cutbuffer 0 >>"$LOG" 2>&1
[ $? -eq 0 ] || fail "wxcopy slot-0 seed exit code"
printf 'fresh' | "$WXCOPY" >>"$LOG" 2>&1
[ $? -eq 0 ] || fail "wxcopy rotate copy exit code"
out=$("$WXPASTE" --cutbuffer 0 2>>"$LOG") || fail "wxpaste rotate slot 0 read failed"
[ "$out" = "fresh" ] || fail "rotate did not keep fresh data in slot 0"
out=$("$WXPASTE" --cutbuffer 1 2>>"$LOG") || fail "wxpaste rotate slot 1 read failed"
[ "$out" = "z" ] || fail "rotate did not move old slot 0 to slot 1"

# NUL bytes survive the file-in/file-out round trip
printf 'a\0b' > "$RUNTIME/bin.dat"
"$WXCOPY" "$RUNTIME/bin.dat" >>"$LOG" 2>&1
[ $? -eq 0 ] || fail "wxcopy file copy exit code"
"$WXPASTE" > "$RUNTIME/out.bin" 2>>"$LOG" || fail "wxpaste file read failed"
cmp -s "$RUNTIME/bin.dat" "$RUNTIME/out.bin" || fail "NUL byte round trip"

# --clear-selection smoke: must not abort (nothing is read from
# /dev/null, so the nothing-stored exit 1 is the documented contract)
"$WXCOPY" --clear-selection </dev/null >>"$LOG" 2>&1
[ $? -eq 1 ] || fail "wxcopy --clear-selection exit code"

# empty input stores nothing and exits 1
printf '' | "$WXCOPY" >>"$LOG" 2>&1
[ $? -eq 1 ] || fail "wxcopy empty-input exit code"

# live STRING owner through the in-tree test client: wxpaste --selection
# must complete the convert/read round trip
"$CLIPTEST" serve-string "wxpaste-string-payload" >>"$LOG" 2>&1 &
OWNER_PID=$!
echo "$OWNER_PID" >> "$PIDFILE"

log_has()
{
	grep -qF "$1" "$LOG" 2>/dev/null
}
wait_for 30 log_has "owning PRIMARY, serving STRING" ||
	fail "cliptest did not own PRIMARY"
out=$("$WXPASTE" --selection PRIMARY 2>>"$LOG") || fail "wxpaste selection read failed"
stop_pid "$OWNER_PID"
[ "$out" = "wxpaste-string-payload" ] || fail "wxpaste --selection PRIMARY"

# bad display name: exit 1 with a message
"$WXCOPY" -display "nosuchhost:77" </dev/null >>"$LOG" 2>&1
"$WXCOPY" -display "nosuchhost:77" </dev/null 2>"$RUNTIME/err1"
[ $? -eq 1 ] || fail "wxcopy bad display exit code"
grep -q 'could not open display' "$RUNTIME/err1" || fail "wxcopy bad display message"
"$WXPASTE" -display "nosuchhost:77" 2>"$RUNTIME/err2"
[ $? -eq 1 ] || fail "wxpaste bad display exit code"
grep -q 'could not open display' "$RUNTIME/err2" || fail "wxpaste bad display message"

printf 'PASS: wxcopy/wxpaste cut buffers, rotate, NUL and STRING selection round trips\n'
