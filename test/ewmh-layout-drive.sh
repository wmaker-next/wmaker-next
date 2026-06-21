#!/bin/sh
# Exercise EWMH/NETWM behavior exposed by Window Maker under Xephyr.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${EWMH_LAYOUT_DRIVE_DISPLAY:-${EWMH_DRIVE_DISPLAY:-:95}}
PIDFILE=$(mktemp /tmp/ewmh-drive.pids.XXXXXX)
WMHOME=$(mktemp -d /tmp/ewmh-drive-wmhome.XXXXXX)
LOG=${EWMH_DRIVE_LOG:-/tmp/ewmh-layout-drive.log}
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
CLIENT_BIN="$WMHOME/ewmh-client"
CLIENT_XID_FILE="$WMHOME/ewmh-client.xid"
CLIENT_XID=
CLIENT_HELPER_XID=
CLIENT_PID=
CLIENT2_XID_FILE="$WMHOME/ewmh-client2.xid"
CLIENT2_XID=
CLIENT2_HELPER_XID=
CLIENT2_PID=
ROOT_XID=

PASSED=0
FAILED=0
SKIPPED=0

: > "$PIDFILE"
: > "$LOG"

cleanup() {
	while read -r p; do
		[ -n "$p" ] && kill "$p" 2>/dev/null
	done < "$PIDFILE"
	rm -f "$PIDFILE"
	rm -rf "$WMHOME"
}
trap cleanup EXIT

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
fatal() { printf 'FATAL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
skip_all() { printf 'SKIP: %s\n' "$*" | tee -a "$LOG"; exit 77; }

record_pass() {
	PASSED=$((PASSED + 1))
	printf 'PASS: %s\n' "$*" | tee -a "$LOG"
}

record_fail() {
	FAILED=$((FAILED + 1))
	printf 'FAIL: %s\n' "$*" | tee -a "$LOG"
}

record_skip() {
	SKIPPED=$((SKIPPED + 1))
	printf 'SKIP: %s\n' "$*" | tee -a "$LOG"
}

check() {
	check_desc=$1
	shift
	if "$@"; then
		record_pass "$check_desc"
	else
		record_fail "$check_desc"
	fi
}

finish() {
	finish_total=$((PASSED + FAILED))
	if [ "$finish_total" -gt 0 ]; then
		finish_pct=$((PASSED * 100 / finish_total))
	else
		finish_pct=0
	fi

	note "EWMH conformance summary"
	printf 'PASS: %d/%d (%d%%), FAIL: %d, SKIP: %d\n' \
		"$PASSED" "$finish_total" "$finish_pct" "$FAILED" "$SKIPPED" \
		| tee -a "$LOG"

	[ "$FAILED" -eq 0 ]
}

for tool in Xephyr xdotool wmctrl xprop xdpyinfo xwininfo; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"

# wait_for N CMD ARGS... : retry CMD up to N times at 100ms intervals
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

run_x() {
	DISPLAY=$DPY "$@"
}

root_prop() {
	run_x xprop -root "$1" 2>/dev/null
}

window_prop() {
	run_x xprop -id "$1" "$2" 2>/dev/null
}

window_values() {
	window_prop "$1" "$2" | sed -e "s/^[^=]*= //" -e "s/^[^:]*: //"
}

window_prop_exists() {
	window_prop "$1" "$2" | grep -F "$2(" >/dev/null
}

window_prop_has_values() {
	[ "$(window_values "$1" "$2")" = "$3" ]
}

window_cardinal_count_is() {
	wc_win=$1
	wc_prop=$2
	wc_expected=$3
	wc_vals=$(window_values "$wc_win" "$wc_prop")
	[ -n "$wc_vals" ] || return 1
	set -- $(printf "%s\n" "$wc_vals" | tr "," " ")
	if [ "$#" -eq "$wc_expected" ]; then
		return 0
	fi
	printf "DETAIL: %s expected %s CARDINALs on %s, got %s: %s\n" \
		"$wc_prop" "$wc_expected" "$wc_win" "$#" "$wc_vals" >>"$LOG"
	return 1
}

window_atom_has() {
	window_values "$1" "$2" \
		| tr "," "\n" \
		| sed "s/^[[:space:]]*//" \
		| grep -Fx "$3" >/dev/null
}

window_atom_lacks() {
	! window_atom_has "$1" "$2" "$3"
}

root_window_list_has() {
	root_prop "$1" | grep -i -F "$2" >/dev/null
}

root_window_list_lacks() {
	! root_window_list_has "$1" "$2"
}

window_refers_to() {
	window_values "$1" "$2" | grep -i -F "$3" >/dev/null
}

stacking_above() {
	sa_top=$(printf "%s" "$1" | tr "A-F" "a-f")
	sa_bottom=$(printf "%s" "$2" | tr "A-F" "a-f")
	sa_i=0
	sa_top_i=
	sa_bottom_i=
	for sa_win in $(prop_values _NET_CLIENT_LIST_STACKING | tr "," " "); do
		sa_win=$(printf "%s" "$sa_win" | tr "A-F" "a-f")
		[ "$sa_win" = "$sa_top" ] && sa_top_i=$sa_i
		[ "$sa_win" = "$sa_bottom" ] && sa_bottom_i=$sa_i
		sa_i=$((sa_i + 1))
	done
	[ -n "$sa_top_i" ] && [ -n "$sa_bottom_i" ] && [ "$sa_bottom_i" -lt "$sa_top_i" ]
}

send_restack_message() {
	DISPLAY=$DPY env EWMH_CLIENT_RESTACK_TARGET="$1" \
		EWMH_CLIENT_RESTACK_SIBLING="$2" \
		EWMH_CLIENT_RESTACK_DETAIL="$3" \
		"$CLIENT_BIN" >>"$LOG" 2>&1
}

# send_wm_state TARGET ACTION ATOM : raw _NET_WM_STATE message via the
# in-tree client. wmctrl 1.07's -b validates against a fixed property
# list (modal, sticky, maximized_*, shaded, skip_*, hidden, fullscreen,
# above, below) and on builds that fall through it interns the bare
# token, so e.g. demands_attention never reaches the WM as
# _NET_WM_STATE_DEMANDS_ATTENTION. ACTION is 0=remove 1=add 2=toggle.
send_wm_state() {
	DISPLAY=$DPY env EWMH_CLIENT_STATE_TARGET="$1" \
		EWMH_CLIENT_STATE_ACTION="$2" \
		EWMH_CLIENT_STATE_ATOM1="$3" \
		"$CLIENT_BIN" >>"$LOG" 2>&1
}

# send_active_window TARGET SOURCE TIMESTAMP : raw _NET_ACTIVE_WINDOW
# message with an arbitrary source indication, since wmctrl/xdotool always
# use source 2 (pager)
send_active_window() {
	DISPLAY=$DPY env EWMH_CLIENT_ACTIVATE_TARGET="$1" \
		EWMH_CLIENT_ACTIVATE_SOURCE="$2" \
		EWMH_CLIENT_ACTIVATE_TIME="$3" \
		"$CLIENT_BIN" >>"$LOG" 2>&1
}

# set_desktop_names a,b,c : write NUL-separated _NET_DESKTOP_NAMES on root
# and send the ClientMessage wmaker reacts to
set_desktop_names() {
	DISPLAY=$DPY env EWMH_CLIENT_SET_DESKTOP_NAMES="$1" \
		"$CLIENT_BIN" >>"$LOG" 2>&1
}

xinerama_head_count() {
	run_x xdpyinfo -ext XINERAMA 2>/dev/null | grep -c '^  head #'
}

# send_fsmonitors TARGET TOP BOTTOM LEFT RIGHT : raw _NET_WM_FULLSCREEN_MONITORS
send_fsmonitors() {
	DISPLAY=$DPY env EWMH_CLIENT_FSMONITORS_TARGET="$1" \
		EWMH_CLIENT_FSMONITORS="$2 $3 $4 $5" \
		"$CLIENT_BIN" >>"$LOG" 2>&1
}

window_at() {
	[ "$(xwininfo_field "$1" 'Absolute upper-left X')" = "$2" ] \
		&& [ "$(xwininfo_field "$1" 'Absolute upper-left Y')" = "$3" ]
}

window_on_head1() {
	wox=$(xwininfo_field "$1" 'Absolute upper-left X')
	[ -n "$wox" ] && [ "$wox" -ge 1024 ]
}

window_on_head0() {
	wox=$(xwininfo_field "$1" 'Absolute upper-left X')
	[ -n "$wox" ] && [ "$wox" -lt 1024 ]
}

window_is_viewable() {
	[ "$(xwininfo_field "$1" 'Map State')" = IsViewable ]
}

window_not_viewable() {
	wnv=$(xwininfo_field "$1" 'Map State')
	[ -n "$wnv" ] && [ "$wnv" != IsViewable ]
}

desktop_name_is() {
	root_prop _NET_DESKTOP_NAMES \
		| sed -e 's/^[^=]*= //' -e 's/"//g' \
		| tr ',' '\n' \
		| sed -n "$(($1 + 1))p" \
		| sed 's/^[[:space:]]*//' \
		| grep -Fx "$2" >/dev/null
}

send_format8_close() {
	DISPLAY=$DPY env EWMH_CLIENT_FORMAT8_CLOSE_TARGET="$1" \
		"$CLIENT_BIN" >>"$LOG" 2>&1
}

prop_values() {
	root_prop "$1" | sed -e 's/^[^=]*= //' -e 's/^[^:]*: //'
}

prop_has_values() {
	[ "$(prop_values "$1")" = "$2" ]
}

prop_exists() {
	root_prop "$1" | grep -F "$1(" >/dev/null
}

supported_atom() {
	prop_values _NET_SUPPORTED \
		| tr ',' '\n' \
		| sed 's/^[	 ]*//' \
		| grep -Fx "$1" >/dev/null
}

supporting_wm_window() {
	prop_values _NET_SUPPORTING_WM_CHECK | sed -n 's/^window id # \([^, ]*\).*/\1/p'
}

supporting_wm_roundtrip() {
	swm_id=$(supporting_wm_window)
	[ -n "$swm_id" ] || return 1
	window_prop "$swm_id" _NET_SUPPORTING_WM_CHECK | grep -F "$swm_id" >/dev/null
}

supporting_wm_name() {
	swm_id=$(supporting_wm_window)
	[ -n "$swm_id" ] || return 1
	window_prop "$swm_id" _NET_WM_NAME | grep -F 'WindowMaker' >/dev/null
}

supporting_wm_pid() {
	swm_id=$(supporting_wm_window)
	[ -n "$swm_id" ] || return 1
	window_prop "$swm_id" _NET_WM_PID | grep -E '= [0-9]+' >/dev/null
}

cardinal_count_is() {
	cc_prop=$1
	cc_expected=$2
	cc_vals=$(prop_values "$cc_prop")
	[ -n "$cc_vals" ] || return 1
	set -- $(printf "%s\n" "$cc_vals" | tr "," " ")
	if [ "$#" -eq "$cc_expected" ]; then
		return 0
	fi
	printf "DETAIL: %s expected %s CARDINALs, got %s: %s\n" \
		"$cc_prop" "$cc_expected" "$#" "$cc_vals" >>"$LOG"
	return 1
}

config_defines() {
	[ -f "$ROOT/config.h" ] || return 1
	grep -E "^#define[ 	]+$1([ 	]+|$)" "$ROOT/config.h" >/dev/null
}

current_desktop() {
	prop_values _NET_CURRENT_DESKTOP
}

desktop_is() {
	[ "$(current_desktop)" = "$1" ]
}

set_desktop() {
	run_x wmctrl -s "$1" >>"$LOG" 2>&1 || return 1
	wait_for 30 desktop_is "$1"
}

set_layout() {
	run_x xprop -root -f _NET_DESKTOP_LAYOUT 32c \
		-set _NET_DESKTOP_LAYOUT "$1, $2, $3, $4" >>"$LOG" 2>&1 || return 1
	wait_for 20 prop_has_values _NET_DESKTOP_LAYOUT "$1, $2, $3, $4"
}

press_key() {
	run_x xdotool key "$1" >>"$LOG" 2>&1
}

check_desktop_transition() {
	cdt_desc=$1
	cdt_from=$2
	cdt_key=$3
	cdt_expect=$4

	if ! set_desktop "$cdt_from"; then
		record_fail "$cdt_desc: could not switch to desktop $cdt_from"
		return
	fi
	if ! press_key "$cdt_key"; then
		record_fail "$cdt_desc: could not send $cdt_key"
		return
	fi
	if wait_for 30 desktop_is "$cdt_expect"; then
		record_pass "$cdt_desc"
	else
		record_fail "$cdt_desc: expected desktop $cdt_expect, got $(current_desktop)"
	fi
}

check_layout_property() {
	clp_desc=$1
	clp_orientation=$2
	clp_cols=$3
	clp_rows=$4
	clp_corner=$5

	if set_layout "$clp_orientation" "$clp_cols" "$clp_rows" "$clp_corner"; then
		record_pass "$clp_desc"
	else
		record_fail "$clp_desc"
	fi
}

xsync_available() {
	run_x xdpyinfo -queryExtensions 2>/dev/null | grep -w SYNC >/dev/null
}

build_ewmh_client() {
	cc_cmd=${CC:-cc}
	if ! command -v "$cc_cmd" >/dev/null 2>&1; then
		record_skip "cc not installed; managed-client checks skipped"
		return 1
	fi
	if "$cc_cmd" -std=c99 -Wall -Wextra \
		"$ROOT/test/ewmh-client.c" -o "$CLIENT_BIN" -lX11 >>"$LOG" 2>&1; then
		return 0
	fi
	record_skip "could not build test/ewmh-client.c; managed-client checks skipped"
	return 1
}

client_xid_ready() {
	[ -s "$CLIENT_XID_FILE" ] || return 1
	CLIENT_XID=$(sed -n "1p" "$CLIENT_XID_FILE")
	CLIENT_HELPER_XID=$(sed -n "2p" "$CLIENT_XID_FILE")
	case "$CLIENT_XID" in
		0x[0-9a-fA-F]*) return 0 ;;
		*) return 1 ;;
	esac
}

