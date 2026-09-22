#!/bin/sh
# Regression drive test for the SharedEdgeResize-gated paired seam
# resize: wFindTilePartnerAxis (moveres.c) detects the window tiled
# against the grabbed one and wMouseResizeWindow moves both windows so
# one grows by d while the other shrinks by d, keeping the seam glued,
# and clears the maximize state of both on release.
#
# The pairs are created with the WindowSnapping drag-to-edge path
# (left/right and top/bottom halves), the seam is dragged through
# _NET_WM_MOVERESIZE plus real button-motion, mirroring how a client
# initiates an interactive resize.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${SHARED_EDGE_DRIVE_DISPLAY:-:$(drive_default_display)}
W=1280
H=800
RUNTIME=$(mktemp -d /tmp/shared-edge-drive.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=$(mktemp /tmp/shared-edge-drive.XXXXXX)
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
WDWRITE="$ROOT/util/.libs/wdwrite"
CLIENT_BIN="$WMHOME/ewmh-client"
SOCKET=
: > "$PIDFILE"
: > "$LOG"

PASSED=0
FAILED=0

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

set_config() {
	set_config_key=$1; set_config_val=$2
	wdwrite WindowMaker "$set_config_key" "$set_config_val" >>"$LOG" 2>&1
	msg -q reconfigure >>"$LOG" 2>&1
	sleep 0.3
}

# xdotool with a short settle so wmaker dispatches each event before
# the next one arrives (same race guard as snapping-drive.sh)
xdo() { DISPLAY=$DPY xdotool "$@"; sleep 0.05; }

for tool in Xvfb xdpyinfo xprop xwininfo xdotool xterm cc; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "wmakermsg not built"
[ -x "$WDWRITE" ] || skip_all "wdwrite not built"

export WMAKER_USER_ROOT="$WMHOME"
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_USER_XDG
unset WMAKER_IPC_SOCKET
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"

# EdgeResistance=0 + Attraction=NO keep snapResizeGeometry out of the
# way so the seam math alone decides the outcome. WindowSnapping=YES
# builds the half pairs via drag-to-edge. OpaqueResize=YES exercises
# the live partner configure path.
cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  OpaqueMove = YES;
  OpaqueResize = YES;
  EdgeResistance = 0;
  Attraction = NO;
  WindowGap = 0;
  DontLinkWorkspaces = YES;
  WindowSnapping = YES;
  SnapEdgeDetect = 8;
  SnapCornerDetect = 20;
  SnapToTopMaximizesFullscreen = NO;
  AlternativeHalfMaximized = NO;
  MoveHalfMaximizedWindowsBetweenScreens = NO;
  DragMaximizedWindow = Move;
  SharedEdgeResize = YES;
}
EOF

cc "$ROOT/test/ewmh-client.c" -o "$CLIENT_BIN" -lX11 >>"$LOG" 2>&1 \
	|| skip_all "could not build ewmh-client helper"

# --- Xvfb ----------------------------------------------------------------
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set SHARED_EDGE_DRIVE_DISPLAY" >&2
	exit 77
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 ${W}x${H}x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
wait_for 40 sh -c "kill -0 $XVFB_PID 2>/dev/null && DISPLAY=$DPY xdpyinfo >/dev/null 2>&1" \
	|| fatal "Xvfb failed to start"

# --- wmaker ---------------------------------------------------------------
note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	ASAN_OPTIONS="abort_on_error=0:halt_on_error=0:detect_leaks=0" \
	DISPLAY=$DPY \
	"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY XDG_RUNTIME_DIR="$RUNTIME" "$WMAKERMSG" --get-socketpath)
wait_for 80 ipc_ready || fatal "wmaker IPC did not come up"
sleep 0.5

# --- geometry helpers (from snapping-drive.sh) ----------------------------
frame_of() {
	DISPLAY=$DPY xwininfo -tree -id "$1" 2>/dev/null \
		| awk '/Parent window id:/ {print $4; exit}'
}
frame_top() {
	ftcur=$1
	ftroot=$(DISPLAY=$DPY xwininfo -root 2>/dev/null \
		| awk '/Window id:/ {print $4; exit}')
	ftn=0
	while [ $ftn -lt 5 ]; do
		ftp=$(frame_of "$ftcur")
		[ -n "$ftp" -a "$ftp" != "0x0" ] || return 1
		[ "$ftp" = "$ftroot" ] && { printf '%s\n' "$ftcur"; return 0; }
		ftcur=$ftp
		ftn=$((ftn + 1))
	done
	return 1
}
# returns "x y w h" for the frame outer rect (border included)
frame_geom() {
	fgframe=$(frame_top "$1") || { echo "0 0 0 0"; return; }
	DISPLAY=$DPY xwininfo -id "$fgframe" 2>/dev/null \
		| awk '/Absolute upper-left X:/ {x=$4}
		       /Absolute upper-left Y:/ {y=$4}
		       /Width:/  && !w {w=$2}
		       /Height:/ && !h {h=$2}
		       /Border width:/ {b=$3}
		       END {print x, y, w+2*b, h+2*b}'
}

