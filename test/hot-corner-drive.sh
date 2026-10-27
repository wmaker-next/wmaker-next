#!/bin/sh
# Verify hot corners fire even when a client (windowed or fullscreen)
# covers the corner, and that the root-window PointerMotionMask for
# ScrollableMenus is selected only while a menu is actually mapped.
# Runs wmaker under Xephyr on an isolated display and WMAKER_USER_ROOT,
# tracks every PID it spawns and kills only those.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${HOTCORNER_DRIVE_DISPLAY:-:$(drive_default_display)}
W=1280
H=800
PIDFILE=$(mktemp /tmp/hotcorner-drive.pids.XXXXXX)
WMHOME=$(mktemp -d /tmp/hotcorner-drive-wmhome.XXXXXX)
MARK=$(mktemp -d /tmp/hotcorner-drive-mark.XXXXXX)
LOG=/tmp/hotcorner-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
: > "$PIDFILE"
: > "$LOG"

cleanup() {
    while read -r p; do
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done < "$PIDFILE"
    rm -f "$PIDFILE"
    rm -rf "$WMHOME" "$MARK"
}
trap cleanup EXIT

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
fail() { printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
xdo()  {
    { DISPLAY=$DPY xdotool "$@" 2>&1 1>&3 \
        | grep -v '^XGetWindowProperty\[_NET_WM_DESKTOP\] failed' >&2; } 3>&1
}

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

# move pointer well away from any corner, then into the target corner,
# wait past HotCornerDelay and check the action ran
hit_corner() {
    local x=$1 y=$2 marker=$3 what=$4
    rm -f "$MARK/$marker"
    xdo mousemove $((W/2)) $((H/2)); sleep 0.1
    xdo mousemove "$x" "$y"
    wait_for 20 test -f "$MARK/$marker" \
        || fail "$what: corner action did not fire"
    note "ok: $what"
}

# --- isolated WMAKER_USER_ROOT with HotCorners enabled ---------------------
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  HotCorners = YES;
  HotCornerDelay = 100;
  HotCornerEdge = 10;
  HotCornerActions = ("touch $MARK/tl", "ShowDesktop",
                      "NextWorkspace", "touch $MARK/br");
  RootMenuKey = "Control+Shift+R";
}
EOF
cat >"$WMHOME/Defaults/WMState" <<'EOF'
{
  Workspaces = (
    { Name = "hotcorner-0"; },
    { Name = "hotcorner-1"; }
  );
}
EOF
export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG

# --- Xephyr -----------------------------------------------------------------
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use, set HOTCORNER_DRIVE_DISPLAY to a free one" >&2
    exit 77