start_ewmh_client() {
	: >"$CLIENT_XID_FILE"
	EWMH_CLIENT_USER_TIME=1 run_x "$CLIENT_BIN" >"$CLIENT_XID_FILE" 2>>"$LOG" &
	CLIENT_PID=$!
	echo "$CLIENT_PID" >>"$PIDFILE"
	wait_for 50 client_xid_ready && kill -0 "$CLIENT_PID" 2>/dev/null
}

start_user_time_helper_client() {
	: >"$CLIENT_XID_FILE"
	DISPLAY=$DPY env EWMH_CLIENT_NAME=time-helper \
		EWMH_CLIENT_USER_TIME=8000 EWMH_CLIENT_USER_TIME_HELPER=1 \
		"$CLIENT_BIN" >"$CLIENT_XID_FILE" 2>>"$LOG" &
	CLIENT_PID=$!
	echo "$CLIENT_PID" >>"$PIDFILE"
	wait_for 50 client_xid_ready && [ -n "$CLIENT_HELPER_XID" ] \
		&& kill -0 "$CLIENT_PID" 2>/dev/null
}

start_gnustep_hide_client() {
	: >"$CLIENT_XID_FILE"
	DISPLAY=$DPY env EWMH_CLIENT_NAME=gnustep-hide \
		EWMH_CLIENT_USER_TIME=1 EWMH_CLIENT_GNUSTEP_HIDE=1 \
		"$CLIENT_BIN" >"$CLIENT_XID_FILE" 2>>"$LOG" &
	CLIENT_PID=$!
	echo "$CLIENT_PID" >>"$PIDFILE"
	wait_for 50 client_xid_ready && kill -0 "$CLIENT_PID" 2>/dev/null
}

client_xid_file_has() {
	grep -q "^$1\$" "$CLIENT_XID_FILE"
}

client2_xid_ready() {
	[ -s "$CLIENT2_XID_FILE" ] || return 1
	CLIENT2_XID=$(sed -n "1p" "$CLIENT2_XID_FILE")
	CLIENT2_HELPER_XID=$(sed -n "2p" "$CLIENT2_XID_FILE")
	case "$CLIENT2_XID" in
		0x[0-9a-fA-F]*) return 0 ;;
		*) return 1 ;;
	esac
}

# start_second_client [VAR=VALUE ...] : spawn a second instance with the
# given environment overrides, results in CLIENT2_XID / CLIENT2_PID
start_second_client() {
	: >"$CLIENT2_XID_FILE"
	DISPLAY=$DPY env "$@" "$CLIENT_BIN" >"$CLIENT2_XID_FILE" 2>>"$LOG" &
	CLIENT2_PID=$!
	echo "$CLIENT2_PID" >>"$PIDFILE"
	wait_for 50 client2_xid_ready && kill -0 "$CLIENT2_PID" 2>/dev/null
}

stop_second_client() {
	[ -n "$CLIENT2_PID" ] || return 0
	run_x wmctrl -i -c "$CLIENT2_XID" >>"$LOG" 2>&1
	if ! wait_for 50 pid_gone "$CLIENT2_PID"; then
		kill "$CLIENT2_PID" 2>/dev/null
		wait_for 30 pid_gone "$CLIENT2_PID" || kill -KILL "$CLIENT2_PID" 2>/dev/null
	fi
	CLIENT2_PID=
	CLIENT2_XID=
	CLIENT2_HELPER_XID=
}

active_window_is() {
	prop_values _NET_ACTIVE_WINDOW \
		| sed -n 's/^window id # \([^, ]*\).*/\1/p' \
		| grep -iFx "$1" >/dev/null
}

workarea_left_is() {
	wa=$(prop_values _NET_WORKAREA | sed 's/,.*//')
	[ "$wa" = "$1" ]
}

xwininfo_field() {
	run_x xwininfo -id "$1" 2>/dev/null \
		| sed -n "s/^[[:space:]]*$2:[[:space:]]*//p"
}

window_geom_is() {
	[ "$(xwininfo_field "$1" Width)" = "$2" ] \
		&& [ "$(xwininfo_field "$1" Height)" = "$3" ]
}

wmaker_alive() {
	kill -0 "$WMAKER_PID" 2>/dev/null
}

no_internal_x_errors() {
	! grep -F 'warning: internal X error:' "$LOG" >/dev/null
}

move_client_to_desktop() {
	run_x wmctrl -i -r "$CLIENT_XID" -t "$1" >>"$LOG" 2>&1 || return 1
	wait_for 40 window_prop_has_values "$CLIENT_XID" _NET_WM_DESKTOP "$1"
}

set_client_state() {
	state_action=$1
	state_name=$2
	state_atom=$3
	run_x wmctrl -i -r "$CLIENT_XID" -b "$state_action,$state_name" >>"$LOG" 2>&1 || return 1
	wait_for 40 window_atom_has "$CLIENT_XID" _NET_WM_STATE "$state_atom"
}

clear_client_state() {
	state_action=$1
	state_name=$2
	state_atom=$3
	run_x wmctrl -i -r "$CLIENT_XID" -b "$state_action,$state_name" >>"$LOG" 2>&1 || return 1
	wait_for 40 window_atom_lacks "$CLIENT_XID" _NET_WM_STATE "$state_atom"
}

close_ewmh_client() {
	run_x wmctrl -i -c "$CLIENT_XID" >>"$LOG" 2>&1 || return 1
	wait_for 50 pid_gone "$CLIENT_PID"
}

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<'WMEOD'
{
  CycleWorkspaces = NO;
  LeftWorkspaceKey = "Control+Left";
  RightWorkspaceKey = "Control+Right";
  UpWorkspaceKey = "Control+Up";
  DownWorkspaceKey = "Control+Down";
  MoveToRightWorkspaceKey = "Control+Shift+Right";
  MoveToDownWorkspaceKey = "Control+Shift+Down";
  AutoFocusAcrossHeads = NO;
}
WMEOD
export WMAKER_USER_ROOT="$WMHOME"

