#!/bin/sh
# Exercise the Window Maker IPC socket end to end under Xvfb.
#
# Harness conventions:
#   - private XDG_RUNTIME_DIR per run so the test wmaker's socket cannot
#     collide with the live :0 desktop
#   - readiness gate is a bounded `wmakermsg -q get_version` poll, not sleep
#   - event capture goes to a logfile, fenced by `send_tick`, ordering
#     asserted by line number
#   - background processes killed by recorded PID only
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${IPC_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-ipc-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=${IPC_DRIVE_LOG:-/tmp/ipc-drive.log}
EVLOG="$RUNTIME/events"
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
SOCKET=

PASSED=0
FAILED=0
SKIPPED=0
MON_PID=

: > "$PIDFILE"
: > "$LOG"

cleanup() {
	[ -n "$MON_PID" ] && kill "$MON_PID" 2>/dev/null
	while read -r p; do
		[ -n "$p" ] && kill "$p" 2>/dev/null
	done < "$PIDFILE"
	rm -rf "$RUNTIME"
}
trap cleanup EXIT

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
fatal() { printf 'FATAL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
skip_all() { printf 'SKIP: %s\n' "$*" | tee -a "$LOG"; exit 77; }

record_pass() { PASSED=$((PASSED + 1)); printf 'PASS: %s\n' "$*" | tee -a "$LOG"; }
record_fail() { FAILED=$((FAILED + 1)); printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; }
record_skip() { SKIPPED=$((SKIPPED + 1)); printf 'SKIP: %s\n' "$*" | tee -a "$LOG"; }

check() {
	check_desc=$1
	shift
	if "$@"; then
		record_pass "$check_desc"
	else
		record_fail "$check_desc"
	fi
}

wait_for() {
	wf_n=$1
	shift
	while [ "$wf_n" -gt 0 ]; do
		"$@" && return 0
		sleep 0.1
		wf_n=$((wf_n - 1))
	done
	return 1
}

pid_gone() { ! kill -0 "$1" 2>/dev/null; }

msg() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" "$@"
}

ipc_ready() {
	msg -q get_version >/dev/null 2>&1
}

config_defines() {
	[ -f "$ROOT/config.h" ] || return 1
	grep -E "^#define[ \t]+$1([ \t]+|$)" "$ROOT/config.h" >/dev/null
}

# evlog_line PATTERN : print 1-based line number of first match in $EVLOG
evlog_line() {
	grep -n -m1 -F "$1" "$EVLOG" 2>/dev/null | cut -d: -f1
}

evlog_has() {
	grep -qF "$1" "$EVLOG" 2>/dev/null
}

# wait_tick MARK : send_tick and poll until the tick event with that
# payload lands in $EVLOG, this is the race-free fence
wait_tick() {
	wt_mark=$1
	msg send_tick "$wt_mark" >/dev/null 2>&1 || return 1
	wait_for 50 evlog_has "payload = \"$wt_mark\""
}

start_monitor() {
	: > "$EVLOG"
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" -m "$@" > "$EVLOG" 2>>"$LOG" &
	MON_PID=$!
	echo "$MON_PID" >> "$PIDFILE"
	# monitor mode prints events only, the SUBSCRIBE reply is
	# swallowed, gate on the first=YES tick that follows it
	wait_for 50 evlog_has "first = YES"
}

stop_monitor() {
	[ -n "$MON_PID" ] && kill "$MON_PID" 2>/dev/null
	wait_for 30 pid_gone "$MON_PID"
	MON_PID=
}

start_wmaker() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY \
	XDG_RUNTIME_DIR="$RUNTIME" \
	WMAKER_USER_ROOT="$WMHOME" \
	"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
	WMAKER_PID=$!
	echo "$WMAKER_PID" >> "$PIDFILE"
	wait_for 80 ipc_ready
}

finish() {
	finish_total=$((PASSED + FAILED))
	note "IPC drive summary"
	printf 'PASS: %d, FAIL: %d, SKIP: %d\n' "$PASSED" "$FAILED" "$SKIPPED" \
		| tee -a "$LOG"
	[ "$FAILED" -eq 0 ] && [ "$finish_total" -gt 0 ]
}

