#!/bin/sh
# Regression test for wmclipd --persist saving and restoring history.

set -u

top_builddir=${top_builddir:-../..}
builddir=${builddir:-.}

DPY=${WMCLIPD_PERSIST_DISPLAY:-:97}
RUNTIME=$(mktemp -d /tmp/wmclipd-persist-test.XXXXXX) || exit 99
PIDFILE="$RUNTIME/pids"
LOG="$RUNTIME/wmclipd.log"
HIST="$RUNTIME/hist"

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

log_count()
{
	grep -cF "$1" "$LOG" 2>/dev/null
}

at_least()
{
	[ "$(log_count "$1")" -ge "$2" ]
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
		"$WMCLIPD" --clipboard --history 5 --debug "$@" >>"$LOG" 2>&1 &
	WMCLIPD_PID=$!
	echo "$WMCLIPD_PID" >> "$PIDFILE"
	wait_for 30 log_has "monitoring CLIPBOARD and PRIMARY" ||
		fail "wmclipd did not start"
}

serve_primary()
{
	DISPLAY=$DPY "$CLIPTEST" serve-primary-direct "$1" >>"$LOG" 2>&1 &
	SOURCE_PID=$!
	echo "$SOURCE_PID" >> "$PIDFILE"
}

for tool in Xvfb xdpyinfo grep stat head; do
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

# Two distinct captures, then a clean exit must write the state file.
start_wmclipd --persist --history-file "$HIST"

serve_primary 2048
wait_for 50 log_has "history: added entry" ||
	fail "first capture was not recorded in history"
stop_pid "$SOURCE_PID"

serve_primary 5120
wait_for 50 at_least "history: added entry" 2 ||
	fail "second capture was not recorded in history"
stop_pid "$SOURCE_PID"

stop_pid "$WMCLIPD_PID"
wait_for 30 log_has "history: saved 2 entries to $HIST" ||
	fail "daemon did not save the history on exit"
[ -f "$HIST" ] || fail "state file was not created"
[ "$(head -c 8 "$HIST")" = "WMCLPHST" ] ||
	fail "state file does not start with the WMCLPHST magic"
perms=$(stat -c %a "$HIST" 2>/dev/null || stat -f %Lp "$HIST" 2>/dev/null)
if [ -z "$perms" ]; then
	skip_all "stat cannot report file modes"
fi
[ "$perms" = "600" ] ||
	fail "state file is not mode 0600"
printf 'PASS: clean exit saved 2 entries to the state file\n'

# A restart must restore both entries.
start_wmclipd --persist --history-file "$HIST"
wait_for 30 log_has "history: loaded 2 entries from $HIST" ||
	fail "daemon did not restore the history"
stop_pid "$WMCLIPD_PID"
printf 'PASS: restart restored 2 entries\n'

# Non-pinned entries past the TTL are skipped on load.
HIST_TTL="$RUNTIME/hist-ttl"
start_wmclipd --persist --ttl 5s --history-file "$HIST_TTL"
serve_primary 2048
wait_for 50 log_has "history: added entry" ||
	fail "ttl capture was not recorded in history"
stop_pid "$SOURCE_PID"
stop_pid "$WMCLIPD_PID"
wait_for 30 log_has "history: saved 1 entry to $HIST_TTL" ||
	fail "ttl history was not saved"

sleep 6

start_wmclipd --persist --ttl 5s --history-file "$HIST_TTL"
wait_for 30 log_has "history: dropped expired entry" ||
	fail "expired entry was not skipped on load"
log_has "history: loaded 0 entries from $HIST_TTL" ||
	fail "expired history was restored"
stop_pid "$WMCLIPD_PID"
printf 'PASS: entries past the TTL are skipped on load\n'

# A garbage file must not crash the daemon.
HIST_BAD="$RUNTIME/hist-bad"
printf 'garbage, not a state file' > "$HIST_BAD"
cp "$HIST_BAD" "$HIST_BAD.expected"
start_wmclipd --persist --history-file "$HIST_BAD"
wait_for 30 log_has "history: $HIST_BAD has no valid header" ||
	fail "corrupt state file was not reported"
sleep 1
kill -0 "$WMCLIPD_PID" 2>/dev/null ||
	fail "daemon crashed on a corrupt state file"
stop_pid "$WMCLIPD_PID"
cmp "$HIST_BAD" "$HIST_BAD.expected" ||
	fail "corrupt state file was overwritten"
printf 'PASS: garbage state file is reported and preserved\n'

# A truncated file is rejected and preserved.
HIST_TRUNC="$RUNTIME/hist-trunc"
head -c 24 "$HIST" > "$HIST_TRUNC"
cp "$HIST_TRUNC" "$HIST_TRUNC.expected"
start_wmclipd --persist --history-file "$HIST_TRUNC"
wait_for 30 log_has "history: $HIST_TRUNC is corrupt or truncated after 0 entries" ||
	fail "truncated state file was not detected"
stop_pid "$WMCLIPD_PID"
cmp "$HIST_TRUNC" "$HIST_TRUNC.expected" ||
	fail "truncated state file was overwritten"
printf 'PASS: truncated state file is rejected and preserved\n'

# Without --persist no state file or directory is created.
NOSTATE_HOME="$RUNTIME/fakehome"
NOSTATE_STATE="$RUNTIME/nostate"
mkdir -p "$NOSTATE_HOME"
: > "$LOG"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
DISPLAY=$DPY HOME="$NOSTATE_HOME" XDG_STATE_HOME="$NOSTATE_STATE" \
	"$WMCLIPD" --clipboard --history 5 --debug >>"$LOG" 2>&1 &
WMCLIPD_PID=$!
echo "$WMCLIPD_PID" >> "$PIDFILE"
wait_for 30 log_has "monitoring CLIPBOARD and PRIMARY" ||
	fail "wmclipd did not start without --persist"
stop_pid "$WMCLIPD_PID"
[ ! -e "$NOSTATE_STATE" ] ||
	fail "default run created state under XDG_STATE_HOME"
[ ! -e "$NOSTATE_HOME/.local/state" ] ||
	fail "default run created state under HOME"
printf 'PASS: default run without --persist creates no state file\n'

# An X disconnect must save the history too: the dead hook unwinds the
# main loop through the graceful shutdown instead of dying unsaved.
HIST_DISC="$RUNTIME/hist-disc"
start_wmclipd --persist --history-file "$HIST_DISC"
serve_primary 2048
wait_for 50 log_has "history: added entry" ||
	fail "disconnect capture was not recorded"
stop_pid "$SOURCE_PID"
kill "$XVFB_PID"
wait_for 30 log_has "history: saved 1 entry to $HIST_DISC" ||
	fail "X disconnect did not save the history"
wait_for 30 pid_gone "$WMCLIPD_PID" ||
	fail "daemon did not exit after the X disconnect"
printf 'PASS: X disconnect saved the history\n'
