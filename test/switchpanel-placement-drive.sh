#!/bin/sh
# Drive switch panel placement: the nine SwitchPanelPlacement anchors
# on a single head, forced strip fitting on a narrower named monitor,
# SwitchPanelMonitor name resolution and fallback, invalid
# SwitchPanelIconSize reload, runtime reload semantics, and panel-less
# cycling with SwitchPanelImages = ().
#
# Harness conventions follow switchpanel-drive.sh: Xephyr on a private
# headless parent, isolated WMAKER_USER_ROOT and XDG_RUNTIME_DIR, PID
# tracking, no pkill. Anchor math uses the width and height measured
# from one xwininfo snapshot plus a pad measured from the first corner
# placement, so no scale-derived constant is hard-coded.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${SWITCHPANEL_PLACEMENT_DRIVE_DISPLAY:-:$(drive_default_display)}
W=1024
H=768
PIDFILE=$(mktemp /tmp/switchpanel-placement.pids.XXXXXX)
WMHOME=$(mktemp -d /tmp/switchpanel-placement-wmhome.XXXXXX)
RUNTIME=$(mktemp -d /tmp/switchpanel-placement-run.XXXXXX)
FAULT_PATH=$WMHOME/panel-fault
FAULT_SHIM=$WMHOME/panel-fault.so
LOG=/tmp/switchpanel-placement-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
WDWRITE="$ROOT/util/.libs/wdwrite"
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
    # release any cycling modifier an aborted test left logically held:
    # XTEST presses outlive the client that sent them and would pin
    # StartWindozeCycle in its event wait with the panel still mapped
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
xr() { DISPLAY=$DPY xrandr "$@"; }

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
# One xwininfo snapshot per check: X, Y, width, height, border, map state.
# Only a visible window with the exact title counts as the panel.
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
# Capture "x y w h border" into PANEL_GEOM. Fails when the panel is
# absent or the snapshot is incomplete, so callers never read an unset
# positional parameter.
PANEL_GEOM=
panel_geom() {
    PANEL_GEOM=$(panel_info) || { PANEL_GEOM=; return 1; }
    set -- $PANEL_GEOM
    [ $# -eq 5 ] || { PANEL_GEOM=; return 1; }
    return 0
}

# Hold the cycling modifier, run one body command with the panel up,
# then release. Exactly one live panel window must be up while held.
panel_up() {
    panel_up_ids=$(DISPLAY=$DPY xdotool search --onlyvisible --name \
        '^Window Maker Switch Panel$' 2>/dev/null)
    [ -n "$panel_up_ids" ] || return 1
    [ "$(printf '%s\n' "$panel_up_ids" | grep -c .)" = 1 ] || return 1
    DISPLAY=$DPY xwininfo -id "$panel_up_ids" 2>/dev/null \
        | grep -q 'Map State: IsViewable'
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
with_panel() {
    xdo windowactivate "$1"; sleep 0.3
    shift
    with_focused_panel "$@"
}
with_panel_at() {
    xdo windowactivate "$1"; sleep 0.3
    move_pointer "$2" "$3"
    shift 3
    with_focused_panel "$@"
}

# --- tool and build preflight ---------------------------------------------
for tool in Xvfb Xephyr xdotool xterm xprop xwininfo xrandr xrdb cc; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not found"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
[ -x "$WDWRITE" ] || skip_all "util/.libs/wdwrite not built"
cc -shared -fPIC -I"$ROOT/wrlib" "$ROOT/test/switchpanel-fault.c" -ldl \
    -o "$FAULT_SHIM" || fatal "cannot build fault injection shim"

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

# --- Xephyr ----------------------------------------------------------------
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use, set SWITCHPANEL_PLACEMENT_DRIVE_DISPLAY" >&2
    exit 77
fi
PARENT_DISPLAY=${SWITCHPANEL_PLACEMENT_PARENT_DISPLAY:-":$(( ${DPY#:} + 1 ))"}
Xvfb "$PARENT_DISPLAY" -screen 0 2048x1536x24 -ac >>"$LOG" 2>&1 &
XVFB_PARENT_PID=$!
echo "$XVFB_PARENT_PID" >> "$PIDFILE"
xvfb_parent_ready() {
    owned_pid "$XVFB_PARENT_PID" || fatal "parent Xvfb exited early"
    DISPLAY=$PARENT_DISPLAY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_parent_ready || fatal "parent Xvfb failed to start"

note "start Xephyr $DPY on parent $PARENT_DISPLAY"
Xephyr "$DPY" -display "$PARENT_DISPLAY" -screen ${W}x${H} -ac >>"$LOG" 2>&1 &
XEPHYR_PID=$!
echo "$XEPHYR_PID" >> "$PIDFILE"
xephyr_ready() {
    owned_pid "$XEPHYR_PID" || fatal "Xephyr exited early"
    DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xephyr_ready || fatal "Xephyr failed to start"

# --- log helpers -----------------------------------------------------------
# The wmaker log is the oracle for the once-per-value diagnostics and
# for the schema warnings. Callers record a baseline first so only the
# messages a scenario emits are counted.
LOG_MARK=0
log_mark() { LOG_MARK=$(wc -l < "$LOG"); }
log_since() { tail -n +"$((LOG_MARK + 1))" "$LOG"; }
log_count() { log_since | grep -c "$1"; }

# --- wmaker ----------------------------------------------------------------
# The baseline fixture runs at 96 dpi so the UI scale is exactly 1.
printf 'Xft.dpi: 96\n' | DISPLAY=$DPY xrdb -merge >>"$LOG" 2>&1
note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    LD_PRELOAD="${LD_PRELOAD:+$LD_PRELOAD:}$FAULT_SHIM" \
    WMAKER_PANEL_FAULT_PATH="$FAULT_PATH" \
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
# Enough windows at the default tile size (64 -> 77 px) that a 512 px
# head cannot show them all: 8 windows need 616 px of strip.
spawn_term() {
    # -e keeps the shell from rewriting the title the searches match,
    # the optional 4th argument groups two windows under one class
    DISPLAY=$DPY xterm -title "$1" -class "${4:-$1}" \
        -geometry 60x20+$2+$3 -e 'sleep 600' >>"$LOG" 2>&1 &
    p=$!
    echo "$p" >> "$PIDFILE"
}
find_client() {
    DISPLAY=$DPY xdotool search --name "^$1\$" 2>/dev/null | tail -n 1
}

note "spawn eight xterms plus two drillable pairs"
i=0
while [ "$i" -lt 8 ]; do
    spawn_term "pt$i" $((40 + i * 110)) $((60 + (i % 4) * 140))
    i=$((i + 1))
done
# GrpA and GrpB share WM_CLASS Grp, Grp2A and Grp2B share Grp2, so the
# app-group cycle has two groups it can drill and refresh between
spawn_term GrpA 300 400 Grp
spawn_term GrpB 500 400 Grp
spawn_term Grp2A 300 520 Grp2
spawn_term Grp2B 500 520 Grp2
i=0
while [ "$i" -lt 8 ]; do
    wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^pt$i\$' 2>/dev/null | grep -q ." \
        || fatal "pt$i did not map"
    i=$((i + 1))
done
for t in GrpA GrpB Grp2A Grp2B; do
    wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^$t\$' 2>/dev/null | grep -q ." \
        || fatal "$t did not map"
done
PT7=$(find_client pt7)
GA=$(find_client GrpA)
GB=$(find_client GrpB)
G2A=$(find_client Grp2A)
G2B=$(find_client Grp2B)

# deterministic MRU: activate in order so GrpB is most recent
i=0
while [ "$i" -lt 8 ]; do
    xdo windowactivate "$(find_client pt$i)"; sleep 0.15
    i=$((i + 1))
done
xdo windowactivate "$GA"; sleep 0.15
xdo windowactivate "$GB"; sleep 0.3

# capture the panel geometry from inside with_panel, the panel is gone
# once the cycling modifier is released
SNAP_X= SNAP_Y= SNAP_W= SNAP_H=
snap_geom() {
    panel_geom || return 1
    set -- $PANEL_GEOM
    SNAP_X=$1; SNAP_Y=$2; SNAP_W=$3; SNAP_H=$4
    return 0
}

# --- 1. default placement is dead center ----------------------------------
note "placement: default center on the whole screen"
place() {
    wmwrite SwitchPanelPlacement "$1"
    wmwrite SwitchPanelMonitor "$2"
    msg -q reconfigure || fatal "reconfigure rejected"
    sleep 0.25
}

center_check() {
    cc_what=$1
    panel_geom || { fail "no visible panel for $cc_what"; return; }
    set -- $PANEL_GEOM
    px=$1; py=$2; pw=$3; ph=$4
    ex=$(( (W - pw) / 2 ))
    ey=$(( (H - ph) / 2 ))
    if [ "$px" = "$ex" ] && [ "$py" = "$ey" ]; then
        ok "$cc_what centered at ($px,$py) for ${pw}x${ph}"
    else
        fail "$cc_what expected ($ex,$ey) got ($px,$py) for ${pw}x${ph}"
    fi
}

with_panel "$PT7" center_check "absent keys" \
    || fail "panel not visible with absent placement keys"
place center '""'
with_panel "$PT7" center_check "unset monitor" \
    || fail "panel not visible for center"

# --- 2. the nine placements on one head ------------------------------------
# The pad is measured from the first corner placement and reused, so the
# drive never hard-codes WMScaleY(10).
note "placement: nine anchors, pad measured from topleft"
PAD_X=
PAD_Y=

anchor_check() {
    what=$1
    panel_geom || { fail "no visible panel for $what"; return; }
    set -- $PANEL_GEOM
    px=$1; py=$2; pw=$3; ph=$4
    case "$what" in
    topleft)
        PAD_X=$px; PAD_Y=$py
        if [ "$px" -ge 0 ] && [ "$px" -le 64 ] && [ "$py" -ge 0 ] && [ "$py" -le 64 ]; then
            ok "topleft at pad ($PAD_X,$PAD_Y)"
        else
            fail "topleft pad implausible ($px,$py)"
        fi
        ;;
    center)
        ex=$(( (W - pw) / 2 )); ey=$(( (H - ph) / 2 ))
        [ "$px" = "$ex" ] && [ "$py" = "$ey" ] \
            && ok "center matches head center" \
            || fail "center expected ($ex,$ey) got ($px,$py)"
        ;;
    top)
        ex=$(( (W - pw) / 2 ))
        [ "$px" = "$ex" ] && [ "$py" = "$PAD_Y" ] \
            && ok "top centered horizontally at pad Y" \
            || fail "top expected ($ex,$PAD_Y) got ($px,$py)"
        ;;
    bottom)
        ex=$(( (W - pw) / 2 )); ey=$(( H - ph - PAD_Y ))
        [ "$px" = "$ex" ] && [ "$py" = "$ey" ] \
            && ok "bottom matches measured pad" \
            || fail "bottom expected ($ex,$ey) got ($px,$py)"
        ;;
    left)
        ey=$(( (H - ph) / 2 ))
        [ "$px" = "$PAD_X" ] && [ "$py" = "$ey" ] \
            && ok "left centered vertically at pad X" \
            || fail "left expected ($PAD_X,$ey) got ($px,$py)"
        ;;
    right)
        ex=$(( W - pw - PAD_X )); ey=$(( (H - ph) / 2 ))
        [ "$px" = "$ex" ] && [ "$py" = "$ey" ] \
            && ok "right matches measured pad" \
            || fail "right expected ($ex,$ey) got ($px,$py)"
        ;;
    topright)
        ex=$(( W - pw - PAD_X ))
        [ "$px" = "$ex" ] && [ "$py" = "$PAD_Y" ] \
            && ok "topright mirrors measured pad" \
            || fail "topright expected ($ex,$PAD_Y) got ($px,$py)"
        ;;
    bottomleft)
        ey=$(( H - ph - PAD_Y ))
        [ "$px" = "$PAD_X" ] && [ "$py" = "$ey" ] \
            && ok "bottomleft mirrors measured pad" \
            || fail "bottomleft expected ($PAD_X,$ey) got ($px,$py)"
        ;;
    bottomright)
        ex=$(( W - pw - PAD_X )); ey=$(( H - ph - PAD_Y ))
        [ "$px" = "$ex" ] && [ "$py" = "$ey" ] \
            && ok "bottomright mirrors measured pad" \
            || fail "bottomright expected ($ex,$ey) got ($px,$py)"
        ;;
    esac
}

