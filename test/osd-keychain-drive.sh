#!/bin/sh
# Exercise the key-chain prefix OSD popup end to end under Xvfb.
#
# scr->keychain_osd is a WOSDView created and realized at startup but
# left unmapped. Entering or advancing a multi-stroke key chain maps it
# at the configured KeychainDisplayPosition, completing the chain or
# letting KeychainTimeoutDelay fire unmaps it again, and with the
# default position "none" it never maps at all. The test asserts on map
# state, override-redirect, the topleft +8+8 placement, and that the
# label width grows when stepping into a deeper internal node.
#
# Harness conventions follow ipc-drive.sh: private display, private
# XDG_RUNTIME_DIR and WMAKER_USER_ROOT, every spawned PID recorded and
# killed by PID only, exit 77 on missing tools.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${OSD_DRIVE_DISPLAY:-:97}
RUNTIME=$(mktemp -d /tmp/wmaker-osd-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=${OSD_DRIVE_LOG:-/tmp/osd-keychain-drive.log}
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"

PASSED=0
FAILED=0
WMAKER_PID=
OSD_XID=

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

xdo() {
	{ DISPLAY=$DPY xdotool "$@" 2>&1 1>&3 \
		| grep -v '^XGetWindowProperty\[_NET_WM_DESKTOP\] failed' >&2; } 3>&1
}

# IDs of currently-mapped direct children of root, one per line
mapped_children() {
	DISPLAY=$DPY xwininfo -root -children 2>/dev/null \
		| awk '$1 ~ /^0x/ { print $1 }' \
		| while read -r mc_id; do
			DISPLAY=$DPY xwininfo -id "$mc_id" 2>/dev/null \
				| grep -q 'Map State: IsViewable' \
				&& printf '%s\n' "$mc_id"
		done | sort
}

BASEFILE="$RUNTIME/base-mapped"

snapshot_base() { mapped_children > "$BASEFILE"; }

# mapped children that were not in the last snapshot_base
new_since_base() { mapped_children | grep -vxF -f "$BASEFILE"; }

osd_info() { DISPLAY=$DPY xwininfo -id "$OSD_XID" 2>/dev/null; }
osd_mapped() { osd_info | grep -q 'Map State: IsViewable'; }
osd_unmapped() { osd_info | grep -q 'Map State: IsUnMapped'; }
osd_width() { osd_info | awk '/^ *Width:/ { print $2 }'; }
osd_override_redirect() { osd_info | grep -q 'Override Redirect.*yes'; }
osd_at_8_8() {
	osd_info | grep -q 'Absolute upper-left X: *8$' \
		&& osd_info | grep -q 'Absolute upper-left Y: *8$'
}
osd_wider_than() {
	ow_now=$(osd_width)
	[ -n "$ow_now" ] && [ "$ow_now" -gt "$1" ]
}

write_domain() {
	# the chain leader is a plain letter combination rather than an
	# F-key: some xdotool builds prepend Alt_L when synthesizing an
	# F-key keysym so the passive grab for a bare F9 never matches
	cat > "$WMHOME/Defaults/WindowMaker" <<EOF
{
  KeychainDisplayPosition = $1;
  KeychainTimeoutDelay = 2000;
  MaximizeKey = "Control+Shift+O a m";
  VMaximizeKey = "Control+Shift+O v";
}
EOF
	# without a root menu OpenRootMenu() pops a modal error dialog
	# during wScreenRestoreState() and startup never completes, so
	# the SIGTERM in stop_wmaker reaches Shutdown() with scr->dock
	# still NULL and wDockSaveState() segfaults
	cat > "$WMHOME/Defaults/WMRootMenu" <<'EOF'
("osd-drive", ("Exit", EXIT))
EOF
}

start_wmaker() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	ASAN_OPTIONS="abort_on_error=0:halt_on_error=0:detect_leaks=0" \
	DISPLAY=$DPY \
	"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
	WMAKER_PID=$!
	echo "$WMAKER_PID" >> "$PIDFILE"
	wait_for 120 wm_ready
}

