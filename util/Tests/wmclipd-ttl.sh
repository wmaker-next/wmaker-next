#!/bin/sh
# Regression test for wmclipd --ttl expiring history entries.

set -u

top_builddir=${top_builddir:-../..}
builddir=${builddir:-.}

DPY=${WMCLIPD_TEST_DISPLAY:-:96}
RUNTIME=$(mktemp -d /tmp/wmclipd-ttl-test.XXXXXX) || exit 99
PIDFILE="$RUNTIME/pids"
LOG="$RUNTIME/wmclipd.log"

WMCLIPD="$top_builddir/util/wmclipd"
CLIPTEST="$builddir/wmclipd_cliptest"
WMLIBS="$top_builddir/WINGs/.libs:$top_builddir/wrlib/.libs"

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
	printf '%s\n' '--- wmclipd log ---'
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

log_has()
{
	grep -qF "$1" "$LOG" 2>/dev/null
}

stop_pid()
{
	spid=$1
	kill "$spid" 2>/dev/null || return 0
	wait_for 30 pid_gone "$spid" && return 0
	kill -9 "$spid" 2>/dev/null || true
	wait_for 10 pid_gone "$spid"
}

start_wmclipd()
{
	: > "$LOG"
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY \
		"$WMCLIPD" --clipboard --history 5 --ttl 5s --debug >>"$LOG" 2>&1 &
	WMCLIPD_PID=$!
	echo "$WMCLIPD_PID" >> "$PIDFILE"
	wait_for 30 log_has "monitoring CLIPBOARD and PRIMARY" ||
		fail "wmclipd did not start"
}

for tool in Xvfb xdpyinfo awk grep; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done

[ -x "$WMCLIPD" ] || skip_all "wmclipd not built"
[ -x "$CLIPTEST" ] || skip_all "wmclipd_cliptest not built"

# A valid duration with no display fails at runtime (exit 1), a
# malformed one at parse time (exit 2). DISPLAY is removed so the
# daemon cannot attach to the caller's display and hang.
check_arg()
{
	ttl=$1
	want_rc=$2
	env -u DISPLAY LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMCLIPD" --clipboard --ttl "$ttl" >/dev/null 2>&1
	rc=$?
	[ "$rc" -eq "$want_rc" ] ||
		fail "--ttl $ttl: expected exit $want_rc, got $rc"
}

check_arg 5x 2
check_arg -3s 2
check_arg 1h30 2
check_arg 1y 2
check_arg 366d 2
check_arg 4s 2
check_arg 3 2
check_arg 0 1
check_arg 5s 1
check_arg 5 1
check_arg 30s 1
check_arg 10m 1
check_arg 2h 1
check_arg 25h 1
check_arg 1d 1
check_arg 1h30m 1
check_arg 365d 1
printf 'PASS: --ttl argument validation\n'

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	skip_all "display $DPY is already in use"
fi

Xvfb "$DPY" -screen 0 800x600x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"

xvfb_ready()
{
	kill -0 "$XVFB_PID" 2>/dev/null || fail "Xvfb exited early"
	DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}

wait_for 40 xvfb_ready || fail "Xvfb failed to start"

start_wmclipd

DISPLAY=$DPY "$CLIPTEST" serve-primary-direct 4096 >>"$LOG" 2>&1 &
SOURCE_PID=$!
echo "$SOURCE_PID" >> "$PIDFILE"

wait_for 50 log_has "history: added entry" ||
	fail "wmclipd did not record the capture in history"
wait_for 50 log_has "owning CLIPBOARD" ||
	fail "wmclipd did not own CLIPBOARD"

# The entry must outlive a short wait, then expire around the 5 s TTL.
sleep 2
log_has "history: expired" &&
	fail "entry expired before the TTL elapsed"

wait_for 40 log_has "history: expired 1 entry (ttl 5s)" ||
	fail "wmclipd did not expire the entry after the TTL"

# Expiry is history-only: the live CLIPBOARD payload must survive.
out=$(DISPLAY=$DPY "$CLIPTEST" read-clipboard 2>>"$LOG") ||
	fail "CLIPBOARD read failed after expiry"
set -- $out
[ "${1:-}" = "4096" ] ||
	fail "expected 4096 bytes from CLIPBOARD after expiry, got ${1:-nothing}"

stop_pid "$SOURCE_PID"
stop_pid "$WMCLIPD_PID"
printf 'PASS: wmclipd expired the history entry after --ttl 5s\n'