if run_x xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set EWMH_LAYOUT_DRIVE_DISPLAY to a free one" >&2
	exit 1
fi

note "start Xephyr $DPY"
# two Xinerama heads side by side so the _NET_ACTIVE_WINDOW same-head
# gate can be exercised, head 0 is the original 1024x768 at 0,0 so the
# single-head checks are unaffected
Xephyr "$DPY" +xinerama -screen 1024x768 -screen 1024x768 -ac >>"$LOG" 2>&1 &
XEPHYR_PID=$!
echo "$XEPHYR_PID" >> "$PIDFILE"
xephyr_ready() {
	kill -0 "$XEPHYR_PID" 2>/dev/null || fatal "Xephyr exited early"
	run_x xdpyinfo >/dev/null 2>&1
}
wait_for 40 xephyr_ready || fatal "Xephyr failed to start"

note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
DISPLAY=$DPY \
"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
wm_ready() {
	root_prop _NET_SUPPORTING_WM_CHECK | grep 'window id' >/dev/null
}
wait_for 120 wm_ready || fatal "wmaker did not come up"

note "configure six desktops"
run_x wmctrl -n 6 >>"$LOG" 2>&1 || fatal "wmctrl -n failed"
wait_for 40 prop_has_values _NET_NUMBER_OF_DESKTOPS 6 || fatal "desktop count did not become 6"

note "root window metadata"
check "_NET_SUPPORTING_WM_CHECK root/window round-trip" supporting_wm_roundtrip
check "_NET_SUPPORTING_WM_CHECK names WindowMaker" supporting_wm_name
check "_NET_SUPPORTING_WM_CHECK exposes _NET_WM_PID" supporting_wm_pid
check "_NET_NUMBER_OF_DESKTOPS follows client request" prop_has_values _NET_NUMBER_OF_DESKTOPS 6
check "_NET_CURRENT_DESKTOP starts at desktop 0" desktop_is 0
check "_NET_DESKTOP_GEOMETRY matches Xephyr screen" prop_has_values _NET_DESKTOP_GEOMETRY "2048, 768"
check "_NET_DESKTOP_VIEWPORT has one origin pair per desktop" wait_for 40 cardinal_count_is _NET_DESKTOP_VIEWPORT 12
check "_NET_WORKAREA has one rectangle per desktop" wait_for 40 cardinal_count_is _NET_WORKAREA 24
check "_NET_DESKTOP_NAMES is present" prop_exists _NET_DESKTOP_NAMES
check "_NET_CLIENT_LIST is present" prop_exists _NET_CLIENT_LIST
check "_NET_CLIENT_LIST_STACKING is present" prop_exists _NET_CLIENT_LIST_STACKING
check "_NET_SHOWING_DESKTOP is present" prop_exists _NET_SHOWING_DESKTOP

run_x wmctrl -n 4 >>"$LOG" 2>&1
check "_NET_NUMBER_OF_DESKTOPS shrinks on request" wait_for 40 prop_has_values _NET_NUMBER_OF_DESKTOPS 4
check "_NET_DESKTOP_VIEWPORT shrinks with desktop count" wait_for 40 cardinal_count_is _NET_DESKTOP_VIEWPORT 8
check "_NET_WORKAREA shrinks with desktop count" wait_for 40 cardinal_count_is _NET_WORKAREA 16
run_x wmctrl -n 6 >>"$LOG" 2>&1
check "_NET_NUMBER_OF_DESKTOPS grows back on request" wait_for 40 prop_has_values _NET_NUMBER_OF_DESKTOPS 6

note "advertised EWMH atoms"
for atom in \
	_NET_CLIENT_LIST \
	_NET_CLIENT_LIST_STACKING \
	_NET_NUMBER_OF_DESKTOPS \
	_NET_DESKTOP_GEOMETRY \
	_NET_DESKTOP_VIEWPORT \
	_NET_CURRENT_DESKTOP \
	_NET_DESKTOP_NAMES \
	_NET_ACTIVE_WINDOW \
	_NET_WORKAREA \
	_NET_SUPPORTING_WM_CHECK \
	_NET_SHOWING_DESKTOP \
	_NET_DESKTOP_LAYOUT \
	_NET_MOVERESIZE_WINDOW \
	_NET_WM_MOVERESIZE \
	_NET_RESTACK_WINDOW \
	_NET_REQUEST_FRAME_EXTENTS \
	_NET_CLOSE_WINDOW \
	_NET_WM_DESKTOP \
	_NET_WM_WINDOW_TYPE \
	_NET_WM_WINDOW_TYPE_DESKTOP \
	_NET_WM_WINDOW_TYPE_DOCK \
	_NET_WM_WINDOW_TYPE_TOOLBAR \
	_NET_WM_WINDOW_TYPE_MENU \
	_NET_WM_WINDOW_TYPE_UTILITY \
	_NET_WM_WINDOW_TYPE_SPLASH \
	_NET_WM_WINDOW_TYPE_DIALOG \
	_NET_WM_WINDOW_TYPE_DROPDOWN_MENU \
	_NET_WM_WINDOW_TYPE_POPUP_MENU \
	_NET_WM_WINDOW_TYPE_TOOLTIP \
	_NET_WM_WINDOW_TYPE_NOTIFICATION \
	_NET_WM_WINDOW_TYPE_COMBO \
	_NET_WM_WINDOW_TYPE_DND \
	_NET_WM_WINDOW_TYPE_NORMAL \
	_NET_WM_STATE \
	_NET_WM_STATE_MODAL \
	_NET_WM_STATE_STICKY \
	_NET_WM_STATE_SHADED \
	_NET_WM_STATE_MAXIMIZED_HORZ \
	_NET_WM_STATE_MAXIMIZED_VERT \
	_NET_WM_STATE_SKIP_TASKBAR \
	_NET_WM_STATE_SKIP_PAGER \
	_NET_WM_STATE_HIDDEN \
	_NET_WM_STATE_FULLSCREEN \
	_NET_WM_STATE_ABOVE \
	_NET_WM_STATE_BELOW \
	_NET_WM_STATE_FOCUSED \
	_NET_WM_STATE_DEMANDS_ATTENTION \
	_NET_WM_ALLOWED_ACTIONS \
	_NET_WM_ACTION_MOVE \
	_NET_WM_ACTION_RESIZE \
	_NET_WM_ACTION_MINIMIZE \
	_NET_WM_ACTION_SHADE \
	_NET_WM_ACTION_STICK \
	_NET_WM_ACTION_MAXIMIZE_HORZ \
	_NET_WM_ACTION_MAXIMIZE_VERT \
	_NET_WM_ACTION_FULLSCREEN \
	_NET_WM_ACTION_CHANGE_DESKTOP \
	_NET_WM_ACTION_CLOSE \
	_NET_WM_ACTION_ABOVE \
	_NET_WM_ACTION_BELOW \
	_NET_WM_PID \
	_NET_WM_PING \
	_NET_WM_USER_TIME \
	_NET_WM_USER_TIME_WINDOW \
	_NET_WM_FULL_PLACEMENT \
	_NET_WM_STRUT \
	_NET_WM_STRUT_PARTIAL \
	_NET_WM_ICON_GEOMETRY \
	_NET_WM_ICON \
	_NET_WM_HANDLED_ICONS \
	_NET_WM_WINDOW_OPACITY \
	_NET_FRAME_EXTENTS \
	_NET_WM_NAME \
	_NET_WM_VISIBLE_NAME \
	_NET_WM_ICON_NAME \
	_NET_WM_VISIBLE_ICON_NAME
do
	check "advertises $atom" supported_atom "$atom"
done

check "advertises _NET_WM_FULLSCREEN_MONITORS" supported_atom _NET_WM_FULLSCREEN_MONITORS

if config_defines USE_XSYNC && xsync_available; then
	check "advertises _NET_WM_SYNC_REQUEST when XSync is built and available" supported_atom _NET_WM_SYNC_REQUEST
	check "advertises _NET_WM_SYNC_REQUEST_COUNTER when XSync is built and available" supported_atom _NET_WM_SYNC_REQUEST_COUNTER
else
	record_skip "XSync support not built or server extension unavailable; sync-request atoms not expected"
fi

CLIENT_BUILT=0
if build_ewmh_client; then
	CLIENT_BUILT=1
fi

note "_NET_DESKTOP_NAMES inbound"
if [ "$CLIENT_BUILT" -eq 1 ]; then
	set_desktop_names "EWMH-Web,EWMH-Mail"
	check "_NET_DESKTOP_NAMES renames workspace 0" \
		wait_for 40 desktop_name_is 0 "EWMH-Web"
	check "_NET_DESKTOP_NAMES renames workspace 1" \
		wait_for 40 desktop_name_is 1 "EWMH-Mail"
else
	record_skip "EWMH client unavailable; _NET_DESKTOP_NAMES checks skipped"
fi

note "_NET_REQUEST_FRAME_EXTENTS pre-map"
if [ "$CLIENT_BUILT" -eq 1 ] \
   && start_second_client EWMH_CLIENT_NAME=frame-extents EWMH_CLIENT_REQUEST_FRAME_EXTENTS=1; then
	check "unmapped client receives _NET_FRAME_EXTENTS estimate" wait_for 40 window_cardinal_count_is "$CLIENT2_XID" _NET_FRAME_EXTENTS 4
	check "frame-extents request does not manage unmapped client" wait_for 20 root_window_list_lacks _NET_CLIENT_LIST "$CLIENT2_XID"
	stop_second_client
elif [ "$CLIENT_BUILT" -eq 1 ]; then
	record_fail "could not launch frame-extents probe client"
else
	record_skip "EWMH client unavailable; frame-extents checks skipped"
fi

