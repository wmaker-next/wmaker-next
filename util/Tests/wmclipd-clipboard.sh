#!/bin/sh
# Regression test for wmclipd preserving image data copied through PRIMARY.

set -u

top_builddir=${top_builddir:-../..}
builddir=${builddir:-.}

DPY=${WMCLIPD_TEST_DISPLAY:-:95}
RUNTIME=$(mktemp -d /tmp/wmclipd-test.XXXXXX) || exit 99
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

expected_sum()
{
	awk -v n="$1" 'BEGIN {
		s = 0;
		for (i = 0; i < n; i++)
			s = (s + (i % 256)) % 4294967296;
		printf "%.0f\n", s;
	}'
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
		"$WMCLIPD" --clipboard --verbose >>"$LOG" 2>&1 &
	WMCLIPD_PID=$!
	echo "$WMCLIPD_PID" >> "$PIDFILE"
	wait_for 30 log_has "monitoring CLIPBOARD and PRIMARY" ||
		fail "wmclipd did not start"
}

run_case()
{
	mode=$1
	bytes=$2
	sum=$(expected_sum "$bytes")

	start_wmclipd

	DISPLAY=$DPY "$CLIPTEST" "$mode" "$bytes" >>"$LOG" 2>&1 &
	SOURCE_PID=$!
	echo "$SOURCE_PID" >> "$PIDFILE"

	wait_for 50 log_has "mirroring PRIMARY to CLIPBOARD" ||
		fail "$mode: wmclipd did not mirror PRIMARY to CLIPBOARD"
	wait_for 50 log_has "owning CLIPBOARD" ||
		fail "$mode: wmclipd did not own CLIPBOARD"

	stop_pid "$SOURCE_PID"

	out=$(DISPLAY=$DPY "$CLIPTEST" read-clipboard 2>>"$LOG") ||
		fail "$mode: CLIPBOARD read failed"
	set -- $out
	[ "${1:-}" = "$bytes" ] ||
		fail "$mode: expected $bytes bytes from CLIPBOARD, got ${1:-nothing}"
	[ "${2:-}" = "$sum" ] ||
		fail "$mode: expected checksum $sum, got ${2:-nothing}"

	stop_pid "$WMCLIPD_PID"
	printf 'PASS: %s preserved %s image bytes through CLIPBOARD\n' "$mode" "$bytes"
}

for tool in Xvfb xdpyinfo awk grep; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done

[ -x "$WMCLIPD" ] || skip_all "wmclipd not built"
[ -x "$CLIPTEST" ] || skip_all "wmclipd_cliptest not built"

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

run_case serve-primary-direct 21827
run_case serve-primary-incr 131072
