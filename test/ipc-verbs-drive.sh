#!/bin/sh
# Drive the IPC surface that ipc-drive.sh skips: action Hide,
# action HideOthers, ShowAll, ArrangeIcons, a window-workspace smoke
# (deep +N/-N/N coverage lives in wsmask-drive.sh), and the
# workspace-delete in-use rejection.
#
# Each is asserted via the EWMH atoms the WM-side handlers publish:
#   action Hide / action HideOthers / ShowAll  -> _NET_WM_STATE_HIDDEN
#   window-workspace                           -> _NET_WM_DESKTOP
#   workspace-delete (in use)                  -> exit 2, unchanged count,
#                                                 no modal dialog
#   ArrangeIcons                              -> exit 0, wmaker alive,
#                                                 _NET_CLIENT_LIST intact
#
# Harness conventions follow wsmask-drive.sh: private XDG_RUNTIME_DIR,
# readiness gate on `wmakermsg -q get_version`, PID tracking, no pkill.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${IPC_VERBS_DRIVE_DISPLAY:-:$(drive_default_display)}
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

# _NET_CURRENT_DESKTOP as a bare number
net_current_desktop() {
	DISPLAY=$DPY xprop -root _NET_CURRENT_DESKTOP 2>/dev/null \
		| sed -n 's/.*= //p'
}

# _NET_SHOWING_DESKTOP as a bare number (1 while show-desktop is active)
net_showing_desktop() {
	DISPLAY=$DPY xprop -root _NET_SHOWING_DESKTOP 2>/dev/null \
		| sed -n 's/.*= //p'
}

# _NET_NUMBER_OF_DESKTOPS as a bare number
net_number_of_desktops() {
	DISPLAY=$DPY xprop -root _NET_NUMBER_OF_DESKTOPS 2>/dev/null \
		| sed -n 's/.*= //p'
}

# ----------------------------------------------------------------------

for tool in Xvfb xdpyinfo xdotool xprop xterm; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

# Dock/clip/drawers disabled so floating appicons are the only icon
# surface and action Hide has a clean app_icon to animate to.
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
	exit 77
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
# are a single app and action Hide would hide both. Distinct classes
# (AppA/AppB) give distinct "xterm.AppA"/"xterm.AppB" identifiers so the
# two apps are genuinely independent and action Hide/HideOthers can
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

# ---- action Hide ----------------------------------------------------------

note "action Hide on A"
# unmanaged xid
msg -q action Hide 0x1 >/dev/null 2>&1
check "action Hide unmanaged xid exits 2" [ $? -eq 2 ]
# explicit xid targets a background window
msg -q action Hide "$XA_HEX" >/dev/null 2>&1
check "action Hide A exits 0" [ $? -eq 0 ]
wait_for 30 is_hidden "$XA"
check "A flagged _NET_WM_STATE_HIDDEN after action Hide" is_hidden "$XA"
# only A's app is hidden, B is untouched
check "B still visible after action Hide A" not_hidden "$XB"

# capture _NET_CLIENT_LIST snapshot, hidden windows stay managed and
# listed, this is the ordering/membership sanity gate
hide_after_hide=$(net_client_list)
check "A still in _NET_CLIENT_LIST when hidden" \
	[ -n "$(echo "$hide_after_hide" | grep -F "$XA_HEX")" ]

# focused-window form (no xid)
msg -q action ShowAll >/dev/null 2>&1
wait_for 30 not_hidden "$XA"
DISPLAY=$DPY xdotool windowactivate "$XA" >/dev/null 2>&1
sleep 0.3
msg -q action Hide >/dev/null 2>&1
check "action Hide with no xid exits 0" [ $? -eq 0 ]
wait_for 30 is_hidden "$XA"
check "A hidden by focused action Hide" is_hidden "$XA"

# ---- ShowAll reverses Hide -----------------------------------------------

