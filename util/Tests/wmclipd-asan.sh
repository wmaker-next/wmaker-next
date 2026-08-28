#!/bin/sh
# Drive wmclipd end to end under a sanitizer build: launch the daemon,
# copy text and image entries through PRIMARY (with an INCR receive and
# an INCR paste-back), restore the persisted ring, expire it through
# --ttl and exit normally via SIGTERM, SIGINT and an X disconnect.
# Every clean exit runs history_save() with the LeakSanitizer audit
# active, so a leak or an overread surfaces as a test failure.

set -u

top_builddir=${top_builddir:-../..}
builddir=${builddir:-.}

DPY=${WMCLIPD_ASAN_DISPLAY:-:98}
RUNTIME=$(mktemp -d /tmp/wmclipd-asan-test.XXXXXX) || exit 99
PIDFILE="$RUNTIME/pids"
LOG="$RUNTIME/wmclipd.log"
SERVE_LOG="$RUNTIME/serve.log"
HIST="$RUNTIME/hist"

WMCLIPD="$top_builddir/util/wmclipd"
CLIPTEST="$builddir/wmclipd_cliptest"
WMLIBS="$top_builddir/WINGs/.libs:$top_builddir/wrlib/.libs"

: > "$PIDFILE"
: > "$LOG"
: > "$SERVE_LOG"

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

# fail on any sanitizer report in the daemon log
sanitizer_clean()
{
	if grep -E 'ERROR: (Address|Leak)Sanitizer|runtime error:' "$LOG" >/dev/null 2>&1; then
		return 1
	fi
	return 0
}

start_wmclipd()
{
	: > "$LOG"
	ASAN_OPTIONS="detect_leaks=1:abort_on_error=0:halt_on_error=0" \
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY XDG_RUNTIME_DIR="$RUNTIME" \
		"$WMCLIPD" --clipboard --debug "$@" >>"$LOG" 2>&1 &
	WMCLIPD_PID=$!
	echo "$WMCLIPD_PID" >> "$PIDFILE"
	wait_for 40 log_has "monitoring CLIPBOARD and PRIMARY" ||
		fail "wmclipd did not start"
}

stop_wmclipd()
{
	kill "$WMCLIPD_PID" 2>/dev/null
	wait_for 60 pid_gone "$WMCLIPD_PID" ||
		fail "wmclipd ignored the exit signal"
}

# a background cliptest owns PRIMARY and serves the entry; the log is
# truncated per copy so the wait markers cannot match a previous copy
copy_entry()
{
	: > "$LOG"
	DISPLAY=$DPY "$CLIPTEST" "$@" >>"$SERVE_LOG" 2>&1 &
	SOURCE_PID=$!
	echo "$SOURCE_PID" >> "$PIDFILE"
}

stop_source()
{
	kill "$SOURCE_PID" 2>/dev/null
	wait_for 40 pid_gone "$SOURCE_PID" ||
		fail "the serve client did not exit"
}

# only the first image of a session is mirrored to the empty CLIPBOARD;
# later ones are history-only because wmclipd never clobbers a clipboard
# it already owns
copy_image()
{
	mode=$1 bytes=$2 expect_mirror=$3
	sum=$(awk -v n="$bytes" 'BEGIN {
		s = 0;
		for (i = 0; i < n; i++)
			s = (s + (i % 256)) % 4294967296;
		printf "%.0f\n", s;
	}')
	copy_entry "$mode" "$bytes"
	wait_for 100 log_has "history: added entry" ||
		fail "$mode: no history entry captured"
	if [ "$expect_mirror" -eq 1 ]; then
		wait_for 100 log_has "mirroring PRIMARY to CLIPBOARD" ||
			fail "$mode: PRIMARY image not mirrored to CLIPBOARD"
		wait_for 100 log_has "owning CLIPBOARD" ||
			fail "$mode: CLIPBOARD ownership not taken"
		out=$(DISPLAY=$DPY "$CLIPTEST" read-clipboard 2>>"$SERVE_LOG") ||
			fail "$mode: read-clipboard failed"
		set -- $out
		[ "${1:-}" = "$bytes" ] ||
			fail "$mode: expected $bytes bytes from CLIPBOARD, got ${1:-nothing}"
		[ "${2:-}" = "$sum" ] ||
			fail "$mode: expected checksum $sum, got ${2:-nothing}"
	else
		wait_for 100 log_has "captured PRIMARY: $bytes bytes" ||
			fail "$mode: image capture not seen"
	fi
	stop_source
}

copy_text()
{
	copy_entry serve-string "$1"
	wait_for 50 log_has "history: added entry" ||
		fail "no history entry captured for '$1'"
	wait_for 50 log_has "cached PRIMARY without owning it" ||
		fail "PRIMARY text was not cached"
	stop_source
}

for tool in Xvfb xdpyinfo grep awk; do
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

# Copy five entries (INCR image, direct image, three strings), check
# that the first image still pastes back through INCR after the whole
# session, then exit normally and require a clean sanitizer report.
start_wmclipd --history 5 --persist --history-file "$HIST"

copy_image serve-primary-incr 131072 1
copy_image serve-primary-direct 21827 0
copy_text "asan-entry-one"
copy_text "asan-entry-two with spaces & special"
copy_text "asan-entry-three"
wait_for 20 log_has "history: added entry (5 total)" ||
	fail "expected 5 history entries, saw: $(grep -oE 'added entry \([0-9]+ total\)' "$LOG" | tail -1)"

out=$(DISPLAY=$DPY "$CLIPTEST" read-clipboard 2>>"$SERVE_LOG") ||
	fail "final read-clipboard failed"
set -- $out
[ "${1:-}" = "131072" ] ||
	fail "final CLIPBOARD read: expected 131072 bytes, got ${1:-nothing}"

stop_wmclipd
wait_for 20 log_has "history: saved 5 entries to $HIST" ||
	fail "history was not saved on the clean exit"
log_has "stopping" || fail "no clean-exit marker in the log"
[ -s "$HIST" ] || fail "history file is missing or empty"
sanitizer_clean || fail "sanitizer reported a problem on the clean exit"
printf 'PASS: copied 5 entries and exited cleanly, no sanitizer reports\n'

# A restart must restore the whole ring, expire it through --ttl and
# still leave through SIGINT cleanly.
start_wmclipd --history 5 --persist --history-file "$HIST" --ttl 5s
wait_for 40 log_has "history: loaded 5 entries from $HIST" ||
	fail "did not restore 5 entries from the history file"
sleep 6
wait_for 20 log_has "history: expired" || fail "TTL expiry did not fire"
kill -INT "$WMCLIPD_PID" 2>/dev/null
wait_for 60 pid_gone "$WMCLIPD_PID" || fail "wmclipd ignored SIGINT"
log_has "history: saved 0 entries to $HIST" ||
	fail "the expired ring was not saved as empty"
sanitizer_clean || fail "sanitizer reported a problem on SIGINT"
printf 'PASS: restored ring expired through --ttl and exited on SIGINT\n'

# X server death is a clean exit too.
start_wmclipd --history 5 --persist --history-file "$HIST"
kill "$XVFB_PID"
wait_for 60 pid_gone "$WMCLIPD_PID" ||
	fail "wmclipd hung after the X server died"
sanitizer_clean || fail "sanitizer reported a problem on the X disconnect"
printf 'PASS: survived the X server death\n'

exit 0