has_atom() {
	DISPLAY=$DPY xprop -id "$1" _NET_WM_STATE 2>/dev/null | grep -Fq "$2"
}
lacks_atom() { ! has_atom "$1" "$2"; }

# --- spawn an xterm and return its client XID -----------------------------
XT_PID=
spawn_xt() {
	title=$1; geom=$2
	DISPLAY=$DPY xterm -title "$title" -geometry "$geom" -e 'sleep 300' >>"$LOG" 2>&1 &
	XT_PID=$!
	echo "$XT_PID" >> "$PIDFILE"
	xt_mapped() { [ -n "$(DISPLAY=$DPY xdotool search --name "^$title\$" 2>/dev/null | tail -n 1)" ]; }
	wait_for 60 xt_mapped || fatal "$title did not map"
	sleep 0.2
	DISPLAY=$DPY xdotool search --name "^$title\$" 2>/dev/null | tail -n 1
}
close_xt() {
	xt_wid=$1; xt_pid=$2
	DISPLAY=$DPY xdotool windowclose "$xt_wid" 2>/dev/null
	sleep 0.2
	kill "$xt_pid" 2>/dev/null
	wait_for 20 pid_gone "$xt_pid" 2>/dev/null
}

# --- drag-to-edge snap helper (from snapping-drive.sh) --------------------
drag_snap() {
	ds_xid=$1; ds_tx=$2; ds_ty=$3
	set -- $(frame_geom "$ds_xid"); ds_fx=$1; ds_fy=$2
	ds_bx=$((ds_fx + 60))
	ds_by=$((ds_fy + 10))
	xdo mousemove "$ds_bx" "$ds_by"
	xdo mousedown 1
	xdo mousemove $((ds_bx + (ds_tx - ds_bx) / 2)) "$ds_ty"
	xdo mousemove "$ds_tx" "$ds_ty"
	xdo mousemove "$ds_tx" "$ds_ty"
	xdo mousemove "$ds_tx" "$ds_ty"
	xdo mouseup 1
	sleep 0.3
}

# --- seam drag helper -----------------------------------------------------
# Arm a resize through _NET_WM_MOVERESIZE with the pointer parked where
# getResizeDirection yields the wanted edge (right/bottom sixth for
# SIZE_RIGHT/SIZE_BOTTOM, inside the perpendicular middle band so only
# one axis is grabbed), then hold a button so the loop's ButtonMotion
# filter sees the moves. The message goes first with button=0: wmaker
# grabs the pointer itself and a press held before it would make the
# grab fail with AlreadyGrabbed, pressing after arming feeds the modal
# loop its motion events without disturbing the grab.
seam_drag() {
	sd_xid=$1; sd_dir=$2; sd_px=$3; sd_py=$4; sdd_dx=$5; sd_dy=$6
	xdo mousemove "$sd_px" "$sd_py"
	send_moveresize "$sd_xid" "$sd_dir" "$sd_px" "$sd_py" 0
	sleep 0.3
	xdo mousedown 1
	sleep 0.2
	xdo mousemove $((sd_px + sdd_dx / 3)) $((sd_py + sd_dy / 3))
	xdo mousemove $((sd_px + 2 * sdd_dx / 3)) $((sd_py + 2 * sd_dy / 3))
	xdo mousemove $((sd_px + sdd_dx)) $((sd_py + sd_dy))
	xdo mouseup 1
	sleep 0.4
}

# send_moveresize TARGET DIRECTION X Y BUTTON : raw _NET_WM_MOVERESIZE
send_moveresize() {
	DISPLAY=$DPY env EWMH_CLIENT_MOVERESIZE_TARGET="$1" \
		EWMH_CLIENT_MOVERESIZE_DIRECTION="$2" \
		EWMH_CLIENT_MOVERESIZE_X="${3:-0}" \
		EWMH_CLIENT_MOVERESIZE_Y="${4:-0}" \
		EWMH_CLIENT_MOVERESIZE_BUTTON="${5:-0}" \
		"$CLIENT_BIN" >>"$LOG" 2>&1
}

