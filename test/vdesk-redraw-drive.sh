#!/bin/sh
# Drive the virtual-desktop pan repaint of titlebar chrome (finding:
# titlebar button glyphs stayed blank while the desktop was panned).
#
# wVdeskSetViewport() moves every window, so each pan step uncovers
# chrome that the server background-fills and republishes as Expose.
# The drag-pan and edge-scroll loops only waited for pointer motion and
# so held those Exposes until the gesture ended: the close glyph only
# appeared once the button was released (drag-pan) or the scroll loop
# idled out (edge-scroll). Both loops now dispatch Expose as it arrives.
#
# Scenario: an xterm parked so its close button sits fully off the right
# screen edge, a second xterm further right so the viewport has room to
# pan. Drag-pan left (MouseRightButtonAction=VirtualDesktopPan) and
# edge-scroll right until the button slides into view; the button crop
# taken mid-gesture must already match the crop taken afterwards.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${VDESK_REDRAW_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-vdesk-redraw.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/vdesk-redraw-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
WDWRITE="$ROOT/util/.libs/wdwrite"
SCR_W=1024
SCR_H=768
PAN=250
PASSED=0
FAILED=0
SOCKET=
XTW=
FILL=
BTN=
BSZ=
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
ok()   { PASSED=$((PASSED + 1)); printf 'PASS: %s\n' "$*" | tee -a "$LOG"; }
fail() { FAILED=$((FAILED + 1)); printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; }
fatal() { printf 'FATAL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
skip_all() { printf 'SKIP: %s\n' "$*" | tee -a "$LOG"; exit 77; }

check() {
	cd_desc=$1; shift
	if "$@"; then ok "$cd_desc"; else fail "$cd_desc"; fi
}

wait_for() {
	n=$1; shift
	while [ "$n" -gt 0 ]; do "$@" && return 0; sleep 0.1; n=$((n - 1)); done
	return 1
}
pid_gone() { ! kill -0 "$1" 2>/dev/null; }

msg() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" "$@"
}
ipc_ready() { msg -q get_version >/dev/null 2>&1; }
wdwrite() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		DISPLAY=$DPY "$WDWRITE" "$@"
}
xdo() { DISPLAY=$DPY xdotool "$@"; }

for tool in Xvfb xdpyinfo xdotool xprop xterm xwininfo import; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
[ -x "$WDWRITE" ] || skip_all "util/.libs/wdwrite not built"
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "wmaker not built"

export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  EnableVirtualDesktop = YES;
  DefaultWorkspaceLayout = float;
  HotCorners = NO;
  NewStyle = new;
  TitlebarLeft = ();
  TitlebarRight = (Close);
}
EOF

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set VDESK_REDRAW_DRIVE_DISPLAY" >&2
	exit 77
fi

note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 ${SCR_W}x${SCR_H}x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
wait_for 40 sh -c "kill -0 $XVFB_PID 2>/dev/null && DISPLAY=$DPY xdpyinfo >/dev/null 2>&1" \
	|| fatal "Xvfb failed to start"

note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	ASAN_OPTIONS="$(drive_asan_options)" \
	DISPLAY=$DPY \
	"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
wait_for 120 ipc_ready || fatal "wmaker IPC did not come up"
sleep 0.5

frame_of() {
	DISPLAY=$DPY xwininfo -tree -id "$1" 2>/dev/null \
		| awk '/Parent window id:/ {print $4; exit}'
}

frame_top() {
	ftcur=$1
	ftroot=$(DISPLAY=$DPY xwininfo -root 2>/dev/null \
		     | awk '/Window id:/ {print $4; exit}')
	ftn=0
	while [ "$ftn" -lt 5 ]; do
		ftp=$(frame_of "$ftcur")
		[ -n "$ftp" ] && [ "$ftp" != "0x0" ] || return 1
		[ "$ftp" = "$ftroot" ] && { printf '%s\n' "$ftcur"; return 0; }
		ftcur=$ftp
		ftn=$((ftn + 1))
	done
	return 1
}

# the close-button core: the rightmost square frame child in the
# titlebar height range, i.e. the TitlebarRight cluster
find_button() {
	frame=$(frame_top "$1")
	[ -n "$frame" ] || return 1
	BTN=
	for pair in $(DISPLAY=$DPY xwininfo -children -id "$frame" 2>/dev/null \
		| awk '/^ *0x/ {
			g = $(NF-1)
			if (split(g, a, /[x+]/) < 4) next
			if (a[1]+0 != a[2]+0) next
			if (a[1]+0 <= 4 || a[1]+0 > 64) next
			print $1
		}'); do
		BTN=$pair
	done
	[ -n "$BTN" ] || return 1
	BSZ=$(DISPLAY=$DPY xwininfo -id "$BTN" | awk '/Width:/ {print $2; exit}')
	return 0
}

button_x() {
	DISPLAY=$DPY xwininfo -id "$BTN" | awk '/Absolute upper-left X/ {print $4}'
}
button_y() {
	DISPLAY=$DPY xwininfo -id "$BTN" | awk '/Absolute upper-left Y/ {print $4}'
}
win_abs_x() {
	DISPLAY=$DPY xwininfo -id "$1" | awk '/Absolute upper-left X/ {print $4}'
}

