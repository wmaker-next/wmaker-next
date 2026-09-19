#!/bin/sh
# Regression drive test for the edge-snap actions (MoveToEdge and its
# NoSnap variants, GrowToEdge, ShrinkToEdge) through the IPC `action'
# verb. Asserts frame-outer geometry via xwininfo and _NET_FRAME_EXTENTS.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${EDGE_SNAP_DRIVE_DISPLAY:-:105}
W=1280
H=800
RUNTIME=$(mktemp -d /tmp/edge-snap-drive.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/edge-snap-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
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

for tool in Xvfb xdpyinfo xprop xwininfo xdotool xterm; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "wmakermsg not built"

export WMAKER_USER_ROOT="$WMHOME"
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  OpaqueMove = YES;
  OpaqueResize = YES;
  EdgeResistance = 0;
  WindowGap = 0;
}
EOF

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	echo "display $DPY is already in use, set EDGE_SNAP_DRIVE_DISPLAY" >&2
	exit 1
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 ${W}x${H}x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
wait_for 40 sh -c "kill -0 $XVFB_PID 2>/dev/null && DISPLAY=$DPY xdpyinfo >/dev/null 2>&1" \
	|| fatal "Xvfb failed to start"

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

# --- geometry helpers (frame-outer rect via window-tree walk) ------------
# Find the frame window (top-level child of root) for a client XID by
# walking the parent chain, then read its xwininfo geometry.
frame_of() {
	DISPLAY=$DPY xwininfo -tree -id "$1" 2>/dev/null \
		| awk '/Parent window id:/ {print $4; exit}'
}
frame_top_wid() {
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
frame_geom() {
	fgframe=$(frame_top_wid "$1") || { echo "0 0 0 0"; return; }
	DISPLAY=$DPY xwininfo -id "$fgframe" 2>/dev/null \
		| awk '/Absolute upper-left X:/ {x=$4}
		       /Absolute upper-left Y:/ {y=$4}
		       /Width:/  && !w {w=$2}
		       /Height:/ && !h {h=$2}
		       END {print x+0, y+0, w+0, h+0}'
}

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

SLACK=16

expect_rect() {
	tag=$1; got=$2; wx=$3; wy=$4; ww=$5; wh=$6; slack=${7:-$SLACK}
	set -- $got
	gx=$1; gy=$2; gw=$3; gh=$4
	dx=$((gx - wx)); dy=$((gy - wy)); dw=$((gw - ww)); dh=$((gh - wh))
	[ "${dx#-}" -le "$slack" -a "${dy#-}" -le "$slack" ] \
		&& [ "${dw#-}" -le "$slack" -a "${dh#-}" -le "$slack" ] \
		|| { fail "$tag: got ($gx,$gy ${gw}x${gh}) want (~$wx,~$wy ${ww}x${wh})"; return 1; }
	ok "$tag: ($gx,$gy ${gw}x${gh})"
	return 0
}

# =================================================================
# Section 1: MoveToEdge
# =================================================================
note "Section 1: MoveToEdge via IPC"
note "MoveToEdgeLeft: window at x=200 should snap to x=0"

W1=$(spawn_xt mte1 80x12+200+200)
[ -n "$W1" ] || fatal "could not spawn mte1"
activate "$W1"
sleep 0.2

msg -q action MoveToEdgeLeft || fail "MoveToEdge left rejected"
sleep 0.3
set -- $(frame_geom "$W1"); NX=$1; NY=$2; NW=$3; NH=$4
note "after MoveToEdge left: ($NX,$NY ${NW}x${NH})"
check "MoveToEdge left snapped to screen left (x~0)" [ "$NX" -ge 0 -a "$NX" -le "$SLACK" ]

note "MoveToEdge right: should snap to right screen edge"
msg -q action MoveToEdgeRight || fail "MoveToEdge right rejected"
sleep 0.3
set -- $(frame_geom "$W1"); NX=$1; NY=$2; NW=$3; NH=$4
RIGHT_EDGE=$((NX + NW))
note "after MoveToEdge right: ($NX,$NY ${NW}x${NH}) right=$RIGHT_EDGE"
check "MoveToEdge right snapped to screen right (~$W)" \
	[ "$((RIGHT_EDGE - W))" -ge $((0 - SLACK)) -a "$((RIGHT_EDGE - W))" -le "$SLACK" ]

note "MoveToEdge up: should snap to top screen edge"
msg -q action MoveToEdgeUp || fail "MoveToEdge up rejected"
sleep 0.3
set -- $(frame_geom "$W1"); NX=$1; NY=$2; NW=$3; NH=$4
note "after MoveToEdge up: ($NX,$NY ${NW}x${NH})"
check "MoveToEdge up snapped to screen top (y~0)" [ "$NY" -ge 0 -a "$NY" -le "$SLACK" ]

note "MoveToEdge down: should snap to bottom screen edge"
msg -q action MoveToEdgeDown || fail "MoveToEdge down rejected"
sleep 0.3
set -- $(frame_geom "$W1"); NX=$1; NY=$2; NW=$3; NH=$4
BOTTOM_EDGE=$((NY + NH))
note "after MoveToEdge down: ($NX,$NY ${NW}x${NH}) bottom=$BOTTOM_EDGE"
check "MoveToEdge down snapped to screen bottom (~$H)" \
	[ "$((BOTTOM_EDGE - H))" -ge $((0 - SLACK)) -a "$((BOTTOM_EDGE - H))" -le "$SLACK" ]

check "wmaker alive after MoveToEdge tests" ipc_ready
close_xt "$W1" "$XT_PID"

# =================================================================
# Section 1b: MoveToEdge with neighbor clamping (snap_to_windows)
# =================================================================
note "Section 1b: MoveToEdge clamps to neighbor window edge"
note "obstacle at x=100, mover at x=700, MoveToEdge left should stop at obstacle"

WOBS=$(spawn_xt obstacle 80x12+100+200)
[ -n "$WOBS" ] || fatal "could not spawn obstacle"
WOBS_PID=$XT_PID
WMTE=$(spawn_xt mte2 80x12+700+200)
[ -n "$WMTE" ] || fatal "could not spawn mte2"

set -- $(frame_geom "$WOBS"); OBS_X=$1; OBS_W=$3; OBS_R=$((OBS_X + OBS_W))
note "obstacle frame: x=$OBS_X w=$OBS_W right=$OBS_R"

activate "$WMTE"
sleep 0.2

msg -q action MoveToEdgeLeft || fail "MoveToEdge left with neighbor rejected"
sleep 0.3
set -- $(frame_geom "$WMTE"); NX=$1; NY=$2; NW=$3; NH=$4
note "mover after MoveToEdge left: ($NX,$NY ${NW}x${NH}) expected ~$OBS_R"
check "MoveToEdge left stopped at obstacle right edge (~$OBS_R)" \
	[ "$((NX - OBS_R))" -ge -3 -a "$((NX - OBS_R))" -le 3 ]

note "MoveToEdgeLeftNoSnap: should ignore the obstacle and reach screen left"
close_xt "$WMTE" "$XT_PID"
WMTE=$(spawn_xt mte3 80x12+700+200)
[ -n "$WMTE" ] || fatal "could not spawn mte3"
activate "$WMTE"
sleep 0.2
msg -q action MoveToEdgeLeftNoSnap || fail "action MoveToEdgeLeftNoSnap rejected"
sleep 0.3
set -- $(frame_geom "$WMTE"); NX=$1
note "mover after MoveToEdgeLeftNoSnap: x=$NX (obstacle right=$OBS_R)"
check "MoveToEdgeLeftNoSnap reached screen left (x~0)" \
	[ "$NX" -ge 0 -a "$NX" -le "$SLACK" ]
check "MoveToEdgeLeftNoSnap passed the obstacle" [ "$NX" -lt "$OBS_R" ]

check "wmaker alive after neighbor clamp test" ipc_ready
close_xt "$WOBS" "$WOBS_PID"
close_xt "$WMTE" "$XT_PID"

# =================================================================
# Section 2: GrowToEdge
# =================================================================
note "Section 2: GrowToEdge via IPC"

W2=$(spawn_xt gte1 80x12+200+200)
[ -n "$W2" ] || fatal "could not spawn gte1"
activate "$W2"
sleep 0.2

set -- $(frame_geom "$W2"); OX=$1; OY=$2; OW=$3; OH=$4
note "gte1 initial: ($OX,$OY ${OW}x$OH)"

note "grow-to-edge right: should grow right edge to screen edge"
msg -q action GrowToEdgeRight || fail "grow-to-edge right rejected"
sleep 0.3
set -- $(frame_geom "$W2"); NX=$1; NY=$2; NW=$3; NH=$4
RIGHT_EDGE=$((NX + NW))
note "after grow-to-edge right: ($NX,$NY ${NW}x${NH}) right=$RIGHT_EDGE"
check "grow-to-edge right: left edge unchanged (~$OX)" \
	[ "$((NX - OX))" -ge $((0 - SLACK)) -a "$((NX - OX))" -le "$SLACK" ]
check "grow-to-edge right: right edge at screen (~$W)" \
	[ "$((RIGHT_EDGE - W))" -ge $((0 - SLACK)) -a "$((RIGHT_EDGE - W))" -le "$SLACK" ]
check "grow-to-edge right: width increased" [ "$NW" -gt "$OW" ]

note "grow-to-edge down: should grow bottom edge to screen bottom"
msg -q action GrowToEdgeDown || fail "grow-to-edge down rejected"
sleep 0.3
set -- $(frame_geom "$W2"); NX=$1; NY=$2; NW=$3; NH=$4
BOTTOM_EDGE=$((NY + NH))
note "after grow-to-edge down: ($NX,$NY ${NW}x${NH}) bottom=$BOTTOM_EDGE"
check "grow-to-edge down: top edge unchanged (~$OY)" \
	[ "$((NY - OY))" -ge $((0 - SLACK)) -a "$((NY - OY))" -le "$SLACK" ]
check "grow-to-edge down: bottom edge at screen (~$H)" \
	[ "$((BOTTOM_EDGE - H))" -ge $((0 - SLACK)) -a "$((BOTTOM_EDGE - H))" -le "$SLACK" ]
check "grow-to-edge down: height increased" [ "$NH" -gt "$OH" ]

check "wmaker alive after GrowToEdge tests" ipc_ready
close_xt "$W2" "$XT_PID"

# =================================================================
# Section 3: ShrinkToEdge
# =================================================================
note "Section 3: ShrinkToEdge via IPC"

W3=$(spawn_xt ste1 80x12+200+200)
[ -n "$W3" ] || fatal "could not spawn ste1"
activate "$W3"
sleep 0.2

set -- $(frame_geom "$W3"); OX=$1; OY=$2; OW=$3; OH=$4
note "ste1 initial: ($OX,$OY ${OW}x$OH)"

note "shrink-to-edge left: left edge fixed, right edge moves in (max 50%)"
msg -q action ShrinkToEdgeLeft || fail "shrink-to-edge left rejected"
sleep 0.3
set -- $(frame_geom "$W3"); NX=$1; NY=$2; NW=$3; NH=$4
note "after shrink-to-edge left: ($NX,$NY ${NW}x${NH})"
check "shrink-to-edge left: left edge unchanged (~$OX)" \
	[ "$((NX - OX))" -ge $((0 - SLACK)) -a "$((NX - OX))" -le "$SLACK" ]
check "shrink-to-edge left: width decreased" [ "$NW" -lt "$OW" ]
check "shrink-to-edge left: width >= 50% of original" [ "$NW" -ge "$((OW / 2 - SLACK))" ]

note "shrink-to-edge up: top edge fixed, bottom edge moves in (max 50%)"
msg -q action ShrinkToEdgeUp || fail "shrink-to-edge up rejected"
sleep 0.3
set -- $(frame_geom "$W3"); NX=$1; NY=$2; NW=$3; NH=$4
note "after shrink-to-edge up: ($NX,$NY ${NW}x${NH})"
check "shrink-to-edge up: top edge unchanged (~$OY)" \
	[ "$((NY - OY))" -ge $((0 - SLACK)) -a "$((NY - OY))" -le "$SLACK" ]
check "shrink-to-edge up: height decreased" [ "$NH" -lt "$OH" ]

check "wmaker alive after ShrinkToEdge tests" ipc_ready
close_xt "$W3" "$XT_PID"

# --- teardown -------------------------------------------------------------
note "edge-snap drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 -a "$total" -gt 0 ]