for pos in topleft center top bottom left right topright bottomleft bottomright; do
    place "$pos" '""'
    with_panel "$PT7" anchor_check "$pos" \
        || fail "panel not visible for $pos"
done

# --- 3. geometry survives drill and undrill -------------------------------
# With center placement the grown panel re-centers: re-measure after the
# sub-strip opens and closes instead of assuming the old corner. The Grp
# group must be the second MRU entry when the panel opens:
# StartWindozeCycle() auto-advances one entry before the event loop, so
# the group pair is activated first and the plain term that opens the
# cycle is activated last. The auto-drill preview then acts on the
# drillable Grp entry instead of an undrillable plain term. Geometry is
# polled against the creation-time rectangle, never slept on: an
# AUTO_DRILL_DELAY-shaped sleep is not an oracle. Height-only growth
# cannot move the centered X, so both axes are asserted.
note "placement: re-measure after preview, drill and undrill"
C_X= C_Y= C_W= C_H=
snap_creation() {
    panel_geom || return 1
    set -- $PANEL_GEOM
    C_X=$1; C_Y=$2; C_W=$3; C_H=$4
    return 0
}
grown_h() {
    panel_geom || return 1
    set -- $PANEL_GEOM
    [ "$4" -gt "$C_H" ]
}
shrunken_h() {
    panel_geom || return 1
    set -- $PANEL_GEOM
    [ "$4" -le "$C_H" ]
}
snap_h() {
    panel_geom || return 1
    set -- $PANEL_GEOM
    G_H=$4
    return 0
}
# expected anchors from the measured size and the pad measured from the
# first corner placement of section 2
expect_geom() {
    eg_what=$1; eg_px=$2; eg_py=$3
    panel_geom || { fail "no visible panel for $eg_what"; return 1; }
    set -- $PANEL_GEOM
    if [ "$1" = "$eg_px" ] && [ "$2" = "$eg_py" ]; then
        ok "$eg_what re-placed to ($1,$2) for ${3}x${4}"
    else
        fail "$eg_what expected ($eg_px,$eg_py) got ($1,$2) for ${3}x${4}"
    fi
    return 0
}
drill_anchor() {
    da_w=$1; da_h=$2
    case "$dc_place" in
    center|top|bottom) ex=$(( (W - da_w) / 2 )) ;;
    left|topleft|bottomleft) ex=$PAD_X ;;
    right|topright|bottomright) ex=$(( W - da_w - PAD_X )) ;;
    esac
    case "$dc_place" in
    center|left|right) ey=$(( (H - da_h) / 2 )) ;;
    top|topleft|topright) ey=$PAD_Y ;;
    bottom|bottomleft|bottomright) ey=$(( H - da_h - PAD_Y )) ;;
    esac
}
drill_cycle() {
    dc_place=$1
    place "$dc_place" '""'
    msg -q reconfigure || true
    sleep 0.2
    # Grp is the second MRU entry and Grp2 the third: the auto-advance
    # lands on Grp and the drillable Grp2 follows it for the refresh
    xdo windowactivate "$G2A"; sleep 0.15
    xdo windowactivate "$G2B"; sleep 0.15
    xdo windowactivate "$GA"; sleep 0.15
    xdo windowactivate "$GB"; sleep 0.15
    xdo windowactivate "$PT7"; sleep 0.3
    xdo keydown super; sleep 0.1
    xdo key Tab
    wait_for 30 snap_creation || {
        xdo keyup super; fail "no panel for the drill check at $dc_place"; return 1
    }
    drill_anchor "$C_W" "$C_H"
    expect_geom "$dc_place creation" "$ex" "$ey" || true
    # the auto-drill preview opens the sub-strip and grows the panel
    if ! wait_for 60 grown_h; then
        xdo keyup super
        fail "sub-strip never opened at $dc_place"
        return 1
    fi
    snap_h || { xdo keyup super; fail "panel vanished after growth"; return 1; }
    drill_anchor "$C_W" "$G_H"
    expect_geom "$dc_place preview" "$ex" "$ey" || true
    # Down transitions the preview into a drill in place
    xdo key Down
    wait_for 30 grown_h || fail "panel shrank during drill at $dc_place"
    drill_anchor "$C_W" "$G_H"
    expect_geom "$dc_place drill" "$ex" "$ey" || true
    # a Tab while drilled refreshes the sub-strip without moving
    xdo key Tab
    sleep 0.3
    expect_geom "$dc_place refresh" "$ex" "$ey" || true
    # Up undrills back to the creation rectangle
    xdo key Up
    wait_for 30 shrunken_h || fail "panel did not shrink on undrill at $dc_place"
    drill_anchor "$C_W" "$C_H"
    expect_geom "$dc_place undrill" "$ex" "$ey" || true
    xdo keyup super; sleep 0.3
}

