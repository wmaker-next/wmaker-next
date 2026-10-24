#!/bin/sh
# Drive the WINGs xkb fallback: a server without the XKB extension (or
# with an uncompilable server keymap) must degrade WINGs text input to
# a best-effort RMLVO keymap instead of dead. XKB cannot be runtime-
# disabled in Xvfb, so the absence is simulated with an LD_PRELOAD shim
# interposing xkb_x11_setup_xkb_extension (mode no-ext) or
# xkb_x11_keymap_new_from_device (mode no-map: the extension comes up
# but the server keymap never compiles). The run dialog (a WINGs text
# field) is opened through a root-menu %A entry, a command is typed
# with XTEST and committed with Return, and the command must run: the
# lowercase sentinel file proves text production, the capital-letter
# one proves the modifier tracking of the fallback state (core key
# stream in the no-ext leg, XKB state events in the no-map leg). An
# XKB-present leg guards the normal path.
#
# Discrimination: without the wxkb.c fallback (git stash), both
# fallback legs fail their sentinel checks (the dialog cannot receive
# text) while the XKB-present leg still passes.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY_BASE=${WXKB_FB_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wxkb-fb-drive.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/wxkb-compose-fallback-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
WMAKER="$ROOT/src/.libs/wmaker"
SHIM="$RUNTIME/xkb-shim.so"
DPY=
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
record_pass() { PASSED=$((PASSED + 1)); printf 'PASS: %s\n' "$*" | tee -a "$LOG"; }
record_fail() { FAILED=$((FAILED + 1)); printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; }
fatal() { printf 'FATAL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
skip_all() { printf 'SKIP: %s\n' "$*" | tee -a "$LOG"; exit 77; }

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
	_wf=$1; shift
	while [ "$_wf" -gt 0 ]; do
		"$@" && return 0
		sleep 0.1
		_wf=$((_wf - 1))
	done
	return 1
}

pid_gone() { ! kill -0 "$1" 2>/dev/null; }

for tool in Xvfb xdpyinfo xwininfo xdotool gcc; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$WMAKER" ] || skip_all "wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

note "build the xkb shim"
cat > "$RUNTIME/xkb-shim.c" <<'EOF'
#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-x11.h>

typedef int (*setup_fn)(xcb_connection_t *, uint16_t, uint16_t,
			enum xkb_x11_setup_xkb_extension_flags,
			uint16_t *, uint16_t *, uint8_t *, uint8_t *);
typedef struct xkb_keymap *(*keymap_fn)(struct xkb_context *,
					xcb_connection_t *, int32_t,
					enum xkb_keymap_compile_flags);

int xkb_x11_setup_xkb_extension(xcb_connection_t *conn,
				uint16_t wanted_major, uint16_t wanted_minor,
				enum xkb_x11_setup_xkb_extension_flags flags,
				uint16_t *major_ext_out, uint16_t *minor_ext_out,
				uint8_t *base_event_out, uint8_t *base_error_out)
{
	if (getenv("XKB_SHIM_NO_EXT"))
		return 0;
	return ((setup_fn)dlsym(RTLD_NEXT, "xkb_x11_setup_xkb_extension"))
		(conn, wanted_major, wanted_minor, flags, major_ext_out,
		 minor_ext_out, base_event_out, base_error_out);
}

struct xkb_keymap *xkb_x11_keymap_new_from_device(struct xkb_context *ctx,
						  xcb_connection_t *conn,
						  int32_t device_id,
						  enum xkb_keymap_compile_flags flags)
{
	if (getenv("XKB_SHIM_NO_MAP"))
		return NULL;
	return ((keymap_fn)dlsym(RTLD_NEXT, "xkb_x11_keymap_new_from_device"))
		(ctx, conn, device_id, flags);
}
EOF
gcc -shared -fPIC -o "$SHIM" "$RUNTIME/xkb-shim.c" -ldl \
	|| skip_all "cannot build the xkb shim"

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  MouseRightButtonAction = OpenApplicationsMenu;
}
EOF
# the %A entry opens the advanced run dialog, EXEC prepends the
# "exec " the shell runs
cat >"$WMHOME/Defaults/WMRootMenu" <<'EOF'
("Root",
 ("Run...", EXEC, "%A(Run,Program to run)"))
