#!/bin/sh
# Drive the per-workspace tiling layout under Xvfb: spawn three xterms,
# set-layout tile via IPC, assert their frame-outer geometries against
# the expected 0.55 master/stack split, cycle through grid and monocle,
# then set-layout float and check the original geometries are restored.
# Next check the maximized reporting of a full-area tile: granted for a
# lone increment-hinted client, dropped when a second window joins, and
# withheld when the constrained frame comes back below the cell (a
# PMaxSize-capped client rides the almost-fullscreen fallback). Finally
# map a 131-window herd
# (past the former 128-window collection cap) and require every window
# to land in one of the two tile columns.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${LAYOUT_DRIVE_DISPLAY:-:$(drive_default_display)}
W=1200
H=800
RUNTIME=$(mktemp -d /tmp/layout-drive.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/layout-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
: > "$PIDFILE"
: > "$LOG"

cleanup() {
    while read -r p; do
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done < "$PIDFILE"
    rm -f "$PIDFILE"
    rm -rf "$RUNTIME"
}
trap cleanup EXIT

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
fail() { printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; exit 1; }

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

command -v Xvfb    >/dev/null || { echo "SKIP: Xvfb not found"; exit 77; }
command -v xdotool >/dev/null || { echo "SKIP: xdotool not found"; exit 77; }
command -v xterm   >/dev/null || { echo "SKIP: xterm not found"; exit 77; }
[ -x "$ROOT/src/.libs/wmaker" ] || { echo "SKIP: wmaker not built"; exit 77; }
[ -x "$WMAKERMSG" ] || { echo "SKIP: wmakermsg not built"; exit 77; }
HERDCLIENT="$RUNTIME/tinywin-client"
${CC:-cc} -std=gnu11 "$ROOT/test/tinywin-client.c" -o "$HERDCLIENT" -lX11 >>"$LOG" 2>&1 \
    || { echo "SKIP: could not build tinywin-client.c (needs libX11 headers)"; exit 77; }

msg() {
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$WMAKERMSG" -s "$SOCKET" "$@"
}

# --- isolated WMAKER_USER_ROOT --------------------------------------------
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
}
EOF
export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET

# --- Xvfb ------------------------------------------------------------------
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use, set LAYOUT_DRIVE_DISPLAY" >&2
    exit 77
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 ${W}x${H}x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
xvfb_ready() {
    kill -0 "$XVFB_PID" 2>/dev/null || fail "Xvfb exited early"
    DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_ready || fail "Xvfb failed to start"

# --- wmaker ----------------------------------------------------------------
note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
ASAN_OPTIONS="$(drive_asan_options)" \
DISPLAY=$DPY \
"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"

SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DISPLAY=$DPY XDG_RUNTIME_DIR="$RUNTIME" "$WMAKERMSG" --get-socketpath)
ipc_ready() { msg -q get_version >/dev/null 2>&1; }
wait_for 120 ipc_ready || fail "wmaker did not come up"
sleep 0.3

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
        [ -n "$ftp" ] && [ "$ftp" != "0x0" ] || return 1
        [ "$ftp" = "$ftroot" ] && { printf '%s\n' "$ftcur"; return 0; }
        ftcur=$ftp
        ftn=$((ftn + 1))
    done
    return 1
}

# frame-outer rect for a client XID, border included, matching the
# WLayoutRect the compute pass produces
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

# match with a small slack to absorb WM_NORMAL_HINTS resize increments
expect_rect() {
    tag=$1; got=$2; wx=$3; wy=$4; ww=$5; wh=$6; slack=${7:-24}
    set -- $got
    gx=$1; gy=$2; gw=$3; gh=$4
    dx=$((gx - wx)); dy=$((gy - wy)); dw=$((gw - ww)); dh=$((gh - wh))
    [ "${dx#-}" -le "$slack" ] && [ "${dy#-}" -le "$slack" ] \
        && [ "${dw#-}" -le "$slack" ] && [ "${dh#-}" -le "$slack" ] \
        || fail "$tag: got ($gx,$gy ${gw}x${gh}) want (~$wx,~$wy ${ww}x${wh})"
    note "ok: $tag = ($gx,$gy ${gw}x${gh})"
}