wmwrite SwitchPanelGroupLabelFormat '"%I"'
msg -q reconfigure || true
sleep 0.2
for anchor in center top bottom left right topleft topright bottomleft bottomright; do
    drill_cycle "$anchor"
done
check "wmaker alive after drill geometry checks" ipc_ready

# --- 3b. allocation and conversion failures -------------------------------
note "fault injection: panel creation and drill rollback"
place center '""'
wmwrite SwitchPanelImages '(swtile.png, swback.png, 290, 290)'
msg -q reconfigure || fatal "reconfigure rejected for fault injection"
for fault in tile convert window windowchild; do
    xdo windowactivate "$PT7"; sleep 0.2
    xdo keydown alt; sleep 0.1
    printf '%s\n' "$fault" > "$FAULT_PATH"
    xdo key Tab; sleep 0.4
    check "$fault failure was injected" [ ! -e "$FAULT_PATH" ]
    check "$fault failure leaves no panel" sh -c \
        "[ -z \"\$(DISPLAY=$DPY xdotool search --onlyvisible --name '^Window Maker Switch Panel\$' 2>/dev/null)\" ]"
    check "$fault failure keeps IPC responsive" ipc_ready
    xdo keyup alt; sleep 0.3
done
xdo windowactivate "$GA"; sleep 0.15
xdo windowactivate "$GB"; sleep 0.15
xdo windowactivate "$PT7"; sleep 0.2
xdo keydown super; sleep 0.1
xdo key Tab
if wait_for 30 snap_creation; then
    printf 'smooth\n' > "$FAULT_PATH"
    sleep 1.1
    check "preview allocation failure was injected" [ ! -e "$FAULT_PATH" ]
    check "failed preview keeps original height" shrunken_h
    printf 'window\n' > "$FAULT_PATH"
    xdo key Down; sleep 0.3
    check "drill window creation failure was injected" [ ! -e "$FAULT_PATH" ]
    check "failed drill window creation keeps original height" shrunken_h
    printf 'windowchild\n' > "$FAULT_PATH"
    xdo key Down; sleep 0.3
    check "drill child-window failure was injected" [ ! -e "$FAULT_PATH" ]
    check "failed drill child-window keeps original height" shrunken_h
    printf 'convert\n' > "$FAULT_PATH"
    xdo key Down; sleep 0.3
    check "drill tile conversion failure was injected" [ ! -e "$FAULT_PATH" ]
    check "failed drill keeps original height" shrunken_h
    xdo key Down
    if wait_for 30 grown_h; then
        ok "drill succeeds after failed preview"
        printf 'mask\n' > "$FAULT_PATH"
        xdo key Up; sleep 0.3
        check "undrill conversion failure was injected" [ ! -e "$FAULT_PATH" ]
        check "failed undrill keeps grown height" grown_h
        xdo key Up
        wait_for 30 shrunken_h && ok "undrill succeeds after retry" \
            || fail "undrill failed after retry"
        rebuild_ok=1
        rebuild_count=0
        while [ "$rebuild_count" -lt 100 ]; do
            xdo key Down
            if ! wait_for 30 grown_h; then
                rebuild_ok=0
                break
            fi
            xdo key Up
            if ! wait_for 30 shrunken_h; then
                rebuild_ok=0
                break
            fi
            rebuild_count=$((rebuild_count + 1))
        done
        if [ "$rebuild_ok" = 1 ]; then
            ok "100 background grow and shrink rebuilds succeed"
        else
            fail "background rebuild failed after $rebuild_count cycles"
        fi
    else
        fail "drill failed after failed preview"
    fi
