#!/bin/sh
# Verify the window tab-group core: attach via GroupTabAttachKey hides
# the joining window and publishes _NET_WM_STATE_HIDDEN, focus moves to
# the visible member, GroupTabNextKey/GroupTabPrevKey cycle the visible
# member in place, GroupTabDetachKey reveals the leaving window at a
# small offset, closing the active member of a two-window group reveals
# the survivor at the group position without remapping the destroyed
# client, and Shift+click on the close button sends WM_DELETE_WINDOW to
# every member. Peer-state propagation is asserted for move, workspace,
# iconify, shade and maximize. Titlebar segment click, wheel cycle and
# Ctrl+click close are driven against computed segment centres. Button2
# drag covers attach, tear-off and reorder. A wmakermsg restart while
# mapped and again while iconified asserts the saved active member and
# tab order survive a warm re-exec. Runs wmaker under Xephyr on an
# isolated display, XDG_RUNTIME_DIR and WMAKER_USER_ROOT, tracks every
# PID it spawns and kills only those.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${TABGROUP_DRIVE_DISPLAY:-:93}
W=1280
H=800
PIDFILE=$(mktemp /tmp/tabgroup-drive.pids.XXXXXX)
WMHOME=$(mktemp -d /tmp/tabgroup-drive-wmhome.XXXXXX)
RUNTIME=$(mktemp -d /tmp/tabgroup-drive-run.XXXXXX)
LOG=/tmp/tabgroup-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
SOCKET=
: > "$PIDFILE"
: > "$LOG"

