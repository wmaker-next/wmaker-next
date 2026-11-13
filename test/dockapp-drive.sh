#!/bin/sh
# Real dockapp lifecycle. A helper client (test/dockapp-client.c) in the
# libdockapp shape (64x64 leader with WM_HINTS WithdrawnState +
# IconWindowHint + WindowGroupHint, unmapped 64x64 icon window) is
# launched and asserted through the whole supported path:
#
#   1. float    - is_dockapp detection (src/window.c:1122 class branch,
#                 :1137 WithdrawnState hints branch), withdrawn unmanage
#                 (:2022), appicon creation and icon-window swallowing
#                 (src/icon.c set_dockapp_in_icon)
#   2. dock     - pointer drag into a free dock slot (Mod1+Button1, the
#                 grab set_dockapp_in_icon installs on the tile),
#                 wDockAttachIcon command discovery, WMState persistence
#   3. restart  - warm restart, tile re-attachment via
#                 wDockTrackWindowLaunch, swallowed icon restored
#   4. withdrawn-hints mode - same float assertions with a normal
#                 WM_CLASS so only the hints branch fires
#   5. real dockapp (DOCKAPP_BIN, optional) - float + drag-dock against
#                 a foreign binary
#
# Runs wmaker under Xvfb on an isolated display and WMAKER_USER_ROOT,
# tracks every PID it spawns and kills only those.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${DOCKAPP_DRIVE_DISPLAY:-:$(drive_default_display)}
W=1024
H=768
RUNTIME=$(mktemp -d /tmp/wmaker-dockapp.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=${DOCKAPP_DRIVE_LOG:-/tmp/dockapp-drive.log}
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
HELPER="$RUNTIME/dockapp-client"
DOCK_X=$((W - 64))
SLOT_Y=64
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

start_wmaker() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		DISPLAY=$DPY \
		"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
	WMAKER_PID=$!
	echo "$WMAKER_PID" >> "$PIDFILE"
	wait_for 80 ipc_ready || fatal "wmaker did not answer get_version"
}

