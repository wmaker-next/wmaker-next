#!/bin/sh
# Drive the WPrefs "Reset Defaults" button under Xvfb: seed non-default
# values into the four WPrefs-managed user domains, run WPrefs, click
# Reset Defaults, confirm the alert, and assert that every domain file
# is emptied or removed, that a .wprefs-backup sibling keeps the old
# content, that wmaker survives the restart, and that an initialized
# panel was refreshed (a later Save writes the schema default, not the
# stale seeded value).
#
# Harness conventions as in ipc-drive.sh: private XDG_RUNTIME_DIR,
# bounded readiness polls, background processes killed by recorded PID.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${WPREFS_RESET_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-wprefs-reset.XXXXXX)
WMHOME="$RUNTIME/home"
DEFS="$WMHOME/Defaults"
PIDFILE="$RUNTIME/pids"
LOG=${WPREFS_RESET_DRIVE_LOG:-/tmp/wprefs-reset-drive.log}
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"

PASSED=0
FAILED=0
W=
A=

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
fail() { printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; FAILED=$((FAILED + 1)); }
ok()   { printf 'PASS: %s\n' "$*" | tee -a "$LOG"; PASSED=$((PASSED + 1)); }
fatal() { printf 'FATAL: %s\n' "$*" | tee -a "$LOG"; exit 1; }