stop_wmaker() {
	[ -n "$WMAKER_PID" ] || return 0
	kill -TERM "$WMAKER_PID" 2>/dev/null
	wait_for 50 pid_gone "$WMAKER_PID"
	WMAKER_PID=
}

wm_ready() {
	DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null \
		| grep 'window id' >/dev/null
}

finish() {
	finish_total=$((PASSED + FAILED))
	note "OSD key-chain drive summary"
	printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
	[ "$FAILED" -eq 0 ] && [ "$finish_total" -gt 0 ]
}

# ----------------------------------------------------------------------

for tool in Xvfb xdpyinfo xdotool xwininfo xprop; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
export WMAKER_USER_ROOT="$WMHOME"
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set OSD_DRIVE_DISPLAY to a free one" >&2
	exit 1
fi

note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 1024x768x24 -noreset -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
	kill -0 "$XVFB_PID" 2>/dev/null || fatal "Xvfb exited early"
	DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fatal "Xvfb failed to start"

# ---- KeychainDisplayPosition = topleft -----------------------------------
# topleft puts the OSD at the head-relative CHAIN_OSD_PAD offset which
# on a single-head display is root +8+8, the easiest placement to
# assert on without measuring fonts.

note "KeychainDisplayPosition = topleft"
write_domain topleft
start_wmaker || fatal "wmaker did not come up"
sleep 0.5

snapshot_base

note "leader maps the OSD"
xdo key --clearmodifiers ctrl+shift+o
new_mapped() {
	NEW=$(new_since_base)
	[ -n "$NEW" ]
}
wait_for 20 new_mapped || fatal "no new top-level mapped after leader"
NCOUNT=$(printf '%s\n' "$NEW" | grep -c '^0x')
check "exactly one new mapped top-level after leader" [ "$NCOUNT" -eq 1 ]
OSD_XID=$(printf '%s\n' "$NEW" | head -n 1)

osd_info >> "$LOG"
check "OSD is override-redirect" osd_override_redirect
check "OSD at topleft +8+8" osd_at_8_8
W1=$(osd_width)
[ -n "$W1" ] && [ "$W1" -gt 20 ] && [ "$W1" -lt 200 ] \
	&& record_pass "OSD has a sane label width ($W1)" \
	|| record_fail "OSD has a sane label width ($W1)"

note "internal-node advance widens the label"
xdo key a
wait_for 20 osd_wider_than "$W1" \
	&& record_pass "OSD still mapped and wider after internal step" \
	|| record_fail "OSD did not widen after internal step (w1=$W1 now=$(osd_width))"

note "leaf hides the OSD"
xdo key m
wait_for 20 osd_unmapped
check "OSD unmapped after leaf executes" osd_unmapped

note "timeout hides the OSD"
xdo key --clearmodifiers ctrl+shift+o
wait_for 20 osd_mapped || fatal "OSD did not remap on second leader"
check "OSD mapped after second leader" osd_mapped
# KeychainTimeoutDelay = 2000 ms, the OSD must unmap on its own
wait_for 40 osd_unmapped
check "OSD unmapped after KeychainTimeoutDelay" osd_unmapped

note "unrecognized follower hides the OSD"
xdo key --clearmodifiers ctrl+shift+o
wait_for 20 osd_mapped || fatal "OSD did not remap on third leader"
xdo key z
wait_for 20 osd_unmapped
check "OSD unmapped after unrecognized follower" osd_unmapped

stop_wmaker

# ---- KeychainDisplayPosition = none --------------------------------------

note "KeychainDisplayPosition = none"
write_domain none
start_wmaker || fatal "wmaker did not come up"
sleep 0.5

snapshot_base
xdo key --clearmodifiers ctrl+shift+o
sleep 0.3
NEW=$(new_since_base)
check "no OSD mapped with KeychainDisplayPosition = none" \
	[ -z "$NEW" ]
# follower still completes the chain so the keyboard grab is released
xdo key v

stop_wmaker

finish