SIZE_RIGHT=3    # _NET_WM_MOVERESIZE_SIZE_RIGHT (wmspec.h table)
SIZE_BOTTOM=5   # _NET_WM_MOVERESIZE_SIZE_BOTTOM
DELTA=60
TOL=4           # pixel tolerance on the paired deltas
SLACK=16        # xterm char-cell rounding + border adj

# =================================================================
# Section 1: left/right half pair, seam drag grows left shrinks right
# =================================================================
note "Section 1: X-pair seam drag via _NET_WM_MOVERESIZE"
L=$(spawn_xt se-left 80x12+300+300)
L_PID=$XT_PID
[ -n "$L" ] || fatal "could not spawn se-left"
drag_snap "$L" 3 $((H / 2))

R=$(spawn_xt se-right 80x12+700+300)
[ -n "$R" ] || fatal "could not spawn se-right"
drag_snap "$R" $((W - 3)) $((H / 2))

set -- $(frame_geom "$L"); LX=$1; LY=$2; LW=$3; LH=$4
set -- $(frame_geom "$R"); RX=$1; RY=$2; RW=$3; RH=$4
note "pair: left ($LX,$LY ${LW}x${LH}), right ($RX,$RY ${RW}x${RH})"

check "pair: left is a left half (has MAXIMIZED_VERT)"  has_atom "$L" _NET_WM_STATE_MAXIMIZED_VERT
check "pair: right is a right half (has MAXIMIZED_VERT)" has_atom "$R" _NET_WM_STATE_MAXIMIZED_VERT
check "pair: windows abut at the seam" \
	[ "$((RX - (LX + LW)))" -ge -$TOL -a "$((RX - (LX + LW)))" -le "$TOL" ]
check "pair: matched heights" \
	[ "$((RH - LH))" -ge -$SLACK -a "$((RH - LH))" -le "$SLACK" ]

# pointer in the right third of the left window, vertically centered,
# so getResizeDirection resolves to RIGHT on first motion
PX=$((LX + LW * 5 / 6))
PY=$((LY + LH / 2))

seam_drag "$L" "$SIZE_RIGHT" "$PX" "$PY" "$DELTA" 0

set -- $(frame_geom "$L"); NX=$1; NY=$2; NW=$3; NH=$4
set -- $(frame_geom "$R"); MX=$1; MY=$2; MW=$3; MH=$4
note "after seam drag: left ($NX,$NY ${NW}x${NH}), right ($MX,$MY ${MW}x${MH})"

check "left window grew by $DELTA" \
	[ "$((NW - LW))" -ge $((DELTA - TOL)) -a "$((NW - LW))" -le $((DELTA + TOL)) ]
check "right window shrank by $DELTA" \
	[ "$((RW - MW))" -ge $((DELTA - TOL)) -a "$((RW - MW))" -le $((DELTA + TOL)) ]
check "seam stayed put" \
	[ "$((MX - (NX + NW)))" -ge -$TOL -a "$((MX - (NX + NW)))" -le "$TOL" ]
check "left lost MAXIMIZED_VERT" lacks_atom "$L" _NET_WM_STATE_MAXIMIZED_VERT
check "left lost MAXIMIZED_HORZ" lacks_atom "$L" _NET_WM_STATE_MAXIMIZED_HORZ
check "right lost MAXIMIZED_VERT" lacks_atom "$R" _NET_WM_STATE_MAXIMIZED_VERT
check "right lost MAXIMIZED_HORZ" lacks_atom "$R" _NET_WM_STATE_MAXIMIZED_HORZ
check "wmaker alive after X-seam drag" ipc_ready
close_xt "$L" "$L_PID"
close_xt "$R" "$XT_PID"

# =================================================================
# Section 2: top/bottom half pair, vertical seam drag
# =================================================================
note "Section 2: Y-pair seam drag"
T=$(spawn_xt se-top 80x12+400+200)
T_PID=$XT_PID
[ -n "$T" ] || fatal "could not spawn se-top"
drag_snap "$T" $((W / 2)) 3

B=$(spawn_xt se-bot 80x12+400+500)
[ -n "$B" ] || fatal "could not spawn se-bot"
drag_snap "$B" $((W / 2)) $((H - 3))