note "managed X11 client"
if [ "$CLIENT_BUILT" -eq 1 ]; then
	if set_desktop 0 && start_ewmh_client; then
		record_pass "launches deterministic X11 client"
		check "client appears in _NET_CLIENT_LIST" wait_for 40 root_window_list_has _NET_CLIENT_LIST "$CLIENT_XID"
		check "client appears in _NET_CLIENT_LIST_STACKING" wait_for 40 root_window_list_has _NET_CLIENT_LIST_STACKING "$CLIENT_XID"
		check "client has _NET_WM_WINDOW_TYPE_NORMAL" wait_for 40 window_atom_has "$CLIENT_XID" _NET_WM_WINDOW_TYPE _NET_WM_WINDOW_TYPE_NORMAL
		check "client exposes _NET_WM_PID" wait_for 40 window_prop_exists "$CLIENT_XID" _NET_WM_PID
		check "client exposes WM_CLIENT_MACHINE" wait_for 40 window_prop_exists "$CLIENT_XID" WM_CLIENT_MACHINE
		check "client exposes _NET_WM_USER_TIME" wait_for 40 window_prop_exists "$CLIENT_XID" _NET_WM_USER_TIME
		check "WM writes _NET_WM_DESKTOP for client" wait_for 40 window_prop_has_values "$CLIENT_XID" _NET_WM_DESKTOP 0
		check "WM writes _NET_WM_STATE for client" wait_for 40 window_prop_exists "$CLIENT_XID" _NET_WM_STATE
		check "WM writes _NET_FRAME_EXTENTS for client" wait_for 40 window_cardinal_count_is "$CLIENT_XID" _NET_FRAME_EXTENTS 4
		for action in \
			_NET_WM_ACTION_MOVE \
			_NET_WM_ACTION_RESIZE \
			_NET_WM_ACTION_MINIMIZE \
			_NET_WM_ACTION_SHADE \
			_NET_WM_ACTION_STICK \
			_NET_WM_ACTION_MAXIMIZE_HORZ \
			_NET_WM_ACTION_MAXIMIZE_VERT \
			_NET_WM_ACTION_FULLSCREEN \
			_NET_WM_ACTION_CHANGE_DESKTOP \
			_NET_WM_ACTION_CLOSE \
			_NET_WM_ACTION_ABOVE \
			_NET_WM_ACTION_BELOW
		do
			check "WM allowed-actions includes $action" wait_for 40 window_atom_has "$CLIENT_XID" _NET_WM_ALLOWED_ACTIONS "$action"
		done
		check "honors _NET_WM_DESKTOP client request" move_client_to_desktop 2
		check "honors _NET_WM_STATE_ABOVE add request" set_client_state add above _NET_WM_STATE_ABOVE
		check "honors _NET_WM_STATE_ABOVE remove request" clear_client_state remove above _NET_WM_STATE_ABOVE
		check "honors _NET_WM_STATE_BELOW add request" set_client_state add below _NET_WM_STATE_BELOW
		check "honors _NET_WM_STATE_BELOW remove request" clear_client_state remove below _NET_WM_STATE_BELOW
		check "honors _NET_WM_STATE_SKIP_TASKBAR add request" set_client_state add skip_taskbar _NET_WM_STATE_SKIP_TASKBAR
		check "honors _NET_WM_STATE_SKIP_TASKBAR remove request" clear_client_state remove skip_taskbar _NET_WM_STATE_SKIP_TASKBAR
		check "honors _NET_WM_STATE_FULLSCREEN add request" set_client_state add fullscreen _NET_WM_STATE_FULLSCREEN
		check "honors _NET_WM_STATE_FULLSCREEN remove request" clear_client_state remove fullscreen _NET_WM_STATE_FULLSCREEN

		# _NET_WM_FULLSCREEN_MONITORS cycle on a plain client, the WM
		# alone must restore the original 320x180 from bfs_geometry
		# after the cycle, retargeting must not clobber it
		if [ "$(xinerama_head_count)" -ge 2 ] \
		   && start_second_client EWMH_CLIENT_NAME=fsm-cycle; then
			run_x wmctrl -i -r "$CLIENT2_XID" -b add,fullscreen >>"$LOG" 2>&1
			wait_for 30 window_atom_has "$CLIENT2_XID" _NET_WM_STATE _NET_WM_STATE_FULLSCREEN >/dev/null

			send_fsmonitors "$CLIENT2_XID" 0 0 0 1
			check "_NET_WM_FULLSCREEN_MONITORS span 0-1: width covers both heads" \
				wait_for 30 window_geom_is "$CLIENT2_XID" 2048 768
			check "_NET_WM_FULLSCREEN_MONITORS span 0-1: at root origin" \
				window_at "$CLIENT2_XID" 0 0
			check "_NET_WM_FULLSCREEN_MONITORS span 0-1: property echoed" \
				wait_for 30 window_prop_has_values "$CLIENT2_XID" _NET_WM_FULLSCREEN_MONITORS "0, 0, 0, 1"

			send_fsmonitors "$CLIENT2_XID" 0 0 0 0
			check "_NET_WM_FULLSCREEN_MONITORS head 0 only: head-sized" \
				wait_for 30 window_geom_is "$CLIENT2_XID" 1024 768
			check "_NET_WM_FULLSCREEN_MONITORS head 0 only: at head 0 origin" \
				window_at "$CLIENT2_XID" 0 0

			send_fsmonitors "$CLIENT2_XID" 1 1 1 1
			check "_NET_WM_FULLSCREEN_MONITORS head 1 only: head-sized" \
				wait_for 30 window_geom_is "$CLIENT2_XID" 1024 768
			check "_NET_WM_FULLSCREEN_MONITORS head 1 only: at head 1 origin" \
				window_at "$CLIENT2_XID" 1024 0

			send_fsmonitors "$CLIENT2_XID" 0 0 0 1
			check "_NET_WM_FULLSCREEN_MONITORS span again: width covers both heads" \
				wait_for 30 window_geom_is "$CLIENT2_XID" 2048 768

			run_x wmctrl -i -r "$CLIENT2_XID" -b remove,fullscreen >>"$LOG" 2>&1
			check "unfullscreen after monitor cycle drops _NET_WM_STATE_FULLSCREEN" \
				wait_for 30 window_atom_lacks "$CLIENT2_XID" _NET_WM_STATE _NET_WM_STATE_FULLSCREEN
			check "unfullscreen after monitor cycle restores pre-fullscreen size" \
				wait_for 30 window_geom_is "$CLIENT2_XID" 320 180
			check "unfullscreen after monitor cycle restores onto head 0" \
				window_on_head0 "$CLIENT2_XID"

			stop_second_client

			# second pass with a client that forces its own
			# distinct geometry on the FULLSCREEN falling edge,
			# the WM must honour the racing ConfigureRequest
			if start_second_client EWMH_CLIENT_NAME=fsm-restore \
					       EWMH_CLIENT_RESTORE_ON_UNFULLSCREEN="400 240"; then
				run_x wmctrl -i -r "$CLIENT2_XID" -b add,fullscreen >>"$LOG" 2>&1
				wait_for 30 window_atom_has "$CLIENT2_XID" _NET_WM_STATE _NET_WM_STATE_FULLSCREEN >/dev/null
				send_fsmonitors "$CLIENT2_XID" 0 0 0 1
				wait_for 30 window_geom_is "$CLIENT2_XID" 2048 768 >/dev/null

				run_x wmctrl -i -r "$CLIENT2_XID" -b remove,fullscreen >>"$LOG" 2>&1
				check "client resize on unfullscreen overrides the WM restore" \
					wait_for 30 window_geom_is "$CLIENT2_XID" 400 240
				check "client resize on unfullscreen is reported back" \
					wait_for 30 sh -c "grep -q '^RESTORED 400x240\$' '$CLIENT2_XID_FILE'"

				stop_second_client
			else
				record_skip "client-forced unfullscreen resize: second client failed"
			fi
		else
			record_skip "_NET_WM_FULLSCREEN_MONITORS cycle: only one head or no second client"
		fi

		check "honors _NET_WM_STATE_MAXIMIZED_VERT add request" set_client_state add maximized_vert _NET_WM_STATE_MAXIMIZED_VERT
		check "honors _NET_WM_STATE_MAXIMIZED_HORZ add request" set_client_state add maximized_horz _NET_WM_STATE_MAXIMIZED_HORZ
		check "honors _NET_WM_STATE_MAXIMIZED_VERT remove request" clear_client_state remove maximized_vert,maximized_horz _NET_WM_STATE_MAXIMIZED_VERT
		check "honors _NET_WM_STATE_MAXIMIZED_HORZ remove request" wait_for 40 window_atom_lacks "$CLIENT_XID" _NET_WM_STATE _NET_WM_STATE_MAXIMIZED_HORZ
		check "honors _NET_WM_STATE_SHADED add request" set_client_state add shaded _NET_WM_STATE_SHADED
		check "honors _NET_WM_STATE_SHADED remove request" clear_client_state remove shaded _NET_WM_STATE_SHADED
		check "honors _NET_WM_STATE_SKIP_PAGER add request" set_client_state add skip_pager _NET_WM_STATE_SKIP_PAGER
		check "honors _NET_WM_STATE_SKIP_PAGER remove request" clear_client_state remove skip_pager _NET_WM_STATE_SKIP_PAGER
		check "honors _NET_WM_STATE_STICKY add request" set_client_state add sticky _NET_WM_STATE_STICKY
		check "sticky window has _NET_WM_DESKTOP = all" wait_for 40 window_prop_has_values "$CLIENT_XID" _NET_WM_DESKTOP 4294967295
		check "honors _NET_WM_STATE_STICKY remove request" clear_client_state remove sticky _NET_WM_STATE_STICKY

		# bring it back to desktop 0 so it can take focus
		move_client_to_desktop 0 >/dev/null
		check "honors _NET_ACTIVE_WINDOW request" sh -c "
			DISPLAY=$DPY wmctrl -i -a '$CLIENT_XID' >>'$LOG' 2>&1 || exit 1
			i=40; while [ \$i -gt 0 ]; do
				DISPLAY=$DPY xprop -root _NET_ACTIVE_WINDOW 2>/dev/null \
					| grep -iF '$CLIENT_XID' >/dev/null && exit 0
				sleep 0.1; i=\$((i-1))
			done; exit 1"

		# _NET_ACTIVE_WINDOW source-indication policy. Spawn a second
		# client with a high _NET_WM_USER_TIME so the screen baseline
		# (net_last_user_time) sits above the stale timestamps below.
		if start_second_client EWMH_CLIENT_NAME=aw-policy \
				       EWMH_CLIENT_USER_TIME=50000; then
			wait_for 30 active_window_is "$CLIENT2_XID" >/dev/null

			# cross-workspace target, source != 2 -> deny + DA
			move_client_to_desktop 1 >/dev/null
			set_desktop 0 >/dev/null
			run_x wmctrl -i -a "$CLIENT2_XID" >>"$LOG" 2>&1
			wait_for 30 active_window_is "$CLIENT2_XID" >/dev/null

			send_active_window "$CLIENT_XID" 0 0
			check "_NET_ACTIVE_WINDOW source 0 cross-workspace is refused" \
				wait_for 30 window_atom_has "$CLIENT_XID" _NET_WM_STATE _NET_WM_STATE_DEMANDS_ATTENTION
			check "_NET_ACTIVE_WINDOW source 0 cross-workspace stays put" desktop_is 0
			run_x wmctrl -i -a "$CLIENT_XID" >>"$LOG" 2>&1
			wait_for 30 window_atom_lacks "$CLIENT_XID" _NET_WM_STATE _NET_WM_STATE_DEMANDS_ATTENTION >/dev/null
			set_desktop 0 >/dev/null
			run_x wmctrl -i -a "$CLIENT2_XID" >>"$LOG" 2>&1
			wait_for 30 active_window_is "$CLIENT2_XID" >/dev/null

			send_active_window "$CLIENT_XID" 99 0
			check "_NET_ACTIVE_WINDOW invalid source cross-workspace is refused" \
				wait_for 30 window_atom_has "$CLIENT_XID" _NET_WM_STATE _NET_WM_STATE_DEMANDS_ATTENTION
			check "_NET_ACTIVE_WINDOW invalid source cross-workspace stays put" desktop_is 0
			run_x wmctrl -i -a "$CLIENT_XID" >>"$LOG" 2>&1
			wait_for 30 window_atom_lacks "$CLIENT_XID" _NET_WM_STATE _NET_WM_STATE_DEMANDS_ATTENTION >/dev/null
			set_desktop 0 >/dev/null
			run_x wmctrl -i -a "$CLIENT2_XID" >>"$LOG" 2>&1
			wait_for 30 active_window_is "$CLIENT2_XID" >/dev/null

			send_active_window "$CLIENT_XID" 2 0
			check "_NET_ACTIVE_WINDOW source 2 cross-workspace switches" \
				wait_for 30 desktop_is 1
			check "_NET_ACTIVE_WINDOW source 2 cross-workspace activates target" \
				wait_for 30 active_window_is "$CLIENT_XID"

			# same-workspace target, stale vs fresh timestamp
			move_client_to_desktop 0 >/dev/null
			set_desktop 0 >/dev/null
			run_x wmctrl -i -a "$CLIENT2_XID" >>"$LOG" 2>&1
			wait_for 30 active_window_is "$CLIENT2_XID" >/dev/null

			send_active_window "$CLIENT_XID" 1 100
			check "_NET_ACTIVE_WINDOW source 1 stale timestamp is refused" \
				wait_for 30 window_atom_has "$CLIENT_XID" _NET_WM_STATE _NET_WM_STATE_DEMANDS_ATTENTION
			check "_NET_ACTIVE_WINDOW source 1 stale timestamp keeps focus" \
				active_window_is "$CLIENT2_XID"
			run_x wmctrl -i -a "$CLIENT_XID" >>"$LOG" 2>&1
			wait_for 30 window_atom_lacks "$CLIENT_XID" _NET_WM_STATE _NET_WM_STATE_DEMANDS_ATTENTION >/dev/null
			run_x wmctrl -i -a "$CLIENT2_XID" >>"$LOG" 2>&1
			wait_for 30 active_window_is "$CLIENT2_XID" >/dev/null

			send_active_window "$CLIENT_XID" 1 90000
			check "_NET_ACTIVE_WINDOW source 1 fresh timestamp activates" \
				wait_for 30 active_window_is "$CLIENT_XID"

			# the 90000 above should have advanced the baseline,
			# 60000 is now stale even though it beats 50000
			send_active_window "$CLIENT2_XID" 1 60000
			check "_NET_ACTIVE_WINDOW honoured timestamp advances baseline" \
				wait_for 30 window_atom_has "$CLIENT2_XID" _NET_WM_STATE _NET_WM_STATE_DEMANDS_ATTENTION
			check "_NET_ACTIVE_WINDOW now-stale request keeps focus" \
				active_window_is "$CLIENT_XID"
			run_x wmctrl -i -a "$CLIENT2_XID" >>"$LOG" 2>&1
			wait_for 30 window_atom_lacks "$CLIENT2_XID" _NET_WM_STATE _NET_WM_STATE_DEMANDS_ATTENTION >/dev/null
			run_x wmctrl -i -a "$CLIENT_XID" >>"$LOG" 2>&1
			wait_for 30 active_window_is "$CLIENT_XID" >/dev/null

			send_active_window "$CLIENT2_XID" 2 100
			check "_NET_ACTIVE_WINDOW source 2 ignores stale timestamp" \
				wait_for 30 active_window_is "$CLIENT2_XID"

			# cross-head gate, AutoFocusAcrossHeads=NO is set in
			# the test defaults so a non-pager activate of a
			# window on the other head must be refused
			if [ "$(xinerama_head_count)" -ge 2 ]; then
				# wmctrl -a may use source 0, which the policy
				# refuses cross-head, so setup activations here
				# go through send_active_window with source 2
				run_x wmctrl -i -r "$CLIENT2_XID" -e 0,1100,100,320,180 >>"$LOG" 2>&1
				wait_for 30 window_on_head1 "$CLIENT2_XID" >/dev/null
				send_active_window "$CLIENT_XID" 2 0
				wait_for 30 active_window_is "$CLIENT_XID" >/dev/null

				send_active_window "$CLIENT2_XID" 1 0
				check "_NET_ACTIVE_WINDOW source 1 cross-head is refused" \
					wait_for 30 window_atom_has "$CLIENT2_XID" _NET_WM_STATE _NET_WM_STATE_DEMANDS_ATTENTION
				check "_NET_ACTIVE_WINDOW source 1 cross-head keeps focus" \
					active_window_is "$CLIENT_XID"
				send_active_window "$CLIENT2_XID" 2 0
				wait_for 30 window_atom_lacks "$CLIENT2_XID" _NET_WM_STATE _NET_WM_STATE_DEMANDS_ATTENTION >/dev/null
				send_active_window "$CLIENT_XID" 2 0
				wait_for 30 active_window_is "$CLIENT_XID" >/dev/null

				send_active_window "$CLIENT2_XID" 2 0
				check "_NET_ACTIVE_WINDOW source 2 cross-head activates" \
					wait_for 30 active_window_is "$CLIENT2_XID"

				run_x wmctrl -i -r "$CLIENT2_XID" -e 0,80,80,320,180 >>"$LOG" 2>&1
			else
				record_skip "_NET_ACTIVE_WINDOW cross-head: only one Xinerama head"
			fi

			stop_second_client
			run_x wmctrl -i -a "$CLIENT_XID" >>"$LOG" 2>&1
			wait_for 30 active_window_is "$CLIENT_XID" >/dev/null
		else
			record_skip "_NET_ACTIVE_WINDOW source policy: second client failed"
		fi

		# DEMANDS_ATTENTION is refused on a focused window (per spec
		# the WM clears it once the window has the user's attention),
		# so move it off-screen first, set the demand, then activate
		# and verify it is cleared. wmctrl -b does not know
		# demands_attention (see send_wm_state), use the in-tree
		# client to send the request by atom name.
		move_client_to_desktop 1 >/dev/null
		set_desktop 0 >/dev/null
		send_wm_state "$CLIENT_XID" 1 _NET_WM_STATE_DEMANDS_ATTENTION
		check "honors _NET_WM_STATE_DEMANDS_ATTENTION add on unfocused window" \
			wait_for 40 window_atom_has "$CLIENT_XID" _NET_WM_STATE _NET_WM_STATE_DEMANDS_ATTENTION
		run_x wmctrl -i -a "$CLIENT_XID" >>"$LOG" 2>&1
		check "clears _NET_WM_STATE_DEMANDS_ATTENTION on focus" wait_for 40 window_atom_lacks "$CLIENT_XID" _NET_WM_STATE _NET_WM_STATE_DEMANDS_ATTENTION
		move_client_to_desktop 0 >/dev/null

		check "honors _NET_MOVERESIZE_WINDOW request" sh -c "
			DISPLAY=$DPY wmctrl -i -r '$CLIENT_XID' -e 0,100,100,400,300 >>'$LOG' 2>&1 || exit 1
			i=40; while [ \$i -gt 0 ]; do
				w=\$(DISPLAY=$DPY xwininfo -id '$CLIENT_XID' 2>/dev/null | sed -n 's/^[[:space:]]*Width:[[:space:]]*//p')
				h=\$(DISPLAY=$DPY xwininfo -id '$CLIENT_XID' 2>/dev/null | sed -n 's/^[[:space:]]*Height:[[:space:]]*//p')
				[ \"\$w\" = 400 ] && [ \"\$h\" = 300 ] && exit 0
				sleep 0.1; i=\$((i-1))
			done; exit 1"

		# minimise -> _NET_WM_STATE_HIDDEN, activate -> cleared
		run_x xdotool windowminimize "$CLIENT_XID" >>"$LOG" 2>&1
		check "minimised window has _NET_WM_STATE_HIDDEN" wait_for 40 window_atom_has "$CLIENT_XID" _NET_WM_STATE _NET_WM_STATE_HIDDEN
		run_x wmctrl -i -a "$CLIENT_XID" >>"$LOG" 2>&1
		check "activating clears _NET_WM_STATE_HIDDEN" wait_for 40 window_atom_lacks "$CLIENT_XID" _NET_WM_STATE _NET_WM_STATE_HIDDEN

		# MoveTo{Right,Down}WorkspaceKey directional move-window keybinds
		set_layout 0 3 2 0 >/dev/null
		set_desktop 0 >/dev/null
		move_client_to_desktop 0 >/dev/null
		run_x wmctrl -i -a "$CLIENT_XID" >>"$LOG" 2>&1
		wait_for 30 active_window_is "$CLIENT_XID" >/dev/null
		press_key Control+Shift+Right
		check "MoveToRightWorkspaceKey moves window to grid neighbour" wait_for 40 window_prop_has_values "$CLIENT_XID" _NET_WM_DESKTOP 1
		set_desktop 1 >/dev/null
		run_x wmctrl -i -a "$CLIENT_XID" >>"$LOG" 2>&1
		wait_for 30 active_window_is "$CLIENT_XID" >/dev/null
		press_key Control+Shift+Down
		check "MoveToDownWorkspaceKey moves window to grid neighbour" wait_for 40 window_prop_has_values "$CLIENT_XID" _NET_WM_DESKTOP 4
		move_client_to_desktop 0 >/dev/null
		set_desktop 0 >/dev/null

		# format-32 enforcement: a format-8 _NET_CLOSE_WINDOW must be
		# ignored, the client stays managed and alive
		send_format8_close "$CLIENT_XID"
		sleep 0.3
		check "ignores format-8 _NET_CLOSE_WINDOW (client survives)" kill -0 "$CLIENT_PID"
		check "ignores format-8 _NET_CLOSE_WINDOW (still in client list)" root_window_list_has _NET_CLIENT_LIST "$CLIENT_XID"

		check "delivers close request to managed client" close_ewmh_client
	else
		record_fail "launches deterministic X11 client"
	fi

	note "_NET_WM_USER_TIME focus-stealing prevention"
	if set_desktop 0 && start_ewmh_client; then
		# give the first client a recent user-activity stamp so the
		# screen-wide last_user_time is non-zero
		run_x xprop -id "$CLIENT_XID" -f _NET_WM_USER_TIME 32c \
			-set _NET_WM_USER_TIME 5000 >>"$LOG" 2>&1
		run_x wmctrl -i -a "$CLIENT_XID" >>"$LOG" 2>&1
		wait_for 30 active_window_is "$CLIENT_XID" >/dev/null
		if start_second_client EWMH_CLIENT_NAME=stale EWMH_CLIENT_USER_TIME=0; then
			check "second client is managed" wait_for 40 root_window_list_has _NET_CLIENT_LIST "$CLIENT2_XID"
			check "user_time=0 window does not steal focus" wait_for 20 active_window_is "$CLIENT_XID"
			check "denied window gets _NET_WM_STATE_DEMANDS_ATTENTION" wait_for 40 window_atom_has "$CLIENT2_XID" _NET_WM_STATE _NET_WM_STATE_DEMANDS_ATTENTION
			stop_second_client
		else
			record_fail "could not launch second client for user-time test"
		fi
		close_ewmh_client >/dev/null
	else
		record_skip "could not relaunch client for user-time test"
	fi

	note "_NET_WM_USER_TIME focus-stealing exemptions"
	if set_desktop 0 && start_ewmh_client; then
		# baseline is already >= 90000 from the _NET_ACTIVE_WINDOW
		# checks above so 100 is a deliberately stale stamp, the
		# transient and the higher-layer window must take focus
		# anyway and the unrelated normal-layer window must not
		run_x wmctrl -i -a "$CLIENT_XID" >>"$LOG" 2>&1
		wait_for 30 active_window_is "$CLIENT_XID" >/dev/null

		if start_second_client EWMH_CLIENT_NAME=transient-stale \
				EWMH_CLIENT_CLASS=EwmhDriveTransient \
				EWMH_CLIENT_TRANSIENT_FOR="$CLIENT_XID" \
				EWMH_CLIENT_USER_TIME=100; then
			check "stale-stamped transient of focused window still takes focus" \
				wait_for 40 active_window_is "$CLIENT2_XID"
			check "transient bypass does not flag _NET_WM_STATE_DEMANDS_ATTENTION" \
				wait_for 20 window_atom_lacks "$CLIENT2_XID" _NET_WM_STATE _NET_WM_STATE_DEMANDS_ATTENTION
			stop_second_client
		else
			record_fail "could not launch transient-stale client"
		fi

		run_x wmctrl -i -a "$CLIENT_XID" >>"$LOG" 2>&1
		wait_for 30 active_window_is "$CLIENT_XID" >/dev/null
		if start_second_client EWMH_CLIENT_NAME=above-stale \
				EWMH_CLIENT_CLASS=EwmhDriveAbove \
				EWMH_CLIENT_STATE_ABOVE=1 \
				EWMH_CLIENT_USER_TIME=100; then
			check "stale-stamped _NET_WM_STATE_ABOVE window still takes focus" \
				wait_for 40 active_window_is "$CLIENT2_XID"
			stop_second_client
		else
			record_fail "could not launch above-stale client"
		fi

		# negative control, an unrelated normal-layer window with the
		# same stale stamp is still denied so the exemptions above
		# are the reason the previous two passed
		run_x wmctrl -i -a "$CLIENT_XID" >>"$LOG" 2>&1
		wait_for 30 active_window_is "$CLIENT_XID" >/dev/null
		if start_second_client EWMH_CLIENT_NAME=unrelated-stale \
				EWMH_CLIENT_CLASS=EwmhDriveUnrelated \
				EWMH_CLIENT_USER_TIME=100; then
			check "stale-stamped unrelated window is still denied" \
				wait_for 30 active_window_is "$CLIENT_XID"
			check "unrelated denied window gets _NET_WM_STATE_DEMANDS_ATTENTION" \
				wait_for 40 window_atom_has "$CLIENT2_XID" _NET_WM_STATE _NET_WM_STATE_DEMANDS_ATTENTION
			stop_second_client
		else
			record_fail "could not launch unrelated-stale client"
		fi
		close_ewmh_client >/dev/null
	else
		record_skip "could not relaunch client for focus-stealing exemption test"
	fi

	note "accepted _NET_WM_USER_TIME_WINDOW helper"
	if set_desktop 0 && start_user_time_helper_client; then
		check "client advertises safe _NET_WM_USER_TIME_WINDOW helper" wait_for 40 window_refers_to "$CLIENT_XID" _NET_WM_USER_TIME_WINDOW "$CLIENT_HELPER_XID"
		run_x wmctrl -i -a "$CLIENT_XID" >>"$LOG" 2>&1
		wait_for 30 active_window_is "$CLIENT_XID" >/dev/null
		run_x xprop -id "$CLIENT_HELPER_XID" -f _NET_WM_USER_TIME 32c \
			-set _NET_WM_USER_TIME 9000 >>"$LOG" 2>&1
		if start_second_client EWMH_CLIENT_NAME=helper-stale EWMH_CLIENT_USER_TIME=8500; then
			check "helper user-time update prevents stale focus steal" wait_for 40 active_window_is "$CLIENT_XID"
			check "helper-denied window gets _NET_WM_STATE_DEMANDS_ATTENTION" wait_for 40 window_atom_has "$CLIENT2_XID" _NET_WM_STATE _NET_WM_STATE_DEMANDS_ATTENTION
			stop_second_client
		else
			record_fail "could not launch stale client for helper user-time test"
		fi
		close_ewmh_client >/dev/null
	else
		record_skip "could not launch helper user-time client"
	fi

	note "_NET_STARTUP_ID feeds the user-time focus decision"
	if ! config_defines HAVE_STARTUP_NOTIFICATION; then
		record_skip "_NET_STARTUP_ID monitor (built --without-startup-notification)"
	elif set_desktop 0 && start_ewmh_client; then
		# timestamps here sit above any earlier _NET_ACTIVE_WINDOW
		# checks so this block owns scr->net_last_user_time

		# 1 - stale launch denied: announce a sequence at T=200000 then
		#     advance the focused window to T=200500 before the new
		#     client maps with the matching startup id, focus stays put
		run_x xprop -id "$CLIENT_XID" -f _NET_WM_USER_TIME 32c \
			-set _NET_WM_USER_TIME 200000 >>"$LOG" 2>&1
		run_x wmctrl -i -a "$CLIENT_XID" >>"$LOG" 2>&1
		wait_for 30 active_window_is "$CLIENT_XID" >/dev/null
		DISPLAY=$DPY EWMH_CLIENT_SN_BROADCAST="ewmh-drive/sn1_TIME200000 200000 0" \
			"$CLIENT_BIN" >>"$LOG" 2>&1
		run_x xprop -id "$CLIENT_XID" -f _NET_WM_USER_TIME 32c \
			-set _NET_WM_USER_TIME 200500 >>"$LOG" 2>&1
		sleep 0.2
		if start_second_client EWMH_CLIENT_NAME=sn-stale \
				EWMH_CLIENT_STARTUP_ID="ewmh-drive/sn1_TIME200000"; then
			check "_NET_STARTUP_ID client managed" wait_for 40 root_window_list_has _NET_CLIENT_LIST "$CLIENT2_XID"
			check "stale-launch _NET_STARTUP_ID does not steal focus" wait_for 30 active_window_is "$CLIENT_XID"
			check "stale-launch _NET_STARTUP_ID gets _NET_WM_STATE_DEMANDS_ATTENTION" \
				wait_for 40 window_atom_has "$CLIENT2_XID" _NET_WM_STATE _NET_WM_STATE_DEMANDS_ATTENTION
			stop_second_client
		else
			record_fail "could not launch sn-stale client"
		fi

		# 2 - fresh launch granted: announce at T=201000, no further
		#     interaction, the new client should take focus because
		#     its launch time is the freshest activity on screen
		DISPLAY=$DPY EWMH_CLIENT_SN_BROADCAST="ewmh-drive/sn2_TIME201000 201000 0" \
			"$CLIENT_BIN" >>"$LOG" 2>&1
		sleep 0.2
		if start_second_client EWMH_CLIENT_NAME=sn-fresh \
				EWMH_CLIENT_STARTUP_ID="ewmh-drive/sn2_TIME201000"; then
			check "fresh-launch _NET_STARTUP_ID takes focus" wait_for 40 active_window_is "$CLIENT2_XID"
			stop_second_client
		else
			record_fail "could not launch sn-fresh client"
		fi

		# 3 - workspace placement: announce on workspace 0, switch to
		#     workspace 1, then map, the client should land on 0
		run_x wmctrl -i -a "$CLIENT_XID" >>"$LOG" 2>&1
		wait_for 30 active_window_is "$CLIENT_XID" >/dev/null
		DISPLAY=$DPY EWMH_CLIENT_SN_BROADCAST="ewmh-drive/sn3_TIME202000 202000 0" \
			"$CLIENT_BIN" >>"$LOG" 2>&1
		set_desktop 1
		if start_second_client EWMH_CLIENT_NAME=sn-ws \
				EWMH_CLIENT_STARTUP_ID="ewmh-drive/sn3_TIME202000"; then
			check "_NET_STARTUP_ID DESKTOP places on launch workspace" \
				wait_for 40 window_prop_has_values "$CLIENT2_XID" _NET_WM_DESKTOP 0
			stop_second_client
		else
			record_fail "could not launch sn-ws client"
		fi
		set_desktop 0

		# 4 - abandoned-sequence cleanup proxy, broadcast a sequence
		#     that never maps, send remove: (same pending_remove()
		#     teardown the 20 s timeout ends in), then confirm a
		#     fresh sequence still matches and grants focus, also
		#     covers that the orphan id is no longer matchable
		run_x wmctrl -i -a "$CLIENT_XID" >>"$LOG" 2>&1
		wait_for 30 active_window_is "$CLIENT_XID" >/dev/null
		DISPLAY=$DPY EWMH_CLIENT_SN_BROADCAST="ewmh-drive/sn-orphan_TIME203000 203000 2" \
			"$CLIENT_BIN" >>"$LOG" 2>&1
		sleep 0.2
		DISPLAY=$DPY EWMH_CLIENT_SN_REMOVE="ewmh-drive/sn-orphan_TIME203000" \
			"$CLIENT_BIN" >>"$LOG" 2>&1
		sleep 0.2
		DISPLAY=$DPY EWMH_CLIENT_SN_BROADCAST="ewmh-drive/sn-after_TIME204000 204000 0" \
			"$CLIENT_BIN" >>"$LOG" 2>&1
		sleep 0.2
		if start_second_client EWMH_CLIENT_NAME=sn-orphan-late \
				EWMH_CLIENT_STARTUP_ID="ewmh-drive/sn-orphan_TIME203000"; then
			# orphan said DESKTOP=2, if the removed sequence were
			# still matchable this client would land on workspace 2,
			# landing on the current workspace 0 proves the entry
			# was torn down by the COMPLETED path
			check "removed sequence is no longer matchable" \
				wait_for 40 window_prop_has_values "$CLIENT2_XID" _NET_WM_DESKTOP 0
			stop_second_client
		else
			record_fail "could not launch sn-orphan-late client"
		fi
		if start_second_client EWMH_CLIENT_NAME=sn-after \
				EWMH_CLIENT_STARTUP_ID="ewmh-drive/sn-after_TIME204000"; then
			check "fresh sequence after abandoned-cleanup still grants focus" \
				wait_for 40 active_window_is "$CLIENT2_XID"
			stop_second_client
		else
			record_fail "could not launch sn-after client"
		fi
		close_ewmh_client >/dev/null
	else
		record_skip "could not relaunch client for _NET_STARTUP_ID test"
	fi

	note "_NET_WM_STRUT_PARTIAL updates _NET_WORKAREA"
	if set_desktop 0 \
	   && start_second_client EWMH_CLIENT_NAME=strut EWMH_CLIENT_STRUT_LEFT=50; then
		check "left strut shrinks _NET_WORKAREA" wait_for 40 workarea_left_is 50
		stop_second_client
		check "removing strut restores _NET_WORKAREA" wait_for 40 workarea_left_is 0
	else
		record_skip "could not launch strut client"
	fi

	note "_NET_RESTACK_WINDOW"
	if set_desktop 0 && start_ewmh_client \
	   && start_second_client EWMH_CLIENT_NAME=restack-peer EWMH_CLIENT_USER_TIME=1; then
		check "restack peer appears in _NET_CLIENT_LIST" wait_for 40 root_window_list_has _NET_CLIENT_LIST "$CLIENT2_XID"
		send_restack_message "$CLIENT_XID" "$CLIENT2_XID" 0
		check "honors _NET_RESTACK_WINDOW above sibling" wait_for 40 stacking_above "$CLIENT_XID" "$CLIENT2_XID"
		send_restack_message "$CLIENT_XID" "$CLIENT2_XID" 1
		check "honors _NET_RESTACK_WINDOW below sibling" wait_for 40 stacking_above "$CLIENT2_XID" "$CLIENT_XID"
		send_restack_message "$CLIENT_XID" "$CLIENT2_XID" 99
		check "ignores invalid _NET_RESTACK_WINDOW detail" wmaker_alive
		ROOT_XID=$(run_x xwininfo -root 2>/dev/null \
			| sed -n 's/^xwininfo: Window id: \(0x[0-9a-fA-F]*\).*/\1/p')
		send_restack_message "$CLIENT_XID" "$ROOT_XID" 0
		check "ignores invalid _NET_RESTACK_WINDOW sibling without X errors" no_internal_x_errors
		stop_second_client
		close_ewmh_client >/dev/null
	else
		record_skip "could not launch clients for restack test"
	fi

	note "malformed _NET_WM_ICON"
	if set_desktop 0 && start_second_client EWMH_CLIENT_NAME=badicon EWMH_CLIENT_BAD_ICON=1; then
		check "client with malformed initial _NET_WM_ICON is managed" wait_for 40 root_window_list_has _NET_CLIENT_LIST "$CLIENT2_XID"
		check "malformed initial _NET_WM_ICON keeps wmaker alive" wmaker_alive
		run_x xprop -id "$CLIENT2_XID" -f _NET_WM_ICON 32c \
			-set _NET_WM_ICON "2147483647, 2147483647, 1" >>"$LOG" 2>&1
		sleep 0.2
		check "malformed changed _NET_WM_ICON keeps wmaker alive" wmaker_alive
		check "malformed _NET_WM_ICON logs no internal X errors" no_internal_x_errors
		stop_second_client
	else
		record_skip "could not launch malformed icon client"
	fi

	note "hostile-input regressions"
	ROOT_XID=$(run_x xwininfo -root 2>/dev/null \
		| sed -n 's/^xwininfo: Window id: \(0x[0-9a-fA-F]*\).*/\1/p')
	if set_desktop 0 && start_ewmh_client && [ -n "$ROOT_XID" ]; then
		run_x xprop -id "$CLIENT_XID" -f _NET_WM_USER_TIME_WINDOW 32x \
			-set _NET_WM_USER_TIME_WINDOW "$ROOT_XID" >>"$LOG" 2>&1
		sleep 0.2
		# if the WM lost SubstructureRedirectMask on root, this map
		# would not be intercepted and the new window would never
		# enter _NET_CLIENT_LIST
		if start_second_client EWMH_CLIENT_NAME=postpoison; then
			check "rejects root as _NET_WM_USER_TIME_WINDOW (still managing)" wait_for 40 root_window_list_has _NET_CLIENT_LIST "$CLIENT2_XID"
			stop_second_client
		else
			record_fail "could not map probe client after USER_TIME_WINDOW=root"
		fi
		close_ewmh_client >/dev/null
	else
		record_skip "could not set up USER_TIME_WINDOW=root regression"
	fi

	if set_desktop 0 \
	   && start_second_client EWMH_CLIENT_NAME=badcounter EWMH_CLIENT_SYNC_COUNTER=3735928559; then
		sleep 0.2
		check "ignores invalid _NET_WM_SYNC_REQUEST_COUNTER without X errors" no_internal_x_errors
		stop_second_client
	else
		record_skip "could not launch client for sync-counter regression"
	fi
fi

note "_GNUSTEP_WM_HIDE_APP protocol"
if set_desktop 0 && start_gnustep_hide_client; then
	check "client advertises _GNUSTEP_WM_HIDE_APP in WM_PROTOCOLS" \
		wait_for 30 window_atom_has "$CLIENT_XID" WM_PROTOCOLS _GNUSTEP_WM_HIDE_APP
	run_x wmctrl -i -a "$CLIENT_XID" >>"$LOG" 2>&1
	wait_for 30 active_window_is "$CLIENT_XID" >/dev/null
	press_key alt+h
	check "delivers _GNUSTEP_WM_HIDE_APP on the user-gesture hide path" \
		wait_for 40 client_xid_file_has HIDE_APP
	sleep 0.2
	check "lets the client drive its own hide (not WM-hidden)" \
		window_atom_lacks "$CLIENT_XID" _NET_WM_STATE _NET_WM_STATE_HIDDEN
	close_ewmh_client >/dev/null
else
	record_fail "could not launch _GNUSTEP_WM_HIDE_APP client"
fi

note "_NET_SHOWING_DESKTOP toggle"
run_x wmctrl -k on >>"$LOG" 2>&1
check "honors _NET_SHOWING_DESKTOP on" wait_for 30 prop_has_values _NET_SHOWING_DESKTOP 1
run_x wmctrl -k off >>"$LOG" 2>&1
check "honors _NET_SHOWING_DESKTOP off" wait_for 30 prop_has_values _NET_SHOWING_DESKTOP 0

note "_NET_DESKTOP_LAYOUT navigation"
check_layout_property "accepts horizontal top-left 3x2 desktop layout" 0 3 2 0
check_desktop_transition "horizontal top-left: Right from 0 reaches 1" 0 Control+Right 1
check_desktop_transition "horizontal top-left: Down from 1 reaches 4" 1 Control+Down 4
check_desktop_transition "horizontal top-left: Left from 4 reaches 3" 4 Control+Left 3
check_desktop_transition "horizontal top-left: Up from 3 reaches 0" 3 Control+Up 0

check_layout_property "accepts horizontal top-right 3x2 desktop layout" 0 3 2 1
check_desktop_transition "horizontal top-right: Right from 0 stays at edge" 0 Control+Right 0
check_desktop_transition "horizontal top-right: Left from 0 reaches 1" 0 Control+Left 1
check_desktop_transition "horizontal top-right: Down from 0 reaches 3" 0 Control+Down 3

check_layout_property "accepts horizontal bottom-right 3x2 desktop layout" 0 3 2 2
check_desktop_transition "horizontal bottom-right: Right from 0 stays at edge" 0 Control+Right 0
check_desktop_transition "horizontal bottom-right: Left from 0 reaches 1" 0 Control+Left 1
check_desktop_transition "horizontal bottom-right: Up from 0 reaches 3" 0 Control+Up 3

check_layout_property "accepts horizontal bottom-left 3x2 desktop layout" 0 3 2 3
check_desktop_transition "horizontal bottom-left: Left from 0 stays at edge" 0 Control+Left 0
check_desktop_transition "horizontal bottom-left: Right from 0 reaches 1" 0 Control+Right 1
check_desktop_transition "horizontal bottom-left: Up from 0 reaches 3" 0 Control+Up 3

check_layout_property "accepts vertical top-left 3x2 desktop layout" 1 3 2 0
check_desktop_transition "vertical top-left: Down from 0 reaches 1" 0 Control+Down 1
check_desktop_transition "vertical top-left: Right from 0 reaches 2" 0 Control+Right 2
check_desktop_transition "vertical top-left: Up from 1 reaches 0" 1 Control+Up 0
check_desktop_transition "vertical top-left: Left from 2 reaches 0" 2 Control+Left 0

check_layout_property "derives rows when _NET_DESKTOP_LAYOUT rows is zero" 0 3 0 0
check_desktop_transition "derived horizontal layout: Down from 1 reaches 4" 1 Control+Down 4

check_layout_property "derives columns when _NET_DESKTOP_LAYOUT columns is zero" 1 0 2 0
check_desktop_transition "derived vertical layout: Right from 0 reaches 2" 0 Control+Right 2

note "_NET_DESKTOP_LAYOUT hostile overflow"
run_x xprop -root -f _NET_DESKTOP_LAYOUT 32c \
	-set _NET_DESKTOP_LAYOUT "0, 2147483647, 2147483647, 0" >>"$LOG" 2>&1
sleep 0.2
check "wmaker survives oversized _NET_DESKTOP_LAYOUT" wmaker_alive
check_desktop_transition "oversized layout falls back to linear: Right from 0 reaches 1" 0 Control+Right 1
# restore a sane layout for any later checks
set_layout 0 3 2 0 >/dev/null

note "_NET_WM_PING timeout"
if set_desktop 0 \
   && start_second_client EWMH_CLIENT_NAME=noping EWMH_CLIENT_NO_PING_REPLY=1 EWMH_CLIENT_IGNORE_DELETE=1; then
	run_x wmctrl -i -c "$CLIENT2_XID" >>"$LOG" 2>&1
	sleep 6
	check "no-reply _NET_WM_PING timeout keeps wmaker alive" wmaker_alive
	check "no-reply _NET_WM_PING timeout logs no internal X errors" no_internal_x_errors
	kill "$CLIENT2_PID" 2>/dev/null
	wait_for 50 pid_gone "$CLIENT2_PID"
	CLIENT2_PID=
	CLIENT2_XID=
else
	record_skip "could not launch no-ping client"
fi

check "no internal X errors logged during the run" no_internal_x_errors

# PerHeadWorkspaces is startup-only, so a second wmaker is brought up on
# the same Xephyr with a per-head config. Xephyr +xinerama exposes no
# active RandR outputs at all, so the workspace pin uses the "#N"
# head-index fallback that WorkspaceOutputs and HeadWorkspaces share.
if [ "$(xinerama_head_count)" -ge 2 ] && [ -x "$CLIENT_BIN" ]; then
	note "PerHeadWorkspaces"

	kill -TERM "$WMAKER_PID" 2>/dev/null
	wait_for 50 pid_gone "$WMAKER_PID"

	cat >"$WMHOME/Defaults/WindowMaker" <<-'WMEOD'
	{
	  PerHeadWorkspaces = YES;
	  AutoFocusAcrossHeads = YES;
	  WorkspaceOutputs = { "3" = "#0"; };
	}
	WMEOD
	# WorkspaceOutputs is parsed against the workspaces restored from
	# WMState, before _NET_NUMBER_OF_DESKTOPS would grow them, so the
	# pinned workspace must already exist there. HeadWorkspaces is
	# seeded so both heads start on workspace 0 instead of the
	# pinned-first default a fresh head would otherwise pick.
	cat >"$WMHOME/Defaults/WMState" <<-'WMEOD'
	{
	  Workspaces = ( {Name = "1";}, {Name = "2";}, {Name = "3";}, {Name = "4";} );
	  HeadWorkspaces = { "#0" = "1"; "#1" = "1"; };
	}
	WMEOD

	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY \
	"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
	WMAKER_PID=$!
	echo "$WMAKER_PID" >> "$PIDFILE"
	wm_roundtrip() { wmaker_alive && run_x wmctrl -m >/dev/null 2>&1; }
	wait_for 120 wm_roundtrip || fatal "wmaker (PerHeadWorkspaces) did not come up"
	run_x wmctrl -n 4 >>"$LOG" 2>&1
	wait_for 40 prop_has_values _NET_NUMBER_OF_DESKTOPS 4 >/dev/null

	if start_ewmh_client \
	   && start_second_client EWMH_CLIENT_NAME=perhead2; then
		run_x wmctrl -i -r "$CLIENT_XID" -e 0,50,50,320,180 >>"$LOG" 2>&1
		run_x wmctrl -i -r "$CLIENT2_XID" -e 0,1100,100,320,180 >>"$LOG" 2>&1
		wait_for 30 window_on_head0 "$CLIENT_XID" >/dev/null
		wait_for 30 window_on_head1 "$CLIENT2_XID" >/dev/null

		# both clients on workspace 0, both heads on workspace 0,
		# focus the head-1 client so it becomes the active head
		send_active_window "$CLIENT2_XID" 2 0
		wait_for 30 active_window_is "$CLIENT2_XID" >/dev/null
		run_x wmctrl -s 1 >>"$LOG" 2>&1

		check "PerHeadWorkspaces: head 1 switch sets _NET_CURRENT_DESKTOP" \
			wait_for 30 desktop_is 1
		check "PerHeadWorkspaces: head 1 switch leaves head 0 client mapped" \
			wait_for 30 window_is_viewable "$CLIENT_XID"
		check "PerHeadWorkspaces: head 1 client on the old workspace is unmapped" \
			wait_for 30 window_not_viewable "$CLIENT2_XID"

		run_x wmctrl -i -r "$CLIENT2_XID" -t 1 >>"$LOG" 2>&1
		check "PerHeadWorkspaces: head 1 client maps on the per-head workspace" \
			wait_for 30 window_is_viewable "$CLIENT2_XID"

		# focus-only _NET_CURRENT_DESKTOP must track the active head
		# (the awesome #170 case for chromium DND visibility)
		send_active_window "$CLIENT_XID" 2 0
		check "PerHeadWorkspaces: focusing head 0 sets _NET_CURRENT_DESKTOP" \
			wait_for 30 desktop_is 0
		check "PerHeadWorkspaces: head 1 client stays mapped across the focus move" \
			window_is_viewable "$CLIENT2_XID"
		send_active_window "$CLIENT2_XID" 2 0
		check "PerHeadWorkspaces: focusing head 1 sets _NET_CURRENT_DESKTOP" \
			wait_for 30 desktop_is 1

		# WorkspaceOutputs head translation: workspace 2 (1-based "3")
		# is pinned to "default" = head 0, sending the head-1 client
		# there must carry the frame across to head 0 instead of
		# leaving last_head on a head where the workspace can never
		# be shown
		run_x wmctrl -i -r "$CLIENT2_XID" -t 2 >>"$LOG" 2>&1
		check "PerHeadWorkspaces: _NET_WM_DESKTOP to a pinned workspace is honoured" \
			wait_for 30 window_prop_has_values "$CLIENT2_XID" _NET_WM_DESKTOP 2
		check "PerHeadWorkspaces: window follows pinned workspace onto head 0" \
			wait_for 30 window_on_head0 "$CLIENT2_XID"

		stop_second_client
		close_ewmh_client >/dev/null
	else
		record_skip "PerHeadWorkspaces: managed clients failed"
	fi

	check "PerHeadWorkspaces: no internal X errors" no_internal_x_errors

	# clean shutdown writes WMState, head 1 was switched from the
	# seeded workspace 0 to workspace 1 above so the saved value for
	# the "#1" index key must reflect that, proving the per-head save
	# ran rather than the seeded dictionary surviving untouched
	kill -TERM "$WMAKER_PID" 2>/dev/null
	wait_for 50 pid_gone "$WMAKER_PID"
	headws_saved() {
		grep -F 'HeadWorkspaces' "$WMHOME/Defaults/WMState" >/dev/null 2>&1 \
			&& grep -E '"#1"[[:space:]]*=[[:space:]]*"?2"?;' \
				"$WMHOME/Defaults/WMState" >/dev/null 2>&1
	}
	check "PerHeadWorkspaces: HeadWorkspaces saved with the per-head switch" \
		wait_for 20 headws_saved
else
	record_skip "PerHeadWorkspaces: only one Xinerama head or no test client"

	kill -TERM "$WMAKER_PID" 2>/dev/null
	wait_for 50 pid_gone "$WMAKER_PID"
fi

finish