note "ShowAll"
msg -q action ShowAll >/dev/null 2>&1
check "ShowAll exits 0" [ $? -eq 0 ]
wait_for 30 not_hidden "$XA"
check "A no longer hidden after ShowAll" not_hidden "$XA"
check "B still visible after ShowAll" not_hidden "$XB"

# ---- action HideOthers ----------------------------------------------------

note "action HideOthers on B"
msg -q action HideOthers 0x1 >/dev/null 2>&1
check "action HideOthers unmanaged xid exits 2" [ $? -eq 2 ]
# A and B are separate WApplications (distinct WM_CLIENT_LEADER / main
# windows), so HideOthers(B) must hide A's app and leave B visible
msg -q action HideOthers "$XB_HEX" >/dev/null 2>&1
check "action HideOthers B exits 0" [ $? -eq 0 ]
wait_for 30 is_hidden "$XA"
check "A hidden by action HideOthers B" is_hidden "$XA"
check "B (the referenced window) stays visible" not_hidden "$XB"

# ShowAll to restore a clean state for the workspace rows
msg -q action ShowAll >/dev/null 2>&1
wait_for 30 not_hidden "$XA"

# ---- ArrangeIcons --------------------------------------------------------

note "ArrangeIcons"
# Seed at least one appicon: action Hide A creates A's app_icon and
# maps its icon, giving ArrangeIcons something to reposition.
msg -q action Hide "$XA_HEX" >/dev/null 2>&1
wait_for 30 is_hidden "$XA"
pre_arrange=$(net_client_list)
msg -q action ArrangeIcons >/dev/null 2>&1
check "ArrangeIcons exits 0" [ $? -eq 0 ]
# idempotent: a second call must also succeed and not disturb membership
msg -q action ArrangeIcons >/dev/null 2>&1
check "ArrangeIcons is idempotent (exit 0)" [ $? -eq 0 ]
check "wmaker still alive after ArrangeIcons" ipc_ready
post_arrange=$(net_client_list)
check "_NET_CLIENT_LIST unchanged across ArrangeIcons" \
	[ "$pre_arrange" = "$post_arrange" ]
# restore for the workspace rows
msg -q action ShowAll >/dev/null 2>&1
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

# workspace-delete pre-checks sole membership instead of calling
# wWorkspaceDelete(), whose modal "in use" dialog would block the IPC
# reply forever. A is the only window on desktop 2 here, so the verb
# must reject without raising the dialog or changing anything.
note "workspace-delete refuses an in-use workspace"
ws_count_before=$(net_number_of_desktops)
msg -q workspace-delete 2 >/dev/null 2>&1
check "workspace-delete on in-use workspace exits 2" [ $? -eq 2 ]
check "in-use workspace still present" \
	[ "$(net_number_of_desktops)" = "$ws_count_before" ]
check "A stays on desktop 2 after refused delete" \
	[ "$(net_wm_desktop "$XA")" = "2" ]
check "wmaker alive after refused delete" ipc_ready
# if the pre-check ever regresses the modal is still up, dismiss it so
# the remaining rows are not wedged behind it
DISPLAY=$DPY xdotool key Escape >/dev/null 2>&1

# bring A back to workspace 0 so the trailing ShowAll deiconifies on
# the workspace the drive is viewing
msg -q workspace 0 >/dev/null 2>&1
msg -q window-workspace "$XA_HEX" 0 >/dev/null 2>&1
wait_for 30 not_hidden "$XA"

# ---- action: generic keybind-action dispatch -------------------------------

note "action generic verb"
msg -q action >/dev/null 2>&1
check "action with no name exits 2" [ $? -eq 2 ]
msg -q action NoSuchThing >/dev/null 2>&1
check "action unknown name exits 2" [ $? -eq 2 ]
msg -q action KeychainCancel >/dev/null 2>&1
check "action KeychainCancel (excluded row) exits 2" [ $? -eq 2 ]

wf_ws0() { [ "$(net_current_desktop)" = "0" ]; }
wait_for 30 wf_ws0