else
    fail "no panel for drill fault injection"
fi
xdo keyup super; sleep 0.3
rm -f "$FAULT_PATH"
wmwrite SwitchPanelImages '(swtile.png, swback.png, 30, 40)'
msg -q reconfigure || fatal "reconfigure rejected after fault injection"

# --- 4. invalid SwitchPanelIconSize adopts 64 -----------------------------
# Each rejected value warns through the numeric schema path and leaves
# the following cycles functional.
note "SwitchPanelIconSize: invalid values adopt 64"
wmwrite SwitchPanelIconSize 64
msg -q reconfigure || fatal "reconfigure rejected for baseline icon size"
with_panel "$PT7" snap_geom || fatal "no baseline panel for icon size"
DEFAULT_ICON_W=$SNAP_W
DEFAULT_ICON_H=$SNAP_H
for bad in 0 '"-5"' 16668; do
    wmwrite SwitchPanelIconSize "$bad"
    log_mark
    msg -q reconfigure || fail "reconfigure rejected for icon size $bad"
    sleep 0.25
    if with_panel "$PT7" snap_geom; then
        check "icon size $bad uses default width" [ "$SNAP_W" = "$DEFAULT_ICON_W" ]
        check "icon size $bad uses default height" [ "$SNAP_H" = "$DEFAULT_ICON_H" ]
    else
        fail "no panel after icon size $bad"
    fi
    check "icon size $bad logs its warning" \
        [ "$(log_count SwitchPanelIconSize)" -ge 1 ]
    check "wmaker alive after icon size $bad" ipc_ready
