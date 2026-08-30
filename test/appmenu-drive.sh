#!/bin/sh
# A wmlib-WMMenu-style app (test/appmenu-client.c) publishes its menu
# through the _WINDOWMAKER_MENU text property on its group leader and
# wmaker adopts it:
#
#   1. adoption - the manage-path probe clears shared_appicon
#                 (src/window.c:1209-1224), wAppMenuGet parses the
#                 property (src/appmenu.c), focusing the app maps the
#                 menu frame (src/actions.c:266-267, the menu window
#                 level never appears in _NET_CLIENT_LIST)
#   2. round trip - hovering the "Info" cascade entry opens the
#                 submenu (menuMotionNotify selectEntry), clicking
#                 "Info Panel..." runs notifyClient which sends the
#                 wmSelectItem ClientMessage back to the app; the
#                 helper acks the tag in _WMENU_TEST_ACK and maps its
#                 panel
#   3. teardown - killing the helper destroys the app and its menu
#                 frames (wAppMenuDestroy on wApplicationDestroy)
#
# Runs wmaker under Xvfb on an isolated display and WMAKER_USER_ROOT,
# tracks every PID it spawns and kills only those.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${APPMENU_DRIVE_DISPLAY:-:108}
W=1024
H=768
RUNTIME=$(mktemp -d /tmp/wmaker-appmenu.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=${APPMENU_DRIVE_LOG:-/tmp/appmenu-drive.log}
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
HELPER="$RUNTIME/appmenu-client"
SOCKET=

PASSED=0
FAILED=0

: > "$PIDFILE"
: > "$LOG"

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

ipc_ready() { msg -q get_version >/dev/null 2>&1; }

client_in_list() {
	DISPLAY=$DPY xprop -root _NET_CLIENT_LIST 2>/dev/null | grep -qw "$1"
}

log_ok() {
	if grep -q 'appmenu:' "$LOG"; then
		return 1
	fi
	if grep -q 'X Error' "$LOG"; then
		return 1
	fi
	return 0
}

# ids of all root children, sorted
window_exists() { DISPLAY=$DPY xwininfo -id "$1" >/dev/null 2>&1; }

window_viewable() {
	DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null \
		| grep -q 'Map State: IsViewable'
}

# viewable 64x64 root children without subwindows (menu frames carry
# the entry-list child); the dock/clip are disabled in this drive so
# the floating appicon is the only match
floating_appicon() {
	for w in $(DISPLAY=$DPY xwininfo -root -children 2>/dev/null \
			| awk '/^     0x/ && $(NF-1) ~ /^64x64\+/ {print $1}'); do
		window_viewable "$w" || continue
		DISPLAY=$DPY xwininfo -tree -id "$w" 2>/dev/null \
			| awk '/^     0x/ {found=1} END {exit !found}' && continue
		echo "$w"
		return 0
	done
	return 1
}

appicon_count() {
	n=0
	for w in $(DISPLAY=$DPY xwininfo -root -children 2>/dev/null \
			| awk '/^     0x/ && $(NF-1) ~ /^64x64\+/ {print $1}'); do
		window_viewable "$w" || continue
		DISPLAY=$DPY xwininfo -tree -id "$w" 2>/dev/null \
			| awk '/^     0x/ {found=1} END {exit !found}' && continue
		n=$((n + 1))
	done
	echo "$n"
}

# "absx absy rowh" of the entry-list child below the title of the menu
# frame $1 with $2 visible entries; fails when the frame has none
menu_entry_list() {
	DISPLAY=$DPY xwininfo -tree -id "$1" 2>/dev/null | awk -v n="$2" '
		/^     0x/ {
			if (match($0, /[0-9]+x[0-9]+\+-?[0-9]+\+-?[0-9]+  \+-?[0-9]+\+-?[0-9]+$/)) {
				split(substr($0, RSTART, RLENGTH), g, /[x+ ]+/);
				if (g[4] > 0) {
					printf "%d %d %d\n", g[5], g[6], int((g[2] + 1) / n);
					found = 1;
					exit;
				}
			}
		}
		END { exit !found }'
}

# "id viewable(0|1)" lines for every root child, sorted
root_view_state() {
	DISPLAY=$DPY xwininfo -root -children 2>/dev/null \
		| awk '/^     0x/ {print $1}' \
		| while read -r w; do
			if window_viewable "$w"; then
				echo "$w 1"
			else
				echo "$w 0"
			fi
		done \
		| sort
}

# the window that became viewable between two state snapshots and has
# an entry-list child below its title: the app menu frames are created
# (unmapped) when the menu property is parsed, so mapping, not creation,
# is the observable; $3 excludes a known frame id
find_menu_frame() {
	find_before=$1
	find_after=$2
	find_excl=$3
	awk '$2 == 1 {print $1}' "$find_after" | sort > "$RUNTIME/v-now"
	awk '$2 == 0 {print $1}' "$find_before" | sort > "$RUNTIME/v-was-unmapped"
	for w in $(comm -12 "$RUNTIME/v-was-unmapped" "$RUNTIME/v-now"); do
		[ "$w" = "$find_excl" ] && continue
		[ -n "$(menu_entry_list "$w" 2)" ] || continue
		echo "$w"
		return 0
	done
	return 1
}

click_at() {
	DISPLAY=$DPY xdotool mousemove --sync "$1" "$2" >>"$LOG" 2>&1
	DISPLAY=$DPY xdotool click 1 >>"$LOG" 2>&1
}

# ----------------------------------------------------------------------

for tool in Xvfb xdpyinfo xwininfo xprop xdotool wmctrl; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

${CC:-cc} -std=gnu11 "$ROOT/test/appmenu-client.c" -o "$HELPER" -lX11 >>"$LOG" 2>&1 \
	|| skip_all "could not build test/appmenu-client.c"

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
# dock/clip/drawers off: nothing here needs them and the positional
# appicon scan must see exactly one icon; AutoFocus off so the app
# menu can only map through the drive's explicit click-to-focus.
# This also stops the first-run installer.
cat > "$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  AutoFocus = NO;
}
EOF
cat > "$WMHOME/Defaults/WMState" <<'EOF'
{
  Workspaces = (
    { Name = "ws-0"; },
    { Name = "ws-1"; }
  );
}
EOF