check() {
	check_desc=$1
	shift
	if "$@"; then
		ok "$check_desc"
	else
		fail "$check_desc"
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
pid_alive() { kill -0 "$1" 2>/dev/null; }
# negated greps for check/wait_for arguments (a bare `!` is not valid
# in command position under every /bin/sh)
lacks() {
	if grep -q "$@"; then
		return 1
	fi
	return 0
}
pid_alive() { kill -0 "$1" 2>/dev/null; }

xdo() {
	{ DISPLAY=$DPY xdotool "$@" 2>&1 1>&3 \
		| grep -v '^XGetWindowProperty\[_NET_WM_DESKTOP\] failed' >&2; } 3>&1
}

for t in Xvfb xdotool; do
	command -v "$t" >/dev/null 2>&1 || { echo "SKIP: $t not installed"; exit 77; }
done
[ -x "$ROOT/src/.libs/wmaker" ] || { echo "SKIP: wmaker not built"; exit 77; }
[ -x "$ROOT/WPrefs.app/.libs/WPrefs" ] || { echo "SKIP: WPrefs not built"; exit 77; }

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY already in use, set WPREFS_RESET_DRIVE_DISPLAY" >&2
	exit 77
fi

note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 1024x768x24 -ac -noreset >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
	kill -0 "$XVFB_PID" 2>/dev/null || return 1
	DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fatal "Xvfb failed to start"

# --- seed the four WPrefs-managed user domains ------------------------------
note "seed non-default user domains"

mkdir -p "$DEFS" "$WMHOME/Library/WindowMaker"

# a marker key no schema default would ever write, plus a value the
# Focus panel would otherwise not hold after a refresh
cat > "$DEFS/WindowMaker" <<'EOS'
{
  ResetProbe = "seeded";
  PointerFollowsFocus = YES;
  MenuStyle = flat;
}
EOS

cat > "$DEFS/WMSound" <<'EOS'
{
  SoundPath = ("/tmp/wprefs-reset-seeded-sounds");
}
EOS

cat > "$DEFS/WMGLOBAL" <<'EOS'
{
  AntialiasedText = YES;
}
EOS

# WMRootMenu as a string reference, the common layout: the real menu
# lives next to it and the domain file just points there
printf '"menu"\n' > "$DEFS/WMRootMenu"
cat > "$WMHOME/Library/WindowMaker/menu" <<'EOS'
("Applications",
 ("XTerm", "EXEC", "xterm")
)
EOS

export WMAKER_USER_ROOT="$WMHOME"
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_USER_XDG WMAKER_USER_DATA WMAKER_USER_CACHE WMAKER_IPC_SOCKET

# --- wmaker -----------------------------------------------------------------
note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS" \
DISPLAY=$DPY \
XDG_RUNTIME_DIR="$RUNTIME" \
WMAKER_USER_ROOT="$WMHOME" \
"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
wm_ready() {
	DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null \
		| grep 'window id' >/dev/null
}
wait_for 80 wm_ready || fatal "wmaker did not start"

# --- WPrefs -----------------------------------------------------------------
note "start WPrefs"
LD_LIBRARY_PATH="$WMLIBS" \
DISPLAY=$DPY \
WMAKER_BIN_NAME="$ROOT/src/.libs/wmaker" \
WMAKER_USER_ROOT="$WMHOME" \
"$ROOT/WPrefs.app/.libs/WPrefs" >>"$LOG" 2>&1 &
WPID=$!
echo "$WPID" >> "$PIDFILE"

wprefs_win() {
	W=$(xdo search --onlyvisible --class WPrefs 2>/dev/null | head -n 1)
	[ -n "$W" ]
}
wait_for 40 wprefs_win || fatal "WPrefs window did not appear"
sleep 0.5

# --- open the Focus panel so it is initialized before the reset -------------
note "open the Focus panel (section 0)"
xdo mousemove --window "$W" 30 50 click 1
sleep 0.5

check "seeded domain still carries the probe before reset" \
	grep -q 'ResetProbe = "seeded"' "$DEFS/WindowMaker"

# --- click Reset Defaults and confirm the alert -----------------------------
note "click Reset Defaults and confirm"
xdo mousemove --window "$W" 264 364 click 1

alert_up() {
	# the alert panel shares the WPrefs WM_CLASS and carries a blank
	# WM_NAME, so find it as the visible WPrefs-class window that is
	# not the main window
	A=$(xdo search --onlyvisible --class WPrefs 2>/dev/null | grep -v -x "$W" | head -n 1)
	[ -n "$A" ]
}
if wait_for 40 alert_up; then
	check "reset confirmation alert appeared" true
	# activate so an XTEST Return lands on the panel, then send Return
	# through the focused window; the alert's key handler maps Return
	# to the default (Reset) button
	xdo windowactivate "$A" >/dev/null 2>&1
	sleep 0.3
	xdo key Return
	sleep 0.5
	if alert_up; then
		# fall back to a window-targeted synthetic key
		xdo key --window "$A" Return
		sleep 0.5
	fi
else
	check "reset confirmation alert appeared" false
	xdo key Escape
fi

# the reset is confirmed once the backup file lands
wait_for 50 [ -f "$DEFS/WindowMaker.wprefs-backup" ] \
	|| fail "reset was not confirmed (no WindowMaker.wprefs-backup)"
check "reset confirmed (backup created)" [ -f "$DEFS/WindowMaker.wprefs-backup" ]

# --- assert the domain files ------------------------------------------------
note "domain files after reset"

wait_for 50 [ -f "$DEFS/WindowMaker.wprefs-backup" ] \
	|| fail "WindowMaker.wprefs-backup missing"
check "WindowMaker backup exists" [ -f "$DEFS/WindowMaker.wprefs-backup" ]
check "WindowMaker backup keeps the seeded value" \
	grep -q 'ResetProbe = "seeded"' "$DEFS/WindowMaker.wprefs-backup"
check "WindowMaker domain no longer carries the probe" \
	lacks ResetProbe "$DEFS/WindowMaker"
check "WindowMaker domain rewritten empty" \
	sh -c "[ \"\$(wc -c < '$DEFS/WindowMaker')\" -lt 16 ]"

check "WMSound backup exists" [ -f "$DEFS/WMSound.wprefs-backup" ]
check "WMSound backup keeps the seeded path" \
	grep -q 'wprefs-reset-seeded-sounds' "$DEFS/WMSound.wprefs-backup"
check "WMSound domain file removed" [ ! -f "$DEFS/WMSound" ]

check "WMGLOBAL backup exists" [ -f "$DEFS/WMGLOBAL.wprefs-backup" ]
check "WMGLOBAL backup keeps AntialiasedText" \
	grep -q 'AntialiasedText' "$DEFS/WMGLOBAL.wprefs-backup"
check "WMGLOBAL domain emptied" \
	lacks 'AntialiasedText' "$DEFS/WMGLOBAL"

check "WMRootMenu backup exists" [ -f "$DEFS/WMRootMenu.wprefs-backup" ]
check "WMRootMenu domain removed" [ ! -f "$DEFS/WMRootMenu" ]

# --- wmaker restart ---------------------------------------------------------
note "wmaker restart"
sleep 2
check "wmaker re-exec'd in place and is alive" pid_alive "$WMAKER_PID"
check "wmaker still serving the display" wm_ready

# --- in-place refresh: a later Save must write the default ------------------
note "in-place refresh: Save after reset"
xdo mousemove --window "$W" 375 364 click 1

# the Focus panel's refreshed checkbox holds the schema default (NO), so
# the stale seeded YES must not reappear in the saved domain
wait_for 50 lacks 'PointerFollowsFocus = YES' "$DEFS/WindowMaker" \
	|| fail "saved domain still has PointerFollowsFocus = YES (stale panel state)"
check "saved domain drops the stale YES" \
	lacks 'PointerFollowsFocus = YES' "$DEFS/WindowMaker"
check "saved domain drops MenuStyle flat" \
	lacks 'MenuStyle = flat' "$DEFS/WindowMaker"
check "saved domain still parses" \
	grep -q '^{' "$DEFS/WindowMaker"

# --- close WPrefs -----------------------------------------------------------
note "close WPrefs"
xdo mousemove --window "$W" 465 364 click 1
wait_for 40 pid_gone "$WPID" || {
	kill "$WPID" 2>/dev/null
	fail "WPrefs did not exit on Close"
}

printf '\n' | tee -a "$LOG"
printf 'passed=%d failed=%d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
[ "$FAILED" -eq 0 ] || exit 1
exit 0
