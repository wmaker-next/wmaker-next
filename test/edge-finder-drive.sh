#!/bin/sh
# Characterization drive tests for the three existing edge-finder
# copies that a later unification onto find_nearest_edge would
# replace. These capture the CURRENT behaviour so a future refactor
# can be verified behaviour-equivalent against these drives, not
# against a prose spec.
#
# 1. Move-loop resistance  — updateResistance() + updateWindowPosition()
#    (moveres.c:482,725): EdgeResistance makes a dragged window snap
#    to the screen edge when its left edge enters the resist zone.
#
# 2. Resize snap — snapResizeGeometry() (moveres.c:1778): during an
#    interactive resize, the moving edge snaps to the screen edge
#    when it approaches within EdgeResistance. Attraction=YES also
#    snaps to neighbour window edges.
#
# 3. Maximus — find_maximus_geometry() (actions.c:980): MaximusKey
#    maximises the focused window but clamps its left edge to the
#    right edge of an obstructing neighbour with vertical overlap,
#    instead of going all the way to the screen edge.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${EDGE_FINDER_DRIVE_DISPLAY:-:$(drive_default_display)}
W=1280
H=800
RUNTIME=$(mktemp -d /tmp/edge-finder-drive.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/edge-finder-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
WDWRITE="$ROOT/util/.libs/wdwrite"
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
activate() { DISPLAY=$DPY timeout 5 xdotool windowactivate --sync "$1" 2>/dev/null || true; }

msg() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$WMAKERMSG" -s "$SOCKET" "$@"
}
ipc_ready() { msg -q get_version >/dev/null 2>&1; }

wdwrite() {
	LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		DISPLAY=$DPY "$WDWRITE" "$@"
}

# xdotool with a short settle so wmaker dispatches each event before
# the next one arrives (same race guard as moveres-drive.sh)
xdo() { DISPLAY=$DPY xdotool "$@"; sleep 0.05; }

for tool in Xvfb xdpyinfo xprop xwininfo xdotool xterm; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "wmakermsg not built"
[ -x "$WDWRITE" ] || skip_all "wdwrite not built"

export WMAKER_USER_ROOT="$WMHOME"
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"

# EdgeResistance=30 → resist=WIN_RESISTANCE(30)=20. The resistance
# zone for the left screen edge is [-20, 0].
cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  OpaqueMove = YES;
  OpaqueResize = YES;
  EdgeResistance = 30;
  WindowSnapping = NO;
  Attraction = NO;
}
EOF

# --- Xvfb ----------------------------------------------------------------
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set EDGE_FINDER_DRIVE_DISPLAY" >&2
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
	ASAN_OPTIONS="$(drive_asan_options)" \
	DISPLAY=$DPY \
	"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	DISPLAY=$DPY XDG_RUNTIME_DIR="$RUNTIME" "$WMAKERMSG" --get-socketpath)
wait_for 80 ipc_ready || fatal "wmaker IPC did not come up"
sleep 0.5

# --- geometry helpers (from moveres-drive.sh + layout-drive.sh) -----------
# frame-outer rect for a client XID via window-tree walk
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
# client geometry + frame extents (from moveres-drive.sh)
client_geom() {
	DISPLAY=$DPY xwininfo -id "$1" | awk \
		'/Absolute upper-left X/{x=$NF} /Absolute upper-left Y/{y=$NF}
		 /Width:/{w=$NF} /Height:/{h=$NF} END{print x,y,w,h}'
}
frame_extents() {
	DISPLAY=$DPY xprop -id "$1" _NET_FRAME_EXTENTS \
		| sed 's/.*= //; s/,//g'
}

# --- spawn an xterm and return its client XID -----------------------------
# Also sets XT_PID to the xterm process PID for later cleanup.
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
# close an xterm spawned by spawn_xt (WM_DELETE_WINDOW, then SIGTERM)
close_xt() {
	xt_wid=$1; xt_pid=$2
	DISPLAY=$DPY xdotool windowclose "$xt_wid" 2>/dev/null
	sleep 0.2
	kill "$xt_pid" 2>/dev/null
	wait_for 20 pid_gone "$xt_pid" 2>/dev/null
}

# =================================================================
# Section 1: Move-loop screen-edge resistance
#            (updateResistance + updateWindowPosition, moveres.c:482,725)
# =================================================================
note "Section 1: move-loop screen-edge resistance"
note "EdgeResistance=30, resist=WIN_RESISTANCE(30)=20, zone [-20,0]"

W1=$(spawn_xt resist1 80x12+200+200)
[ -n "$W1" ] || fatal "could not spawn resist1"

set -- $(client_geom "$W1"); CX=$1; CY=$2; CW=$3; CH=$4
set -- $(frame_extents "$W1"); FL=$1; FR=$2; FT=$3; FB=$4
FX=$((CX - FL)); FY=$((CY - FT))
FW=$((CW + FL + FR)); FH=$((CH + FT + FB))
note "resist1 frame: ($FX,$FY ${FW}x${FH})"

# grab titlebar: 60px from left, mid-titlebar
TBX=$((FX + 60))
TBY=$((FY + FT/2 - 1))

# drag left in steps so the left edge enters the resist zone [-20,0].
# calcX accumulates the raw mouse delta, the final step must land
# inside the zone so hresist triggers and snaps newX to l_edge=0.
xdo mousemove "$TBX" "$TBY"
xdo mousedown 1
xdo mousemove $((TBX - 30))  "$TBY"   # arm started
xdo mousemove $((TBX - 100)) "$TBY"   # ~FX-100
xdo mousemove $((TBX - 150)) "$TBY"   # ~FX-150 (left edge ~50)
xdo mousemove $((TBX - 210)) "$TBY"   # left edge ~-10 (in zone) → snap
xdo mouseup 1
sleep 0.3

