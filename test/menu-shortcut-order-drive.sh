#!/bin/sh
# Regression gate for root-menu shortcut handling: entries sharing one
# shortcut run in menu definition order, and that order must not change
# across a menu reparse that tears down several shortcut-bearing menus at
# once (the old code rebuilt the key trie once per destroyed menu) nor
# across a later key-trie rebuild from a keybinding change. A shortcut
# dropped from the menu must also stop firing, and a chain removed while
# it is half-typed must not keep masking a shorter binding that shares
# its prefix.
#
# SHOW_ALL and HIDE_OTHERS run synchronously in-process in dispatch
# order, so the final visibility of the unfocused xclock encodes the
# order of the two entries sharing the shortcut: definition order runs
# SHOW_ALL first and HIDE_OTHERS last, leaving the clock hidden, while a
# flipped order leaves it visible.
#
# Harness conventions follow osd-keychain-drive.sh: private display,
# private XDG_RUNTIME_DIR and WMAKER_USER_ROOT, every spawned PID
# recorded and killed by PID only, exit 77 on missing tools.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${MENU_SHORTCUT_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/menu-shortcut-drive.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=${MENU_SHORTCUT_DRIVE_LOG:-/tmp/menu-shortcut-order-drive.log}
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
WDWRITE="$ROOT/util/.libs/wdwrite"
SOCKET=
WMAKER_PID=
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

xdo() {
	DISPLAY=$DPY xdotool "$@" >>"$LOG" 2>&1
}

wm_ready() {
	DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null \
		| grep 'window id' >/dev/null
}

msg() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" "$@"
}

xclock_visible() {
	DISPLAY=$DPY xdotool search --onlyvisible --name '^xclock$' 2>>"$LOG" \
		| grep -q .
}

xclock_hidden() {
	! xclock_visible
}

xterm_mapped() {
	DISPLAY=$DPY xdotool search --onlyvisible --name '^menu-shortcut-term$' \
		2>>"$LOG" | grep -q .
}

# SHOW_ALL is bound alone to this key: it re-shows the clock so every
# order probe starts from the same visible state
reset_clock() {
	xdo key --clearmodifiers ctrl+shift+a
	wait_for 30 xclock_visible || true
}

press_order_key() {
	xdo key --clearmodifiers ctrl+shift+y
	sleep 0.3
}

write_root_menu() {
	# $1 = revision label; the shared-shortcut pair is identical in both
	# revisions, the shortcut-bearing submenus and the stale entry only
	# exist in the first
	cat > "$WMHOME/Defaults/WMRootMenu" <<EOF
("menu-shortcut",
 ("Show", SHORTCUT, "Control+Shift+A", SHOW_ALL),
 ("First", SHORTCUT, "Control+Shift+Y", SHOW_ALL),
 ("Second", SHORTCUT, "Control+Shift+Y", HIDE_OTHERS)$2)
EOF
}

submenus_v1() {
	cat <<'EOF'
,
 ("Sub1", ("S1", SHORTCUT, "Control+Shift+Q", SHOW_ALL)),
 ("Sub2", ("S2", SHORTCUT, "Control+Shift+W", SHOW_ALL)),
 ("Sub3", ("S3", SHORTCUT, "Control+Shift+E", SHOW_ALL)),
 ("Sub4", ("S4", SHORTCUT, "Control+Shift+R", SHOW_ALL)),
 ("Sub5", ("S5", SHORTCUT, "Control+Shift+T", SHOW_ALL)),
 ("Sub6", ("S6", SHORTCUT, "Control+Shift+U", SHOW_ALL)),
 ("Sub7", ("S7", SHORTCUT, "Control+Shift+I", SHOW_ALL)),
 ("Sub8", ("S8", SHORTCUT, "Control+Shift+O", SHOW_ALL)),
 ("Stale", SHORTCUT, "Control+Shift+G", HIDE_OTHERS)
EOF
}

write_wmaker_domain() {
	cat > "$WMHOME/Defaults/WindowMaker" <<EOF
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  DisableSound = YES;
  KeychainTimeoutDelay = 0;
  NextWorkspaceKey = "$1";
}
EOF
}

start_wmaker() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	ASAN_OPTIONS="$(drive_asan_options)" \
	DISPLAY=$DPY \
	"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
	WMAKER_PID=$!
	echo "$WMAKER_PID" >> "$PIDFILE"
	wait_for 120 wm_ready || fatal "wmaker did not come up"
	SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
}

finish() {
	finish_total=$((PASSED + FAILED))
	note "menu-shortcut-order drive summary"
	printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
	[ "$FAILED" -eq 0 ] && [ "$finish_total" -gt 0 ]
}

# ----------------------------------------------------------------------

for tool in Xvfb xdpyinfo xdotool xprop xterm xclock; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
[ -x "$WDWRITE" ] || skip_all "util/.libs/wdwrite not built"

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set MENU_SHORTCUT_DRIVE_DISPLAY" >&2
	exit 77
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

note "start wmaker with the V1 menu"
write_wmaker_domain "Control+Shift+Alt+n"
write_root_menu V1 "$(submenus_v1)"
start_wmaker

