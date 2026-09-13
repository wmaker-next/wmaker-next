#!/bin/sh
# Drive titlebar double-click semantics under Xvfb:
#  1. the close button double-click force-kills a client that does not
#     advertise WM_DELETE_WINDOW, while a plain single click on the
#     same button is a no-op for such a client (the two are therefore
#     distinguishable).
#  2. with DbClickMaximize=YES a titlebar double-click toggles the
#     window between maximized-both and its saved geometry.
# The default DbClickMaximize=NO shade flavor is exercised by
# asan-drive.sh.
#
# Runs against an isolated WMAKER_USER_ROOT and tracks every PID it
# spawns, the live :0 wmaker is the same binary so no pkill.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${TITLEBAR_DBL_DRIVE_DISPLAY:-:119}
W=1280
H=800
RUNTIME=$(mktemp -d /tmp/wmaker-titlebardbl-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/titlebar-dblclick-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
MOTH="$RUNTIME/mothints-client"
SOCKET=
ROOTW=
CLIENT_WID=
CLIENT_PID=
FRAME=
FX=
FY=
FW=
FH=
FB=
TH=
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
    td_desc=$1; shift
    if "$@"; then ok "$td_desc"; else fail "$td_desc"; fi
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

xdo() { DISPLAY=$DPY xdotool "$@"; sleep 0.1; }

frame_of() {
    DISPLAY=$DPY xwininfo -tree -id "$1" 2>/dev/null \
        | awk '/Parent window id:/ {print $4; exit}'
}

frame_top() {
    ftcur=$1
    ftn=0
    while [ $ftn -lt 5 ]; do
        ftp=$(frame_of "$ftcur")
        [ -n "$ftp" ] && [ "$ftp" != "0x0" ] || return 1
        [ "$ftp" = "$ROOTW" ] && { printf '%s\n' "$ftcur"; return 0; }
        ftcur=$ftp
        ftn=$((ftn + 1))
    done
    return 1
}

geom_of() {
    DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null | awk \
        '/Absolute upper-left X:/ {x=$4}
         /Absolute upper-left Y:/ {y=$4}
         /Width:/ && !w {w=$2}
         /Height:/ && !h {h=$2}
         /Border width:/ {b=$3}
         END {print x, y, w, h, b}'
}

client_found() {
    for w in $(DISPLAY=$DPY xdotool search --class "$moth_name" 2>/dev/null); do
        wp=$(frame_of "$w")
        [ -n "$wp" ] && [ "$wp" != "$ROOTW" ] && [ "$wp" != "0x0" ] || continue
        CLIENT_WID=$w
        return 0
    done
    return 1
}

# start_client <name>, sets CLIENT_PID and CLIENT_WID
start_client() {
    moth_name=$1
    CLIENT_WID=
    DISPLAY=$DPY env MOTH_NAME="$moth_name" MOTH_FLAGS=0 \
        "$MOTH" >>"$LOG" 2>&1 &
    CLIENT_PID=$!
    echo "$CLIENT_PID" >> "$PIDFILE"
    wait_for 50 client_found
}

stop_client() {
    [ -n "$CLIENT_PID" ] || return 0
    kill "$CLIENT_PID" 2>/dev/null
    wait_for 30 pid_gone "$CLIENT_PID" || kill -9 "$CLIENT_PID" 2>/dev/null
    CLIENT_PID=
    CLIENT_WID=
}

# titlebar_metrics, sets FRAME FX FY FW FH FB TH
titlebar_metrics() {
    FRAME=$(frame_top "$CLIENT_WID") || return 1
    set -- $(geom_of "$FRAME")
    FX=$1; FY=$2; FW=$3; FH=$4; FB=$5
    set -- $(DISPLAY=$DPY xprop -id "$CLIENT_WID" _NET_FRAME_EXTENTS 2>/dev/null \
                 | sed -n 's/.*= //p' | tr ',' ' ')
    [ $# -eq 4 ] || return 1
    TH=$(( $3 - FB ))
    [ "$TH" -gt 0 ]
}

# first viewable button-sized frame child at x-offset <want>
button_wid_at() {
    bwa_frame=$1
    bwa_want=$2
    DISPLAY=$DPY xwininfo -children -id "$bwa_frame" 2>/dev/null \
        | awk -v th="$TH" -v want="$bwa_want" '
            /^ *0x/ {
                g = $(NF-1)
                if (split(g, a, /[x+]/) < 4) next
                if (a[1]+0 == th+0 && a[2]+0 == th+0 && a[3]+0 == want+0)
                    print $1
            }' | while read -r wid; do
                DISPLAY=$DPY xwininfo -id "$wid" 2>/dev/null \
                    | grep -q 'Map State: IsViewable' && { printf '%s\n' "$wid"; break; }
            done
}

click_at() {
    xdo mousemove "$1" "$2"
    xdo click 1
}

dblclick_at() {
    xdo mousemove "$1" "$2"
    xdo click --repeat 2 --delay 60 1
}

for tool in Xvfb xdpyinfo xdotool xprop xwininfo; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

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
  NewStyle = new;
  TitlebarLeft = (Close, Miniaturize, Maximize);
  TitlebarRight = ();
  DbClickMaximize = YES;
  DoubleClickTime = 300;
}
EOF

${CC:-cc} -std=gnu11 "$ROOT/test/mothints-client.c" -o "$MOTH" -lX11 >>"$LOG" 2>&1 \
    || fatal "could not build test/mothints-client.c"

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use" >&2; exit 1
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
    DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
wait_for 80 ipc_ready || fatal "wmaker IPC did not come up"
ROOTW=$(DISPLAY=$DPY xwininfo -root 2>/dev/null \
            | awk '/Window id:/ {print $4; exit}')
[ -n "$ROOTW" ] || fatal "could not read the root window id"
sleep 0.5

# --- 1. close-button double-click force-kills a delete-less client ---------
note "close-button double-click force-kills"
start_client tdclose || fatal "close test client did not map"
titlebar_metrics || fatal "could not read the close client frame"
CWID=$(button_wid_at "$FRAME" 0)
[ -n "$CWID" ] || fatal "no viewable close button at offset 0"
set -- $(geom_of "$CWID")
CX=$(( $1 + $3 / 2 ))
CY=$(( $2 + $4 / 2 ))
note "close button $CWID center $CX,$CY, titlebar height $TH"

click_at "$CX" "$CY"
sleep 0.4
check "single close click leaves a delete-less client alive" kill -0 "$CLIENT_PID"
check "single close click keeps wmaker healthy" ipc_ready

sleep 0.7
dblclick_at "$CX" "$CY"
check "double close click force-kills the client" wait_for 40 pid_gone "$CLIENT_PID"
check "wmaker alive after the force-kill" ipc_ready
stop_client

# --- 2. DbClickMaximize titlebar double-click toggles maximize -------------
note "DbClickMaximize titlebar double-click toggles maximize"
start_client tdmax || fatal "maximize test client did not map"
titlebar_metrics || fatal "could not read the maximize client frame"
set -- $(geom_of "$CLIENT_WID")
ORIG_X=$1; ORIG_Y=$2; ORIG_W=$3; ORIG_H=$4
note "original client geometry ${ORIG_W}x${ORIG_H}+${ORIG_X}+${ORIG_Y}"

TX=$(( FX + FW / 2 ))
TY=$(( FY + TH / 2 ))
note "titlebar double-click point $TX,$TY"
dblclick_at "$TX" "$TY"
sleep 0.3

maximized_wide() {
    set -- $(geom_of "$CLIENT_WID")
    [ "$3" -gt $((ORIG_W + 100)) ]
}
maximized_tall() {
    set -- $(geom_of "$CLIENT_WID")
    [ "$4" -gt $((ORIG_H + 100)) ]
}
maximize_state() {
    DISPLAY=$DPY xprop -id "$CLIENT_WID" _NET_WM_STATE 2>/dev/null \
        | grep -q _NET_WM_STATE_MAXIMIZED_HORZ \
    && DISPLAY=$DPY xprop -id "$CLIENT_WID" _NET_WM_STATE 2>/dev/null \
        | grep -q _NET_WM_STATE_MAXIMIZED_VERT
}
check "titlebar double-click maximizes horizontally" wait_for 40 maximized_wide
check "titlebar double-click maximizes vertically" wait_for 40 maximized_tall
check "_NET_WM_STATE reports both maximize directions" wait_for 40 maximize_state

titlebar_metrics || fail "lost the frame after maximize"
TX=$(( FX + FW / 2 ))
TY=$(( FY + TH / 2 ))
dblclick_at "$TX" "$TY"
sleep 0.3

restored_geometry() {
    set -- $(geom_of "$CLIENT_WID")
    [ "$1" = "$ORIG_X" ] && [ "$2" = "$ORIG_Y" ] \
    && [ "$3" = "$ORIG_W" ] && [ "$4" = "$ORIG_H" ]
}
cleared_state() { ! maximize_state; }
check "second titlebar double-click restores the geometry" wait_for 40 restored_geometry
check "second titlebar double-click clears the maximize state" wait_for 40 cleared_state
check "wmaker alive after the maximize toggle" ipc_ready
stop_client

# --- teardown ----------------------------------------------------------------
note "titlebar-dblclick drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