# --- spawn three clients ---------------------------------------------------
note "spawn 3 xterms"
xt_client() {
    DISPLAY=$DPY xdotool search --name "^$1\$" 2>/dev/null | tail -n 1
}
spawn_xt() {
    DISPLAY=$DPY xterm -title "$1" -geometry "$2" -e 'sleep 300' >>"$LOG" 2>&1 &
    echo "$!" >> "$PIDFILE"
    xt_mapped() { [ -n "$(xt_client "$1")" ]; }
    wait_for 60 xt_mapped "$1" || fail "$1 did not map"
    sleep 0.2
}
spawn_xt lxa 60x8+40+40
spawn_xt lxb 60x8+80+80
spawn_xt lxc 60x8+120+120

Wa=$(xt_client lxa); Wb=$(xt_client lxb); Wc=$(xt_client lxc)
[ -n "$Wa" ] && [ -n "$Wb" ] && [ -n "$Wc" ] || fail "could not resolve client XIDs"

# focus in a known order so the MRU list is c, b, a (c = master)
DISPLAY=$DPY timeout 5 xdotool windowactivate --sync "$Wa" 2>/dev/null || true
DISPLAY=$DPY timeout 5 xdotool windowactivate --sync "$Wb" 2>/dev/null || true
DISPLAY=$DPY timeout 5 xdotool windowactivate --sync "$Wc" 2>/dev/null || true
sleep 0.2
W1=$Wc; W2=$Wb; W3=$Wa

G1_ORIG=$(frame_geom "$W1")
G2_ORIG=$(frame_geom "$W2")
G3_ORIG=$(frame_geom "$W3")

# --- tile ------------------------------------------------------------------
note "set-layout tile"
msg -q set-layout tile || fail "set-layout tile rejected"
sleep 0.3
msg get_workspaces | grep -q 'layout = tile' \
    || fail "get_workspaces does not report layout = tile"

expect_rect "tile master" "$(frame_geom "$W1")" 0 0 660 800
expect_rect "tile stack0" "$(frame_geom "$W2")" 660 0 540 400
expect_rect "tile stack1" "$(frame_geom "$W3")" 660 400 540 400

# --- master_ratio tune ------------------------------------------------------
note "set-layout tile master_ratio 0.7"
msg -q set-layout tile master_ratio 0.7 || fail "set-layout master_ratio 0.7 rejected"
sleep 0.3
expect_rect "tile 0.7 master" "$(frame_geom "$W1")" 0 0 840 800
expect_rect "tile 0.7 stack0" "$(frame_geom "$W2")" 840 0 360 400
expect_rect "tile 0.7 stack1" "$(frame_geom "$W3")" 840 400 360 400

# --- malformed master_ratio must be rejected --------------------------------
note "set-layout tile master_ratio nan/inf/garbage"
if msg -q set-layout tile master_ratio nan; then
    fail "set-layout accepted master_ratio nan"
fi
if msg -q set-layout tile master_ratio inf; then
    fail "set-layout accepted master_ratio inf"
fi
if msg -q set-layout tile master_ratio 0.5abc; then
    fail "set-layout accepted master_ratio 0.5abc"
fi
msg get_workspaces | grep -q 'master_ratio = .\{0,1\}0\.70' \
    || fail "rejected master_ratio changed the stored ratio"
expect_rect "tile unchanged after rejects" "$(frame_geom "$W1")" 0 0 840 800

note "set-layout rejection is atomic"
if msg -q set-layout tile master_count 3 master_ratio nan; then
    fail "set-layout accepted a compound request with an invalid ratio"
fi
msg get_workspaces | sed 's/^({//; s/},.*//' | grep -q 'master_count = 1' \
    || fail "rejected set-layout changed master_count"