set -- $(frame_geom "$W1"); NX=$1; NY=$2; NW=$3; NH=$4
note "after drag: frame ($NX,$NY ${NW}x${NH})"
check "resistance snapped left edge to screen edge (x=0)" [ "$NX" -eq 0 ]
check "wmaker alive after resistance test" ipc_ready
close_xt "$W1" "$XT_PID"

# =================================================================
# Section 2: Resize snap (snapResizeGeometry, moveres.c:1778)
# =================================================================
note "Section 2: resize snap to screen edge"
note "Attraction=YES, EdgeResistance=30, resist=20"

# enable Attraction so the snap triggers when the candidate edge is
# within resist pixels of the screen edge (bidirectional, not just
# when dragging past it)
wdwrite WindowMaker Attraction YES
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3

W2=$(spawn_xt snap1 80x12+200+200)
[ -n "$W2" ] || fatal "could not spawn snap1"

set -- $(client_geom "$W2"); CX=$1; CY=$2; CW=$3; CH=$4
set -- $(frame_extents "$W2"); FL=$1; FR=$2; FT=$3; FB=$4
FX=$((CX - FL)); FY=$((CY - FT))
FW=$((CW + FL + FR)); FH=$((CH + FT + FB))
note "snap1 frame: ($FX,$FY ${FW}x${FH})"

# grab the right corner of the resizebar (RIGHT resize).
# resizebar is in the bottom frame area, the right corner triggers
# getResizeDirection() to return RIGHT.
RBX=$((FX + FW - 5))
RBY=$((FY + FH - FB/2))
note "resizebar grab at ($RBX,$RBY)"

# drag right so the right edge approaches the screen width (1280).
# with Attraction the snap triggers when cand_x is within resist=20
# of scr_hi=1280. Step through so each MotionNotify is processed.
TARGET=$((W - 10))   # 1270: right edge within resist of 1280
DRAG=$((TARGET - (FX + FW)))
note "drag right $DRAG px (right edge $((FX+FW)) → $TARGET, snap to $W)"

xdo mousemove "$RBX" "$RBY"
xdo mousedown 1
xdo mousemove $((RBX + 50))  "$RBY"   # arm + first move
xdo mousemove $((RBX + 150)) "$RBY"
xdo mousemove $((RBX + 250)) "$RBY"
xdo mousemove $((RBX + DRAG)) "$RBY"  # within resist → snap to 1280
xdo mouseup 1
sleep 0.3

set -- $(frame_geom "$W2"); NX=$1; NY=$2; NW=$3; NH=$4
note "after resize: frame ($NX,$NY ${NW}x${NH})"
RIGHT_EDGE=$((NX + NW))
SLACK=16   # xterm rounds to char-cell increments
check "resize snapped right edge to screen edge (~$W)" \
	[ "$((RIGHT_EDGE - W))" -ge $((0 - SLACK)) -a "$((RIGHT_EDGE - W))" -le "$SLACK" ]
check "resize grew the window (width increased)" [ "$NW" -gt "$FW" ]
check "wmaker alive after resize snap test" ipc_ready
close_xt "$W2" "$XT_PID"

# =================================================================
# Section 3: Maximus tiled maximization (find_maximus_geometry, actions.c:980)
# =================================================================
note "Section 3: Maximus tiled maximization"
note "MaximusKey=Mod4+m, neighbour clamps left edge"

# bind MaximusKey so we can trigger it via xdotool
wdwrite WindowMaker MaximusKey 'Mod4+m'
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3

# two windows with vertical overlap but NO horizontal overlap:
# A on the left (right edge ~586), B on the right (left edge 650).
# find_maximus_geometry requires obs.right < orig.left for the
# left-edge clamp to fire, so B must start completely to the right of A.
WA=$(spawn_xt maxA 80x12+100+200)
WA_PID=$XT_PID
WB=$(spawn_xt maxB 80x12+650+200)
WB_PID=$XT_PID
[ -n "$WA" -a -n "$WB" ] || fatal "could not spawn maxA/maxB"

# focus B (the one we will Maximus)
activate "$WB"
sleep 0.2

set -- $(frame_geom "$WA"); AX=$1; AY=$2; AW=$3; AH=$4
set -- $(frame_geom "$WB"); BX=$1; BY=$2; BW=$3; BH=$4
A_RIGHT=$((AX + AW))
note "maxA frame: ($AX,$AY ${AW}x${AH}) right=$A_RIGHT"
note "maxB frame: ($BX,$BY ${BW}x${BH})"

# trigger Maximus on B
note "send Mod4+m to maxB"
DISPLAY=$DPY xdotool key super+m
sleep 0.5

set -- $(frame_geom "$WB"); MX=$1; MY=$2; MW=$3; MH=$4
note "after Maximus: maxB frame ($MX,$MY ${MW}x${MH})"

# find_maximus_geometry clamps new.left to obs.right+1 where
# obs.right = neighbour frame_x + core->width. The frame outer
# left of the maximised window should be near the neighbour's
# frame outer right edge, not at the screen left edge (0).
check "Maximus clamped left edge away from screen edge (x>0)" [ "$MX" -gt 0 ]
check "Maximus left edge near neighbour right edge (~$A_RIGHT)" \
	[ "$((MX - A_RIGHT))" -ge -3 -a "$((MX - A_RIGHT))" -le 3 ]
check "Maximus grew the window (width increased)" [ "$MW" -gt "$BW" ]
check "wmaker alive after Maximus test" ipc_ready

# --- teardown -------------------------------------------------------------
note "edge-finder drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 -a "$total" -gt 0 ]
