#!/bin/sh
# Drop a .desktop file onto the dock's main tile via XDND and verify a
# launcher is created in the next free slot and persisted to WMState.
# Runs wmaker under Xvfb on an isolated display and WMAKER_USER_ROOT,
# tracks every PID it spawns and kills only those.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${DOCKDROP_DRIVE_DISPLAY:-:92}
W=1024
H=768
RUNTIME=$(mktemp -d /tmp/wmaker-dockdrop.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=${DOCKDROP_DRIVE_LOG:-/tmp/dock-drop-drive.log}
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
XDNDSRC="$RUNTIME/xdnd-source"
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

# ----------------------------------------------------------------------

for tool in Xvfb xdpyinfo xwininfo xprop; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
# the DisplayName check asserts Name[en] wins over Name, which needs a
# working en_* LC_MESSAGES so the desktop-file parser sees "en" as the
# locale prefix, on a box with only C generated the localized key is
# never matched and both DisplayName checks would false-fail
locale -a 2>/dev/null | grep -qi '^en_' \
	|| skip_all "no en_* locale installed (locale-gen en_US.UTF-8)"

${CC:-cc} -std=gnu11 "$ROOT/test/xdnd-source.c" -o "$XDNDSRC" -lX11 >>"$LOG" 2>&1 \
	|| skip_all "could not build test/xdnd-source.c"

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
# an empty WindowMaker domain stops the first-run installer from
# overwriting WMState with the stock template
printf '{\n}\n' > "$WMHOME/Defaults/WindowMaker"

# minimal hicolor tree so launcherFindThemeIcon resolves a bare
# Icon= name without depending on the host's icon theme, the 48x48
# raster must lose to the 64x64 one
XDG_DATA_HOME="$RUNTIME/share"
ICON48="$XDG_DATA_HOME/icons/hicolor/48x48/apps"
ICON64="$XDG_DATA_HOME/icons/hicolor/64x64/apps"
mkdir -p "$ICON48" "$ICON64"
cp "$ROOT/wrlib/Tests/test.png" "$ICON48/scratch-term.png"
cp "$ROOT/wrlib/Tests/test.png" "$ICON64/scratch-term.png"

# the .desktop fixture, %F must be stripped from the resulting Command,
# the localized Name must win over the bare one
cat > "$RUNTIME/scratch.desktop" <<'EOD'
[Desktop Entry]
Type=Application
Name=Scratch Term
Name[en]=Scratch Terminal
Comment=A throwaway terminal for the test
Exec=/usr/bin/xterm -class ScratchTerm %F
Icon=scratch-term
StartupWMClass=ScratchTerm
EOD

export XDG_RUNTIME_DIR="$RUNTIME"
export XDG_DATA_HOME
export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export LANG=en_US.UTF-8
unset WMAKER_IPC_SOCKET
unset XDG_DATA_DIRS

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set DOCKDROP_DRIVE_DISPLAY to a free one" >&2
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

# locate the dock's main tile, it is the only XdndAware child of root
# sitting at the right edge of a fresh-config screen
DOCK_X=$((W - 64))
find_dock_tile() {
	DOCK=
	for w in $(DISPLAY=$DPY xwininfo -root -children \
			| awk '/^     0x/ {print $1}'); do
		DISPLAY=$DPY xprop -id "$w" XdndAware 2>/dev/null \
			| grep -q '^XdndAware(ATOM)' || continue
		x=$(DISPLAY=$DPY xwininfo -id "$w" \
			| awk '/Absolute upper-left X/{print $NF}')
		y=$(DISPLAY=$DPY xwininfo -id "$w" \
			| awk '/Absolute upper-left Y/{print $NF}')
		[ "$x" = "$DOCK_X" ] && [ "$y" = "0" ] && DOCK=$w && return 0
	done
	return 1
}
note "find dock main tile"
find_dock_tile || fatal "could not find dock main tile"
record_pass "dock main tile is XdndAware"

count_dock_tiles() {
	DISPLAY=$DPY xwininfo -root -children \
		| grep -c "64x64+${DOCK_X}+"
}
BEFORE=$(count_dock_tiles)

note "drop $RUNTIME/scratch.desktop on $DOCK"
DISPLAY=$DPY "$XDNDSRC" "$DOCK" "$RUNTIME/scratch.desktop" >>"$LOG" 2>&1
check "XdndStatus accepted and XdndFinished received" [ $? -eq 0 ]

tile_added() { [ "$(count_dock_tiles)" -gt "$BEFORE" ]; }
check "new dock tile mapped below the main tile" wait_for 30 tile_added

note "exit wmaker to flush WMState"
msg exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

WMSTATE="$WMHOME/Defaults/WMState"
[ -f "$WMSTATE" ] || fatal "WMState was not written"

state_has() { grep -qF "$1" "$WMSTATE"; }
attrs_has() { grep -qF "$1" "$WMHOME/Defaults/WMWindowAttributes"; }
check "Command stripped of %F field code" \
	state_has 'Command = "/usr/bin/xterm -class ScratchTerm";'
check "Name derived from Exec basename and StartupWMClass" \
	state_has "Name = xterm.ScratchTerm;"
check "DisplayName picked the localized Name[en] over Name" \
	state_has 'DisplayName = "Scratch Terminal";'
check "Comment carried through" \
	state_has 'Comment = "A throwaway terminal for the test";'
check "launcher placed at the first free slot" \
	state_has 'Position = "0,1";'
check "bare Icon name resolved to the 64x64 hicolor raster" \
	attrs_has "$ICON64/scratch-term.png"

note "restart wmaker, DisplayName/Comment must survive a load/save cycle"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
DISPLAY=$DPY \
"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
wait_for 80 ipc_ready || fatal "wmaker did not answer get_version on restart"
msg exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"
check "DisplayName survives a restore/save cycle" \
	state_has 'DisplayName = "Scratch Terminal";'
check "Comment survives a restore/save cycle" \
	state_has 'Comment = "A throwaway terminal for the test";'

note "restart wmaker, launch a class-mismatched tile via PID fallback"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
DISPLAY=$DPY \
"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
wait_for 80 ipc_ready || fatal "wmaker did not answer get_version on restart"

# a fixture without StartupWMClass= whose Exec basename does not match the
# WM_CLASS the app actually maps with, this is the .desktop-drop case for
# any toolkit that sets a generic class (FLTK) or where the binary name and
# class differ (firefox -> Navigator), so the tile ends up xterm.xterm but
# the window is notxterm.NotXTerm and only the spawned-child PID can link
# the two
cat > "$RUNTIME/noclass.desktop" <<'EOD'
[Desktop Entry]
Type=Application
Name=NoClass
Exec=xterm -name notxterm -class NotXTerm -geometry 60x4+100+100 -e sleep 60
EOD

count_yard_icons() {
	DISPLAY=$DPY xwininfo -root -children \
		| grep -c "64x64+[0-9]*+$((H - 64)) "
}
window_managed() {
	msg get_windows 2>/dev/null | grep -q "class = NotXTerm"
}
window_gone() { ! window_managed; }
notxterm_pid() {
	notx_id=$(msg get_windows 2>/dev/null \
		| sed -n 's/.*id = \(0x[0-9a-f]*\);class = NotXTerm.*/\1/p')
	[ -n "$notx_id" ] || return 1
	DISPLAY=$DPY xprop -id "$notx_id" _NET_WM_PID 2>/dev/null \
		| awk '/CARDINAL/{print $NF}'
}

YARD_BEFORE=$(count_yard_icons)

find_dock_tile || fatal "could not re-find dock main tile after restart"
note "drop $RUNTIME/noclass.desktop on $DOCK"
DISPLAY=$DPY "$XDNDSRC" "$DOCK" "$RUNTIME/noclass.desktop" >>"$LOG" 2>&1
tile2_added() { [ "$(count_dock_tiles)" -ge "$((BEFORE + 2))" ]; }
check "second launcher tile mapped" wait_for 30 tile2_added

note "launch via dock-launch 2 (class mismatch, PID fallback)"
msg -q dock-launch 2 >>"$LOG" 2>&1
check "dock-launch 2 accepted" [ $? -eq 0 ]
check "NotXTerm window managed" wait_for 50 window_managed

# without the PID fallback the dock tile cannot claim the window and a
# loose appicon lands in the bottom-row icon yard instead
check "no loose appicon spawned for class-mismatched launch" \
	[ "$(count_yard_icons)" -eq "$YARD_BEFORE" ]

NOTX_PID=$(notxterm_pid)
echo "$NOTX_PID" >> "$PIDFILE"
kill "$NOTX_PID" 2>/dev/null
i=0
while [ $i -lt 45 ]; do
    ipc_n=$(msg get_windows 2>/dev/null | grep -c 'class = NotXTerm')
    [ "$ipc_n" = "0" ] && { echo "DEBUGCLEARED t=$i" >> "$LOG"; break; }
    i=$((i+1)); sleep 1
done
if [ "$ipc_n" != "0" ]; then
    echo "DEBUGSTALE holding for inspection" >> "$LOG"
    n=0
    while [ ! -f /tmp/opencode/hold-release ] && [ "$n" -lt 1200 ]; do
        sleep 1; n=$((n+1))
    done
fi
check "NotXTerm window unmanaged after kill" [ "$i" -lt 45 ]

# wDockLaunchWithState silently refuses while running||launching, and
# with shared_appicon the tile only resets once the DestroyNotify for
# the wmaker-owned fake group leader has round-tripped, so poll the
# relaunch instead of assuming the first attempt is accepted
relaunch_works() {
	msg -q dock-launch 2 >/dev/null 2>&1
	window_managed
}
check "tile reset after child exit, second launch maps a window" \
	wait_for 50 relaunch_works
NOTX_PID=$(notxterm_pid)
echo "$NOTX_PID" >> "$PIDFILE"
kill "$NOTX_PID" 2>/dev/null
wait_for 50 window_gone

msg exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"
check "second tile recorded as basename.basename without StartupWMClass" \
	state_has "Name = xterm.xterm;"

note "dock drop drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
[ "$FAILED" -eq 0 ] && [ "$PASSED" -gt 0 ]