# --- grid ------------------------------------------------------------------
note "set-layout grid"
msg -q set-layout grid || fail "set-layout grid rejected"
sleep 0.3
expect_rect "grid[0]" "$(frame_geom "$W1")" 0 0 600 400
expect_rect "grid[1]" "$(frame_geom "$W2")" 0 400 600 400
expect_rect "grid[2]" "$(frame_geom "$W3")" 600 0 600 800

# --- monocle ---------------------------------------------------------------
note "set-layout monocle"
msg -q set-layout monocle || fail "set-layout monocle rejected"
sleep 0.3
expect_rect "monocle[0]" "$(frame_geom "$W1")" 0 0 1200 800

# --- float restores --------------------------------------------------------
note "set-layout float"
msg -q set-layout float || fail "set-layout float rejected"
sleep 0.3
msg get_workspaces | grep -q 'layout = float' \
    || fail "get_workspaces does not report layout = float"

expect_rect "float restore W1" "$(frame_geom "$W1")" $G1_ORIG 4
expect_rect "float restore W2" "$(frame_geom "$W2")" $G2_ORIG 4
expect_rect "float restore W3" "$(frame_geom "$W3")" $G3_ORIG 4

# --- maximized state on full-area tiles --------------------------------------
# A single tileable window fills the whole usable area and reports
# maximized. The xterm's resize increments round it below the cell by
# less than one increment, which stays within the grant. A client that
# asks beyond its PMaxSize in both dimensions does not fill the cell
# either: wmaker does not clamp tile sizes to PMaxSize, so
# wWindowConstrainSize substitutes its almost-fullscreen fallback
# (screen minus twice the icon size) and the maximized state must stay
# withheld.
has_state() {
    DISPLAY=$DPY xprop -id "$1" -notype _NET_WM_STATE 2>/dev/null | grep -q "$2"
}
is_maximized() { has_state "$1" MAXIMIZED_VERT && has_state "$1" MAXIMIZED_HORZ; }
not_maximized() { ! has_state "$1" MAXIMIZED_VERT && ! has_state "$1" MAXIMIZED_HORZ; }

note "close lxb and lxc, tile lxa alone"
DISPLAY=$DPY xdotool windowclose "$W1" >>"$LOG" 2>&1 || true
DISPLAY=$DPY xdotool windowclose "$W2" >>"$LOG" 2>&1 || true
lxa_alone() { [ -z "$(xt_client lxb)" ] && [ -z "$(xt_client lxc)" ]; }
wait_for 60 lxa_alone || fail "lxb/lxc did not close"
msg -q set-layout tile || fail "set-layout tile rejected"
wait_for 30 is_maximized "$W3" \
    || fail "full-area tile did not report maximized"
note "ok: full-area tile reports maximized"

note "spawn a PMaxSize-capped client"
lxmax_up() { [ -n "$(xt_client lxmax)" ]; }
TINYWIN_NAME=lxmax TINYWIN_GEOM=400x300+300+300 TINYWIN_MAXSIZE=500x400 \
    DISPLAY=$DPY "$HERDCLIENT" >>"$LOG" 2>&1 &
echo "$!" >> "$PIDFILE"
wait_for 60 lxmax_up || fail "capped client did not map"
Wmax=$(xt_client lxmax)
wait_for 30 not_maximized "$W3" \
    || fail "master kept the maximized state once a second window joined"
note "ok: two-window tile reports no maximized state"

note "close lxa, leave the capped client alone"
DISPLAY=$DPY xdotool windowclose "$W3" >>"$LOG" 2>&1 || true
lxa_gone() { [ -z "$(xt_client lxa)" ]; }
wait_for 60 lxa_gone || fail "lxa did not close"
wait_for 60 not_maximized "$Wmax" \
    || fail "full-area tile with a PMaxSize-capped client claims maximized state"