# ----------------------------------------------------------------------

for tool in Xvfb xdpyinfo; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
# a Defaults/WindowMaker file must exist or check_defaults() runs the
# first-run installer, which overwrites WMState with the stock template
# and discards the Workspaces and Dock entries seeded below
DPOFF_WITNESS="$RUNTIME/display-off-ran"
DPON_WITNESS="$RUNTIME/display-on-ran"
cat > "$WMHOME/Defaults/WindowMaker" <<WMEOD
{
	DisplayOffCommand = "touch '$DPOFF_WITNESS'";
	DisplayOnCommand = "touch '$DPON_WITNESS'";
}
WMEOD
cat > "$WMHOME/Defaults/WMState" <<'WMEOD'
{
  Workspaces = ( { Name = "ipc-one"; }, { Name = "ipc-two"; }, { Name = "ipc-three"; } );
  Dock = {
    Applications = (
      { Name = "ipc-drive-fail.ipc-drive-fail";
        Command = "/nonexistent-ipc-drive";
        AutoLaunch = NO; Lock = NO; Forced = NO; BuggyApplication = NO;
        Position = "0,1"; }
    );
  };
}
WMEOD
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET
# keep the legacy Defaults/ layout so the seeded $WMHOME/Defaults/WindowMaker
# and $WMHOME/Defaults/WMState below are seen by wdefaultspathfordomain(),
# otherwise an empty WMAKER_USER_XDG still counts as set and check_defaults()
# runs the first-run installer, clobbering WMState
unset WMAKER_USER_XDG

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set IPC_DRIVE_DISPLAY to a free one" >&2
	exit 77
fi

note "start Xvfb $DPY"
# -noreset so root properties (RESOURCE_MANAGER for the Xft.dpi checks
# below, and anything a future row wants to survive the row-2 restart)
# are not wiped when wmaker's exec() briefly drops the client count to
# zero, this matches a real session where other clients keep the server
# alive
Xvfb "$DPY" -screen 0 1024x768x24 -ac -noreset >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
	kill -0 "$XVFB_PID" 2>/dev/null || fatal "Xvfb exited early"
	DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fatal "Xvfb failed to start"

note "resolve socket path"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY XDG_RUNTIME_DIR="$RUNTIME" \
	"$WMAKERMSG" --get-socketpath)
case "$SOCKET" in
	"$RUNTIME"/wmaker/ipc.*) record_pass "--get-socketpath under private XDG_RUNTIME_DIR" ;;
	*) record_fail "--get-socketpath returned $SOCKET" ;;
esac

note "start wmaker (readiness via get_version poll)"
start_wmaker || fatal "wmaker did not answer get_version within 8s"
record_pass "IPC readiness poll succeeds"