EOF

export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET
unset XKB_DEFAULT_RULES XKB_DEFAULT_MODEL XKB_DEFAULT_LAYOUT \
	XKB_DEFAULT_VARIANT XKB_DEFAULT_OPTIONS

msg() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" "$@"
}
ipc_ready() { msg -q get_version >/dev/null 2>&1; }

no_fallback_warning() { ! grep -q "falling back to a best-effort keymap" "$1"; }

xdo() { DISPLAY=$DPY xdotool "$@"; sleep 0.05; }

# locate the root menu's first entry row after a Button3 press at
# CX,CY, echoes "abs_x abs_y inner_height"
menu_entry() {
	DISPLAY=$DPY xwininfo -root -tree 2>/dev/null \
	| awk -v cx="$CX" -v cy="$CY" '
		/^     0x/ { fx=""; fy=""; fw="";
			if (match($0, /[0-9]+x[0-9]+\+-?[0-9]+\+-?[0-9]+/)) {
				split(substr($0,RSTART,RLENGTH), g, /[x+]/);
				fw=g[1]; fx=g[3]; fy=g[4];
			}
			inframe = (fx!="" && fy==cy && fx<=cx && cx<=fx+fw);
			next; }
		inframe && /^        0x/ {
			if (match($0, /[0-9]+x[0-9]+\+-?[0-9]+\+-?[0-9]+  \+-?[0-9]+\+-?[0-9]+$/)) {
				split(substr($0,RSTART,RLENGTH), g, /[x+ ]+/);
				if (g[6] > cy) { print g[5] " " g[6] " " g[2]; exit; }
			} }'
}