set -- $(frame_geom "$Wmax")
[ "$3" -ge 1000 ] && [ "$3" -lt 1180 ] && [ "$4" -ge 640 ] && [ "$4" -lt 780 ] \
    || fail "capped client is neither almost-fullscreen nor below the cell: got ${3}x${4}"
note "ok: capped tile stays unmaximized below the cell (${3}x${4})"

note "close the capped client"
DISPLAY=$DPY xdotool windowclose "$Wmax" >>"$LOG" 2>&1 || true
lxmax_gone() { [ -z "$(xt_client lxmax)" ]; }
wait_for 60 lxmax_gone || fail "capped client did not close"
msg -q set-layout float || fail "set-layout float rejected"
sleep 0.3

# --- herd past the former 128-window collection cap -------------------------
note "spawn 131-window herd"
HERD=131
hi=0
while [ "$hi" -lt "$HERD" ]; do
    TINYWIN_NAME=lxherd TINYWIN_GEOM=120x80+90+90 TINYWIN_DELAY=0 \
        DISPLAY=$DPY "$HERDCLIENT" >>"$LOG" 2>&1 &
    echo "$!" >> "$PIDFILE"
    hi=$((hi + 1))
done
herd_ready() {
    [ "$(DISPLAY=$DPY xdotool search --name '^lxherd$' 2>/dev/null | wc -l)" \
        -ge "$HERD" ]
}
wait_for 600 herd_ready || fail "herd did not map"
sleep 1

note "set-layout tile over the herd"
msg -q set-layout tile master_ratio 0.55 || fail "set-layout tile master_ratio 0.55 rejected"
sleep 1

# master column starts at x=0, the 0.55 stack column at x=660. Every
# tiled window must sit on one of the two
note "check every window landed in a tile column"
masters=0
stackers=0
strays=0
for hx in $(DISPLAY=$DPY xdotool search --name '^lxherd$' 2>/dev/null) \
          $(xt_client lxa) $(xt_client lxb) $(xt_client lxc); do
    set -- $(frame_geom "$hx")
    hxx=$1
    [ "$2" -ge 0 ] && [ $(($2 + $4)) -le "$H" ] \
        || fail "window $hx exceeds usable height: y=$2 height=$4"
    if [ "$hxx" -le 16 ]; then
        masters=$((masters + 1))
    elif [ "$hxx" -ge 644 ] && [ "$hxx" -le 676 ]; then
        stackers=$((stackers + 1))
    else
        strays=$((strays + 1))
        note "window $hx left at x=$hxx"
    fi
done
[ "$strays" -eq 0 ] || fail "$strays of $((HERD + 3)) windows were left out of the tiling"
[ "$masters" -eq 1 ] || fail "expected exactly one master column window, got $masters"
note "ok: herd tiled, $masters master and $stackers stack windows"

note "check crowded layouts with a 50-pixel gap"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$ROOT/util/.libs/wdwrite" WindowMaker WindowGap 50 >>"$LOG" 2>&1 \
    || fail "could not set WindowGap"
msg -q reconfigure || fail "reconfigure rejected"
for layout in float tile tileh grid; do
    msg -q set-layout "$layout" || fail "set-layout $layout rejected"
    [ "$layout" = float ] && continue
    sleep 0.5
    for hx in $(DISPLAY=$DPY xdotool search --name '^lxherd$' 2>/dev/null); do
        set -- $(frame_geom "$hx")
        [ "$1" -ge 0 ] && [ "$2" -ge 0 ] &&
            [ $(($1 + $3)) -le "$W" ] && [ $(($2 + $4)) -le "$H" ] \
            || fail "$layout window $hx exceeds usable area: ($1,$2 ${3}x${4})"
    done
    note "ok: crowded $layout stays within the usable area"
done

# --- teardown --------------------------------------------------------------
kill -TERM "$WMAKER_PID" 2>/dev/null
wait_for 50 pid_gone "$WMAKER_PID"

note "PASS"
exit 0