fi
# Xephyr is a nested server: on the session display its host pointer
# tracks the live mouse and can override the XTEST warps the drive
# sends. Run it on a private headless parent whose virtual pointer
# only moves when Xephyr mirrors a warp.
PARENT_DISPLAY=${HOTCORNER_DRIVE_PARENT_DISPLAY:-":$(( ${DPY#:} + 1 ))"}
Xvfb "$PARENT_DISPLAY" -screen 0 2400x1600x24 -ac >>"$LOG" 2>&1 &
XVFB_PARENT_PID=$!
echo "$XVFB_PARENT_PID" >> "$PIDFILE"
xvfb_parent_ready() {
    kill -0 "$XVFB_PARENT_PID" 2>/dev/null || fail "parent Xvfb exited early"
    DISPLAY=$PARENT_DISPLAY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_parent_ready || fail "parent Xvfb failed to start"

note "start Xephyr $DPY on parent $PARENT_DISPLAY"
Xephyr "$DPY" -display "$PARENT_DISPLAY" -screen ${W}x${H} -ac >>"$LOG" 2>&1 &
XEPHYR_PID=$!
echo "$XEPHYR_PID" >> "$PIDFILE"
xephyr_ready() {
    kill -0 "$XEPHYR_PID" 2>/dev/null || fail "Xephyr exited early"
    DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xephyr_ready || fail "Xephyr failed to start"

# --- wmaker -----------------------------------------------------------------
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
sleep 0.5

# --- 1. baseline: bare root -------------------------------------------------
hit_corner 1 1 tl "baseline top-left over root"

# --- 2. regression: window covers the corner --------------------------------
note "map xterm at +0+0 covering top-left"
DISPLAY=$DPY xterm -geometry 200x60+0+0 -e 'sleep 300' >>"$LOG" 2>&1 &
XTPID=$!
echo "$XTPID" >> "$PIDFILE"
XTW=""
xterm_mapped() {
    XTW=$(DISPLAY=$DPY xdotool search --class XTerm 2>/dev/null | head -n 1)
    [ -n "$XTW" ]
}
wait_for 40 xterm_mapped || fail "xterm did not map"
xdo windowactivate "$XTW"; sleep 0.3

hit_corner 1 1 tl "top-left over windowed client"

# --- 3. fullscreen covers the corner ----------------------------------------
note "fullscreen the xterm"
DISPLAY=$DPY wmctrl -i -r "$XTW" -b add,fullscreen 2>/dev/null; sleep 0.4

hit_corner 1 1 tl "top-left over fullscreen client"

# --- 4. second corner under fullscreen --------------------------------------
hit_corner $((W-2)) $((H-2)) br "bottom-right over fullscreen client"

DISPLAY=$DPY wmctrl -i -c "$XTW" 2>/dev/null
wait_for 30 pid_gone "$XTPID"
# wSetFocusTo(scr, NULL) after unmanage moves input focus back onto
# no_focus_win, give that a beat before the RootMenuKey press below so
# the KeyPress lands on a window wmaker listens on
sleep 0.3

# --- 5. hot corner dispatches a built-in action -----------------------------
# The bottom-left corner maps to NextWorkspace. Hitting it must change the
# current workspace instead of spawning a shell, while the tl/tr/br corners
# keep their touch(1) commands so the shell path stays covered.
current_desktop() {
    DISPLAY=$DPY xprop -root _NET_CURRENT_DESKTOP 2>/dev/null | sed -n 's/.*= //p'
}
ws_changed() { [ -n "$(current_desktop)" ] && [ "$(current_desktop)" != "$1" ]; }
note "bottom-left corner switches workspace"
WS0=$(current_desktop)
[ -n "$WS0" ] || fail "could not read _NET_CURRENT_DESKTOP"
xdo mousemove $((W/2)) $((H/2)); sleep 0.1
xdo mousemove 1 $((H-2))
wait_for 30 ws_changed "$WS0" \
    || fail "bottom-left corner did not switch workspace"
note "ok: bottom-left corner switched workspace"

# --- 5b. a second built-in action: ShowDesktop ------------------------------
showing_desktop() {
    DISPLAY=$DPY xprop -root _NET_SHOWING_DESKTOP 2>/dev/null | sed -n 's/.*= //p'
}
sd_changed() { [ -n "$(showing_desktop)" ] && [ "$(showing_desktop)" != "$1" ]; }
note "top-right corner toggles showing-desktop"
SD0=$(showing_desktop)
[ -n "$SD0" ] || fail "could not read _NET_SHOWING_DESKTOP"
xdo mousemove $((W/2)) $((H/2)); sleep 0.1
xdo mousemove $((W-2)) 1
wait_for 30 sd_changed "$SD0" \
    || fail "top-right corner did not toggle showing-desktop"
SD1=$(showing_desktop)
note "ok: top-right corner toggled showing-desktop"
xdo mousemove $((W/2)) $((H/2)); sleep 0.1
xdo mousemove $((W-2)) 1
wait_for 30 sd_changed "$SD1" \
    || fail "top-right corner did not toggle showing-desktop back"
note "ok: top-right corner toggled showing-desktop back"

# --- 5c. virtual desktop edge strips must not cover the corners -------------
cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  HotCorners = YES;
  HotCornerDelay = 100;
  HotCornerEdge = 10;
  HotCornerActions = ("touch $MARK/tl", "ShowDesktop",
                      "NextWorkspace", "touch $MARK/br");
  EnableVirtualDesktop = YES;
  VirtualEdgeBorderSize = 2;
  RootMenuKey = "Control+Shift+R";
}
EOF
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DISPLAY=$DPY "$ROOT/util/.libs/wmakermsg" -q reconfigure >>"$LOG" 2>&1 \
    || fail "reconfigure for virtual desktop failed"
edge_ready() {
    DISPLAY=$DPY xwininfo -root -tree 2>/dev/null | grep -Eq "([0-9]{3,}x2|2x[0-9]{3,})"
}
pointer_is() {
    eval "$(DISPLAY=$DPY xdotool getmouselocation --shell)"
    [ "$X" = "$1" ] && [ "$Y" = "$2" ]
}
if wait_for 20 edge_ready; then
    note "corner guard keeps edge scroll off the approach"
    rm -f "$MARK/tl"
    xdo mousemove $((W/2)) $((H/2)); sleep 0.1
    xdo mousemove 12 1
    sleep 0.8
    pointer_is 12 1 || fail "edge scroll grabbed the pointer inside the corner guard"
    [ ! -f "$MARK/tl" ] || fail "hot corner fired inside the guard outside its edge"
    note "ok: pointer stayed in the corner guard"
    hit_corner 1 1 tl "top-left corner over virtual desktop edge strip"
else
    note "skipping: virtual desktop edge windows not built"
fi

# --- 5d. a mapped action must not retrigger while the pointer stays put ----
cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  HotCorners = YES;
  HotCornerDelay = 100;
  HotCornerEdge = 10;
  HotCornerActions = ("xterm -class HotCornerRepeat -e sleep 60",
                      "None", "None", "None");
  EnableVirtualDesktop = YES;
  VirtualEdgeBorderSize = 2;
  RootMenuKey = "Control+Shift+R";
}
EOF
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DISPLAY=$DPY "$ROOT/util/.libs/wmakermsg" -q reconfigure >>"$LOG" 2>&1 \
    || fail "reconfigure for the repeat test failed"
