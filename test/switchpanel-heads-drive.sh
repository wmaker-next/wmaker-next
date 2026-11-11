#!/bin/sh
# Drive switch panel head selection on an unnamed two-head fixture:
# the "#0" and "#1" index tokens, the reserved Cursor and Window
# keywords across heads, and the placement anchors each keyword
# selects. The fixture is the Xephyr +xinerama two-screen arrangement,
# so the heads carry no RandR monitor names and the index tokens are
# the only way to name them. Named-monitor targeting lives in
# switchpanel-placement-drive.sh.
#
# Harness conventions follow switchpanel-placement-drive.sh: a private
# headless parent, isolated WMAKER_USER_ROOT and XDG_RUNTIME_DIR, PID
# tracking, no pkill. Expected anchors use the pad and panel size
# measured from one xwininfo snapshot per case.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${SWITCHPANEL_HEADS_DRIVE_DISPLAY:-:$(drive_default_display)}
W=1024
H=768
HEAD_W=512
PIDFILE=$(mktemp /tmp/switchpanel-heads.pids.XXXXXX)
WMHOME=$(mktemp -d /tmp/switchpanel-heads-wmhome.XXXXXX)
RUNTIME=$(mktemp -d /tmp/switchpanel-heads-run.XXXXXX)
LOG=/tmp/switchpanel-heads-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
WDWRITE="$ROOT/util/.libs/wdwrite"
WDREAD="$ROOT/util/.libs/wdread"
SOCKET=
: > "$PIDFILE"
: > "$LOG"

PASSED=0
FAILED=0
SKIPPED=0

owned_pid() {
    opid=$1
    grep -Fx "$opid" "$PIDFILE" >/dev/null || return 1
    entry=$(pgrep -a -P "$$" | awk -v pid="$opid" '$1 == pid') || return 1
    [ -n "$entry" ] || return 1
    case "$entry" in
        *"Xvfb $PARENT_DISPLAY "*|*"Xephyr $DPY "*) return 0 ;;
    esac
    tr '\0' '\n' < "/proc/$opid/environ" 2>/dev/null | grep -Fx "DISPLAY=$DPY" >/dev/null
}