export XDG_RUNTIME_DIR="$RUNTIME"
export XDG_DATA_HOME="$RUNTIME/share"
export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export LANG=en_US.UTF-8
unset WMAKER_IPC_SOCKET
unset XDG_DATA_DIRS

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set APPMENU_DRIVE_DISPLAY to a free one" >&2
	exit 1
fi

note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 ${W}x${H}x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
	kill -0 "$XVFB_PID" 2>/dev/null || fatal "Xvfb exited early"
	DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fatal "Xvfb failed to start"

SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
case "$SOCKET" in
	"$RUNTIME"/wmaker/ipc.*) ;;
	*) fatal "--get-socketpath returned $SOCKET, refusing" ;;
esac

note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY \
	"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
wait_for 80 ipc_ready || fatal "wmaker did not answer get_version"
sleep 0.3

# --- section 1: adoption --------------------------------------------------------

note "launch the appmenu helper"
DISPLAY=$DPY "$HELPER" >"$RUNTIME/helper.out" 2>>"$LOG" &
HELPER_PID=$!
echo "$HELPER_PID" >> "$PIDFILE"
wait_for 30 sh -c "grep -q '^panel ' '$RUNTIME/helper.out'" \
	|| fatal "appmenu helper did not start"
LEADER=$(awk '/^leader/ {print $2; exit}' "$RUNTIME/helper.out")
PANEL=$(awk '/^panel/ {print $2; exit}' "$RUNTIME/helper.out")
[ -n "$LEADER" ] && [ -n "$PANEL" ] || fatal "could not read helper window ids"
note "helper leader=$LEADER panel=$PANEL pid=$HELPER_PID"

check "main window joined _NET_CLIENT_LIST" wait_for 50 client_in_list "$LEADER"

appicon_appeared() {
	FLOATING=$(floating_appicon)
	[ -n "$FLOATING" ]
}
check "appicon built from the real leader appeared" wait_for 60 appicon_appeared
AICON=$FLOATING
[ -n "$AICON" ] || fatal "could not locate the appicon"
check "exactly one floating appicon exists" \
	[ "$(appicon_count)" = "1" ]
note "appicon: $AICON"

check "log free of appmenu parse warnings" log_ok