cleanup() {
    while read -r p; do
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done < "$PIDFILE"
    rm -f "$PIDFILE"
    rm -rf "$WMHOME" "$RUNTIME"
}
trap cleanup EXIT

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
ok()   { printf 'ok: %s\n' "$*" | tee -a "$LOG"; }
fail() { printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
skip() { printf 'SKIP: %s\n' "$*" | tee -a "$LOG"; exit 77; }
xdo()  { DISPLAY=$DPY xdotool "$@"; }

for tool in Xvfb Xephyr xdotool xterm xprop xwininfo wmctrl; do
    command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done
[ -x "$WMAKERMSG" ] || skip "util/.libs/wmakermsg not built"

wait_for() {
    n=$1; shift
    while [ "$n" -gt 0 ]; do
        "$@" && return 0
        sleep 0.1
        n=$((n - 1))
    done
    return 1
}
pid_gone() { ! kill -0 "$1" 2>/dev/null; }
all_gone() { for p in "$@"; do kill -0 "$p" 2>/dev/null && return 1; done; return 0; }

# Inter-section teardown: a polite wmctrl -c on a hidden tab-group member
# is not guaranteed to reach the client, so the per-PID wait_for 40 that
# followed it could spin its full 4s for nothing. The next section uses
# distinct term names so we only need the PIDs gone, kill them directly
# and wait once for the lot.
reap_terms() {
    kill "$@" 2>/dev/null
    wait_for 20 all_gone "$@"
}

is_viewable() {
    DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null | grep -q 'Map State: IsViewable'
}
not_viewable() { ! is_viewable "$1"; }
has_state() {
    DISPLAY=$DPY xprop -id "$1" _NET_WM_STATE 2>/dev/null | grep -q "$2"
}
no_state() { ! has_state "$1" "$2"; }
abs_x() {
    DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null \
        | sed -n 's/^ *Absolute upper-left X: *\([0-9-]*\).*/\1/p'
}
abs_y() {
    DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null \
        | sed -n 's/^ *Absolute upper-left Y: *\([0-9-]*\).*/\1/p'
}
client_w() {
    DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null \
        | sed -n 's/^ *Width: *\([0-9]*\).*/\1/p'
}

frame_of() {
    DISPLAY=$DPY xwininfo -tree -id "$1" 2>/dev/null \
        | awk '/Parent window id:/ {print $4; exit}'
}
frame_top() {
    # walk parents until the next hop is root, returning the
    # child-of-root frame core, never emits 0x0 which would hang xwininfo
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
frame_geom() {
    # frame_geom CLIENT -> sets FX FY FW FB THEIGHT for the frame core,
    # THEIGHT is derived from client_y - frame_inside_y so it does not
    # depend on the configured font
    fgclient=$1
    fgframe=$(frame_top "$fgclient") || return 1
    set -- $(DISPLAY=$DPY xwininfo -id "$fgframe" 2>/dev/null \
                 | awk '/Absolute upper-left X:/ {x=$4}
                        /Absolute upper-left Y:/ {y=$4}
                        /Width:/ && !w {w=$2}
                        /Border width:/ {b=$3}
                        END {print x, y, w, b}')
    FX=$1; FY=$2; FW=$3; FB=$4
    fgcy=$(abs_y "$fgclient")
    THEIGHT=$((fgcy - FY - FB))
    [ "$THEIGHT" -gt 0 ] || return 1
}
seg_x() {
    # seg_x I N -> root x of the centre of titlebar segment I out of N,
    # NewStyle=new puts one bsize=THEIGHT button on each side of the
    # titlebar core and lofs=rofs=6 collapses to a [0, core_w] segment
    # span, so the span runs from FX+FB+THEIGHT for FW-2*THEIGHT pixels
    sxi=$1; sxn=$2
    sxspan=$((FW - 2 * THEIGHT))
    printf '%s\n' $((FX + FB + THEIGHT + (2 * sxi + 1) * sxspan / (2 * sxn)))
}
seg_y() {
    printf '%s\n' $((FY + FB + THEIGHT / 2))
}
count_miniwindows() {
    # with dock, clip and app icons disabled the only viewable
    # IconSize x IconSize toplevel children of root are miniwindows
    cmn=0
    for cmw in $(DISPLAY=$DPY xwininfo -root -children 2>/dev/null \
                     | awk '/^ *0x/ { g=$(NF-1)
                                       if (g ~ /^64x64\+/) print $1 }'); do
        DISPLAY=$DPY xwininfo -id "$cmw" 2>/dev/null \
            | grep -q 'Map State: IsViewable' && cmn=$((cmn + 1))
    done
    printf '%s\n' "$cmn"
}
miniwindows_eq() { [ "$(count_miniwindows)" = "$1" ]; }

msg() {
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$WMAKERMSG" -s "$SOCKET" "$@"
}
ipc_ready() { msg -q get_version >/dev/null 2>&1; }

unset WMAKER_IPC_SOCKET
export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
# pin the titlebar layout so segment geometry is computable from the
# frame rectangle alone, NewStyle=new makes bsize=theight and one button
# on each side, dock/clip/app-icons are disabled so the restart block can
# count miniwindows by enumerating IconSize root children
cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  GroupTabAttachKey = "Control+Shift+A";
  GroupTabDetachKey = "Control+Shift+D";
  GroupTabNextKey = "Control+Shift+N";
  GroupTabPrevKey = "Control+Shift+P";
  NewStyle = new;
  TitlebarLeft = (Miniaturize);
  TitlebarRight = (Close);
  TabsInTitlebar = YES;
  IconSize = 64;
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
}
EOF
cat >"$WMHOME/Defaults/WMWindowAttributes" <<EOF
{
  "*" = { NoAppIcon = YES; };
}
EOF

# --- Xephyr ----------------------------------------------------------------
# Xephyr is a nested server: on the session display its host pointer
# tracks the live mouse, and a host motion landing between the XTEST
# warp and the XTEST press makes the press fire wherever the host
# pointer happened to be. Run it on a private headless parent instead,
# whose virtual pointer only moves when Xephyr itself mirrors a warp.
PARENT_DISPLAY=":$(( ${DPY#:} + 1 ))"
Xvfb "$PARENT_DISPLAY" -screen 0 2400x1600x24 -ac >>"$LOG" 2>&1 &
XVFB_PARENT_PID=$!
echo "$XVFB_PARENT_PID" >> "$PIDFILE"
xvfb_parent_ready() {
    kill -0 "$XVFB_PARENT_PID" 2>/dev/null || return 1
    DISPLAY=$PARENT_DISPLAY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_parent_ready || fail "parent Xvfb failed to start"

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use, set TABGROUP_DRIVE_DISPLAY" >&2
    exit 1
fi
note "start Xephyr $DPY on parent $PARENT_DISPLAY"
Xephyr "$DPY" -display "$PARENT_DISPLAY" -screen ${W}x${H} -ac >>"$LOG" 2>&1 &
XEPHYR_PID=$!
echo "$XEPHYR_PID" >> "$PIDFILE"
xephyr_ready() {
    kill -0 "$XEPHYR_PID" 2>/dev/null || fail "Xephyr exited early"
    DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xephyr_ready || fail "Xephyr failed to start"

# --- wmaker ----------------------------------------------------------------
note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
ASAN_OPTIONS="abort_on_error=0:halt_on_error=0:detect_leaks=0" \
DISPLAY=$DPY \
"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
wm_ready() {
    DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null \
        | grep -q 'window id'
}
wait_for 120 wm_ready || fail "wmaker did not come up"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
case "$SOCKET" in
    "$RUNTIME"/*) : ;;
    *) fail "IPC socket $SOCKET escaped private XDG_RUNTIME_DIR" ;;
esac
wait_for 80 ipc_ready || fail "wmaker IPC did not come up at $SOCKET"
sleep 0.5

# drag_btn BUTTON SX SY TX TY -> press at (SX,SY), nudge past
# MOVE_THRESHOLD so the modal drag loop sets started=True, then move to
# the drop point and release. Each step is a separate xdotool call with
# --sync so wmaker's grabbed event loop sees motion before the release
drag_btn() {
    dbbtn=$1; dbsx=$2; dbsy=$3; dbtx=$4; dbty=$5
    xdo mousemove --sync "$dbsx" "$dbsy"
    xdo mousedown "$dbbtn"; sleep 0.15
    xdo mousemove --sync $((dbsx + 12)) $((dbsy + 4)); sleep 0.1
    xdo mousemove --sync "$dbtx" "$dbty"; sleep 0.2
    xdo mouseup "$dbbtn"; sleep 0.2
}

# --- spawn three xterms ----------------------------------------------------
spawn_term() {
    DISPLAY=$DPY xterm -title "$1" -name "$1" -geometry "$2" \
        -e 'sleep 600' >>"$LOG" 2>&1 &
    p=$!
    echo "$p" >> "$PIDFILE"
    eval "${1}PID=$p"
}
find_client() {
    DISPLAY=$DPY xdotool search --name "^$1\$" 2>/dev/null | tail -n 1
}

spawn_term termA 80x24+50+50
spawn_term termB 80x24+400+200
spawn_term termC 80x24+200+350
for t in termA termB termC; do
    wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^$t\$' 2>/dev/null | grep -q ." \
        || fail "$t did not map"
done
TA=$(find_client termA)
TB=$(find_client termB)
TC=$(find_client termC)
note "termA=$TA termB=$TB termC=$TC"

# wGroupFindAttachTarget() walks the focus MRU list, so make the order
# deterministic regardless of xterm spawn races: A then B then C, leaving
# termB as termC->prev so the keybind attach picks termB as the target
xdo windowactivate "$TA"; sleep 0.2
xdo windowactivate "$TB"; sleep 0.2

# --- 1. attach termC to termB's group -------------------------------------
note "attach: focus termC, GroupTabAttachKey"
xdo windowactivate "$TC"; sleep 0.3
xdo key --clearmodifiers ctrl+shift+a
wait_for 30 not_viewable "$TC" || fail "termC was not hidden after attach"
is_viewable "$TB" || fail "termB is not visible after attach"
ok "termC hidden, termB visible"

wait_for 20 has_state "$TC" _NET_WM_STATE_HIDDEN \
    || fail "termC missing _NET_WM_STATE_HIDDEN"
has_state "$TC" _NET_WM_STATE_SKIP_PAGER \
    && fail "termC has _NET_WM_STATE_SKIP_PAGER, only HIDDEN expected"
ok "termC has HIDDEN, no SKIP_PAGER"

wait_for 20 no_state "$TC" _NET_WM_STATE_FOCUSED \
    || fail "termC still FOCUSED after being hidden"
wait_for 20 has_state "$TB" _NET_WM_STATE_FOCUSED \
    || fail "termB did not receive focus after attach"
ok "focus transferred to active member"

# --- 2. attach termA into the same group ----------------------------------
note "attach: focus termA, GroupTabAttachKey"
xdo windowactivate "$TA"; sleep 0.3
xdo key --clearmodifiers ctrl+shift+a
wait_for 30 not_viewable "$TA" || fail "termA was not hidden after attach"
is_viewable "$TB" || fail "termB is not visible after second attach"
not_viewable "$TC" || fail "termC became visible after second attach"
ok "three-member group, only termB visible"

# --- 3. cycle next/prev ---------------------------------------------------
note "cycle: GroupTabNextKey twice, then GroupTabPrevKey"
xdo windowactivate "$TB"; sleep 0.3
xdo key --clearmodifiers ctrl+shift+n
wait_for 30 is_viewable "$TC" || fail "next did not reveal termC"
not_viewable "$TB" || fail "termB still visible after next"
ok "next: termB -> termC"

xdo key --clearmodifiers ctrl+shift+n
wait_for 30 is_viewable "$TA" || fail "next did not reveal termA"
not_viewable "$TC" || fail "termC still visible after second next"
ok "next: termC -> termA"

xdo key --clearmodifiers ctrl+shift+p
wait_for 30 is_viewable "$TC" || fail "prev did not reveal termC"
not_viewable "$TA" || fail "termA still visible after prev"
ok "prev: termA -> termC"

# --- 4. detach termC ------------------------------------------------------
note "detach: GroupTabDetachKey on termC"
GX=$(abs_x "$TC"); GY=$(abs_y "$TC")
xdo key --clearmodifiers ctrl+shift+d
wait_for 30 is_viewable "$TC" || fail "detached termC is not visible"
wait_for 30 is_viewable "$TA" || fail "next group member not revealed after detach"
not_viewable "$TB" || fail "termB became visible after detach"
DX=$(abs_x "$TC"); DY=$(abs_y "$TC")
[ "$DX" -gt "$GX" ] && [ "$DY" -gt "$GY" ] \
    || fail "detached termC was not offset from group ($GX,$GY -> $DX,$DY)"
ok "termC detached with offset ($GX,$GY -> $DX,$DY)"
wait_for 20 no_state "$TC" _NET_WM_STATE_HIDDEN \
    || fail "detached termC still has HIDDEN"
ok "termC HIDDEN cleared"

# --- 5. close active member reveals survivor ------------------------------
note "close: WM_DELETE_WINDOW to active member termA"
SX=$(abs_x "$TB")
xdo windowactivate "$TA"; sleep 0.3
DISPLAY=$DPY wmctrl -i -c "$TA"
wait_for 40 pid_gone "$termAPID" || fail "termA did not exit"
wait_for 30 is_viewable "$TB" || fail "survivor termB was not revealed"
[ "$(abs_x "$TB")" = "$SX" ] \
    || fail "survivor termB jumped from x=$SX to x=$(abs_x "$TB")"
wait_for 20 no_state "$TB" _NET_WM_STATE_HIDDEN \
    || fail "survivor termB still has HIDDEN"
kill -0 "$WMAKER_PID" 2>/dev/null || fail "wmaker died handling member close"
ok "survivor revealed at group position, group dissolved"

# --- 6. Shift+close closes whole group ------------------------------------
note "shift+close: re-group termC into termB, click close button"
xdo windowactivate "$TC"; sleep 0.3
xdo key --clearmodifiers ctrl+shift+a
wait_for 30 not_viewable "$TC" || fail "termC was not hidden after re-attach"
xdo windowactivate "$TB"; sleep 0.3
BX=$(abs_x "$TB"); BY=$(abs_y "$TB"); BW=$(client_w "$TB")
# close button sits at the right edge of the titlebar, roughly half a
# titlebar height in and above the client origin
CX=$((BX + BW - 10)); CY=$((BY - 10))
xdo mousemove "$CX" "$CY"; sleep 0.2
xdo keydown shift click 1 keyup shift
wait_for 40 pid_gone "$termBPID" || fail "termB did not exit on Shift+close"
wait_for 40 pid_gone "$termCPID" || fail "termC did not exit on Shift+close"
kill -0 "$WMAKER_PID" 2>/dev/null || fail "wmaker died on Shift+close"
ok "Shift+close closed every group member"

# --- 7. state propagation: move, workspace, iconify -----------------------
note "state: re-group two terms"
spawn_term termD 80x24+100+100
spawn_term termE 80x24+450+250
for t in termD termE; do
    wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^$t\$' 2>/dev/null | grep -q ." \
        || fail "$t did not map"
done
TD=$(find_client termD)
TE=$(find_client termE)
sleep 0.3
xdo windowactivate "$TE"; sleep 0.3
xdo key --clearmodifiers ctrl+shift+a
wait_for 30 not_viewable "$TE" || fail "termE was not hidden after attach"

note "state: move active, switch tab"
DISPLAY=$DPY wmctrl -i -r "$TD" -e 0,600,400,-1,-1
wait_for 30 sh -c "[ \"\$(DISPLAY=$DPY xwininfo -id $TD 2>/dev/null \
    | sed -n 's/^ *Absolute upper-left X: *\\([0-9-]*\\).*/\\1/p')\" -ge 590 ]" \
    || fail "termD did not move"
NX=$(abs_x "$TD"); NY=$(abs_y "$TD")
xdo windowactivate "$TD"; sleep 0.2
xdo key --clearmodifiers ctrl+shift+n
wait_for 30 is_viewable "$TE" || fail "next did not reveal termE"
[ "$(abs_x "$TE")" = "$NX" ] && [ "$(abs_y "$TE")" = "$NY" ] \
    || fail "termE at $(abs_x "$TE"),$(abs_y "$TE"), expected $NX,$NY"
ok "peer position follows active member ($NX,$NY)"

note "state: send group to workspace 2 and back"
DISPLAY=$DPY wmctrl -n 2
wait_for 20 sh -c "DISPLAY=$DPY wmctrl -d 2>/dev/null | wc -l | grep -q '^2\$'" \
    || fail "could not create second workspace"
DISPLAY=$DPY wmctrl -i -r "$TE" -t 1
wait_for 30 not_viewable "$TE" || fail "termE still visible after workspace move"
DISPLAY=$DPY wmctrl -i -r "$TE" -t 0
wait_for 30 is_viewable "$TE" || fail "termE not visible after returning to ws 0"
not_viewable "$TD" \
    || fail "inactive termD became visible after workspace round-trip"
ok "workspace round-trip keeps inactive member hidden"

note "state: iconify group, deiconify"
xdo windowactivate "$TE"; sleep 0.2
DISPLAY=$DPY xdotool windowminimize "$TE"
wait_for 30 not_viewable "$TE" || fail "termE still visible after iconify"
wait_for 20 has_state "$TE" _NET_WM_STATE_HIDDEN \
    || fail "termE missing HIDDEN after iconify"
wait_for 20 has_state "$TD" _NET_WM_STATE_HIDDEN \
    || fail "inactive termD missing HIDDEN after group iconify"
xdo windowactivate "$TE"
wait_for 30 is_viewable "$TE" || fail "termE not visible after deiconify"
not_viewable "$TD" \
    || fail "inactive termD became visible after deiconify"
ok "iconify+deiconify keeps one member visible"

note "state: shade active, switch tab"
xdo windowactivate "$TE"; sleep 0.2
DISPLAY=$DPY wmctrl -i -r "$TE" -b add,shaded
wait_for 30 has_state "$TE" _NET_WM_STATE_SHADED \
    || fail "termE missing SHADED after shade"
wait_for 30 has_state "$TD" _NET_WM_STATE_SHADED \
    || fail "inactive termD missing SHADED after group shade"
xdo key --clearmodifiers ctrl+shift+n
wait_for 30 not_viewable "$TE" || fail "termE still visible after shaded switch"
has_state "$TD" _NET_WM_STATE_SHADED \
    || fail "termD lost SHADED on activation"
not_viewable "$TD" \
    || fail "termD client mapped while group shaded"
DISPLAY=$DPY wmctrl -i -r "$TD" -b remove,shaded
wait_for 30 is_viewable "$TD" || fail "termD not visible after unshade"
wait_for 20 no_state "$TE" _NET_WM_STATE_SHADED \
    || fail "inactive termE still SHADED after group unshade"
ok "shade flag mirrors across peers and survives switch"

note "state: maximize active, switch tab"
PREW=$(client_w "$TD")
xdo windowactivate "$TD"; sleep 0.2
DISPLAY=$DPY wmctrl -i -r "$TD" -b add,maximized_vert,maximized_horz
wait_for 30 has_state "$TD" _NET_WM_STATE_MAXIMIZED_HORZ \
    || fail "termD missing MAXIMIZED_HORZ after maximize"
wait_for 30 has_state "$TE" _NET_WM_STATE_MAXIMIZED_HORZ \
    || fail "inactive termE missing MAXIMIZED_HORZ after group maximize"
xdo key --clearmodifiers ctrl+shift+n
wait_for 30 is_viewable "$TE" || fail "next did not reveal termE while maximized"
has_state "$TE" _NET_WM_STATE_MAXIMIZED_VERT \
    || fail "termE lost MAXIMIZED_VERT on activation"
MW=$(client_w "$TE")
[ "$MW" -gt "$PREW" ] \
    || fail "termE width $MW not larger than pre-maximize $PREW"
DISPLAY=$DPY wmctrl -i -r "$TE" -b remove,maximized_vert,maximized_horz
wait_for 30 no_state "$TE" _NET_WM_STATE_MAXIMIZED_HORZ \
    || fail "termE still MAXIMIZED_HORZ after unmaximize"
wait_for 20 no_state "$TD" _NET_WM_STATE_MAXIMIZED_HORZ \
    || fail "inactive termD still MAXIMIZED_HORZ after group unmaximize"
ok "maximize flags and size mirror across peers"

reap_terms "$termDPID" "$termEPID"
kill -0 "$WMAKER_PID" 2>/dev/null || fail "wmaker died during state propagation"

# --- 8. segmented titlebar: click, wheel, ctrl+click ----------------------
note "segments: build a three-member group"
spawn_term termF 80x24+100+100
spawn_term termG 80x24+450+250
spawn_term termH 80x24+250+400
for t in termF termG termH; do
    wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^$t\$' 2>/dev/null | grep -q ." \
        || fail "$t did not map"
done
TF=$(find_client termF); TG=$(find_client termG); TH=$(find_client termH)
xdo windowactivate "$TF"; sleep 0.2
xdo windowactivate "$TG"; sleep 0.2
xdo key --clearmodifiers ctrl+shift+a
wait_for 30 not_viewable "$TG" || fail "termG was not hidden after attach"
xdo windowactivate "$TH"; sleep 0.2
xdo key --clearmodifiers ctrl+shift+a
wait_for 30 not_viewable "$TH" || fail "termH was not hidden after attach"
is_viewable "$TF" || fail "termF not visible as active member"
# member order is [F, G, H], F active at index 0

note "segments: click inactive segment activates it"
frame_geom "$TF" || fail "could not resolve frame geometry for termF"
SY=$(seg_y)
xdo mousemove --sync "$(seg_x 2 3)" "$SY"; sleep 0.1
xdo click 1
wait_for 30 is_viewable "$TH" || fail "click on segment 2 did not reveal termH"
not_viewable "$TF" || fail "termF still visible after segment click"
ok "Button1 on inactive segment activates that member"

note "segments: wheel cycles next/prev"
frame_geom "$TH" || fail "could not resolve frame geometry for termH"
SY=$(seg_y)
xdo mousemove --sync "$(seg_x 1 3)" "$SY"; sleep 0.1
xdo click 4
wait_for 30 is_viewable "$TG" || fail "wheel-up did not reveal termG (prev)"
xdo click 4
wait_for 30 is_viewable "$TF" || fail "second wheel-up did not reveal termF"
xdo click 5
wait_for 30 is_viewable "$TG" || fail "wheel-down did not reveal termG (next)"
not_viewable "$TF" || fail "termF still visible after wheel-down"
ok "Button4/5 on grouped titlebar cycles tabs"

note "segments: ctrl+click closes that member only"
frame_geom "$TG" || fail "could not resolve frame geometry for termG"
SY=$(seg_y)
xdo mousemove --sync "$(seg_x 2 3)" "$SY"; sleep 0.1
xdo keydown ctrl click 1 keyup ctrl
wait_for 40 pid_gone "$termHPID" || fail "termH did not exit on ctrl+segment click"
is_viewable "$TG" || fail "active termG lost visibility on ctrl+segment close"
kill -0 "$termFPID" 2>/dev/null || fail "termF was closed by ctrl+segment click"
ok "Ctrl+Button1 on segment closes only that member"

reap_terms "$termFPID" "$termGPID"
kill -0 "$WMAKER_PID" 2>/dev/null || fail "wmaker died during segment block"

# --- 9. drag-to-group: attach, tear-off, reorder --------------------------
note "drag: spawn three terms, Button2-drag attach"
spawn_term termJ 80x24+80+80
spawn_term termK 80x24+640+80
spawn_term termL 80x24+360+440
for t in termJ termK termL; do
    wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^$t\$' 2>/dev/null | grep -q ." \
        || fail "$t did not map"
done
TJ=$(find_client termJ); TK=$(find_client termK); TL=$(find_client termL)
sleep 0.3
xdo windowactivate "$TJ"; sleep 0.2
frame_geom "$TJ" || fail "could not resolve frame geometry for termJ"
JSX=$((FX + FB + FW / 2)); JSY=$(seg_y)
frame_geom "$TK" || fail "could not resolve frame geometry for termK"
KTX=$((FX + FB + FW / 2)); KTY=$(seg_y)
# Button2 on a singleton titlebar drags the whole window as payload, the
# drop on termK's titlebar attaches termJ and the drag loop activates the
# payload, so termJ stays mapped at termK's position and termK is hidden
drag_btn 2 "$JSX" "$JSY" "$KTX" "$KTY"
wait_for 30 not_viewable "$TK" || fail "termK was not hidden after drag-attach"
wait_for 30 is_viewable "$TJ" || fail "payload termJ not visible after drag-attach"
[ "$(abs_x "$TJ")" = "$(abs_x "$TK")" ] \
    || fail "termJ at x=$(abs_x "$TJ"), expected termK x=$(abs_x "$TK")"
ok "Button2 drag onto another titlebar attaches and activates payload"

note "drag: attach termL into the same group"
frame_geom "$TL" || fail "could not resolve frame geometry for termL"
LSX=$((FX + FB + FW / 2)); LSY=$(seg_y)
frame_geom "$TJ" || fail "could not resolve frame geometry for termJ"
drag_btn 2 "$LSX" "$LSY" "$((FX + FB + FW / 2))" "$(seg_y)"
wait_for 30 not_viewable "$TJ" || fail "termJ was not hidden after second drag-attach"
wait_for 30 is_viewable "$TL" || fail "payload termL not visible after drag-attach"
not_viewable "$TK" || fail "termK became visible after second drag-attach"
sleep 0.5
# member order is [K, J, L], L active at index 2
ok "three-member group built via Button2 drag"

note "drag: reorder segment 1 -> segment 0"
xdo windowactivate "$TL"; sleep 0.2
frame_geom "$TL" || fail "could not resolve frame geometry for termL"
SY=$(seg_y)
drag_btn 2 "$(seg_x 1 3)" "$SY" "$(seg_x 0 3)" "$SY"
sleep 0.2
# order is now [J, K, L], L still active, segment 0 should activate termJ
frame_geom "$TL" || fail "frame geometry lookup after reorder failed"
SY=$(seg_y)
xdo mousemove --sync "$(seg_x 0 3)" "$SY"; sleep 0.1
xdo click 1
wait_for 30 is_viewable "$TJ" \
    || fail "segment 0 after reorder revealed wrong member (expected termJ)"
not_viewable "$TK" || fail "termK visible, reorder fell back to original order"
ok "Button2 drag within own titlebar reorders segments"

note "drag: tear off inactive segment to root"
# group is [J, K, L], J active at index 0
frame_geom "$TJ" || fail "could not resolve frame geometry for termJ"
SY=$(seg_y)
drag_btn 2 "$(seg_x 2 3)" "$SY" 200 600
wait_for 30 is_viewable "$TL" || fail "torn-off termL not visible"
is_viewable "$TJ" || fail "active termJ lost visibility on tear-off"
not_viewable "$TK" || fail "inactive termK became visible on tear-off"
wait_for 20 no_state "$TL" _NET_WM_STATE_HIDDEN \
    || fail "torn-off termL still has HIDDEN"
LX=$(abs_x "$TL"); JX=$(abs_x "$TJ")
[ "$LX" != "$JX" ] || fail "torn-off termL still at group x=$JX"
ok "Button2 drag onto root tears member off at drop point"

reap_terms "$termJPID" "$termKPID" "$termLPID"
kill -0 "$WMAKER_PID" 2>/dev/null || fail "wmaker died during drag block"

# --- 10. warm restart: mapped and iconified ------------------------------
note "restart: build a three-member group, switch to middle"
spawn_term termP 80x24+100+100
spawn_term termQ 80x24+450+250
spawn_term termR 80x24+250+400
for t in termP termQ termR; do
    wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^$t\$' 2>/dev/null | grep -q ." \
        || fail "$t did not map"
done
TP=$(find_client termP); TQ=$(find_client termQ); TR=$(find_client termR)
xdo windowactivate "$TP"; sleep 0.2
xdo windowactivate "$TQ"; sleep 0.2
xdo key --clearmodifiers ctrl+shift+a
wait_for 30 not_viewable "$TQ" || fail "termQ was not hidden after attach"
xdo windowactivate "$TR"; sleep 0.2
xdo key --clearmodifiers ctrl+shift+a
wait_for 30 not_viewable "$TR" || fail "termR was not hidden after attach"
# order [P, Q, R], P active, switch to Q so the saved active index is mid-group
xdo windowactivate "$TP"; sleep 0.2
xdo key --clearmodifiers ctrl+shift+n
wait_for 30 is_viewable "$TQ" || fail "next did not reveal termQ"

note "restart: wmakermsg restart while mapped"
msg -q restart >>"$LOG" 2>&1
sleep 0.3
wait_for 120 ipc_ready || fail "IPC did not re-bind after restart"
WMAKER_PID=$(msg get_version 2>/dev/null \
    | sed -n 's/.*pid = \([0-9]*\);.*/\1/p')
[ -n "$WMAKER_PID" ] && echo "$WMAKER_PID" >> "$PIDFILE"
wait_for 60 is_viewable "$TQ" \
    || fail "saved active termQ not viewable after restart"
not_viewable "$TP" || fail "termP visible after restart, only termQ expected"
not_viewable "$TR" || fail "termR visible after restart, only termQ expected"
wait_for 30 has_state "$TQ" _NET_WM_STATE_FOCUSED \
    || fail "saved active termQ not focused after restart"
ok "mapped restart restores saved active member"

note "restart: cycle confirms saved order"
xdo windowactivate "$TQ"; sleep 0.2
xdo key --clearmodifiers ctrl+shift+n
wait_for 30 is_viewable "$TR" || fail "next from termQ did not reveal termR"
xdo key --clearmodifiers ctrl+shift+p
wait_for 30 is_viewable "$TQ" || fail "prev did not return to termQ"
ok "tab order [P, Q, R] survives restart"

note "restart: iconify group, wmakermsg restart"
xdo windowactivate "$TQ"; sleep 0.2
DISPLAY=$DPY xdotool windowminimize "$TQ"
wait_for 30 not_viewable "$TQ" || fail "termQ still visible after iconify"
wait_for 30 miniwindows_eq 1 \
    || fail "expected 1 miniwindow before restart, got $(count_miniwindows)"
msg -q restart >>"$LOG" 2>&1
sleep 0.3
wait_for 120 ipc_ready || fail "IPC did not re-bind after iconified restart"
WMAKER_PID=$(msg get_version 2>/dev/null \
    | sed -n 's/.*pid = \([0-9]*\);.*/\1/p')
[ -n "$WMAKER_PID" ] && echo "$WMAKER_PID" >> "$PIDFILE"
wait_for 60 miniwindows_eq 1 \
    || fail "expected 1 miniwindow after restart, got $(count_miniwindows)"
not_viewable "$TP" || fail "termP visible after iconified restart"
not_viewable "$TQ" || fail "termQ visible after iconified restart"
not_viewable "$TR" || fail "termR visible after iconified restart"
ok "iconified restart rebuilds exactly one miniwindow"

note "restart: deiconify reveals saved active member"
xdo windowactivate "$TQ"
wait_for 30 is_viewable "$TQ" \
    || fail "deiconify did not reveal saved active termQ"
not_viewable "$TP" || fail "termP visible after deiconify"
not_viewable "$TR" || fail "termR visible after deiconify"
ok "deiconify after restart reveals only the saved active member"

reap_terms "$termPPID" "$termQPID" "$termRPID"
kill -0 "$WMAKER_PID" 2>/dev/null || fail "wmaker died during restart block"

# --- 14. bystander raise after tab switch ---------------------------------
# wGroupSetActive() restacks the incoming frame above the outgoing one via
# MoveInStackListAbove(), which used WMBagIteratorAtIndex(level+1) to find
# a higher-level anchor. That helper is exact-match, so a level gap fell
# through to XRaiseWindow() and pushed the active frame above the anchor
# wRaiseFrame() restacks under, leaving the next clicked window stuck
# below the group on screen even though the internal list and
# _NET_CLIENT_LIST_STACKING said it was on top.
note "stacking: bystander click-raise after a tab switch"
x_above() {
    # x_above FA FB -> true when frame FA is above FB in X server order,
    # xwininfo -root -children lists top-most first
    DISPLAY=$DPY xwininfo -root -children 2>/dev/null \
        | awk -v a="$1" -v b="$2" \
              '$1 == a { ia = NR } $1 == b { ib = NR }
               END { exit (ia && ib && ia < ib) ? 0 : 1 }'
}
spawn_term termS 80x24+50+50
spawn_term termT 80x24+400+300
spawn_term termU 80x24+200+150
for t in termS termT termU; do
    wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^$t\$' 2>/dev/null | grep -q ." \
        || fail "$t did not map"
done
TS=$(find_client termS); TT=$(find_client termT); TU=$(find_client termU)
FU=$(frame_top "$TU") || fail "could not resolve termU frame"
xdo windowactivate "$TU"; sleep 0.2
xdo windowactivate "$TS"; sleep 0.2
xdo windowactivate "$TT"; sleep 0.2
xdo key --clearmodifiers ctrl+shift+a
wait_for 30 not_viewable "$TT" || fail "termT was not hidden after attach"
FS=$(frame_top "$TS") || fail "could not resolve termS frame"
# cycle next then prev so MoveInStackListAbove() runs with the incoming
# frame landing on top of the level
xdo key --clearmodifiers ctrl+shift+n
wait_for 30 is_viewable "$TT" || fail "next did not reveal termT"
xdo key --clearmodifiers ctrl+shift+p
wait_for 30 is_viewable "$TS" || fail "prev did not return to termS"
x_above "$FS" "$FU" \
    || fail "group frame not above bystander before the click"
# click termU's exposed client area outside the group footprint
ux=$(($(abs_x "$TU") + $(client_w "$TU") - 20))
uy=$(($(abs_y "$TU") + 20))
xdo mousemove --sync "$ux" "$uy"; xdo click 1
wait_for 30 has_state "$TU" _NET_WM_STATE_FOCUSED \
    || fail "bystander click did not focus termU"
wait_for 30 x_above "$FU" "$FS" \
    || fail "bystander frame did not raise above the group after click"
ok "bystander raises above the group after a tab switch"

reap_terms "$termSPID" "$termTPID" "$termUPID"
kill -0 "$WMAKER_PID" 2>/dev/null || fail "wmaker died during bystander block"

# --- teardown --------------------------------------------------------------
kill -TERM "$WMAKER_PID" 2>/dev/null
wait_for 50 pid_gone "$WMAKER_PID"

note "PASS"
exit 0