done
wmwrite SwitchPanelIconSize 64
msg -q reconfigure || true
sleep 0.2

# --- 5. runtime reload keeps an open panel, next cycle adopts -------------
note "runtime reload: open panel keeps geometry, next cycle adopts"
place topleft '""'
sleep 0.2
xdo windowactivate "$PT7"; sleep 0.2
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.35
panel_geom || fail "panel missing before reload"
set -- $PANEL_GEOM
old_x=$1; old_y=$2
wmwrite SwitchPanelPlacement bottomright
msg -q reconfigure || fail "reconfigure rejected mid-cycle"
sleep 0.35
if panel_geom; then
    set -- $PANEL_GEOM
    if [ "$1" = "$old_x" ] && [ "$2" = "$old_y" ]; then
        ok "open panel retained its geometry across reload"
    else
        fail "open panel moved on reload: ($old_x,$old_y) -> ($1,$2)"
    fi
else
    fail "panel vanished on reload"
fi
xdo keyup alt; sleep 0.3
check "panel closed after mid-cycle reload" sh -c "[ -z \"\$(DISPLAY=$DPY xdotool search --onlyvisible --name '^Window Maker Switch Panel\$' 2>/dev/null)\" ]"
with_panel "$PT7" anchor_check bottomright \
    || fail "panel not visible after reload"
check "wdread keeps the configured value" sh -c \
    "LD_LIBRARY_PATH=$WMLIBS DISPLAY=$DPY $ROOT/util/.libs/wdread WindowMaker SwitchPanelPlacement | grep -q bottomright"

# --- 5b. unresolved monitor warns once per value ---------------------------
# One diagnostic per distinct unresolved value: three invocations of the
# same value warn once, a changed value warns again, and the values stay
# configured so they can resolve after a topology change.
note "monitor: unresolved value warns once per value"
place topleft '"stale-output-a"'
log_mark
i=0
while [ "$i" -lt 3 ]; do
    with_panel "$PT7" true || fail "no panel with an unresolved monitor"
    i=$((i + 1))
done
check "three invocations, one diagnostic" [ "$(log_count 'matches no output')" -eq 1 ]
place topleft '"stale-output-b"'
log_mark
with_panel "$PT7" true || fail "no panel with a changed unresolved monitor"
check "a changed value warns again" [ "$(log_count 'matches no output')" -eq 1 ]
check "wdread keeps the changed value" sh -c \
    "LD_LIBRARY_PATH=$WMLIBS DISPLAY=$DPY $ROOT/util/.libs/wdread WindowMaker SwitchPanelMonitor | grep -q stale-output-b"

# --- 5c. mid-cycle SwitchPanelImages reload -------------------------------
# An open panel owns its creation-time images: disabling or replacing
# SwitchPanelImages must not pull them out from under a running cycle.
# The IPC dispatch this depends on runs inside the cycling wait.
note "runtime reload: SwitchPanelImages under an open panel"
wmwrite SwitchPanelImages '(swtile.png, swback.png, 290, 290)'
place center '""'
msg -q reconfigure || true
sleep 0.3
xdo windowactivate "$GA"; sleep 0.15
xdo windowactivate "$GB"; sleep 0.15
xdo windowactivate "$PT7"; sleep 0.3
xdo keydown super; sleep 0.1
xdo key Tab
if wait_for 30 snap_creation; then
    wmwrite SwitchPanelImages '(swtile.png, swback.png, 280, 280)'
    msg -q reconfigure || fail "reconfigure rejected for replacement images"
    xdo key Down
    wait_for 30 grown_h || fail "drill failed after replacing images"
    xdo key Up
    wait_for 30 shrunken_h || fail "undrill failed after replacing images"
    wmwrite SwitchPanelImages '()'
    log_mark
    msg -q reconfigure || fail "reconfigure rejected for SwitchPanelImages"
    sleep 0.35
    if panel_geom; then
        ok "open panel survived the SwitchPanelImages reload"
    else
        fail "panel vanished on the SwitchPanelImages reload"
    fi
    msg -q action FocusNext >>"$LOG" 2>&1
    check "nested cycling is rejected" [ "$?" -eq 2 ]
    msg -q workspace next >>"$LOG" 2>&1
    check "workspace mutation is rejected during cycling" [ "$?" -eq 2 ]
    check "IPC queries remain responsive during cycling" ipc_ready
    xdo key Down
    if wait_for 30 grown_h; then
        ok "drill still works after the image reload"
    else
        fail "drill failed after the image reload"
    fi
    xdo key Up
    wait_for 30 shrunken_h || fail "undrill failed after the image reload"
    xdo keyup super; sleep 0.3