# click-to-focus the leader; the app menu maps on the focus path
# (src/actions.c:266-267) and only ever exists as a wmaker-internal
# root child, never in _NET_CLIENT_LIST. AutoFocus is off, so the menu
# frame's viewability flips exactly at this click.
root_view_state > "$RUNTIME/state-preclick"
ml=$(DISPLAY=$DPY xwininfo -id "$LEADER" 2>/dev/null \
	| awk '/Absolute upper-left X:/ {x=$4}
	       /Absolute upper-left Y:/ {y=$4}
	       /Width:/ {w=$2}
	       /Height:/ {h=$2}
	       END {print x + w / 2, y + h / 2}')
[ -n "$ml" ] || fatal "could not read the leader geometry"
set -- $ml
note "click-to-focus leader at $1,$2"
click_at "$1" "$2"

menu_frame_appeared() {
	root_view_state > "$RUNTIME/state-postclick"
	MENU_FRAME=$(find_menu_frame "$RUNTIME/state-preclick" \
		"$RUNTIME/state-postclick" "")
	[ -n "$MENU_FRAME" ]
}
check "app menu frame mapped on focus" wait_for 60 menu_frame_appeared
[ -n "$MENU_FRAME" ] || fatal "could not locate the app menu frame"
note "app menu frame: $MENU_FRAME"

# --- section 2: round trip --------------------------------------------------------

# Bare pointer motion selects menu entries (menuMotionNotify) and
# selectEntry maps the cascade of a highlighted entry, so hovering the
# "Info" row opens the submenu. Pressing it instead would toggle the
# hover-opened cascade back closed.

note "hover the Info cascade entry"
root_view_state > "$RUNTIME/state-precascade"
ml=$(menu_entry_list "$MENU_FRAME" 2) || fatal "could not read menu entry list"
set -- $ml
DISPLAY=$DPY xdotool mousemove --sync $(( $1 + 6 )) $(( $2 + $3 / 2 )) >>"$LOG" 2>&1

submenu_appeared() {
	root_view_state > "$RUNTIME/state-postcascade"
	SUB_FRAME=$(find_menu_frame "$RUNTIME/state-precascade" \
		"$RUNTIME/state-postcascade" "$MENU_FRAME")
	[ -n "$SUB_FRAME" ]
}
check "Info submenu cascade mapped on hover" wait_for 30 submenu_appeared
[ -n "$SUB_FRAME" ] || fatal "could not locate the submenu frame"
note "submenu frame: $SUB_FRAME"

note "click the Info Panel... item"
ml=$(menu_entry_list "$SUB_FRAME" 1) || fatal "could not read submenu entry list"
set -- $ml
click_at $(( $1 + 6 )) $(( $2 + $3 / 2 ))

ack_is_101() {
	DISPLAY=$DPY xprop -id "$LEADER" _WMENU_TEST_ACK 2>/dev/null | grep -q '= 101'
}
check "menu item click acked tag 101 via _WMENU_TEST_ACK" wait_for 30 ack_is_101
check "panel window joined _NET_CLIENT_LIST" wait_for 30 client_in_list "$PANEL"

note "close the panel via wmctrl"
DISPLAY=$DPY wmctrl -i -c "$PANEL" >>"$LOG" 2>&1
panel_gone() { ! client_in_list "$PANEL"; }
check "panel left _NET_CLIENT_LIST" wait_for 50 panel_gone
check "helper still alive after the round trip" kill -0 "$HELPER_PID"

# --- section 3: teardown ----------------------------------------------------------

note "kill the helper"
kill "$HELPER_PID" 2>/dev/null
wait_for 30 pid_gone "$HELPER_PID" || fatal "helper refused to die"

frame_gone() { ! window_exists "$1"; }
check "app menu frame destroyed" wait_for 50 frame_gone "$MENU_FRAME"
check "submenu frame destroyed" wait_for 50 frame_gone "$SUB_FRAME"
check "appicon destroyed" wait_for 50 frame_gone "$AICON"
check "wmaker still healthy" kill -0 "$WMAKER_PID"
check "log free of appmenu warnings and X errors" log_ok

note "exit wmaker"
msg exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

note "appmenu drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
[ "$FAILED" -eq 0 ] && [ "$PASSED" -gt 0 ]