set -- $(frame_geom "$T"); TX=$1; TY=$2; TW=$3; TH=$4
set -- $(frame_geom "$B"); BX=$1; BY=$2; BW=$3; BH=$4
note "pair: top ($TX,$TY ${TW}x${TH}), bottom ($BX,$BY ${BW}x${BH})"

check "ypair: top is a top half (has MAXIMIZED_HORZ)"    has_atom "$T" _NET_WM_STATE_MAXIMIZED_HORZ
check "ypair: bottom is a bottom half (has MAXIMIZED_HORZ)" has_atom "$B" _NET_WM_STATE_MAXIMIZED_HORZ
check "ypair: windows abut at the seam" \
	[ "$((BY - (TY + TH)))" -ge -$TOL -a "$((BY - (TY + TH)))" -le "$TOL" ]
check "ypair: matched widths" \
	[ "$((BW - TW))" -ge -$SLACK -a "$((BW - TW))" -le "$SLACK" ]

# pointer in the bottom sixth of the top window, horizontally centered
PX=$((TX + TW / 2))
PY=$((TY + TH - 10))

seam_drag "$T" "$SIZE_BOTTOM" "$PX" "$PY" 0 "$DELTA"

set -- $(frame_geom "$T"); NX=$1; NY=$2; NW=$3; NH=$4
set -- $(frame_geom "$B"); MX=$1; MY=$2; MW=$3; MH=$4
note "after seam drag: top ($NX,$NY ${NW}x${NH}), bottom ($MX,$MY ${MW}x${MH})"

check "top window grew by $DELTA" \
	[ "$((NH - TH))" -ge $((DELTA - TOL)) -a "$((NH - TH))" -le $((DELTA + TOL)) ]
check "bottom window shrank by $DELTA" \
	[ "$((BH - MH))" -ge $((DELTA - TOL)) -a "$((BH - MH))" -le $((DELTA + TOL)) ]
check "vertical seam stayed put" \
	[ "$((MY - (NY + NH)))" -ge -$TOL -a "$((MY - (NY + NH)))" -le "$TOL" ]
check "top lost MAXIMIZED_HORZ"    lacks_atom "$T" _NET_WM_STATE_MAXIMIZED_HORZ
check "top lost MAXIMIZED_VERT"    lacks_atom "$T" _NET_WM_STATE_MAXIMIZED_VERT
check "bottom lost MAXIMIZED_HORZ" lacks_atom "$B" _NET_WM_STATE_MAXIMIZED_HORZ
check "bottom lost MAXIMIZED_VERT" lacks_atom "$B" _NET_WM_STATE_MAXIMIZED_VERT
check "wmaker alive after Y-seam drag" ipc_ready
close_xt "$T" "$T_PID"
close_xt "$B" "$XT_PID"

# =================================================================
# Section 3: SharedEdgeResize=NO control (plain single-window resize)
# =================================================================
note "Section 3: feature off control"
set_config SharedEdgeResize NO

L=$(spawn_xt off-left 80x12+300+300)
L_PID=$XT_PID
[ -n "$L" ] || fatal "could not spawn off-left"
drag_snap "$L" 3 $((H / 2))

R=$(spawn_xt off-right 80x12+700+300)
[ -n "$R" ] || fatal "could not spawn off-right"
drag_snap "$R" $((W - 3)) $((H / 2))

set -- $(frame_geom "$L"); LX=$1; LY=$2; LW=$3; LH=$4
set -- $(frame_geom "$R"); RX=$1; RY=$2; RW=$3; RH=$4
check "control: pair abuts" \
	[ "$((RX - (LX + LW)))" -ge -$TOL -a "$((RX - (LX + LW)))" -le "$TOL" ]

PX=$((LX + LW * 5 / 6))
PY=$((LY + LH / 2))

seam_drag "$L" "$SIZE_RIGHT" "$PX" "$PY" "$DELTA" 0

set -- $(frame_geom "$L"); NX=$1; NY=$2; NW=$3; NH=$4
set -- $(frame_geom "$R"); MX=$1; MY=$2; MW=$3; MH=$4
note "after seam drag (off): left ($NX,$NY ${NW}x${NH}), right ($MX,$MY ${MW}x${MH})"

check "control: left window still grew" \
	[ "$((NW - LW))" -ge $((DELTA - TOL)) -a "$((NW - LW))" -le $((DELTA + TOL)) ]