else
    xdo keyup super
    fail "no panel for the SwitchPanelImages reload"
fi
# the next invocation adopts the reloaded panel-less setting
xdo windowactivate "$PT7"; sleep 0.2
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.4
check "next invocation adopts panel-less mode" sh -c \
    "[ -z \"\$(DISPLAY=$DPY xdotool search --onlyvisible --name '^Window Maker Switch Panel\$' 2>/dev/null)\" ]"
xdo keyup alt; sleep 0.3
# restoring the images brings the panel back on the next invocation
wmwrite SwitchPanelImages '(swtile.png, swback.png, 30, 40)'
msg -q reconfigure || true
sleep 0.3
with_panel "$PT7" true || fail "panel did not return after restoring SwitchPanelImages"
check "wmaker alive after the image reload" ipc_ready

# --- 6. monitor targeting --------------------------------------------------
# Split the framebuffer into two named monitors. The right monitor is
# created against the "none" output but is still a named monitor, so it
# covers named targeting only, never unnamed #N. Expected geometry is
# derived from the tile the configured icon size yields and the pad
# measured in section 2, never from hard-coded dimensions.
note "monitor: named targeting and fallback"
ICON=$(LD_LIBRARY_PATH="$WMLIBS" DISPLAY=$DPY \
    "$ROOT/util/.libs/wdread" WindowMaker SwitchPanelIconSize 2>/dev/null)
case "$ICON" in
    ''|*[!0-9]*) ICON=64 ;;
esac
TILE=$(( (ICON * 12 + 5) / 10 ))
REF_W= REF_H=
if with_panel "$PT7" snap_geom; then
    REF_W=$SNAP_W; REF_H=$SNAP_H
fi
FIT_N=$(( (512 - 2 * PAD_X) / TILE ))
[ "$FIT_N" -ge 1 ] || FIT_N=1
ONE_W=$(( TILE + 2 * PAD_X ))
monitor_index() {
    xr --listactivemonitors 2>/dev/null | awk -v want="$1" '
        /^[ \t]*[0-9]+:/ {
            idx = $1
            sub(/:$/, "", idx)
            name = $2
            sub(/:$/, "", name)
            sub(/^[*+]+/, "", name)
            if (name == want) { print idx; exit }
        }'
}
mode_roundtrip() {
    xr --output default --mode 800x600 >>"$LOG" 2>&1
    xr --output default --mode ${W}x${H} >>"$LOG" 2>&1
    sleep 0.8
}
xr --setmonitor left 512/135x768/203+0+0 default >>"$LOG" 2>&1 \
    || skip "xrandr --setmonitor unsupported"
xr --setmonitor right 512/135x768/203+512+0 none >>"$LOG" 2>&1 \
    || skip "xrandr --setmonitor unsupported"
# round-trip the mode so the server publishes the monitors
mode_roundtrip