cleanup() {
    xdo keyup alt 2>/dev/null
    xdo keyup super 2>/dev/null
    while read -r p; do
        [ -n "$p" ] && owned_pid "$p" && kill "$p" 2>/dev/null
    done < "$PIDFILE"
    rm -f "$PIDFILE"
    rm -rf "$WMHOME" "$RUNTIME"
}
trap cleanup EXIT

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
ok()   { PASSED=$((PASSED + 1)); printf 'PASS: %s\n' "$*" | tee -a "$LOG"; }
fail() { FAILED=$((FAILED + 1)); printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; }
skip() { SKIPPED=$((SKIPPED + 1)); printf 'SKIP: %s\n' "$*" | tee -a "$LOG"; }
fatal() { printf 'FATAL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
skip_all() { printf 'SKIP: %s\n' "$*" | tee -a "$LOG"; exit 77; }

check() {
    cd_desc=$1
    shift
    if "$@"; then
        ok "$cd_desc"
    else
        fail "$cd_desc"
    fi
}

wait_for() {
    n=$1; shift
    while [ "$n" -gt 0 ]; do
        "$@" && return 0
        sleep 0.1
        n=$((n - 1))
    done
    return 1
}
pid_gone() { ! pgrep -P "$$" | grep -Fx "$1" >/dev/null; }

xdo() { DISPLAY=$DPY xdotool "$@"; }

msg() {
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$WMAKERMSG" -s "$SOCKET" "$@"
}
ipc_ready() { msg -q get_version >/dev/null 2>&1; }

wmwrite() {
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" DISPLAY=$DPY \
        "$WDWRITE" WindowMaker "$1" "$2" || fatal "wdwrite $1 failed"
}

# --- panel inspection ------------------------------------------------------
panel_info() {
    id=$(DISPLAY=$DPY xdotool search --onlyvisible --name \
        '^Window Maker Switch Panel$' 2>/dev/null | tail -n 1)
    [ -n "$id" ] || return 1
    DISPLAY=$DPY xwininfo -id "$id" 2>/dev/null | awk '
        /Map State:/ { map = $NF }
        /^  Absolute upper-left X:/ { x = $NF }
        /^  Absolute upper-left Y:/ { y = $NF }
        /^  Width:/ { w = $NF }
        /^  Height:/ { h = $NF }
        /^  Border width:/ { b = $NF }
        END {
            if (map != "IsViewable" || x == "" || w == "")
                exit 1
            printf "%s %s %s %s %s\n", x, y, w, h, b
        }'
}
PANEL_GEOM=
panel_geom() {
    PANEL_GEOM=$(panel_info) || { PANEL_GEOM=; return 1; }
    set -- $PANEL_GEOM
    [ $# -eq 5 ] || { PANEL_GEOM=; return 1; }
    return 0
}

panel_up() {
    panel_up_ids=$(DISPLAY=$DPY xdotool search --onlyvisible --name \
        '^Window Maker Switch Panel$' 2>/dev/null)
    [ -n "$panel_up_ids" ] || return 1
    [ "$(printf '%s\n' "$panel_up_ids" | grep -c .)" = 1 ] || return 1
    DISPLAY=$DPY xwininfo -id "$panel_up_ids" 2>/dev/null \
        | grep -q 'Map State: IsViewable'
}
with_focused_panel() {
    xdo keydown alt; sleep 0.1
    xdo key Tab
    if ! wait_for 40 panel_up; then
        xdo keyup alt; sleep 0.25
        return 2
    fi
    "$@"
    with_panel_rc=$?
    xdo keyup alt; sleep 0.25
    return $with_panel_rc
}
# Park the pointer and verify the server reports it there. The nested
# Xephyr server can silently drop a synthetic motion and its virtual
# pointer can jump on its own, so re-issue the move until it sticks.
move_pointer() {
    mp_x=$1
    mp_y=$2
    mp_left=50
    mp_at=
    while [ "$mp_left" -gt 0 ]; do
        xdo mousemove "$mp_x" "$mp_y"
        sleep 0.1
        mp_at=$(xdo getmouselocation 2>/dev/null \
            | sed -n 's/^x:\([0-9-]*\) y:\([0-9-]*\).*/\1 \2/p')
        [ "$mp_at" = "$mp_x $mp_y" ] && return 0
        mp_left=$((mp_left - 1))
    done
    fatal "pointer would not reach ($mp_x,$mp_y), stuck at ${mp_at:-unreachable}"
}
with_panel_at() {
    xdo windowactivate "$1"; sleep 0.3
    move_pointer "$2" "$3"
    shift 3
    with_focused_panel "$@"
}

# --- tool and build preflight ---------------------------------------------
for tool in Xvfb Xephyr xdotool xterm xprop xwininfo xrdb; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not found"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
[ -x "$WDWRITE" ] || skip_all "util/.libs/wdwrite not built"
[ -x "$WDREAD" ] || skip_all "util/.libs/wdread not built"

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
  StrictWindozeCycle = YES;
}
EOF
cat >"$WMHOME/Defaults/WMWindowAttributes" <<'EOF'
{
  "*" = { NoAppIcon = YES; };
}
EOF

# --- Xephyr with two unnamed Xinerama heads --------------------------------
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use, set SWITCHPANEL_HEADS_DRIVE_DISPLAY" >&2
    exit 77
fi
PARENT_DISPLAY=${SWITCHPANEL_HEADS_PARENT_DISPLAY:-":$(( ${DPY#:} + 1 ))"}
Xvfb "$PARENT_DISPLAY" -screen 0 2048x1536x24 -ac >>"$LOG" 2>&1 &
XVFB_PARENT_PID=$!
echo "$XVFB_PARENT_PID" >> "$PIDFILE"
xvfb_parent_ready() {
    owned_pid "$XVFB_PARENT_PID" || fatal "parent Xvfb exited early"
    DISPLAY=$PARENT_DISPLAY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_parent_ready || fatal "parent Xvfb failed to start"

note "start Xephyr $DPY with +xinerama, two ${HEAD_W}x${H} heads"
Xephyr "$DPY" -display "$PARENT_DISPLAY" +xinerama \
    -screen ${HEAD_W}x${H} -screen ${HEAD_W}x${H} -ac >>"$LOG" 2>&1 &
XEPHYR_PID=$!
echo "$XEPHYR_PID" >> "$PIDFILE"
xephyr_ready() {
    owned_pid "$XEPHYR_PID" || fatal "Xephyr exited early"
    DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xephyr_ready || fatal "Xephyr failed to start"

xinerama_heads() {
    DISPLAY=$DPY xdpyinfo -ext XINERAMA 2>/dev/null | grep -c '^  head #'
}
# capability preflight: without two real Xinerama heads the index
# tokens have nothing to address and no ordinary pass may accumulate
heads_ready() { [ "$(xinerama_heads)" -ge 2 ]; }
wait_for 20 heads_ready \
    || skip_all "Xephyr did not publish two Xinerama heads"

# --- wmaker ----------------------------------------------------------------
printf 'Xft.dpi: 96\n' | DISPLAY=$DPY xrdb -merge >>"$LOG" 2>&1
note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    ASAN_OPTIONS="$(drive_asan_options)" \
    DISPLAY=$DPY \
    "$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
wm_ready() {
    DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null \
        | grep -q 'window id'
}
wait_for 120 wm_ready || fatal "wmaker did not come up"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
case "$SOCKET" in
    "$RUNTIME"/*) : ;;
    *) fatal "IPC socket $SOCKET escaped private XDG_RUNTIME_DIR" ;;
esac
wait_for 80 ipc_ready || fatal "wmaker IPC did not come up at $SOCKET"
sleep 0.5

# --- spawn windows ---------------------------------------------------------
# Terms on both heads so pointer and focus can sit on different heads.
spawn_term() {
    DISPLAY=$DPY xterm -title "$1" -class "$1" \
        -geometry 30x10+$2+$3 -e 'sleep 600' >>"$LOG" 2>&1 &
    p=$!
    echo "$p" >> "$PIDFILE"
}
find_client() {
    DISPLAY=$DPY xdotool search --name "^$1\$" 2>/dev/null | tail -n 1
}

note "spawn two xterms per head"
spawn_term h0a 40 60
spawn_term h0b 40 300
spawn_term h1a $((HEAD_W + 40)) 60
spawn_term h1b $((HEAD_W + 40)) 300
for t in h0a h0b h1a h1b; do
    wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^$t\$' 2>/dev/null | grep -q ." \
        || fatal "$t did not map"
done
H0A=$(find_client h0a)
H0B=$(find_client h0b)
H1A=$(find_client h1a)
H1B=$(find_client h1b)

# --- geometry helpers ------------------------------------------------------
SNAP_X= SNAP_Y= SNAP_W= SNAP_H=
snap_geom() {
    panel_geom || return 1
    set -- $PANEL_GEOM
    SNAP_X=$1; SNAP_Y=$2; SNAP_W=$3; SNAP_H=$4
    return 0
}
PAD_X= PAD_Y= REF_W= REF_H=
place() {
    wmwrite SwitchPanelPlacement "$1"
    wmwrite SwitchPanelMonitor "$2"
    msg -q reconfigure || fatal "reconfigure rejected"
    sleep 0.25
}

# expected anchors for a panel of the measured size inside the head at
# $1, $2, of size HEAD_W x H, with the pad measured from topleft
expect_at() {
    ea_what=$1; ea_hx=$2; ea_hy=$3; ea_pos=$4
    panel_geom || { fail "no visible panel for $ea_what"; return 1; }
    set -- $PANEL_GEOM
    px=$1; py=$2; pw=$3; ph=$4
    case "$ea_pos" in
    center)     ex=$(( ea_hx + (HEAD_W - pw) / 2 )); ey=$(( ea_hy + (H - ph) / 2 )) ;;
    top)        ex=$(( ea_hx + (HEAD_W - pw) / 2 )); ey=$(( ea_hy + PAD_Y )) ;;
    bottom)     ex=$(( ea_hx + (HEAD_W - pw) / 2 )); ey=$(( ea_hy + H - ph - PAD_Y )) ;;
    left)       ex=$(( ea_hx + PAD_X )); ey=$(( ea_hy + (H - ph) / 2 )) ;;
    right)      ex=$(( ea_hx + HEAD_W - pw - PAD_X )); ey=$(( ea_hy + (H - ph) / 2 )) ;;
    topleft)    ex=$(( ea_hx + PAD_X )); ey=$(( ea_hy + PAD_Y )) ;;
    topright)   ex=$(( ea_hx + HEAD_W - pw - PAD_X )); ey=$(( ea_hy + PAD_Y )) ;;
    bottomleft) ex=$(( ea_hx + PAD_X )); ey=$(( ea_hy + H - ph - PAD_Y )) ;;
    bottomright) ex=$(( ea_hx + HEAD_W - pw - PAD_X )); ey=$(( ea_hy + H - ph - PAD_Y )) ;;
    esac
    if [ "$px" = "$ex" ] && [ "$py" = "$ey" ]; then
        ok "$ea_what at ($px,$py) for ${pw}x${ph}"
    else
        fail "$ea_what expected ($ex,$ey) got ($px,$py) for ${pw}x${ph}"
    fi
    return 0
}

# measure the pad and the reference panel size from one topleft case
place topleft '""'
if with_panel_at "$H0A" 128 200 snap_geom; then
    PAD_X=$SNAP_X; PAD_Y=$SNAP_Y; REF_W=$SNAP_W; REF_H=$SNAP_H
    ok "reference pad ($PAD_X,$PAD_Y) size ${REF_W}x${REF_H} on head 0"
else
    fatal "no panel for the reference measurement"
fi

# --- 1. unnamed-head index tokens -----------------------------------------
note "monitor: unnamed head indices #0 and #1"
# pointer on head 1, panel must land on head 0 for #0
place topleft '"#0"'
with_panel_at "$H0A" $((HEAD_W + 128)) 200 expect_at "#0 targets head 0" 0 0 topleft \
    || fail "no panel for #0"
# pointer on head 0, panel must land on head 1 for #1
place topleft '"#1"'
with_panel_at "$H1A" 128 200 expect_at "#1 targets head 1" "$HEAD_W" 0 topleft \
    || fail "no panel for #1"
# out of range and malformed tokens fall back to the pointer head
place topleft '"#2"'
if with_panel_at "$H0A" 128 200 snap_geom; then
    if [ "$SNAP_X" -lt "$HEAD_W" ]; then
        ok "#2 out of range falls back to the pointer head"
    else
        fail "#2 resolved past the head list (x=$SNAP_X)"
    fi
else
    fail "no panel for the out-of-range index"
fi
place topleft '"#5abc"'
if with_panel_at "$H1A" $((HEAD_W + 128)) 200 snap_geom; then
    if [ "$SNAP_X" -ge "$HEAD_W" ]; then
        ok "malformed index falls back to the pointer head"
    else
        fail "malformed index resolved (x=$SNAP_X)"
    fi
else
    fail "no panel for the malformed index"
fi
check "malformed index stays configured" sh -c \
    "LD_LIBRARY_PATH=$WMLIBS DISPLAY=$DPY $WDREAD WindowMaker SwitchPanelMonitor | grep -q '#5abc'"

# --- 2. Cursor is the pointer head and keeps the legacy fit ---------------
note "keyword: Cursor selects the pointer head with the legacy fit"
place center '""'
with_panel_at "$H1A" $((HEAD_W + 128)) 200 snap_geom || fail "no panel for the empty default"
EMPTY_W=$SNAP_W
place center Cursor
if with_panel_at "$H1A" $((HEAD_W + 128)) 200 snap_geom; then
    if [ "$SNAP_X" -ge "$HEAD_W" ] && [ "$SNAP_W" = "$EMPTY_W" ]; then
        ok "Cursor targets the pointer head with the legacy count ($SNAP_W px)"
    else
        fail "Cursor geometry wrong: x=$SNAP_X w=$SNAP_W, empty default was $EMPTY_W"
    fi
else
    fail "no panel for the Cursor keyword"
fi
# the keyword is recognized case-insensitively
place center CURSOR
if with_panel_at "$H0A" 128 200 snap_geom; then
    [ "$SNAP_X" -lt "$HEAD_W" ] \
        && ok "CURSOR is recognized case-insensitively" \
        || fail "CURSOR did not select the pointer head (x=$SNAP_X)"
else
    fail "no panel for the uppercase keyword"
fi

# --- 3. Window is the focused-window head and stays put during the cycle --
note "keyword: Window targets the focused-window head"
place topleft Window
# focus on head 0 while the pointer sits on head 1
xdo windowactivate "$H0B"; sleep 0.3
if with_panel_at "$H0A" $((HEAD_W + 128)) 200 snap_geom; then
    if [ "$SNAP_X" -lt "$HEAD_W" ]; then
        ok "Window targets the focused window head (x=$SNAP_X)"
    else
        fail "Window followed the pointer instead (x=$SNAP_X)"
    fi
else
    fail "no panel for the Window keyword"
fi
# the snapshot is immutable: cycling onto windows of the other head
# must not drag the panel along
xdo windowactivate "$H0B"; sleep 0.3
move_pointer $((HEAD_W + 128)) 200
xdo keydown alt; sleep 0.1
xdo key Tab
if wait_for 40 panel_up; then
    panel_geom || fail "no geometry for the Window snapshot"
    set -- $PANEL_GEOM
    snap_x=$1
    xdo key Tab; sleep 0.3
    xdo key Tab; sleep 0.3
    if panel_geom; then
        set -- $PANEL_GEOM
        if [ "$1" = "$snap_x" ] && [ "$1" -lt "$HEAD_W" ]; then
            ok "Window head kept across selection changes (x=$1)"
        else
            fail "panel moved during the cycle: x=$snap_x -> $1"
        fi
    else
        fail "panel vanished during the cycle"
    fi
else
    fail "panel did not open for the Window snapshot"
fi
xdo keyup alt; sleep 0.3

# --- 4. every placement under both keywords -------------------------------
# With pointer and focus on different heads, each keyword must anchor
# against its own head's rectangle for all nine positions.
note "placement: nine anchors under Cursor and Window"
for pos in center top bottom left right topleft topright bottomleft bottomright; do
    # Cursor: pointer on head 1
    place "$pos" Cursor
    with_panel_at "$H0A" $((HEAD_W + 128)) 200 expect_at "Cursor $pos" "$HEAD_W" 0 "$pos" \
        || fail "no panel for Cursor $pos"
    # Window: focused window on head 0, pointer on head 1
    place "$pos" Window
    with_panel_at "$H0A" $((HEAD_W + 128)) 200 expect_at "Window $pos" 0 0 "$pos" \
        || fail "no panel for Window $pos"
done
check "wmaker alive after the keyword placements" ipc_ready

# --- 5. the reverse separation --------------------------------------------
# Focus on head 1 while the pointer sits on head 0: Window must follow
# the focus and Cursor the pointer, in the opposite direction.
note "keywords in the reverse head separation"
place topleft Window
with_panel_at "$H1B" 128 200 expect_at "Window reverse" "$HEAD_W" 0 topleft \
    || fail "no panel for the reverse Window case"
place topleft Cursor
if with_panel_at "$H1B" 128 200 snap_geom; then
    [ "$SNAP_X" -lt "$HEAD_W" ] \
        && ok "Cursor reverse follows the pointer (x=$SNAP_X)" \
        || fail "Cursor reverse followed the focus (x=$SNAP_X)"
else
    fail "no panel for the reverse Cursor case"
fi
place center '""'

# --- teardown --------------------------------------------------------------
note "switchpanel heads drive summary"
printf 'PASS: %d, FAIL: %d, SKIP: %d\n' "$PASSED" "$FAILED" "$SKIPPED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
