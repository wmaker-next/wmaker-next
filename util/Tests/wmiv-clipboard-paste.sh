#!/bin/sh
# Regression test for wmiv importing image/png from CLIPBOARD with Ctrl+V.

set -u

top_builddir=${top_builddir:-../..}
top_srcdir=${top_srcdir:-../..}
builddir=${builddir:-.}

DPY=${WMIV_TEST_DISPLAY:-:93}
RUNTIME=$(mktemp -d /tmp/wmiv-paste-test.XXXXXX) || exit 99
PIDFILE="$RUNTIME/pids"
LOG="$RUNTIME/wmiv.log"

WMIV="$top_builddir/util/wmiv"
CLIPTEST="$builddir/wmclipd_cliptest"
SOURCE_PNG="$top_srcdir/wrlib/Tests/test.png"
START_PPM="$RUNTIME/start.ppm"
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
	printf '%s\n' '--- wmiv log ---'
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

xvfb_ready()
{
	kill -0 "$XVFB_PID" 2>/dev/null || fail "Xvfb exited early"
	DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}

find_wmiv_window()
{
	# the previous run's wmiv window can outlive its PID briefly, so
	# take the most recently created match rather than the first one
	WMIV_WIN=$(DISPLAY=$DPY xdotool search --class wmiv 2>/dev/null | tail -n 1)
	[ -n "$WMIV_WIN" ]
}

wmiv_window_gone()
{
	[ -z "$WMIV_WIN" ] && return 0
	! DISPLAY=$DPY xdotool search --class wmiv 2>/dev/null | grep -qx "$WMIV_WIN"
}

title_has_paste_path()
{
	DISPLAY=$DPY xprop -id "$WMIV_WIN" WM_NAME 2>>"$LOG" | grep -q 'wmiv_paste_'
}

for tool in Xvfb xdpyinfo xdotool xprop grep head; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done

[ -x "$WMIV" ] || skip_all "wmiv not built"
[ -x "$CLIPTEST" ] || skip_all "wmclipd_cliptest not built"
[ -f "$SOURCE_PNG" ] || skip_all "source PNG missing"

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	skip_all "display $DPY is already in use"
fi

cat > "$START_PPM" <<'PPM'
P3
2 2
255
255 0 0   0 255 0
0 0 255   255 255 255
PPM

Xvfb "$DPY" -screen 0 800x600x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
wait_for 40 xvfb_ready || fail "Xvfb failed to start"

run_case()
{
	mode=$1

	DISPLAY=$DPY "$CLIPTEST" "$mode" "$SOURCE_PNG" >>"$LOG" 2>&1 &
	SOURCE_PID=$!
	echo "$SOURCE_PID" >> "$PIDFILE"
	sleep 0.2

	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$WMIV" "$START_PPM" >>"$LOG" 2>&1 &
	WMIV_PID=$!
	echo "$WMIV_PID" >> "$PIDFILE"

	WMIV_WIN=
	wait_for 60 find_wmiv_window || fail "$mode: wmiv window did not appear"
	sleep 1.2

	# the wmiv window can be found by xdotool before it has selected
	# KeyPressMask, and a key press sent in that gap is silently
	# dropped, so re-send Ctrl+V while polling the title. The keys go
	# through XTEST because wmiv ignores XSendEvent-forged input.
	tries=6
	while [ "$tries" -gt 0 ]; do
		DISPLAY=$DPY xdotool windowfocus --sync "$WMIV_WIN" >>"$LOG" 2>&1 ||
			fail "$mode: failed to focus wmiv"
		DISPLAY=$DPY xdotool key --clearmodifiers ctrl+v >>"$LOG" 2>&1 ||
			fail "$mode: failed to send Ctrl+V"
		wait_for 10 title_has_paste_path && break
		tries=$((tries - 1))
	done
	[ "$tries" -gt 0 ] || fail "$mode: wmiv did not switch to pasted clipboard image"

	kill "$WMIV_PID" 2>/dev/null || true
	wait_for 30 pid_gone "$WMIV_PID" || true
	wait_for 30 wmiv_window_gone || true
	kill "$SOURCE_PID" 2>/dev/null || true
	wait_for 30 pid_gone "$SOURCE_PID" || true
	printf 'PASS: wmiv pasted image/png from CLIPBOARD via %s\n' "$mode"
}

run_case serve-clipboard-file
run_case serve-clipboard-file-incr