# place the watched client so its close button sits fully off the right
# screen edge: one probe move gives the fixed button-vs-client offset
park_offscreen() {
	xdo windowmove "$XTW" "$SCR_W" 120
	sleep 0.5
	bx=$(button_x)
	[ -n "$bx" ] || fatal "close button has no position"
	xdo windowmove "$XTW" $((2 * SCR_W - bx)) 120
	sleep 0.5
	bx=$(button_x)
	[ "$bx" -ge "$SCR_W" ] || fatal "could not park the button off-screen (at $bx)"
}

crop_button() {
	crop_out=$1
	crop_x=$(button_x)
	crop_y=$(button_y)
	[ -n "$crop_x" ] && [ -n "$crop_y" ] || fatal "close button has no position"
	[ "$crop_x" -ge 0 ] && [ "$((crop_x + BSZ))" -le "$SCR_W" ] \
		|| fatal "close button is not fully on-screen at $crop_x"
	DISPLAY=$DPY import -window root -crop "${BSZ}x${BSZ}+${crop_x}+${crop_y}" \
		+repage "png:$crop_out" >>"$LOG" 2>&1 || fatal "import failed for $crop_out"
}

start_xterm() {
	DISPLAY=$DPY xterm -geometry 40x12+120+120 -e 'sleep 600' >>"$LOG" 2>&1 &
	echo "$!" >> "$PIDFILE"
	ROOTW=$(DISPLAY=$DPY xwininfo -root 2>/dev/null \
		    | awk '/Window id:/ {print $4; exit}')
	xt_mapped() {
		for w in $(DISPLAY=$DPY xdotool search --class XTerm 2>/dev/null); do
			p=$(frame_of "$w")
			[ -n "$p" ] && [ "$p" != "0x0" ] && [ "$p" != "$ROOTW" ] || continue
			[ "$w" = "$XTW" ] && continue
			[ "$w" = "$FILL" ] && continue
			NEWXT=$w
			return 0
		done
		return 1
	}
	NEWXT=
	wait_for 50 xt_mapped || fatal "xterm did not map"
}

note "map the watched xterm and park its close button off-screen right"
start_xterm
XTW=$NEWXT
find_button "$XTW" || fatal "no close-button core on the watched xterm"
xdo windowactivate "$XTW"; sleep 0.4
park_offscreen
ok "close button parked off-screen at x=$(button_x)"

note "map the filler xterm past the right edge so the pan has room"
start_xterm
FILL=$NEWXT
xdo windowmove "$FILL" $((SCR_W + 500)) 300
sleep 0.5

# --- 1. drag-pan (VirtualDesktopPan) --------------------------------------
note "drag-pan left slides the button into view mid-drag"
wdwrite WindowMaker MouseRightButtonAction VirtualDesktopPan
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.4

xdo mousemove 500 $((SCR_H - 80)); sleep 0.2
xdo mousedown 3; sleep 0.2
i=0
while [ "$i" -lt 5 ]; do
	xdo mousemove $((500 - (i + 1) * PAN / 5)) $((SCR_H - 80))
	sleep 0.15
	i=$((i + 1))
done
sleep 0.3
crop_button "$RUNTIME/pan-mid.png"
xdo mouseup 3
sleep 1.0
crop_button "$RUNTIME/pan-post.png"
if cmp -s "$RUNTIME/pan-mid.png" "$RUNTIME/pan-post.png"; then
	ok "drag-pan repaints the close glyph mid-drag"
else
	fail "drag-pan repaints the close glyph mid-drag (blank until ButtonRelease)"
fi
check "wmaker alive after the drag-pan" kill -0 "$WMAKER_PID"

# a known-good crop of the same button for the glyph sanity check
note "reference crop of the painted glyph"
xdo windowmove "$XTW" 200 120
sleep 0.8
find_button "$XTW" || fatal "close button disappeared"
crop_button "$RUNTIME/ref.png"
check "drag-pan crop already shows the painted glyph" \
	cmp -s "$RUNTIME/pan-post.png" "$RUNTIME/ref.png"

# --- 2. edge-scroll -------------------------------------------------------
note "edge-scroll slides the button into view mid-scroll"
msg set-viewport 0 0
sleep 0.4
find_button "$XTW" || fatal "close button disappeared"
park_offscreen

( i=0
  while [ "$i" -lt 30 ]; do
	  DISPLAY=$DPY xdotool mousemove $((SCR_W - 1)) $((SCR_H / 2)) >>"$LOG" 2>&1
	  sleep 0.15
	  i=$((i + 1))
  done ) &
WIG=$!
sleep 1.2
crop_button "$RUNTIME/edge-mid.png"
kill "$WIG" 2>/dev/null
wait "$WIG" 2>/dev/null
sleep 1.2
crop_button "$RUNTIME/edge-post.png"
if cmp -s "$RUNTIME/edge-mid.png" "$RUNTIME/edge-post.png"; then
	ok "edge-scroll repaints the close glyph mid-scroll"
else
	fail "edge-scroll repaints the close glyph mid-scroll (blank until idle)"
fi
check "wmaker alive after the edge-scroll" kill -0 "$WMAKER_PID"
check "edge-scroll crop already shows the painted glyph" \
	cmp -s "$RUNTIME/edge-post.png" "$RUNTIME/ref.png"

note "summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

if [ "$total" -eq 0 ]; then
	echo "no checks ran" | tee -a "$LOG"
	exit 1
fi
[ "$FAILED" -eq 0 ] || exit 1
exit 0