ws_before=$(net_current_desktop)
wf_ws_changed() { [ -n "$(net_current_desktop)" ] && [ "$(net_current_desktop)" != "$1" ]; }
msg -q action NextWorkspace >/dev/null 2>&1
check "action NextWorkspace exits 0" [ $? -eq 0 ]
wait_for 30 wf_ws_changed "$ws_before"
check "NextWorkspace moved to another workspace" wf_ws_changed "$ws_before"

# the config-key spelling resolves to the same command
ws_mid=$(net_current_desktop)
msg -q action NextWorkspaceKey >/dev/null 2>&1
check "action NextWorkspaceKey (spelling) exits 0" [ $? -eq 0 ]
wait_for 30 wf_ws_changed "$ws_mid"
check "NextWorkspaceKey moved to another workspace" wf_ws_changed "$ws_mid"

note "action ShowDesktop"
wait_for 30 wf_ws0
wf_showing() { [ "$(net_showing_desktop)" = "1" ]; }
wf_not_showing() { [ "$(net_showing_desktop)" != "1" ]; }
msg -q action ShowDesktop >/dev/null 2>&1
check "action ShowDesktop exits 0" [ $? -eq 0 ]
wait_for 30 wf_showing
check "_NET_SHOWING_DESKTOP set by ShowDesktop" wf_showing
msg -q action ShowDesktop >/dev/null 2>&1
wait_for 30 wf_not_showing
check "_NET_SHOWING_DESKTOP cleared by ShowDesktop toggle" wf_not_showing

note "action Miniaturize"
DISPLAY=$DPY xdotool windowactivate "$XA" >/dev/null 2>&1
sleep 0.3
msg -q action Miniaturize >/dev/null 2>&1
check "action Miniaturize (focused window) exits 0" [ $? -eq 0 ]
msg -q action ShowAll >/dev/null 2>&1
wait_for 30 not_hidden "$XA"
DISPLAY=$DPY xdotool windowactivate "$XA" >/dev/null 2>&1
sleep 0.3
msg -q action Miniaturize "$XA_HEX" >/dev/null 2>&1
check "action Miniaturize with window id exits 0" [ $? -eq 0 ]
wf_hidden() { is_hidden "$XA"; }
wait_for 30 wf_hidden
check "A hidden by action Miniaturize" wf_hidden
msg -q action NoSuchThing "$XA_HEX" >/dev/null 2>&1
check "action unknown name with window id exits 2" [ $? -eq 2 ]
msg -q action ShowAll >/dev/null 2>&1
wait_for 30 not_hidden "$XA"

note "info exposes the action-name list"
INFO_OUT=$(msg info 2>/dev/null)
check "info exits 0" [ $? -eq 0 ]
ACT_TOKENS=$(printf '%s\n' "$INFO_OUT" \
	| sed -n 's/.*actions = (\(.*\)).*/\1/p' \
	| tr ',' '\n' | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' \
	| grep -v '^$')
ACT_TOTAL=$(printf '%s\n' "$ACT_TOKENS" | grep -c '^.')
ACT_UNIQ=$(printf '%s\n' "$ACT_TOKENS" | sort -u | grep -c '^.')
check "info actions list is non-empty" [ "$ACT_TOTAL" -gt 0 ]
check "info actions list is duplicate-free" [ "$ACT_TOTAL" = "$ACT_UNIQ" ]
info_has() { printf '%s\n' "$ACT_TOKENS" | grep -qx "$1"; }
info_lacks_keychain() { ! info_has KeychainCancel; }
check "info actions list contains NextWorkspace" info_has NextWorkspace
check "info actions list contains ShowAll" info_has ShowAll
check "info actions list contains ArrangeIcons" info_has ArrangeIcons
check "info actions list contains MoveToEdgeLeftNoSnap" info_has MoveToEdgeLeftNoSnap
check "info actions list excludes KeychainCancel" info_lacks_keychain

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