if xr --listactivemonitors 2>/dev/null | grep -q 'right'; then
    # pointer on the left head, panel must target the right head and
    # fit its 512 px width
    place topleft '"right"'
    if with_panel_at "$PT7" 256 384 snap_geom; then
        ex=$(( 512 + PAD_X ))
        ew=$(( FIT_N * TILE + 2 * PAD_X ))
        if [ "$SNAP_X" = "$ex" ] && [ "$SNAP_Y" = "$PAD_Y" ] \
           && [ "$SNAP_W" = "$ew" ] && [ "$SNAP_H" = "$REF_H" ]; then
            ok "named monitor targeted: ${SNAP_W}x${SNAP_H} at ($ex,$PAD_Y)"
        else
            fail "named monitor geometry wrong: ($SNAP_X,$SNAP_Y) ${SNAP_W}x${SNAP_H}, expected ($ex,$PAD_Y) ${ew}x${REF_H}"
        fi
    else
        fail "no panel geometry for named monitor"
    fi

    # unknown name falls back to the pointer head, and the value stays
    place topleft '"no-such-output"'
    if with_panel_at "$PT7" 256 384 snap_geom; then
        if [ "$SNAP_X" -lt 512 ]; then
            ok "unknown name fell back to the pointer head (x=$SNAP_X)"
        else
            fail "unknown name did not fall back (x=$SNAP_X)"
        fi
    else
        fail "no panel geometry for unknown monitor"
    fi
    check "unresolved value kept in the domain" sh -c \
        "LD_LIBRARY_PATH=$WMLIBS DISPLAY=$DPY $ROOT/util/.libs/wdread WindowMaker SwitchPanelMonitor | grep -q no-such-output"

    # case-mismatched name is not a match
    place topleft '"RIGHT"'
    if with_panel_at "$PT7" 256 384 snap_geom; then
        if [ "$SNAP_X" -lt 512 ]; then
            ok "case-mismatched name fell back to the pointer head"
        else
            fail "case-mismatched name matched a monitor"
        fi
    else
        fail "no panel geometry for case-mismatched monitor"
    fi

    # "#N" is refused while heads carry names
    place topleft '"#1"'
    if with_panel_at "$PT7" 256 384 snap_geom; then
        if [ "$SNAP_X" -lt 512 ]; then
            ok "#1 rejected for named heads, pointer head used"
        else
            fail "#1 resolved on named heads (x=$SNAP_X)"
        fi
    else
        fail "no panel geometry for #1 on named heads"
    fi

    # the configured name follows its monitor across a removal and a
    # recreation that gives it a different monitor index
    place topleft '"left"'
    if with_panel_at "$PT7" 768 384 snap_geom; then
        if [ "$SNAP_X" -lt 512 ]; then
            ok "left monitor targeted with the pointer on the right head"
        else
            fail "left monitor ignored: x=$SNAP_X"
        fi
    else
        fail "no panel geometry for the left monitor"
    fi
    idx_before=$(monitor_index left)
    xr --delmonitor left >>"$LOG" 2>&1
    mode_roundtrip
    if with_panel_at "$PT7" 768 384 snap_geom; then
        check "removed monitor falls back to pointer" [ "$SNAP_X" -ge 512 ]
    else
        fail "no panel after monitor removal"
    fi
    xr --setmonitor left 512/135x768/203+0+0 default >>"$LOG" 2>&1
    mode_roundtrip
    idx_after=$(monitor_index left)
    if with_panel_at "$PT7" 768 384 snap_geom; then
        if [ "$SNAP_X" -lt 512 ]; then
            ok "recreated left monitor resolved at index ${idx_after:-?}"
        else
            fail "recreated left monitor did not resolve (x=$SNAP_X)"
        fi
    else
        fail "no panel geometry for the recreated monitor"
    fi
    check "wdread kept the name across recreation" sh -c \
        "LD_LIBRARY_PATH=$WMLIBS DISPLAY=$DPY $ROOT/util/.libs/wdread WindowMaker SwitchPanelMonitor | grep -q left"
    if [ -n "$idx_before" ] && [ -n "$idx_after" ] \
       && [ "$idx_before" != "$idx_after" ]; then
        ok "name followed the monitor to its new index ($idx_before -> $idx_after)"
    else
        skip "monitor index did not change ($idx_before -> $idx_after), reorder not covered"
    fi

    # a target that cannot fit one tile anchors at the head origin and
    # still shows the forced one-icon minimum
    xr --setmonitor tiny 64/17x768/203+960+0 none >>"$LOG" 2>&1
    xr --delmonitor right >>"$LOG" 2>&1
    mode_roundtrip
    if xr --listactivemonitors 2>/dev/null | grep -q 'tiny'; then
        place topleft '"tiny"'
        if with_panel_at "$PT7" 256 384 snap_geom; then
            if [ "$SNAP_X" = 960 ] && [ "$SNAP_Y" = "$PAD_Y" ] \
               && [ "$SNAP_W" = "$ONE_W" ] && [ "$SNAP_H" = "$REF_H" ]; then
                ok "tiny head keeps one icon (${ONE_W}x${REF_H}) at (960,$PAD_Y)"
            else
                fail "tiny head geometry wrong: ($SNAP_X,$SNAP_Y) ${SNAP_W}x${SNAP_H}, expected (960,$PAD_Y) ${ONE_W}x${REF_H}"
            fi
        else
            fail "no panel geometry on the tiny head"
        fi
    else
        skip "xrandr --delmonitor did not publish the tiny head"
    fi
    xr --delmonitor tiny >>"$LOG" 2>&1

    # the reserved keyword stays dynamic against a monitor of the same
    # name, and the name: escape selects that monitor literally
    xr --setmonitor Cursor 512/135x768/203+512+0 none >>"$LOG" 2>&1
    mode_roundtrip
    if xr --listactivemonitors 2>/dev/null | grep -q 'Cursor'; then
        place topleft Cursor
        if with_panel_at "$PT7" 256 384 snap_geom; then
            if [ "$SNAP_X" -lt 512 ]; then
                ok "Cursor keyword wins over a monitor named Cursor"
            else
                fail "Cursor keyword resolved a monitor (x=$SNAP_X)"
            fi
        else
            fail "no panel for the Cursor keyword"
        fi
        place topleft '"name:Cursor"'
        if with_panel_at "$PT7" 256 384 snap_geom; then
            if [ "$SNAP_X" -ge 512 ]; then
                ok "name:Cursor selects the monitor literally (x=$SNAP_X)"
            else
                fail "name:Cursor did not select the monitor (x=$SNAP_X)"
            fi
        else
            fail "no panel for name:Cursor"
        fi
        xr --delmonitor Cursor >>"$LOG" 2>&1
        mode_roundtrip
    else
        skip "xrandr did not publish a monitor named Cursor"
    fi

    xr --delmonitor left >>"$LOG" 2>&1
    mode_roundtrip
else
    skip "xrandr --setmonitor monitors were not published"
fi

# --- 7. panel-less cycling preserved --------------------------------------
note "SwitchPanelImages = (): panel-less flat, group and class cycles"
wmwrite SwitchPanelImages '()'
wmwrite SwitchPanelPlacement center
wmwrite SwitchPanelMonitor '""'
msg -q reconfigure || fail "reconfigure rejected for panel-less mode"
sleep 0.3
before=$(DISPLAY=$DPY xprop -root _NET_ACTIVE_WINDOW 2>/dev/null \
    | sed -n 's/.*window id #//p' | tr -d ' ')
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.3
check "panel-less mode shows no panel" sh -c \
    "[ -z \"\$(DISPLAY=$DPY xdotool search --onlyvisible --name '^Window Maker Switch Panel\$' 2>/dev/null)\" ]"