check "control: right window untouched" [ "$MW" -eq "$RW" -a "$MX" -eq "$RX" ]
check "control: right kept MAXIMIZED_VERT" has_atom "$R" _NET_WM_STATE_MAXIMIZED_VERT
check "wmaker alive after control" ipc_ready
close_xt "$L" "$L_PID"
close_xt "$R" "$XT_PID"

# re-enable the feature for the remaining sections
set_config SharedEdgeResize YES

# =================================================================
# Section 4: hand-aligned pair away from the head edges. Pairing
# keys only on abutment plus matched perpendicular extent, so two
# windows sharing an edge mid-screen pair without spanning the
# full usable extent along the split axis.
# =================================================================
note "Section 4: non-filling mid-screen pair"
if command -v wmctrl >/dev/null 2>&1; then
	M1=$(spawn_xt mid-a 80x12+300+300)
	M1_PID=$XT_PID
	[ -n "$M1" ] || fatal "could not spawn mid-a"
	M2=$(spawn_xt mid-b 80x12+900+300)
	[ -n "$M2" ] || fatal "could not spawn mid-b"

	# identical explicit client sizes so the frames match exactly,
	# then park them flush against each other mid-screen
	DISPLAY=$DPY wmctrl -i -r "$M1" -e 0,400,250,320,200 >>"$LOG" 2>&1
	DISPLAY=$DPY wmctrl -i -r "$M2" -e 0,1200,250,320,200 >>"$LOG" 2>&1
	sleep 0.5
	set -- $(frame_geom "$M1"); AX=$1; AY=$2; AW=$3; AH=$4
	set -- $(frame_geom "$M2"); BX=$1; BY=$2; BW=$3; BH=$4

	# frame_x = client_x - border_width, so placing B's client at
	# AX + AW + border puts B's frame edge on A's frame boundary
	DISPLAY=$DPY wmctrl -i -r "$M2" -e 0,$((AX + AW + 1)),250,320,200 >>"$LOG" 2>&1
	sleep 0.5
	set -- $(frame_geom "$M2"); BX=$1; BY=$2; BW=$3; BH=$4
	note "midpair: left ($AX,$AY ${AW}x${AH}), right ($BX,$BY ${BW}x${BH})"

	check "midpair: matched frame sizes" \
		[ "$BW" -eq "$AW" -a "$BH" -eq "$AH" ]
	check "midpair: abuts without filling the screen" \
		[ "$BX" -ge "$((AX + AW))" -a "$((BX - (AX + AW)))" -le "$TOL" \
		  -a "$((BX + BW))" -lt "$W" ]

	PX=$((AX + AW * 5 / 6))
	PY=$((AY + AH / 2))

	seam_drag "$M1" "$SIZE_RIGHT" "$PX" "$PY" "$DELTA" 0

	set -- $(frame_geom "$M1"); NX=$1; NY=$2; NW=$3; NH=$4
	set -- $(frame_geom "$M2"); MX=$1; MY=$2; MW=$3; MH=$4
	note "after seam drag: left ($NX,$NY ${NW}x${NH}), right ($MX,$MY ${MW}x${MH})"

	check "midpair: left window grew by $DELTA" \
		[ "$((NW - AW))" -ge $((DELTA - TOL)) -a "$((NW - AW))" -le $((DELTA + TOL)) ]
	check "midpair: right window shrank by $DELTA" \
		[ "$((BW - MW))" -ge $((DELTA - TOL)) -a "$((BW - MW))" -le $((DELTA + TOL)) ]
	check "midpair: seam stayed put" \
		[ "$((MX - (NX + NW)))" -ge -$TOL -a "$((MX - (NX + NW)))" -le "$TOL" ]
	check "midpair: no maximize state invented" \
		lacks_atom "$M1" _NET_WM_STATE_MAXIMIZED_VERT
	check "wmaker alive after midpair" ipc_ready
	close_xt "$M1" "$M1_PID"
	close_xt "$M2" "$XT_PID"
else
	fail "section 4 skipped: wmctrl not installed"
fi

