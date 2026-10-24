#!/bin/sh
# Regression gate for the root-menu Button3 path: a press on the root
# window maps the menu and must fall through into menuMouseDown() so the
# entry under the pointer is highlighted while the button is held and
# its callback runs on release. executeButtonAction() repoints
# event->xbutton.window at the just-mapped menu frame so the descriptor
# lookup in handleButtonPress() resolves to the menu, that lookup must
# therefore read the cooked event field, not the immutable xcb wire copy
# of the original root window.
#
# The highlight check samples the same pixel before and after the
# pointer lands on the entry, so it is independent of the configured
# theme colours, and a second sample on the neighbour entry confirms the
# highlight is per-row.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${ROOTMENU_DRIVE_DISPLAY:-:$(drive_default_display)}
W=1280
H=800
RUNTIME=$(mktemp -d /tmp/rootmenu-drive.XXXXXX)
WMHOME="$RUNTIME/home"
MARK="$RUNTIME/mark"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/rootmenu-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
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
fail() { printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
xdo()  { DISPLAY=$DPY xdotool "$@"; sleep 0.05; }

wait_for() {
    local n=$1; shift
    while [ "$n" -gt 0 ]; do
        "$@" && return 0
        sleep 0.1
        n=$((n - 1))
    done
    return 1
}
pid_gone() { ! kill -0 "$1" 2>/dev/null; }

command -v Xvfb    >/dev/null || { echo "SKIP: Xvfb not found"; exit 77; }
command -v xdotool >/dev/null || { echo "SKIP: xdotool not found"; exit 77; }
command -v convert >/dev/null || { echo "SKIP: ImageMagick convert not found"; exit 77; }
command -v xwd     >/dev/null || { echo "SKIP: xwd not found"; exit 77; }

# --- isolated WMAKER_USER_ROOT --------------------------------------------
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker" "$MARK"
cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  WrapMenus = NO;
  ScrollableMenus = NO;
  MouseRightButtonAction = OpenApplicationsMenu;
}
EOF
# A two-entry root menu, both entries are EXEC so a release on the first
# one is observable as a sentinel file without depending on any external
# program. The second entry exists only as the unhighlighted control row
# for the pixel comparison.
cat >"$WMHOME/Defaults/WMRootMenu" <<EOF
("Root",
 ("First", EXEC, "touch $MARK/first"),
 ("Second", EXEC, "touch $MARK/second"))
EOF
export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET

# --- Xvfb ------------------------------------------------------------------
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use, set ROOTMENU_DRIVE_DISPLAY" >&2
    exit 77
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 ${W}x${H}x24 -bs -ac >>"$LOG" 2>&1 &
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
wm_ready() {
    DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null \
        | grep 'window id' >/dev/null
}
wait_for 120 wm_ready || fail "wmaker did not come up"
sleep 0.3

# Hex colour at root coordinate ($1,$2) via xwd, the pointer grab held
# by the menu loop is on a different connection so GetImage is fine.
pixel_at() {
    DISPLAY=$DPY xwd -root -silent \
        | convert xwd:- -crop "1x1+$1+$2" +repage txt:- 2>/dev/null \
        | sed -n 's/.*\(#[0-9A-Fa-f]\{6,8\}\).*/\1/p'
}

CX=$((W/2))
CY=$((H/2))

# --- 1. open via Button3 on root ------------------------------------------
note "press Button3 on root at $CX,$CY"
xdo mousemove "$CX" "$CY"
xdo mousedown 3
# OpenRootMenu maps the frame with its top edge at y_root, the inner
# entry list (menu->menu) is the only child positioned below the title
# so its absolute y gives the first row regardless of font metrics.
ENTRY_WIN=$(DISPLAY=$DPY xwininfo -root -tree 2>/dev/null \
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
            } }')
[ -n "$ENTRY_WIN" ] || fail "could not locate the menu entry window"
set -- $ENTRY_WIN
EX=$1; EY=$2; EH=$3
# wMenuRealize sizes the list to entry_no * entry_height - 1
ROWH=$(( (EH + 1) / 2 ))
PX=$((EX + 6))
ROW1_Y=$((EY + ROWH / 4))
ROW2_Y=$((EY + ROWH + ROWH / 4))
note "entry list at +$EX+$EY h=$EH row=$ROWH, sampling x=$PX y1=$ROW1_Y y2=$ROW2_Y"

# Baseline: pointer is still on the title bar so no entry is selected.
BASE1=$(pixel_at "$PX" "$ROW1_Y")
[ -n "$BASE1" ] || fail "could not sample baseline pixel"

# --- 2. hover highlight on the first entry --------------------------------
note "drag onto first entry"
xdo mousemove "$PX" "$ROW1_Y"
sleep 0.2
SEL1=$(pixel_at "$PX" "$ROW1_Y")
SEL2=$(pixel_at "$PX" "$ROW2_Y")
note "row1 $BASE1 -> $SEL1, row2 $SEL2"
[ "$SEL1" != "$BASE1" ] \
    || fail "first entry not highlighted under pointer (still $SEL1)"
[ "$SEL2" != "$SEL1" ] \
    || fail "second entry has the same colour as the highlighted first"
note "ok: first entry highlighted under pointer"

# --- 3. highlight follows the pointer -------------------------------------
note "drag onto second entry"
xdo mousemove "$PX" "$ROW2_Y"
sleep 0.2
NOW1=$(pixel_at "$PX" "$ROW1_Y")
NOW2=$(pixel_at "$PX" "$ROW2_Y")
note "row1 $NOW1, row2 $NOW2"
[ "$NOW1" = "$BASE1" ] \
    || fail "first entry still highlighted after pointer left it"
[ "$NOW2" = "$SEL1" ] \
    || fail "second entry not highlighted under pointer"
note "ok: highlight tracks the pointer"

# --- 4. release runs the entry callback -----------------------------------
note "drag back to first entry and release"
xdo mousemove "$PX" "$ROW1_Y"
sleep 0.1
xdo mouseup 3
wait_for 30 test -f "$MARK/first" \
    || fail "release on first entry did not run its EXEC command"
[ -f "$MARK/second" ] && fail "second entry command ran but was not selected"
note "ok: release on selected entry executed its command"

# --- teardown --------------------------------------------------------------
kill -TERM "$WMAKER_PID" 2>/dev/null
wait_for 50 pid_gone "$WMAKER_PID"

note "PASS"
exit 0