xdo keyup alt; sleep 0.3
after=$(DISPLAY=$DPY xprop -root _NET_ACTIVE_WINDOW 2>/dev/null \
    | sed -n 's/.*window id #//p' | tr -d ' ')
check "panel-less Alt+Tab changes focus" [ "$before" != "$after" ]
xdo keydown super; sleep 0.1
xdo key Tab; sleep 0.3
check "panel-less group cycle shows no panel" sh -c \
    "[ -z \"\$(DISPLAY=$DPY xdotool search --onlyvisible --name '^Window Maker Switch Panel\$' 2>/dev/null)\" ]"
xdo keyup super; sleep 0.3
check "panel-less group cycle alive" ipc_ready
xdo keydown super; sleep 0.1
xdo key grave; sleep 0.3
check "panel-less class cycle shows no panel" sh -c \
    "[ -z \"\$(DISPLAY=$DPY xdotool search --onlyvisible --name '^Window Maker Switch Panel\$' 2>/dev/null)\" ]"
xdo keyup super; sleep 0.3
check "panel-less class cycle alive" ipc_ready
wmwrite SwitchPanelImages '(swtile.png, swback.png, 30, 40)'
msg -q reconfigure || true

# --- 8. scaled UI fixture (Xft.dpi 192) ------------------------------------
# Restart wmaker on the same Xephyr with a 192 dpi RESOURCE_MANAGER so
# the UI scale is 2. Anchors are computed from the size and pad
# measured at this scale, and the fitted panel must stay inside the
# screen: a fit that compared logical sizes would select too many
# icons and produce an oversized panel here.
note "scaled UI: Xft.dpi 192 anchors and device-pixel fitting"
msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"
printf 'Xft.dpi: 192\n' | DISPLAY=$DPY xrdb -merge >>"$LOG" 2>&1
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    ASAN_OPTIONS="$(drive_asan_options)" \
    DISPLAY=$DPY \
    "$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
wait_for 120 wm_ready || fatal "wmaker did not return for the scaled fixture"
wait_for 80 ipc_ready || fatal "wmaker IPC did not return for the scaled fixture"
sleep 0.5

wmwrite SwitchPanelImages '(swtile.png, swback.png, 30, 40)'
wmwrite SwitchPanelIconSize 64
wmwrite SwitchPanelMonitor '""'
place topleft '""'
S2_PAD_X= S2_PAD_Y=
scale_topleft() {
    panel_geom || { fail "no panel for the scaled topleft anchor"; return; }
    set -- $PANEL_GEOM
    S2_PAD_X=$1; S2_PAD_Y=$2
    if [ "$1" -ge 0 ] && [ "$1" -le 64 ] && [ "$2" -ge 0 ] && [ "$2" -le 64 ]; then
        ok "scaled topleft pad ($S2_PAD_X,$S2_PAD_Y)"
    else
        fail "scaled topleft pad implausible ($1,$2)"
    fi
}
with_panel "$PT7" scale_topleft || fail "no panel for the scaled topleft anchor"

place center '""'
scale_center() {
    panel_geom || { fail "no panel in the scaled fixture"; return; }
    set -- $PANEL_GEOM
    spx=$1; spy=$2; spw=$3; sph=$4
    ex=$(( (W - spw) / 2 ))
    ey=$(( (H - sph) / 2 ))
    if [ "$spx" = "$ex" ] && [ "$spy" = "$ey" ] \
       && [ "$spw" -le "$W" ] && [ "$sph" -le "$H" ]; then
        ok "scaled center contained at ($spx,$spy) for ${spw}x${sph}"
    else
        fail "scaled center wrong: ($spx,$spy) ${spw}x${sph}, expected ($ex,$ey) within ${W}x${H}"
    fi
    # the fit in device pixels selects fewer icons than the unit-scale
    # fixture did. A logical fit would keep the old count and overflow
    n2=$(( (spw - 2 * S2_PAD_X) / (TILE * 2) ))
    n1=$(( (REF_W - 2 * PAD_X) / TILE ))
    if [ "$n2" -ge 1 ] && [ "$n2" -lt "$n1" ]; then
        ok "scaled fit shows $n2 of $n1 icons in device pixels"
    else
        fail "scaled fit count wrong: $n2 icons, unit scale had $n1"
    fi
}
with_panel "$PT7" scale_center || fail "no panel at 192 dpi"

# the opt-in fit path (non-center placement) in device pixels
place bottomright '""'
scale_bottomright() {
    panel_geom || { fail "no panel for the scaled bottomright anchor"; return; }
    set -- $PANEL_GEOM
    ex=$(( W - $3 - S2_PAD_X ))
    ey=$(( H - $4 - S2_PAD_Y ))
    if [ "$1" = "$ex" ] && [ "$2" = "$ey" ] && [ "$3" -le "$W" ] && [ "$4" -le "$H" ]; then
        ok "scaled bottomright mirrors the measured pad at ($1,$2)"
    else
        fail "scaled bottomright expected ($ex,$ey) got ($1,$2) for ${3}x${4}"
    fi
}
with_panel "$PT7" scale_bottomright || fail "no panel for the scaled bottomright anchor"
check "wmaker alive after the scaled fixture" ipc_ready

# --- teardown --------------------------------------------------------------
note "switchpanel placement drive summary"
printf 'PASS: %d, FAIL: %d, SKIP: %d\n' "$PASSED" "$FAILED" "$SKIPPED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
