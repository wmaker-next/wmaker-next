#!/bin/sh
# Drive the IPC verbs that ipc-drive.sh skips: hide-application,
# hide-others, show-all, arrange-icons, and a window-workspace smoke
# (deep +N/-N/N coverage lives in wsmask-drive.sh).
#
# Each verb is asserted via the EWMH atoms the WM-side handlers publish:
#   hide-application / hide-others / show-all  -> _NET_WM_STATE_HIDDEN
#   window-workspace                           -> _NET_WM_DESKTOP
#   arrange-icons                              -> exit 0, wmaker alive,
#                                                 _NET_CLIENT_LIST intact
#
# Harness conventions follow wsmask-drive.sh: private XDG_RUNTIME_DIR,
# readiness gate on `wmakermsg -q get_version`, PID tracking, no pkill.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${IPC_VERBS_DRIVE_DISPLAY:-:96}
RUNTIME=$(mktemp -d /tmp/wmaker-ipcverbs-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=${IPC_VERBS_DRIVE_LOG:-/tmp/ipc-verbs-drive.log}
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
SOCKET=
: > "$PIDFILE"
: > "$LOG"

PASSED=0
FAILED=0
SKIPPED=0

cleanup() {
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
	cd_desc=$1
	shift
	if "$@"; then
		record_pass "$cd_desc"
	else
		record_fail "$cd_desc"
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

# --- EWMH probes on a client window ---------------------------------------

# _NET_WM_STATE contains _NET_WM_STATE_HIDDEN
is_hidden() {
	DISPLAY=$DPY xprop -id "$1" _NET_WM_STATE 2>/dev/null \
		| grep -q '_NET_WM_STATE_HIDDEN'
}

not_hidden() { ! is_hidden "$1"; }

# _NET_WM_DESKTOP == N
net_wm_desktop() {
	DISPLAY=$DPY xprop -id "$1" _NET_WM_DESKTOP 2>/dev/null \
		| sed -n 's/.*_NET_WM_DESKTOP(CARDINAL) = \([0-9]*\).*/\1/p'
}

# _NET_CLIENT_LIST token list (space-separated cardinals)
net_client_list() {
	DISPLAY=$DPY xprop -root _NET_CLIENT_LIST 2>/dev/null \
		| sed -n 's/.*_NET_CLIENT_LIST(WINDOW): //p' | tr ',' ' '
}

# ----------------------------------------------------------------------

for tool in Xvfb xdpyinfo xdotool xprop xterm; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

# Dock/clip/drawers disabled so floating appicons are the only icon
# surface and hide-application has a clean app_icon to animate to.
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat > "$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
}
EOF
cat > "$WMHOME/Defaults/WMState" <<'EOF'
{
  Workspaces = (
    { Name = "ipcverbs-0"; },
    { Name = "ipcverbs-1"; },
    { Name = "ipcverbs-2"; }
  );
}
EOF
export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set IPC_VERBS_DRIVE_DISPLAY to a free one" >&2
	exit 1
fi

note "start Xvfb $DPY"
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
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY \
	"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
wait_for 80 ipc_ready || fatal "wmaker did not answer get_version within 8s"
record_pass "IPC readiness poll succeeds"

# --- spawn two xterms as distinct WApplications ---------------------------

note "spawn two xterm clients on workspace 0"
# Distinct -class per xterm so each forms its own WApplication.
# wmaker coalesces same-instance.class windows into one shared-appicon
# fake group leader (window.c:1209), so two plain xterms ("xterm.XTerm")
# are a single app and hide-application would hide both. Distinct classes
# (AppA/AppB) give distinct "xterm.AppA"/"xterm.AppB" identifiers so the
# two apps are genuinely independent and hide-application/hide-others can
# be asserted against each other.
DISPLAY=$DPY xterm -class AppA -title ipcverbs-A -geometry 80x24+50+50 -e 'sleep 300' >>"$LOG" 2>&1 &
echo "$!" >> "$PIDFILE"
DISPLAY=$DPY xterm -class AppB -title ipcverbs-B -geometry 80x24+400+50 -e 'sleep 300' >>"$LOG" 2>&1 &
echo "$!" >> "$PIDFILE"

xa_xid() { DISPLAY=$DPY xdotool search --name "^ipcverbs-A$" 2>/dev/null | tail -n 1; }
xb_xid() { DISPLAY=$DPY xdotool search --name "^ipcverbs-B$" 2>/dev/null | tail -n 1; }
xa_mapped() { [ -n "$(xa_xid)" ]; }
xb_mapped() { [ -n "$(xb_xid)" ]; }
wait_for 60 xa_mapped || fatal "xterm A did not map"
wait_for 60 xb_mapped || fatal "xterm B did not map"
sleep 0.3
XA=$(xa_xid)
XB=$(xb_xid)
[ -n "$XA" ] && [ -n "$XB" ] || fatal "could not resolve both xterm XIDs ($XA / $XB)"
XA_HEX=$(printf '0x%x' "$XA")
XB_HEX=$(printf '0x%x' "$XB")
record_pass "both xterms mapped (A=$XA_HEX B=$XB_HEX)"

# baseline: neither hidden, both on desktop 0
wait_for 30 not_hidden "$XA"
check "A not hidden at baseline" not_hidden "$XA"
check "B not hidden at baseline" not_hidden "$XB"

# ---- hide-application -----------------------------------------------------

note "hide-application A"
# missing xid arg
msg -q hide-application >/dev/null 2>&1
check "hide-application with no xid exits 2" [ $? -eq 2 ]
# unmanaged xid
msg -q hide-application 0x1 >/dev/null 2>&1
check "hide-application unmanaged xid exits 2" [ $? -eq 2 ]
# happy path
msg -q hide-application "$XA_HEX" >/dev/null 2>&1
check "hide-application A exits 0" [ $? -eq 0 ]
wait_for 30 is_hidden "$XA"
check "A flagged _NET_WM_STATE_HIDDEN after hide-application" is_hidden "$XA"
# only A's app is hidden, B is untouched
check "B still visible after hide-application A" not_hidden "$XB"

# capture _NET_CLIENT_LIST snapshot; hidden windows stay managed and
# listed, this is the ordering/membership sanity gate
hide_after_hide=$(net_client_list)
check "A still in _NET_CLIENT_LIST when hidden" \
	[ -n "$(echo "$hide_after_hide" | grep -F "$XA_HEX")" ]

# ---- show-all reverses hide-application -----------------------------------

note "show-all"
msg -q show-all >/dev/null 2>&1
check "show-all exits 0" [ $? -eq 0 ]
wait_for 30 not_hidden "$XA"
check "A no longer hidden after show-all" not_hidden "$XA"
check "B still visible after show-all" not_hidden "$XB"

# ---- hide-others ---------------------------------------------------------

note "hide-others B"
msg -q hide-others >/dev/null 2>&1
check "hide-others with no xid exits 2" [ $? -eq 2 ]
msg -q hide-others 0x1 >/dev/null 2>&1
check "hide-others unmanaged xid exits 2" [ $? -eq 2 ]
# A and B are separate WApplications (distinct WM_CLIENT_LEADER / main
# windows), so hide-others(B) must hide A's app and leave B visible
msg -q hide-others "$XB_HEX" >/dev/null 2>&1
check "hide-others B exits 0" [ $? -eq 0 ]
wait_for 30 is_hidden "$XA"
check "A hidden by hide-others B" is_hidden "$XA"
check "B (the referenced window) stays visible" not_hidden "$XB"

# show-all to restore a clean state for the workspace rows
msg -q show-all >/dev/null 2>&1
wait_for 30 not_hidden "$XA"

# ---- arrange-icons --------------------------------------------------------

note "arrange-icons"
# Seed at least one appicon: hide-application A creates A's app_icon and
# maps its icon, giving arrange-icons something to reposition.
msg -q hide-application "$XA_HEX" >/dev/null 2>&1
wait_for 30 is_hidden "$XA"
pre_arrange=$(net_client_list)
msg -q arrange-icons >/dev/null 2>&1
check "arrange-icons exits 0" [ $? -eq 0 ]
# idempotent: a second call must also succeed and not disturb membership
msg -q arrange-icons >/dev/null 2>&1
check "arrange-icons is idempotent (exit 0)" [ $? -eq 0 ]
check "wmaker still alive after arrange-icons" ipc_ready
post_arrange=$(net_client_list)
check "_NET_CLIENT_LIST unchanged across arrange-icons" \
	[ "$pre_arrange" = "$post_arrange" ]
# restore for the workspace rows
msg -q show-all >/dev/null 2>&1
wait_for 30 not_hidden "$XA"

# ---- window-workspace smoke ----------------------------------------------
# Deep +N/-N/N and restart-survival coverage lives in wsmask-drive.sh;
# this row only asserts the verb dispatches and publishes _NET_WM_DESKTOP.

note "window-workspace absolute move"
[ "$(net_wm_desktop "$XA")" = "0" ] \
	&& record_pass "baseline _NET_WM_DESKTOP(A) = 0" \
	|| record_fail "baseline _NET_WM_DESKTOP(A) != 0 got $(net_wm_desktop "$XA")"

msg -q window-workspace >/dev/null 2>&1
check "window-workspace with no args exits 2" [ $? -eq 2 ]
msg -q window-workspace "$XA_HEX" >/dev/null 2>&1
check "window-workspace with no spec exits 2" [ $? -eq 2 ]
msg -q window-workspace 0x1 0 >/dev/null 2>&1
check "window-workspace unmanaged xid exits 2" [ $? -eq 2 ]
msg -q window-workspace "$XA_HEX" 99 >/dev/null 2>&1
check "window-workspace out-of-range index exits 2" [ $? -eq 2 ]

msg -q window-workspace "$XA_HEX" 1 >/dev/null 2>&1
check "window-workspace A -> 1 exits 0" [ $? -eq 0 ]
wf_desktop_1() { [ "$(net_wm_desktop "$XA")" = "1" ]; }
wait_for 30 wf_desktop_1
check "A _NET_WM_DESKTOP = 1 after move" wf_desktop_1

msg -q window-workspace "$XA_HEX" 2 >/dev/null 2>&1
check "window-workspace A -> 2 exits 0" [ $? -eq 0 ]
wf_desktop_2() { [ "$(net_wm_desktop "$XA")" = "2" ]; }
wait_for 30 wf_desktop_2
check "A _NET_WM_DESKTOP = 2 after move" wf_desktop_2

# bring A back to workspace 0 so the trailing show-all deiconifies on
# the workspace the drive is viewing
msg -q workspace 0 >/dev/null 2>&1
msg -q window-workspace "$XA_HEX" 0 >/dev/null 2>&1
wait_for 30 not_hidden "$XA"

# ---- shutdown ------------------------------------------------------------

note "orderly exit via IPC"
msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"
check "wmaker exits on IPC exit" pid_gone "$WMAKER_PID"

# ----------------------------------------------------------------------

note "IPC verbs drive summary"
printf 'PASS: %d, FAIL: %d, SKIP: %d\n' "$PASSED" "$FAILED" "$SKIPPED" \
	| tee -a "$LOG"
total=$((PASSED + FAILED))
[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