note "map xclock and an xterm, focus the xterm"
DISPLAY=$DPY xclock >>"$LOG" 2>&1 &
XCLOCK_PID=$!
echo "$XCLOCK_PID" >> "$PIDFILE"
DISPLAY=$DPY xterm -geometry 40x12+40+40 -T menu-shortcut-term -e 'sleep 300' \
	>>"$LOG" 2>&1 &
XTERM_PID=$!
echo "$XTERM_PID" >> "$PIDFILE"
clients_ready() {
	xterm_mapped && xclock_visible
}
wait_for 60 clients_ready || fatal "xclock/xterm did not map"
TERM_WID=$(DISPLAY=$DPY xdotool search --onlyvisible \
	--name '^menu-shortcut-term$' 2>>"$LOG" | head -n 1)
[ -n "$TERM_WID" ] || fatal "xterm window id not found"
xdo windowactivate "$TERM_WID"
sleep 0.3

# ---- 1. definition order after startup -------------------------------
# The startup wKeyTreeRebuild() re-walks the shortcut list after the
# menus are parsed, so this press already fails if the rebuild order
# disagrees with the parse-time order.

note "startup: the shared shortcut runs in definition order"
reset_clock
press_order_key
check "startup order leaves the clock hidden" xclock_hidden

# ---- 2. order survives a reparse that destroys many menus ------------
# Rewriting WMRootMenu and reparsing destroys V1's root menu plus eight
# shortcut-bearing submenus in one cascade; the old code rebuilt the
# whole key trie once per menu there.

note "reparse destroys eight shortcut-bearing submenus at once"
sleep 1
write_root_menu V2 ""
msg -q reconfigure || fatal "reconfigure failed"
sleep 0.5
reset_clock
press_order_key
check "order survives the multi-menu destroy" xclock_hidden

# ---- 3. order survives a later key-trie rebuild ----------------------
# A keybinding change re-runs wReadKeybindings() which rebuilds the
# trie from scratch; the reviewer's order flip showed up on exactly
# this "later trie rebuild".

note "keybinding change forces a key-trie rebuild"
"$WDWRITE" WindowMaker NextWorkspaceKey "Control+Shift+Alt+m" >>"$LOG" 2>&1
msg -q reconfigure || fatal "reconfigure failed"
sleep 0.5
reset_clock
press_order_key
check "order survives the key-trie rebuild" xclock_hidden

# ---- 4. stale shortcut stops firing ---------------------------------
# V2 dropped the entries behind Control+Shift+G and the V1 submenus;
# pressing a dead binding must not hide the clock.

note "stale shortcuts are inert after the reparse"
reset_clock
xdo key --clearmodifiers ctrl+shift+g
sleep 0.3
check "dropped shortcut no longer hides the clock" xclock_visible
xdo key --clearmodifiers ctrl+shift+q
sleep 0.3
check "shortcut of a destroyed submenu is inert" xclock_visible

# ---- 5. order is stable over repeated presses ------------------------

note "repeated presses keep the same order"
press_order_key
check "second press keeps the clock hidden" xclock_hidden
reset_clock
press_order_key
check "third press keeps the clock hidden" xclock_hidden

# ---- 6. a chain removed while half-typed must not mask a shorter ----
# binding that shares its prefix. "Deep" hangs off the same leader key
# as the single-key "Shorter"; arming "Control+Shift+P x" pins the
# middle node of the chain when the reparse tears the chain down. The
# teardown must end the chain and drop the nodes it kills, so "Shorter"
# matches again instead of a dead prefix swallowing the key.

note "a removed chain stops masking the shorter binding"
cat > "$WMHOME/Defaults/WMRootMenu" <<'EOF'
("menu-shortcut",
 ("Show", SHORTCUT, "Control+Shift+A", SHOW_ALL),
 ("Hide", SHORTCUT, "Control+Shift+H", HIDE_OTHERS),
 ("Shorter", SHORTCUT, "Control+Shift+P", SHOW_ALL),
 ("Deep", SHORTCUT, "Control+Shift+P X Z", HIDE_OTHERS))
EOF
msg -q reconfigure || fatal "reconfigure failed"
sleep 0.5
xdo key --clearmodifiers ctrl+shift+h
sleep 0.3
check "hide runs before the chain is armed" xclock_hidden

note "arm the chain up to its middle node, then tear the chain down"
xdo key --clearmodifiers ctrl+shift+p
xdo key --clearmodifiers x
sleep 0.3
cat > "$WMHOME/Defaults/WMRootMenu" <<'EOF'
("menu-shortcut",
 ("Show", SHORTCUT, "Control+Shift+A", SHOW_ALL),
 ("Hide", SHORTCUT, "Control+Shift+H", HIDE_OTHERS),
 ("Shorter", SHORTCUT, "Control+Shift+P", SHOW_ALL))
EOF
msg -q reconfigure || fatal "reconfigure failed"
sleep 0.5
xdo key --clearmodifiers ctrl+shift+p
sleep 0.3
check "shorter binding fires after the chain teardown" xclock_visible

if [ -n "$WMAKER_PID" ]; then
	kill -TERM "$WMAKER_PID" 2>/dev/null
	wait_for 50 sh -c "! kill -0 $WMAKER_PID 2>/dev/null"
fi

finish