# drive_leg DPY SHIM REQ_WARN NO_WARN TAG WMLOG TYPED SENT_LO SENT_HI
# SHIM: "no-ext" | "no-map" | ""
# REQ_WARN: wmaker log line that must be present ("" skips the check)
# NO_WARN: 1 asserts no fallback warning landed in the wmaker log
# TYPED: the command the dialog receives, SENT_LO/HI its sentinel
#        files, SENT_HI contains a capital so the T rides Shift
drive_leg() {
	_DPY=$1; _SHIM=$2; _REQ=$3; _NO=$4; _TAG=$5; _WMLOG=$6
	_TYPED=$7; _LO=$8; _HI=$9

	DPY=$_DPY
	rm -f "$_LO" "$_HI" "$_WMLOG"
	note "leg $_TAG: Xvfb $DPY"

	if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
		echo "display $DPY is already in use, set WXKB_FB_DRIVE_DISPLAY" >&2; exit 77
	fi

	Xvfb "$DPY" -screen 0 1024x768x24 -ac >>"$LOG" 2>&1 &
	XVFB_PID=$!
	echo "$XVFB_PID" >> "$PIDFILE"
	xvfb_ready() {
		kill -0 "$XVFB_PID" 2>/dev/null || return 1
		DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
	}
	wait_for 40 xvfb_ready || fatal "Xvfb failed to start ($_TAG)"

	note "start wmaker (shim: ${_SHIM:-none})"
	case "$_SHIM" in
	no-ext)
		# the shim preloads ahead of libasan, whose link-order check
		# would otherwise abort the instrumented binary at startup
		LD_LIBRARY_PATH="$WMLIBS" LD_PRELOAD="$SHIM" XKB_SHIM_NO_EXT=1 \
			ASAN_OPTIONS="$(drive_asan_options):verify_asan_link_order=0" \
			DISPLAY=$DPY "$WMAKER" --for-real >>"$_WMLOG" 2>&1 & ;;
	no-map)
		LD_LIBRARY_PATH="$WMLIBS" LD_PRELOAD="$SHIM" XKB_SHIM_NO_MAP=1 \
			ASAN_OPTIONS="$(drive_asan_options):verify_asan_link_order=0" \
			DISPLAY=$DPY "$WMAKER" --for-real >>"$_WMLOG" 2>&1 & ;;
	"")
		LD_LIBRARY_PATH="$WMLIBS" ASAN_OPTIONS="$(drive_asan_options)" \
			DISPLAY=$DPY "$WMAKER" --for-real \
			>>"$_WMLOG" 2>&1 & ;;
	esac
	WMAKER_PID=$!
	echo "$WMAKER_PID" >> "$PIDFILE"
	wm_ready() {
		DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null \
			| grep 'window id' >/dev/null
	}
	wait_for 120 wm_ready || fatal "wmaker did not come up ($_TAG)"
	SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
	wait_for 120 ipc_ready || fatal "wmaker IPC did not come up ($_TAG)"
	sleep 0.5

	check "wmaker alive on startup ($_TAG)" kill -0 "$WMAKER_PID"
	if [ -n "$_REQ" ]; then
		check "fallback warning logged ($_TAG)" grep -q "$_REQ" "$_WMLOG"
	fi
	if [ "$_NO" = 1 ]; then
		check "no fallback warning logged ($_TAG)" \
			no_fallback_warning "$_WMLOG"
	fi

	CX=512
	CY=384
	note "open the run dialog through the root menu ($_TAG)"
	xdo mousemove "$CX" "$CY"
	xdo mousedown 3
	sleep 0.3
	ENTRY=$(menu_entry)
	[ -n "$ENTRY" ] || fatal "could not locate the menu entry ($_TAG)"
	set -- $ENTRY
	EX=$1; EY=$2; EH=$3
	ROWH=$(( EH + 1 ))
	PX=$(( EX + 6 ))
	PY=$(( EY + ROWH / 4 ))
	note "entry at +$EX+$EY h=$EH, releasing at $PX,$PY"
	xdo mousemove "$PX" "$PY"
	sleep 0.2
	xdo mouseup 3
	sleep 1

	note "type the command ($_TAG)"
	DISPLAY=$DPY xdotool type --delay 40 -- "$_TYPED"
	sleep 0.5
	DISPLAY=$DPY xdotool key --delay 100 Return

	if wait_for 100 test -f "$_LO"; then
		record_pass "typed command ran ($_TAG)"
	else
		record_fail "typed command ran ($_TAG)"
		DISPLAY=$DPY xwininfo -root -tree >>"$LOG" 2>&1
	fi
	check "modifier state tracked ($_TAG)" test -f "$_HI"
	check "wmaker alive after typing ($_TAG)" kill -0 "$WMAKER_PID"

	note "tear down leg $_TAG"
	msg -q exit >>"$LOG" 2>&1
	wait_for 50 pid_gone "$WMAKER_PID" || kill -9 "$WMAKER_PID" 2>/dev/null
	kill "$XVFB_PID" 2>/dev/null
	wait_for 50 pid_gone "$XVFB_PID"
	grep -v "^$WMAKER_PID$\|^$XVFB_PID$" "$PIDFILE" > "$PIDFILE.new"
	mv "$PIDFILE.new" "$PIDFILE"
	sleep 0.5
}

BASEN=${DPY_BASE#:}
D0=$DPY_BASE
D1=":$(( BASEN + 1 ))"
D2=":$(( BASEN + 2 ))"
RM="$RUNTIME"

drive_leg "$D0" no-ext \
	"the X server has no XKB extension" 0 "no-ext" "$RUNTIME/wm-noext.log" \
	"touch $RM/typed $RM/Typed" "$RM/typed" "$RM/Typed"

drive_leg "$D1" no-map \
	"could not load the XKB keymap" 0 "no-map" "$RUNTIME/wm-nomap.log" \
	"touch $RM/mapped $RM/Mapped" "$RM/mapped" "$RM/Mapped"

drive_leg "$D2" "" "" 1 "xkb-present" "$RUNTIME/wm-live.log" \
	"touch $RM/live $RM/Live" "$RM/live" "$RM/Live"

note "summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"

[ "$FAILED" -eq 0 ] && [ "$PASSED" -gt 0 ]