hotcorner_repeat_count() {
    local n=0
    local w
    for w in $(DISPLAY=$DPY xdotool search --class HotCornerRepeat 2>/dev/null); do
        DISPLAY=$DPY xprop -id "$w" WM_STATE 2>/dev/null | grep -q Normal && n=$((n + 1))
    done
    echo "$n"
}
repeat_started() { [ "$(hotcorner_repeat_count)" -ge 1 ]; }
repeat_two() { [ "$(hotcorner_repeat_count)" -ge 2 ]; }
note "hot corner mapped action fires once per corner entry"
xdo mousemove $((W/2)) $((H/2)); sleep 0.1
xdo mousemove 1 1
wait_for 30 repeat_started || fail "top-left repeat action did not fire"
sleep 2
[ "$(hotcorner_repeat_count)" = "1" ] \
    || fail "top-left repeat action fired again without leaving the corner"
note "ok: only one action while the pointer stayed in the corner"
xdo mousemove $((W/2)) $((H/2)); sleep 0.2
xdo mousemove 1 1
wait_for 30 repeat_two || fail "top-left repeat action did not fire on re-entry"
note "ok: a second action fired after leaving and re-entering"
for w in $(DISPLAY=$DPY xdotool search --class HotCornerRepeat 2>/dev/null); do
    DISPLAY=$DPY wmctrl -i -c "$w" 2>/dev/null
done

# --- 6. a None corner is inert ----------------------------------------------
# Rewire the bottom-left corner from NextWorkspace to None: hitting it
# must change nothing (no workspace switch, no shell) and wmaker must
# survive the NULL action entry.
note "bottom-left corner set to None is inert"
WS_NONE=$(current_desktop)
cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  HotCorners = YES;
  HotCornerDelay = 100;
  HotCornerEdge = 10;
  HotCornerActions = ("touch $MARK/tl", "touch $MARK/tr",
                      "None", "touch $MARK/br");
  RootMenuKey = "Control+Shift+R";
}
EOF
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DISPLAY=$DPY "$ROOT/util/.libs/wmakermsg" -q reconfigure >>"$LOG" 2>&1 \
    || fail "reconfigure after the None rewrite failed"
xdo mousemove $((W/2)) $((H/2)); sleep 0.1
xdo mousemove 1 $((H-2))
sleep 0.5
[ "$(current_desktop)" = "$WS_NONE" ] \
    || fail "None corner switched the workspace"
kill -0 "$WMAKER_PID" 2>/dev/null || fail "wmaker died on a None corner"
note "ok: bottom-left None corner was inert"

# --- 7. root PointerMotionMask gated on mapped menus ------------------------
# ScrollableMenus is at its YES default (not in the WindowMaker domain
# above), so an idle desktop must not select PointerMotion on root, it
# only appears while a menu is mapped and goes again on close.
# ButtonMotion must always be selected so the implicit grab from a
# root-window button press carries motion to menuMouseDown(), without
# it the root menu opened by that press shows no entry highlight.
root_mask() {
    DISPLAY=$DPY xwininfo -root -all 2>/dev/null \
        | sed -n '/Someone wants/,/Do not propagate/p'
}
root_has_motion() { root_mask | grep PointerMotion >/dev/null; }
no_root_motion() { ! root_has_motion; }

xdo mousemove $((W/2)) $((H/2)); sleep 0.2
note "root motion mask: idle desktop"
root_has_motion && fail "root has PointerMotion with no menu mapped"
note "ok: no root PointerMotion with no menu mapped"
root_mask | grep ButtonMotion >/dev/null \
    || fail "root lacks ButtonMotion, root-menu hover highlight will not work"
note "ok: root ButtonMotion always selected"

note "root motion mask: menu open"
# some xdotool builds prepend Alt_L when synthesizing an F-key keysym
# so the passive grab for the F12 default never matches, the domain
# above rebinds RootMenuKey to a plain letter combination instead
xdo key --clearmodifiers ctrl+shift+r
wait_for 30 root_has_motion \
    || fail "root has no PointerMotion with a menu mapped"
note "ok: root PointerMotion with menu mapped"

note "root motion mask: menu closed"
xdo key Escape
wait_for 30 no_root_motion \
    || fail "root still has PointerMotion after menu unmapped"
note "ok: no root PointerMotion after menu unmapped"

# --- teardown ---------------------------------------------------------------
kill -TERM "$WMAKER_PID" 2>/dev/null
wait_for 50 pid_gone "$WMAKER_PID"

note "PASS"
exit 0