# =================================================================
# Section 5: hand-aligned pair with deliberate slop. Real hand
# placement never lands to the pixel, so pairing must survive a
# seam offset and perpendicular extents that differ by far more
# than pixel rounding, while staying inside the relaxed bounds.
# =================================================================
note "Section 5: sloppy hand-aligned pair"
if command -v wmctrl >/dev/null 2>&1; then
	S1=$(spawn_xt slop-a 80x12+300+300)
	S1_PID=$XT_PID
	[ -n "$S1" ] || fatal "could not spawn slop-a"
	S2=$(spawn_xt slop-b 80x12+900+300)
	[ -n "$S2" ] || fatal "could not spawn slop-b"

	# same X alignment as section 4 but heights differ by a full
	# xterm row (~26px after increment quantization) and B's
	# frame starts ~6px right of A's frame edge
	DISPLAY=$DPY wmctrl -i -r "$S1" -e 0,400,250,320,200 >>"$LOG" 2>&1
	DISPLAY=$DPY wmctrl -i -r "$S2" -e 0,$((400 + 320 + 6)),250,320,184 >>"$LOG" 2>&1
	sleep 0.5
	set -- $(frame_geom "$S1"); AX=$1; AY=$2; AW=$3; AH=$4
	set -- $(frame_geom "$S2"); BX=$1; BY=$2; BW=$3; BH=$4
	note "sloppair: left ($AX,$AY ${AW}x${AH}), right ($BX,$BY ${BW}x${BH})"

	check "sloppair: extents differ by ~26px" \
		[ "$((AH - BH))" -ge $((26 - TOL)) -a "$((AH - BH))" -le $((26 + TOL)) ]
	check "sloppair: seam gap ~6px" \
		[ "$((BX - (AX + AW)))" -ge $((6 - TOL)) -a "$((BX - (AX + AW)))" -le $((6 + TOL)) ]

	PX=$((AX + AW * 5 / 6))
	PY=$((AY + AH / 2))

	seam_drag "$S1" "$SIZE_RIGHT" "$PX" "$PY" "$DELTA" 0

	set -- $(frame_geom "$S1"); NX=$1; NY=$2; NW=$3; NH=$4
	set -- $(frame_geom "$S2"); MX=$1; MY=$2; MW=$3; MH=$4
	note "after seam drag: left ($NX,$NY ${NW}x${NH}), right ($MX,$MY ${MW}x${MH})"

	check "sloppair: left window grew by $DELTA" \
		[ "$((NW - AW))" -ge $((DELTA - TOL)) -a "$((NW - AW))" -le $((DELTA + TOL)) ]
	check "sloppair: right window shrank by $DELTA" \
		[ "$((BW - MW))" -ge $((DELTA - TOL)) -a "$((BW - MW))" -le $((DELTA + TOL)) ]
	check "wmaker alive after sloppair" ipc_ready
	close_xt "$S1" "$S1_PID"
	close_xt "$S2" "$XT_PID"

	# negative control: a half next to a quarter must not pair.
	# The spans differ by half the head so the extent match has
	# to reject it even with the relaxed tolerances.
	L=$(spawn_xt neg-half 80x12+300+300)
	L_PID=$XT_PID
	[ -n "$L" ] || fatal "could not spawn neg-half"
	drag_snap "$L" 3 $((H / 2))
	Q=$(spawn_xt neg-quarter 80x12+700+100)
	[ -n "$Q" ] || fatal "could not spawn neg-quarter"
	drag_snap "$Q" $((W - 3)) 3

	set -- $(frame_geom "$L"); LX=$1; LY=$2; LW=$3; LH=$4
	set -- $(frame_geom "$Q"); QX=$1; QY=$2; QW=$3; QH=$4
	check "negpair: quarter is much shorter than the half" \
		[ "$((LH - QH))" -gt $((QH / 2)) ]

	PX=$((LX + LW * 5 / 6))
	PY=$((LY + LH / 2))

	seam_drag "$L" "$SIZE_RIGHT" "$PX" "$PY" "$DELTA" 0

	set -- $(frame_geom "$L"); NX=$1; NY=$2; NW=$3; NH=$4
	set -- $(frame_geom "$Q"); MX=$1; MY=$2; MW=$3; MH=$4

	check "negpair: half window grew alone" \
		[ "$((NW - LW))" -ge $((DELTA - TOL)) -a "$((NW - LW))" -le $((DELTA + TOL)) ]
	check "negpair: quarter untouched" [ "$MW" -eq "$QW" -a "$MX" -eq "$QX" ]
	check "wmaker alive after negpair" ipc_ready
	close_xt "$L" "$L_PID"
	close_xt "$Q" "$XT_PID"
else
	fail "section 5 skipped: wmctrl not installed"
fi

# --- teardown -------------------------------------------------------------
note "shared-edge drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 -a "$total" -gt 0 ]