# ---- socket layout and permissions ---------------------------------------
# one managed screen owns one listener, and the socket directory and node
# must stay private even though the /tmp fallback name is predictable
note "socket layout"
SOCKDIR=${SOCKET%/*}
sock_count=0
for sock in "$SOCKDIR"/ipc.*; do
	[ -S "$sock" ] && sock_count=$((sock_count + 1))
done
check "one IPC socket for one managed screen" [ "$sock_count" -eq 1 ]
dir_mode=$(LC_ALL=C ls -ld "$SOCKDIR" 2>/dev/null | awk '{print $1}')
case "$dir_mode" in
drwx------) record_pass "IPC socket directory is 0700" ;;
*) record_fail "IPC socket directory mode is '$dir_mode', expected drwx------" ;;
esac
sock_mode=$(LC_ALL=C ls -l "$SOCKET" 2>/dev/null | awk '{print $1}')
case "$sock_mode" in
srw-------) record_pass "IPC socket is 0600" ;;
*) record_fail "IPC socket mode is '$sock_mode', expected srw-------" ;;
esac

# ---- Xft.dpi from RESOURCE_MANAGER --------------------------------------

log_has_dpi() {
	log_has_dpi_n=$(grep -c "Xft.dpi $1 " "$LOG" 2>/dev/null)
	[ "${log_has_dpi_n:-0}" -ge "$2" ]
}

note "Xft.dpi from RESOURCE_MANAGER"
# a multi-line database with a red-herring key that contains the
# substring Xft.dpi so the line-anchored scan is exercised
DISPLAY=$DPY xprop -root -f RESOURCE_MANAGER 8s -set RESOURCE_MANAGER \
	"$(printf 'Xft.antialias:\t1\nBogus.notXft.dpi:\t42\nXft.dpi:\t192\nXft.hinting:\t1\n')"
wait_for 30 log_has_dpi 192 1
check "Xft.dpi picked up on RESOURCE_MANAGER PropertyNotify" log_has_dpi 192 1
# left in place for row 2 to assert the cold-boot re-read after restart

# ---- exit-status convention ----------------------------------------------

note "exit-status convention"
msg -q get_version >/dev/null 2>&1
check "get_version exits 0" [ $? -eq 0 ]

msg -q frob >/dev/null 2>&1
check "unknown verb exits 2" [ $? -eq 2 ]

LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	"$WMAKERMSG" -s "$RUNTIME/nonexistent" -q get_version >/dev/null 2>&1
check "transport failure exits 1" [ $? -eq 1 ]

msg get_version 2>/dev/null | grep -q "pid = $WMAKER_PID;"
check "GET_VERSION reports wmaker pid" [ $? -eq 0 ]

# ---- get-keybinds ---------------------------------------------------------

note "get-keybinds"

if ! command -v python3 >/dev/null 2>&1; then
	record_skip "get-keybinds rows: python3 not installed"
else
	KB_JSON=$(msg --json get-keybinds 2>/dev/null)
	KB_ALL_JSON=$(msg --json get-keybinds all 2>/dev/null)
	INFO_JSON=$(msg --json info 2>/dev/null)

	kb_shape() {
		printf '%s' "$KB_JSON" | python3 -c '
import json, sys
d = json.load(sys.stdin)
assert d["success"] == "YES", d
rows = d["keybinds"]
assert rows, "empty keybinds"
for r in rows:
    assert {"action", "config_key", "spec", "category", "description",
            "bound"} <= set(r), r
    assert r["bound"] == "YES", r
    assert r.get("binding"), r
    assert r["category"] and r["description"], r
'
	}
	check "get-keybinds default rows are bound and complete" kb_shape

	kb_default_has() {
		printf '%s' "$KB_JSON" | python3 -c '
import json, sys
d = json.load(sys.stdin)
sys.exit(0 if any(r["config_key"] == sys.argv[1] for r in d["keybinds"]) else 1)
' "$1"
	}
	check "HideKey present by default" kb_default_has HideKey

	kb_all_unbound() {
		printf '%s' "$KB_ALL_JSON" | python3 -c '
import json, sys
d = json.load(sys.stdin)
r = [x for x in d["keybinds"] if x["config_key"] == sys.argv[1]]
assert r, "missing " + sys.argv[1]
r = r[0]
assert r["bound"] == "NO", r
assert "binding" not in r, r
assert r["spec"] == "None", r
' "$1"
	}
	check "CloseKey unbound under all without a binding key" kb_all_unbound CloseKey

	kb_set_equal() {
		printf '%s' "$INFO_JSON" | python3 -c '
import json, sys
info = json.load(sys.stdin)
allb = json.loads(sys.argv[1])
a = set(info["actions"])
b = set(r["action"] for r in allb["keybinds"])
assert a == b, (sorted(a - b), sorted(b - a))
' "$KB_ALL_JSON"
	}
	check "info actions equal get-keybinds all actions" kb_set_equal

	msg -q get-keybinds bogus >/dev/null 2>&1
	check "get-keybinds bad-arg exits 2" [ $? -eq 2 ]
fi

# ---- get_window_tree ------------------------------------------------------

note "get_window_tree"
if ! command -v xterm >/dev/null 2>&1; then
	record_skip "get_window_tree row: xterm not installed"
elif ! command -v xdotool >/dev/null 2>&1; then
	record_skip "get_window_tree row: xdotool not installed"
elif ! command -v python3 >/dev/null 2>&1; then
	record_skip "get_window_tree row: python3 not installed"
else
	DISPLAY=$DPY xterm -class WTree -title wtree-1 -geometry 80x24+60+60 \
		-e 'sleep 300' >>"$LOG" 2>&1 &
	WT_PID=$!
	echo "$WT_PID" >> "$PIDFILE"

	wt_xid() { DISPLAY=$DPY xdotool search --name "^wtree-1$" 2>/dev/null | tail -n 1; }
	wt_mapped() { [ -n "$(wt_xid)" ]; }
	wait_for 60 wt_mapped || fatal "xterm for tree row did not map"
	sleep 0.3
	WT_HEX=$(printf '0x%x' "$(wt_xid)")

	TREE=$(msg get_window_tree 2>/dev/null)
	check "get_window_tree returns a body" [ -n "$TREE" ]

	printf '%s' "$TREE" | python3 -m json.tool >/dev/null 2>&1
	check "get_window_tree reply parses as JSON" [ $? -eq 0 ]

	tree_shape() {
		printf '%s' "$TREE" | python3 -c '
import json, sys
t = json.load(sys.stdin)
xid = sys.argv[1]
assert {"screen", "version", "workspace_count", "current_workspace",
        "applications"} <= set(t)
wins = [w for a in t["applications"] for w in a["windows"]]
m = [w for w in wins if w["id"] == xid]
assert len(m) == 1, m
assert "frame" in m[0]
assert "focused" in m[0]["state"]
# the wire has no integer type, pl_int values arrive as strings
assert int(m[0]["geometry"]["width"]) > 0
assert int(m[0]["workspace"]) == 0
assert int(m[0]["creation_id"]) > 0
' "$WT_HEX"
	}
	check "tree lists $WT_HEX in exactly one application" tree_shape

	# an attacker-owned property must not balloon the serialized tree,
	# the resolver caps each field at 1 KiB
	if command -v xprop >/dev/null 2>&1; then
		WT_LONG=$(python3 -c 'print("A" * 5000)')
		DISPLAY=$DPY xprop -id "$WT_HEX" -f WM_NAME 8s -set WM_NAME "$WT_LONG"
		tree_title_capped() {
			msg get_window_tree 2>/dev/null | python3 -c '
import json, sys
t = json.load(sys.stdin)
xid = sys.argv[1]
wins = [w for a in t["applications"] for w in a["windows"]]
m = [w for w in wins if w["id"] == xid]
assert len(m) == 1, m
assert len(m[0]["title"]) == 1024, len(m[0]["title"])
' "$WT_HEX"
		}
		wait_for 50 tree_title_capped
		check "get_window_tree truncates an oversized title at 1 KiB" tree_title_capped
	else
		record_skip "get_window_tree oversized title: xprop not installed"
	fi

	# query while the client dies to exercise the teardown race
	kill "$WT_PID"
	wt_i=0
	while [ "$wt_i" -lt 5 ]; do
		msg get_window_tree >/dev/null 2>&1
		sleep 0.1
		wt_i=$((wt_i + 1))
	done
	wait_for 50 pid_gone "$WT_PID"

	tree_without() { ! msg get_window_tree 2>/dev/null | grep -qF "$WT_HEX"; }
	wait_for 50 tree_without
	check "closed client gone from the tree" tree_without
fi

# ---- pipelined modal action on one connection ---------------------------

note "modal action pipeline"

if ! command -v python3 >/dev/null 2>&1; then
	record_skip "modal action pipeline: python3 not installed"
elif ! command -v xdotool >/dev/null 2>&1; then
	record_skip "modal action pipeline: xdotool not installed"
else
	PIPELINE_PY="$RUNTIME/ipc-pipeline.py"
	PIPELINE_OUT="$RUNTIME/pipeline.out"
	cat > "$PIPELINE_PY" <<'PYEOF'
import socket, struct, sys, time

sock_path = sys.argv[1]
MAGIC = b"wmaker0\n"


def frame(body):
    data = body.encode()
    return MAGIC + struct.pack("II", len(data), 0) + data


s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sock_path)
start = time.monotonic()
# a modal action and a second frame pipelined on the same connection, the
# modal blocks the first reply until the shell dismisses it
s.sendall(frame("((action,RootMenu))"))
s.sendall(frame("((info))"))
s.settimeout(8)
buf = b""
try:
    while buf.count(b"wmaker0\n") < 2:
        chunk = s.recv(8192)
        if not chunk:
            break
        buf += chunk
except socket.timeout:
    pass
s.close()
blocked = (time.monotonic() - start) >= 0.8
print("first=%d second=%d blocked=%d" % (
    b"{success = YES;}" in buf, b"display_off" in buf, blocked))
PYEOF
	python3 "$PIPELINE_PY" "$SOCKET" >"$PIPELINE_OUT" 2>&1 &
	PIPE_PID=$!
	echo "$PIPE_PID" >> "$PIDFILE"
	sleep 1.2
	DISPLAY=$DPY xdotool key Escape
	wait_for 80 pid_gone "$PIPE_PID"
	wait "$PIPE_PID" 2>/dev/null
	PIPE_LINE=$(cat "$PIPELINE_OUT")
	check "pipelined modal action answered and blocked" \
		[ "$PIPE_LINE" = "first=1 second=1 blocked=1" ]
fi

# ---- display-power verb, display_power event, DisplayOff/OnCommand ------

note "display-power"

info_display_off() {
	msg info 2>/dev/null | grep -q "display_off = $1;"
}

evlog_count() {
	grep -cF "$1" "$EVLOG" 2>/dev/null
}

check "info reports display_off = NO at boot" info_display_off NO

start_monitor display_power tick || fatal "display_power monitor did not see first tick"

msg -q display-power off >/dev/null 2>&1
check "display-power off exits 0" [ $? -eq 0 ]
check "info reflects display_off = YES" info_display_off YES

# idempotent, a second off must succeed and stay YES, and must not
# emit a second event
msg -q display-power off >/dev/null 2>&1
check "display-power off is idempotent" info_display_off YES

msg -q display-power on >/dev/null 2>&1
check "display-power on exits 0" [ $? -eq 0 ]
check "info reflects display_off = NO" info_display_off NO

msg -q display-power maybe >/dev/null 2>&1
check "display-power bad-arg exits 2" [ $? -eq 2 ]

msg -q display-power >/dev/null 2>&1
check "display-power missing-arg exits 2" [ $? -eq 2 ]

check "bad arg did not flip the flag" info_display_off NO

wait_tick "dp-ipc-fence" || record_fail "tick fence after IPC display-power"
check "display_power {state=off,source=ipc} emitted" \
	evlog_has 'source = ipc;state = off;'
check "display_power {state=on,source=ipc} emitted" \
	evlog_has 'source = ipc;state = on;'
check "no-op transition emits no event" \
	[ "$(evlog_count 'source = ipc;state = off;')" -eq 1 ]

# the IPC source must not run the configured shell hook
check "DisplayOffCommand not run for source=ipc" [ ! -e "$DPOFF_WITNESS" ]
check "DisplayOnCommand not run for source=ipc" [ ! -e "$DPON_WITNESS" ]

# native detection: Xvfb exposes MIT-SCREEN-SAVER, so xset s
# activate / reset should round-trip through the notify handler.
# the IPC connect drives a poll cycle so by the time `info` returns
# the notify event has been dispatched, but probe with wait_for so a
# slow server does not flake
if config_defines USE_XCB_SCREENSAVER \
	&& DISPLAY=$DPY xdpyinfo 2>/dev/null | grep -q MIT-SCREEN-SAVER \
	&& command -v xset >/dev/null 2>&1; then
	DISPLAY=$DPY xset s 1 1 >/dev/null 2>&1
	DISPLAY=$DPY xset s activate >/dev/null 2>&1
	wait_for 30 info_display_off YES
	check "ScreenSaverNotify ON sets display_off" info_display_off YES

	wait_tick "dp-ss-off-fence" || record_fail "tick fence after xset s activate"
	check "display_power {state=off,source=screensaver} emitted" \
		evlog_has 'source = screensaver;state = off;'
	wait_for 30 test -e "$DPOFF_WITNESS"
	check "DisplayOffCommand runs for source=screensaver" \
		[ -e "$DPOFF_WITNESS" ]
	check "DisplayOnCommand not yet run" [ ! -e "$DPON_WITNESS" ]

	DISPLAY=$DPY xset s reset >/dev/null 2>&1
	wait_for 30 info_display_off NO
	check "ScreenSaverNotify OFF clears display_off" info_display_off NO

	wait_tick "dp-ss-on-fence" || record_fail "tick fence after xset s reset"
	check "display_power {state=on,source=screensaver} emitted" \
		evlog_has 'source = screensaver;state = on;'
	wait_for 30 test -e "$DPON_WITNESS"
	check "DisplayOnCommand runs for source=screensaver" \
		[ -e "$DPON_WITNESS" ]

	DISPLAY=$DPY xset s off >/dev/null 2>&1
else
	record_skip "ScreenSaverNotify ON/OFF: MIT-SCREEN-SAVER or xset unavailable"
fi

stop_monitor
rm -f "$DPOFF_WITNESS" "$DPON_WITNESS"

# ---- row 3: workspace event scope ----------------------------------------

note "workspace event"
start_monitor workspace tick || fatal "monitor did not see first tick"

msg -q workspace next >>"$LOG" 2>&1
wait_tick "ws-done" || record_fail "workspace fence tick never arrived"

WS_LINE=$(evlog_line 'change = focus')
TICK_LINE=$(evlog_line 'payload = "ws-done"')
check "workspace next emits change=focus" [ -n "$WS_LINE" ]
check "exactly one workspace event" \
	[ "$(grep -cF 'change = focus' "$EVLOG")" -eq 1 ]
check "workspace event arrives before tick fence" \
	sh -c "[ -n '$WS_LINE' ] && [ '$WS_LINE' -lt '$TICK_LINE' ]"

msg get_workspaces 2>/dev/null | grep -q 'current = YES'
check "GET_WORKSPACES marks one current" [ $? -eq 0 ]

stop_monitor
msg -q workspace 0 >>"$LOG" 2>&1

# ---- row 1: child_exit ordering ------------------------------------------

note "child_exit before dialog"
start_monitor child_exit dialog tick || fatal "monitor did not see first tick"

# the seeded WMState docks /nonexistent at yindex 1, dock-launch takes
# the same execCommand path a double-click does so the death handler
# is registered with source=dock/slot=1 and the failure dialog is
# raised by that handler after the child_exit event has been emitted
msg -q dock-launch 1 >>"$LOG" 2>&1
check "dock-launch 1 accepted" [ $? -eq 0 ]

# the verb reply arrives as soon as the child is forked, the SIGCHLD
# and the dialog the death handler raises are asynchronous to that, so
# gate on the dialog event before fencing, the failure dialog is modal
# but its event loop still drives the IPC input handler so send_tick
# is answered while the dialog is up
wait_for 50 evlog_has 'kind = message' \
	|| record_fail "no dialog event within 5s of dock-launch"
wait_tick "child-done" || record_fail "child_exit fence tick never arrived"

CE_LINE=$(evlog_line 'status = 127')
DLG_LINE=$(evlog_line 'kind = message')
T_LINE=$(evlog_line 'payload = "child-done"')

check "child_exit {status=127} delivered" [ -n "$CE_LINE" ]
check "child_exit reports source=dock" evlog_has 'source = dock'
check "child_exit reports slot=1" evlog_has 'slot = 1'
if [ -n "$DLG_LINE" ]; then
	check "child_exit precedes dialog" \
		sh -c "[ -n '$CE_LINE' ] && [ '$CE_LINE' -lt '$DLG_LINE' ]"
	check "dialog precedes tick fence" \
		sh -c "[ -n '$T_LINE' ] && [ '$DLG_LINE' -lt '$T_LINE' ]"
else
	record_skip "no failure dialog raised, ordering check skipped"
fi
stop_monitor
# the failure dialog is still mapped, the next row's restart tears it
# down with everything else so no explicit dismiss is needed

# ---- row 2: shutdown observability and restart re-bind -------------------

note "shutdown event and restart re-bind"
start_monitor shutdown tick || fatal "monitor did not see first tick"

msg -q restart >>"$LOG" 2>&1
# the monitor connection sees shutdown {change=restart} then EOF, the
# monitor process exits 0 on orderly EOF (ECONNRESET path)
wait_for 50 evlog_has 'change = restart'
check "shutdown {change=restart} delivered before EOF" \
	evlog_has 'change = restart'
wait_for 50 pid_gone "$MON_PID"
check "monitor sees EOF after restart" pid_gone "$MON_PID"
MON_PID=

# old wmaker pid is replaced (--for-real re-execs in place), the same
# socket path must answer again
wait_for 80 ipc_ready
check "same socket path re-binds after restart" ipc_ready

# the RESOURCE_MANAGER property set earlier survived the restart
# thanks to -noreset, so the cold-boot wScreenReadXftDpi() read must
# have logged the same value a second time
check "Xft.dpi read from RESOURCE_MANAGER at cold boot" log_has_dpi 192 2

# refresh the recorded pid, restart re-exec'd in place so the original
# entry in $PIDFILE is stale
WMAKER_PID=$(msg get_version 2>/dev/null | sed -n 's/.*pid = \([0-9]*\);.*/\1/p')
[ -n "$WMAKER_PID" ] && echo "$WMAKER_PID" >> "$PIDFILE"

# ---- row 4: SIGUSR1 reaches the same restart path as the IPC verb -------

note "SIGUSR1 -> restart"
start_monitor shutdown tick || record_fail "could not re-subscribe for SIGUSR1"
kill -USR1 "$WMAKER_PID"
wait_for 50 evlog_has 'change = restart'
check "SIGUSR1 delivers shutdown {change=restart}" evlog_has 'change = restart'
wait_for 50 pid_gone "$MON_PID"
check "monitor sees EOF after SIGUSR1 restart" pid_gone "$MON_PID"
MON_PID=

wait_for 80 ipc_ready
check "socket re-binds after SIGUSR1 restart" ipc_ready

WMAKER_PID=$(msg get_version 2>/dev/null | sed -n 's/.*pid = \([0-9]*\);.*/\1/p')
[ -n "$WMAKER_PID" ] && echo "$WMAKER_PID" >> "$PIDFILE"

start_monitor shutdown tick || record_fail "could not re-subscribe after restart"
msg -q exit >>"$LOG" 2>&1
wait_for 50 evlog_has 'change = exit'
check "shutdown {change=exit} delivered" evlog_has 'change = exit'
wait_for 50 pid_gone "$MON_PID"
check "monitor sees EOF after exit" pid_gone "$MON_PID"
MON_PID=

wait_for 50 pid_gone "$WMAKER_PID"
check "wmaker process exits on IPC exit" pid_gone "$WMAKER_PID"

# ---- row 5: SIGTERM reaches the same orderly-exit path as the IPC verb --

note "SIGTERM -> orderly exit"
start_wmaker || fatal "could not relaunch wmaker for SIGTERM row"
start_monitor shutdown tick || record_fail "could not subscribe for SIGTERM"
kill -TERM "$WMAKER_PID"
wait_for 50 evlog_has 'change = exit'
check "SIGTERM delivers shutdown {change=exit}" evlog_has 'change = exit'
wait_for 50 pid_gone "$MON_PID"
MON_PID=
wait_for 50 pid_gone "$WMAKER_PID"
check "wmaker process exits on SIGTERM" pid_gone "$WMAKER_PID"

# ---- row 5b: SIGHUP and SIGINT reach the same orderly-exit path ---------

for sig_name in HUP INT; do
	note "SIG$sig_name -> orderly exit"
	start_wmaker || fatal "could not relaunch wmaker for SIG$sig_name row"
	start_monitor shutdown tick \
		|| record_fail "could not subscribe for SIG$sig_name"
	kill -s "$sig_name" "$WMAKER_PID"
	wait_for 50 evlog_has 'change = exit'
	check "SIG$sig_name delivers shutdown {change=exit}" \
		evlog_has 'change = exit'
	wait_for 50 pid_gone "$MON_PID"
	MON_PID=
	wait_for 50 pid_gone "$WMAKER_PID"
	check "wmaker process exits on SIG$sig_name" pid_gone "$WMAKER_PID"
done

# ---- rows 6-7: source=menu and source=helper death handlers ------------
# These need a wmaker instance with HotCorners (to reach
# ExecuteShellCommand without driving a menu) and WorkspaceSpecificBack
# (to spawn the wmsetbg -helper child with a registered death handler),
# done on a fresh instance so the earlier rows are unaffected by the
# extra child processes and the sleep(1) in setWorkspaceSpecificBack().

note "death handlers: source=menu and source=helper"
cat > "$WMHOME/Defaults/WindowMaker" <<'WMEOD'
{
  HotCorners = YES;
  HotCornerDelay = 50;
  HotCornerEdge = 8;
  HotCornerActions = ("/nonexistent-ipc-menu", None, None, None);
  WorkspaceSpecificBack = ((solid, "#202020"));
}
WMEOD

PATH="$ROOT/util/.libs:$PATH" start_wmaker \
	|| fatal "could not relaunch wmaker for death-handler rows"
start_monitor child_exit dialog tick \
	|| record_fail "could not subscribe for death-handler rows"

xdo() { DISPLAY=$DPY xdotool "$@"; }
# handleDeadProcess() runs at the top of DispatchEvent(), so after a
# SIGCHLD has been recorded the child_exit emission waits for the next
# X event, root MotionNotify is gated on a mapped menu so generate a
# PropertyNotify instead which is always selected on root
pump_x() {
	DISPLAY=$DPY xprop -root -f _IPC_DRIVE_PUMP 8s \
		-set _IPC_DRIVE_PUMP x 2>/dev/null
}

if command -v xdotool >/dev/null 2>&1; then
	# move into the top-left corner, wait past HotCornerDelay so the
	# action fires, then move away again so the LeaveNotify pumps
	# DispatchEvent() and handleDeadProcess() sees the SIGCHLD record
	xdo mousemove 500 400; sleep 0.1
	xdo mousemove 0 0; sleep 0.4
	xdo mousemove 500 400
	wait_for 50 evlog_has 'source = menu'
	check "child_exit {source=menu} delivered" evlog_has 'source = menu'
	wait_for 50 evlog_has 'kind = message'
	M_LINE=$(evlog_line 'source = menu')
	M_DLG=$(evlog_line 'kind = message')
	check "shellCommandHandler raises a dialog" [ -n "$M_DLG" ]
	if [ -n "$M_LINE" ] && [ -n "$M_DLG" ]; then
		check "child_exit {source=menu} precedes its dialog" \
			[ "$M_LINE" -lt "$M_DLG" ]
	else
		record_skip "menu child_exit/dialog ordering"
	fi
	# dismiss the failure dialog so the helper death handler runs
	# from the main loop rather than nested inside this modal
	xdo key Return; sleep 0.3
else
	record_skip "child_exit {source=menu}: xdotool not installed"
fi

HELPER_PID=$(pgrep -P "$WMAKER_PID" -f 'wmsetbg' 2>/dev/null | head -n 1)
if [ -n "$HELPER_PID" ]; then
	kill -TERM "$HELPER_PID"
	wait_for 30 pid_gone "$HELPER_PID"
	pump_x
	wait_for 50 evlog_has 'source = helper'
	check "child_exit {source=helper} delivered" evlog_has 'source = helper'
	helper_signalled() {
		grep -F 'source = helper' "$EVLOG" 2>/dev/null \
			| grep -q 'signal = 15'
	}
	check "child_exit {source=helper} reports SIGTERM" helper_signalled
else
	record_skip "child_exit {source=helper}: wmsetbg helper not running"
fi

stop_monitor
msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

note "socket teardown"
check "IPC socket unlinked on clean exit" [ ! -e "$SOCKET" ]
no_ipc_listener() {
	! LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" -q get_version >/dev/null 2>&1
}
check "no IPC listener accepts after clean exit" no_ipc_listener

# ----------------------------------------------------------------------

finish
exit $?