# viewable 64x64 root children, one "id x y" line per window
icon_candidates() {
	for w in $(DISPLAY=$DPY xwininfo -root -children 2>/dev/null \
			| awk '/^     0x/ && $(NF-1) ~ /^64x64\+/ {print $1}'); do
		DISPLAY=$DPY xwininfo -id "$w" 2>/dev/null \
			| grep -q 'Map State: IsViewable' || continue
		set -- $(DISPLAY=$DPY xwininfo -id "$w" 2>/dev/null \
			| awk '/Absolute upper-left X:/ {x=$4}
			       /Absolute upper-left Y:/ {y=$4}
			       END {print x, y}')
		echo "$w ${1:-} ${2:-}"
	done
}

# first viewable 64x64 root child outside the dock column (the floating
# appicon), the docked tiles and the withdrawn leader windows are
# excluded
floating_icon() {
	icon_candidates | while read -r w x y; do
		[ -n "$x" ] || continue
		[ "$x" = "$DOCK_X" ] && continue
		echo "$w"
		break
	done
}

count_dock_tiles() {
	DISPLAY=$DPY xwininfo -root -children 2>/dev/null \
		| grep -c "64x64+${DOCK_X}+"
}

# the icon window id inside the appicon tile tree, matched exactly
tree_has_icon() {
	DISPLAY=$DPY xwininfo -id "$1" -tree 2>/dev/null \
		| grep -E "$2( |$|\()" | grep -q '64x64'
}

single_64_child() {
	[ "$(DISPLAY=$DPY xwininfo -id "$1" -tree 2>/dev/null \
		| awk '/^     0x/ && /64x64/' | wc -l)" = "1" ]
}

log_clean() { ! grep -qF "Drawable invalid, skip reparenting dock app to icon." "$LOG"; }

client_in_list() {
	DISPLAY=$DPY xprop -root _NET_CLIENT_LIST 2>/dev/null | grep -qw "$1"
}
client_not_in_list() { ! client_in_list "$1"; }

# appicon abs coords as "x y"
icon_at_slot() {
	set -- $(DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null \
		| awk '/Absolute upper-left X:/ {x=$4}
		       /Absolute upper-left Y:/ {y=$4}
		       END {print x, y}')
	[ "$1" = "$DOCK_X" ] && [ "$2" = "$SLOT_Y" ]
}

# press Mod1+Button1 on the appicon center and drag to dock slot "0,1"
drag_to_slot() {
	dg_icon=$1
	# wAppBounce animates a freshly created appicon for up to ~1s,
	# let it settle before reading the drag origin
	sleep 1.5
	set -- $(DISPLAY=$DPY xwininfo -id "$dg_icon" 2>/dev/null \
		| awk '/Absolute upper-left X:/ {x=$4}
		       /Absolute upper-left Y:/ {y=$4}
		       /Width:/ {w=$2}
		       /Height:/ {h=$2}
		       END {print x + w / 2, y + h / 2}')
	[ -n "${1:-}" ] || return 1
	DISPLAY=$DPY xdotool mousemove --sync "$1" "$2" >>"$LOG" 2>&1
	DISPLAY=$DPY xdotool keydown Alt_L >>"$LOG" 2>&1
	DISPLAY=$DPY xdotool mousedown 1 >>"$LOG" 2>&1
	DISPLAY=$DPY xdotool mousemove_relative --sync 10 0 >>"$LOG" 2>&1
	i=0
	while [ "$i" -lt 6 ]; do
		i=$((i + 1))
		DISPLAY=$DPY xdotool mousemove --sync \
			$(( $1 + (DOCK_X + 32 - $1) * i / 6 )) \
			$(( $2 + (SLOT_Y + 32 - $2) * i / 6 )) >>"$LOG" 2>&1
	done
	DISPLAY=$DPY xdotool mouseup 1 >>"$LOG" 2>&1
	DISPLAY=$DPY xdotool keyup Alt_L >>"$LOG" 2>&1
}

WMSTATE="$WMHOME/Defaults/WMState"
state_has() { grep -qF "$1" "$WMSTATE"; }
entry_command_is() {
	awk -v key="$1" -v expected="$2" '
		/^[[:space:]]*{[[:space:]]*$/ { named = 0; count = 0; valid = 1 }
		$1 == "Name" && $3 == "datest.DockApp;" { named = 1 }
		$1 == key {
			count++
			line = $0
			sub(/^[[:space:]]*/, "", line)
			if (line != expected) valid = 0
		}
		/^[[:space:]]*},?[[:space:]]*$/ {
			if (named) {
				seen++
				if (count != 1 || !valid) failed = 1
			}
		}
		END { exit !(seen > 0 && !failed) }
	' "$WMSTATE"
}
entry_command_absent() {
	awk -v key="$1" '
		/^[[:space:]]*{[[:space:]]*$/ { named = 0; count = 0 }
		$1 == "Name" && $3 == "datest.DockApp;" { named = 1 }
		$1 == key { count++ }
		/^[[:space:]]*},?[[:space:]]*$/ {
			if (named) {
				seen++
				if (count != 0) failed = 1
			}
		}
		END { exit !(seen > 0 && !failed) }
	' "$WMSTATE"
}
entry_has() { grep -B2 -A7 -F "Name = $1;" "$WMSTATE" | grep -qF "$2"; }

# helper_out: print the window ids the helper wrote at startup
helper_ids() {
	hf_out=$1
	hf_leader=$(awk '/^leader/ {print $2; exit}' "$hf_out")
	hf_icon=$(awk '/^icon/ {print $2; exit}' "$hf_out")
}

# ----------------------------------------------------------------------

for tool in Xvfb xdpyinfo xwininfo xprop xdotool; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

${CC:-cc} -std=gnu11 "$ROOT/test/dockapp-client.c" -o "$HELPER" -lX11 >>"$LOG" 2>&1 \
	|| skip_all "could not build test/dockapp-client.c"

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
# dock enabled, clip/drawers off so the positional appicon scan sees
# only dockapp icons, an empty-ish WindowMaker domain stops the
# first-run installer
cat > "$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableClip = YES;
  DisableDrawers = YES;
}
EOF
# no Dock key: wmaker builds the default dock with just the main tile,
# so the first free slot is "0,1" at (DOCK_X, 64)
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
	echo "display $DPY is already in use, set DOCKAPP_DRIVE_DISPLAY to a free one" >&2
	exit 77
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
start_wmaker

# --- section 1: float ---------------------------------------------------------

note "launch the dockapp helper"
DISPLAY=$DPY "$HELPER" >"$RUNTIME/helper.out" 2>>"$LOG" &
HELPER_PID=$!
echo "$HELPER_PID" >> "$PIDFILE"
wait_for 30 sh -c "grep -q '^icon ' '$RUNTIME/helper.out'" \
	|| fatal "dockapp helper did not start"
helper_ids "$RUNTIME/helper.out"
LEADER=$hf_leader
ICON=$hf_icon
[ -n "$LEADER" ] && [ -n "$ICON" ] || fatal "could not read helper window ids"
note "helper leader=$LEADER icon=$ICON pid=$HELPER_PID"

appicon_appeared() {
	FLOATING=$(floating_icon)
	[ -n "$FLOATING" ]
}
check "floating appicon appeared outside the dock column" wait_for 60 appicon_appeared
AICON=$FLOATING
[ -n "$AICON" ] || fatal "could not locate the dockapp appicon"
note "appicon: $AICON"

check "dockapp leader is not managed (withdrawn, no frame)" \
	client_not_in_list "$LEADER"
check "appicon swallowed the 64x64 icon window" \
	wait_for 30 tree_has_icon "$AICON" "$ICON"
check "appicon tree has exactly one 64x64 child" single_64_child "$AICON"
check "no dockapp reparent failure in log" log_clean

# --- section 2: dock by drag ---------------------------------------------------

note "drag the appicon into dock slot 0,1"
BEFORE=$(count_dock_tiles)
drag_to_slot "$AICON"

tile_added() { [ "$(count_dock_tiles)" -gt "$BEFORE" ]; }
check "dock tile count increased" wait_for 30 tile_added
check "appicon docked at slot 0,1" wait_for 30 icon_at_slot "$AICON"
check "docked tile still holds the swallowed icon window" \
	tree_has_icon "$AICON" "$ICON"

note "exit wmaker to flush WMState"
msg exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ -f "$WMSTATE" ] || fatal "WMState was not written"
check "WMState Name = datest.DockApp" state_has 'Name = datest.DockApp;'
check "WMState Command = helper path" state_has "Command = \"$HELPER\";"
check "WMState PasteCommand = helper path with %s" \
	entry_command_is PasteCommand "PasteCommand = \"$HELPER %s\";"
if grep -q '^#define USE_DOCK_XDND 1' "$ROOT/config.h"; then
	check "WMState DropCommand = helper path with %d" \
		entry_command_is DropCommand "DropCommand = \"$HELPER %d\";"
fi
check "WMState entry AutoLaunch = No" \
	entry_has 'datest.DockApp' 'AutoLaunch = No;'
check "WMState entry Forced = No" \
	entry_has 'datest.DockApp' 'Forced = No;'
check "WMState entry Position = 0,1" \
	entry_has 'datest.DockApp' 'Position = "0,1";'

# --- section 3: restart --------------------------------------------------------

note "restart wmaker, the helper must survive and re-attach"
start_wmaker

check "dockapp helper survived the restart" kill -0 "$HELPER_PID"

tile_reattached() {
	TILE1=
	for t in $(DISPLAY=$DPY xwininfo -root -children 2>/dev/null \
			| awk -v col="64x64+${DOCK_X}+${SLOT_Y}" \
				'/^     0x/ && $(NF-1) == col {print $1}'); do
		if tree_has_icon "$t" "$ICON"; then
			TILE1=$t
			return 0
		fi
	done
	return 1
}
check "datest tile re-attached at 0,1 with the swallowed icon" \
	wait_for 100 tile_reattached
[ -n "$TILE1" ] || fatal "datest tile not found after restart"
# wAppBounce animates the freshly re-attached tile for ~1s, let it
# settle before any positional scan treats it as the dock column
sleep 2
check "re-attached tile settled at slot 0,1" icon_at_slot "$TILE1"
check "dock shows main tile + datest tile" \
	[ "$(count_dock_tiles)" = "2" ]

# --- section 4: withdrawn-hints mode --------------------------------------------

note "launch the helper in withdrawn-hints mode (normal WM_CLASS)"
DOCKAPP_HINTS=withdrawn DISPLAY=$DPY "$HELPER" >"$RUNTIME/helper2.out" 2>>"$LOG" &
HELPER2_PID=$!
echo "$HELPER2_PID" >> "$PIDFILE"
wait_for 30 sh -c "grep -q '^icon ' '$RUNTIME/helper2.out'" \
	|| fatal "second helper did not start"
helper_ids "$RUNTIME/helper2.out"
LEADER2=$hf_leader
ICON2=$hf_icon
[ -n "$LEADER2" ] && [ -n "$ICON2" ] || fatal "could not read second helper ids"
note "helper2 leader=$LEADER2 icon=$ICON2 pid=$HELPER2_PID"

appicon2_appeared() {
	FLOATING2=$(floating_icon)
	[ -n "$FLOATING2" ]
}
check "withdrawn-mode appicon appeared" wait_for 60 appicon2_appeared
AICON2=$FLOATING2
[ -n "$AICON2" ] || fatal "could not locate the withdrawn-mode appicon"

check "withdrawn-mode leader is not managed" client_not_in_list "$LEADER2"
check "withdrawn-mode appicon swallowed its icon window" \
	wait_for 30 tree_has_icon "$AICON2" "$ICON2"
check "withdrawn-mode appicon tree has exactly one 64x64 child" \
	single_64_child "$AICON2"

note "tear the withdrawn-mode helper down"
kill "$HELPER2_PID" 2>/dev/null
wait_for 30 pid_gone "$HELPER2_PID" || fatal "second helper refused to die"
appicon2_gone() {
	w=$(floating_icon)
	[ -z "$w" ]
}
check "withdrawn-mode appicon destroyed with its app" \
	wait_for 50 appicon2_gone

# --- section 5: real dockapp (optional) -----------------------------------------

if [ -n "${DOCKAPP_BIN:-}" ] && command -v "$DOCKAPP_BIN" >/dev/null 2>&1; then
	note "real dockapp: $DOCKAPP_BIN"
	DISPLAY=$DPY "$DOCKAPP_BIN" >>"$LOG" 2>&1 &
	REAL_PID=$!
	echo "$REAL_PID" >> "$PIDFILE"

	check "real dockapp appicon appeared" wait_for 60 appicon_appeared
	AICON3=$FLOATING
	if [ -n "$AICON3" ]; then
		# identify the leader by its WithdrawnState WM_HINTS, xprop
		# spells state 0 "Initial state is Don't Care State."
		REAL_LEADER=
		for w in $(DISPLAY=$DPY xwininfo -root -children 2>/dev/null \
				| awk '/^     0x/ {print $1}'); do
			DISPLAY=$DPY xprop -id "$w" WM_HINTS 2>/dev/null \
				| grep -Eqi "initial state is (don't care|withdrawn)" || continue
			REAL_LEADER=$w
			break
		done
		if [ -n "$REAL_LEADER" ]; then
			check "real dockapp leader is not managed" \
				client_not_in_list "$REAL_LEADER"
		else
			record_fail "real dockapp leader not found"
		fi
		# the icon window id comes from the leader's WM_HINTS, a
		# foreign dockapp's icon window need not be 64x64 (wmclock
		# is 54x57), so assert the appicon swallowed exactly that
		# child whatever its size
		REAL_ICONWIN=$(DISPLAY=$DPY xprop -id "${REAL_LEADER:-$AICON3}" \
			WM_HINTS 2>/dev/null \
			| awk '/to use for icon:/ {print $NF}')
		real_swallowed() {
			[ -n "$REAL_ICONWIN" ] || return 1
			[ "$(DISPLAY=$DPY xwininfo -tree -id "$1" 2>/dev/null \
				| awk '/^     0x/' | wc -l)" = "1" ] || return 1
			DISPLAY=$DPY xwininfo -tree -id "$1" 2>/dev/null \
				| grep -qE "$REAL_ICONWIN( |$|\()"
		}
		check "real dockapp appicon swallowed its WM_HINTS icon window" \
			wait_for 30 real_swallowed "$AICON3"
		# some dockapps do not publish WM_COMMAND, without it
		# wDockAttachIcon pops the command dialog and stalls
		DISPLAY=$DPY xprop -id "${REAL_LEADER:-$AICON3}" \
			-name WM_COMMAND -set WM_COMMAND "$DOCKAPP_BIN" \
			>>"$LOG" 2>&1
		BEFORE3=$(count_dock_tiles)
		drag_to_slot "$AICON3"
		tile3_added() { [ "$(count_dock_tiles)" -gt "$BEFORE3" ]; }
		check "real dockapp docked into a tile" wait_for 30 tile3_added
		check "real dockapp appicon sits at slot 0,1" \
			wait_for 30 icon_at_slot "$AICON3"
	else
		record_fail "real dockapp appicon never appeared"
	fi
	kill "$REAL_PID" 2>/dev/null
else
	note "skip real dockapp section (DOCKAPP_BIN not set)"
fi

# --- teardown -------------------------------------------------------------------

msg exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

check "PasteCommand survives restore and save without duplication" \
	entry_command_is PasteCommand "PasteCommand = \"$HELPER %s\";"
if grep -q '^#define USE_DOCK_XDND 1' "$ROOT/config.h"; then
	check "DropCommand survives restore and save without duplication" \
		entry_command_is DropCommand "DropCommand = \"$HELPER %d\";"
fi

note "restore the dockapp without secondary commands"
sed '/^[[:space:]]*PasteCommand = /d; /^[[:space:]]*DropCommand = /d' \
	"$WMSTATE" > "$RUNTIME/WMState.no-secondary" || fatal "could not remove secondary commands"
mv "$RUNTIME/WMState.no-secondary" "$WMSTATE" || fatal "could not update WMState"
start_wmaker
check "dockapp re-attaches without secondary commands" wait_for 100 tile_reattached
msg exit >>"$LOG" 2>&1 || fatal "could not save restored WMState"
wait_for 50 pid_gone "$WMAKER_PID" || fatal "wmaker did not exit"
check "restore does not regenerate PasteCommand" entry_command_absent PasteCommand
if grep -q '^#define USE_DOCK_XDND 1' "$ROOT/config.h"; then
	check "restore does not regenerate DropCommand" entry_command_absent DropCommand
fi

note "dockapp drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
[ "$FAILED" -eq 0 ] && [ "$PASSED" -gt 0 ]
