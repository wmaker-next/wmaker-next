#!/bin/sh
# Drive the wmlib wire-format smoke test (test/wtest.c) under a private
# X server. wtest builds a menu tree and checks the _WINDOWMAKER_MENU
# property against the token format src/appmenu.c parses, dispatches a
# synthetic wmSelectItem ClientMessage through WMProcessEvent, and
# round-trips the packed _GNUSTEP_WM_ATTR format-32 property.
#
# No window manager is needed, the test only wants an X connection. It
# tracks the PIDs it spawns so it can reap its private Xvfb; the live
# :0 wmaker is the same binary so no pkill.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${WTEST_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-wtest-test.XXXXXX)
PIDFILE="$RUNTIME/pids"
LOG=/tmp/wtest-drive.log
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
	n=$1; shift
	while [ "$n" -gt 0 ]; do "$@" && return 0; sleep 0.1; n=$((n - 1)); done
	return 1
}

for tool in Xvfb xdpyinfo make; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -f "$ROOT/test/Makefile" ] || skip_all "test/Makefile absent, tree not configured"

note "build wtest"
make -C "$ROOT/test" wtest >>"$LOG" 2>&1 || skip_all "could not build test/wtest"
[ -x "$ROOT/test/wtest" ] || skip_all "test/wtest not built"

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use" >&2; exit 77
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 1024x768x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
wait_for 40 sh -c "kill -0 $XVFB_PID 2>/dev/null && DISPLAY=$DPY xdpyinfo >/dev/null 2>&1" \
	|| fatal "Xvfb failed to start"

OUT="$RUNTIME/wtest.out"
note "run wtest against $DPY"
# under an ASan build wtest always exits with a LeakSanitizer report:
# the WMApp/WMMenu objects it builds are process-lifetime by design and
# wmlib has no teardown API, so leak detection stays off for the run
# itself (ASan and UBSan still check it)
if DISPLAY=$DPY ASAN_OPTIONS="$(drive_asan_options)" \
	"$ROOT/test/wtest" >"$OUT" 2>&1; then
	ok "wtest exited 0"
else
	fail "wtest exited $?"
fi
tee -a "$LOG" <"$OUT"
if grep -q '^wtest: ok$' "$OUT"; then
	ok "wtest reported the wire-format contract"
else
	fail "wtest did not report success"
fi

note "wtest drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
