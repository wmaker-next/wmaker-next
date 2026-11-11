#!/bin/sh
# Drive the RandR head rebuild on a live display. Xephyr exposes the
# parent server modes and supports RandR 1.5 user monitors, so splitting
# the one output into a left and a right monitor makes wRandRHandleNotify
# -> wRandR_Update() rebuild xine_info from GetMonitors while the window
# manager is running. Moving a window between the new heads and removing
# one again exercises moveBetweenHeads(), warpPointerToNextMonitor() and
# wRandR_BringAllWindowsInside().
#
# Xvfb alone ignores RRSetMonitor, hence the nested Xephyr. Runs against
# an isolated WMAKER_USER_ROOT and XDG_RUNTIME_DIR, tracks every PID it
# spawns, the live desktop is untouched.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${RANDR_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-randr-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/randr-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
WDWRITE="$ROOT/util/.libs/wdwrite"
SOCKET=
PARENT_DISPLAY=
: > "$PIDFILE"
: > "$LOG"

PASSED=0
FAILED=0

record_pid() {
    printf '%s %s %s\n' "$1" "$2" "$3" >> "$PIDFILE"
}

# display_bound PID DISPLAY: DISPLAY is in the process environment or
# appears as a command line argument (nested servers take it as argv)
display_bound() {
    cat "/proc/$1/environ" 2>/dev/null | tr '\0' '\n' | grep -Fx -- "DISPLAY=$2" >/dev/null 2>&1 && return 0
    cat "/proc/$1/cmdline" 2>/dev/null | tr '\0' '\n' | grep -Fx -- "$2" >/dev/null 2>&1
}

cleanup() {
    cleanup_status=$?
    while read -r p name d; do
        [ -n "$p" ] || continue
        if [ -z "$name" ] || [ -z "$d" ]; then
            printf 'ERROR: malformed PIDFILE entry %s, not terminating it\n' "$p" >&2
            cleanup_status=1
            continue
        fi
        [ -d "/proc/$p" ] || continue
        if [ "$(cat "/proc/$p/comm" 2>/dev/null)" != "$name" ]; then
            printf 'WARNING: PID %s is no longer the %s from %s, not terminating it\n' \
                "$p" "$name" "$d" >&2
            continue
        fi
        # the display can vanish while the process exits on its own,
        # only terminate a process still bound to the recorded display
        display_bound "$p" "$d" || continue
        kill "$p" 2>/dev/null
    done < "$PIDFILE"
    rm -rf "$RUNTIME"
    exit "$cleanup_status"
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
pid_exists() {
    pgrep -a -x "$2" | awk -v p="$1" \
        '$1 == p { found=1 } END { exit !found }'
}
pid_gone() { ! pid_exists "$1" wmaker; }

msg() {
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$WMAKERMSG" -s "$SOCKET" "$@"
}
ipc_ready() { msg -q get_version >/dev/null 2>&1; }

wdwrite() {
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        DISPLAY=$DPY "$WDWRITE" "$@"
}

xdo() { DISPLAY=$DPY xdotool "$@"; }
xr() { DISPLAY=$DPY xrandr "$@"; }

background_pixel() {
    DISPLAY=$DPY xwd -root -silent 2>/dev/null \
        | convert xwd:- -format '%[fx:int(255*p{400,10}.r)]' info: 2>/dev/null
}
background_in_range() {
    value=$(background_pixel)
    [ -n "$value" ] && [ "$value" -ge "$1" ] && [ "$value" -le "$2" ]
}
root_pixmap() {
    DISPLAY=$DPY xprop -root _XROOTPMAP_ID 2>/dev/null | sed 's/.*# //'
}
pixmap_changed() {
    value=$(root_pixmap)
    [ -n "$value" ] && [ "$value" != "$1" ]
}
helper_stopped() {
    ! pgrep -P "$WMAKER_PID" -x wmsetbg >/dev/null 2>&1
}
property_count_at_least() {
    count=$(grep -c "^$1(" "$RUNTIME/property-watch" || true)
    [ "$count" -ge "$2" ]
}

win_x() {
    DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null \
        | awk '/Absolute upper-left X/ {print $4}'
}
win_y() {
    DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null \
        | awk '/Absolute upper-left Y/ {print $4}'
}
win_w() {
    DISPLAY=$DPY xwininfo -id "$1" 2>/dev/null \
        | awk '/Width:/ {print $2}'
}
pointer_x() {
    xdo getmouselocation --shell 2>/dev/null | sed -n 's/^X=//p'
}
desktop_geometry() {
    DISPLAY=$DPY xprop -root _NET_DESKTOP_GEOMETRY 2>/dev/null \
        | sed 's/.*= //'
}
desktop_geometry_is() {
    [ "$(desktop_geometry)" = "$1" ]
}

for tool in Xvfb Xephyr xrandr xdpyinfo xdotool xprop xwininfo xterm pgrep; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
PATH="$ROOT/util:$PATH"
export PATH

export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  DisableSound = YES;
  HotplugMonitor = YES;
  MoveTo12to6Head = "Control+Shift+l";
  MoveTo6to12Head = "Control+Shift+h";
  ScreenSwitchKey = "Control+Shift+m";
  LockScreenCommand = "touch $RUNTIME/key-dispatch";
  LockScreenKey = "Control+Shift+z";
}
EOF

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use" >&2; exit 77
fi
# Xephyr's own RandR is what changes, the parent only supplies its modes
PARENT_DISPLAY=${RANDR_DRIVE_PARENT_DISPLAY:-":$(( ${DPY#:} + 1 ))"}
if DISPLAY=$PARENT_DISPLAY xdpyinfo >/dev/null 2>&1; then
    echo "parent display $PARENT_DISPLAY is already in use" >&2; exit 77
fi
note "start Xvfb $PARENT_DISPLAY and Xephyr $DPY"
Xvfb "$PARENT_DISPLAY" -screen 0 1600x1200x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
record_pid "$XVFB_PID" Xvfb "$PARENT_DISPLAY"
wait_for 40 sh -c "DISPLAY=$PARENT_DISPLAY xdpyinfo >/dev/null 2>&1" \
    || fatal "Xvfb failed to start"
DISPLAY=$PARENT_DISPLAY Xephyr "$DPY" -screen 1024x768 -ac -noreset >>"$LOG" 2>&1 &
XEPHYR_PID=$!
record_pid "$XEPHYR_PID" Xephyr "$DPY"
wait_for 40 sh -c "DISPLAY=$DPY xdpyinfo >/dev/null 2>&1" \
    || fatal "Xephyr failed to start"

note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    ASAN_OPTIONS="$(drive_asan_options)" \
    DISPLAY=$DPY \
    "$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
record_pid "$WMAKER_PID" wmaker "$DPY"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
wait_for 80 ipc_ready || fatal "wmaker IPC did not come up"
sleep 0.5

note "map an xterm on the left head"
DISPLAY=$DPY xterm -geometry 40x12+40+100 -e 'sleep 300' >>"$LOG" 2>&1 &
XTERM_PID=$!
record_pid "$XTERM_PID" xterm "$DPY"
XTERM_WID=
xterm_ready() {
    XTERM_WID=$(xdo search --onlyvisible --class XTerm 2>/dev/null | head -n 1)
    [ -n "$XTERM_WID" ]
}
wait_for 60 xterm_ready || fatal "xterm did not map"
xdo windowactivate "$XTERM_WID" 2>/dev/null
sleep 0.3

BASE_X=$(win_x "$XTERM_WID")
check "xterm starts on the left half" sh -c "[ -n '$BASE_X' ] && [ $BASE_X -lt 512 ]"

# --- 1. split the output into two virtual monitors -------------------------
note "split $DPY into left/right monitors"
xr --setmonitor left 512/135x768/203+0+0 default >>"$LOG" 2>&1 \
    || fail "could not create left monitor"
xr --setmonitor right 512/135x768/203+512+0 none >>"$LOG" 2>&1 \
    || fail "could not create right monitor"
# RRSetMonitor alone does not reliably emit the CRTC/output notifies the
# WM selected, and a same-mode SetCrtcConfig is a no-op. A real mode
# round-trip makes the server publish the new GetMonitors view.
xr --output default --mode 800x600 >>"$LOG" 2>&1 \
    || fail "could not nudge the mode after adding heads"
sleep 0.4
xr --output default --mode 1024x768 >>"$LOG" 2>&1 \
    || fail "could not restore the mode after adding heads"
sleep 0.6
check "wmaker alive after adding a head" ipc_ready
check "left monitor is advertised" \
    sh -c "DISPLAY=$DPY xrandr --listmonitors | grep -q ' left '"
check "right monitor is advertised" \
    sh -c "DISPLAY=$DPY xrandr --listmonitors | grep -q ' right '"

# --- 2. move the window between the new heads -----------------------------
# the RandR rebuild may leave the root focused, and the frame key grabs
# only exist while a managed window owns the focus, so re-focus the client
xdo windowactivate "$XTERM_WID" 2>/dev/null
sleep 0.2
xdo key ctrl+shift+z >>"$LOG" 2>&1
wait_for 20 [ -f "$RUNTIME/key-dispatch" ] \
    || fail "key dispatch stopped working after the RandR rebuild"
check "key dispatch still works after the RandR rebuild" [ -f "$RUNTIME/key-dispatch" ]
note "move the xterm to the right head with MoveTo12to6Head"
xdo key ctrl+shift+l >>"$LOG" 2>&1
wait_for 40 sh -c "[ $(win_x "$XTERM_WID") -ge 512 ]" \
    || fail "window did not move to the right head (x=$(win_x "$XTERM_WID"))"
check "window is on the right head" [ "$(win_x "$XTERM_WID")" -ge 512 ]

note "move the xterm back to the left head with MoveTo6to12Head"
xdo windowactivate "$XTERM_WID" 2>/dev/null
xdo key ctrl+shift+h >>"$LOG" 2>&1
wait_for 40 sh -c "[ $(win_x "$XTERM_WID") -lt 512 ]" \
    || fail "window did not move back to the left head (x=$(win_x "$XTERM_WID"))"
check "window is back on the left head" [ "$(win_x "$XTERM_WID")" -lt 512 ]

# --- 3. ScreenSwitchKey warps the pointer to the next head -----------------
note "warp the pointer to the next head with ScreenSwitchKey"
xdo windowactivate "$XTERM_WID" 2>/dev/null
xdo mousemove 100 300 >>"$LOG" 2>&1
sleep 0.2
PX0=$(pointer_x)
xdo key ctrl+shift+m >>"$LOG" 2>&1
wait_for 40 sh -c "[ $(pointer_x) -ge 512 ]" \
    || fail "pointer did not warp to the right head (x=$(pointer_x))"
check "pointer started on the left head" [ "$PX0" -lt 512 ]
check "pointer is on the right head" [ "$(pointer_x)" -ge 512 ]

# --- 4. removing the occupied head brings the window back ------------------
note "remove the right head while the window is on it"
xdo windowactivate "$XTERM_WID" 2>/dev/null
xdo key ctrl+shift+l >>"$LOG" 2>&1
wait_for 40 sh -c "[ $(win_x "$XTERM_WID") -ge 512 ]" \
    || fail "window is not on the right head before removal"
xr --delmonitor right >>"$LOG" 2>&1 || fail "could not remove right monitor"
sleep 0.6
check "wmaker alive after removing a head" ipc_ready
WX=$(win_x "$XTERM_WID")
WW=$(win_w "$XTERM_WID")
check "window was brought back inside the remaining head" \
    sh -c "[ -n '$WX' ] && [ -n '$WW' ] && [ $WX -ge 0 ] && [ $((WX + WW)) -le 1024 ]"

# --- 5. restore the default monitor and change the mode --------------------
note "restore the default monitor"
xr --delmonitor left >>"$LOG" 2>&1 || fail "could not remove left monitor"
sleep 0.5
check "wmaker alive after restoring the default monitor" ipc_ready

note "shrink the framebuffer from 1024x768 to 800x600"
xr --output default --mode 800x600 >>"$LOG" 2>&1 || fail "could not switch mode"
sleep 0.6
check "server reports 800x600" \
    sh -c "DISPLAY=$DPY xdpyinfo | grep -q 'dimensions:.*800x600'"
wait_for 30 desktop_geometry_is '800, 600' \
    || fail "_NET_DESKTOP_GEOMETRY did not follow the mode change (got $(desktop_geometry))"
check "_NET_DESKTOP_GEOMETRY follows the mode change" \
    desktop_geometry_is '800, 600'
check "wmaker alive after the mode change" ipc_ready
WX=$(win_x "$XTERM_WID")
WY=$(win_y "$XTERM_WID")
WW=$(win_w "$XTERM_WID")
check "window stays inside the shrunken framebuffer" \
    sh -c "[ -n '$WX' ] && [ -n '$WY' ] && [ -n '$WW' ] && [ $WX -ge 0 ] && [ $WY -ge 0 ] && [ $((WX + WW)) -le 800 ]"

note "grow the framebuffer back to 1024x768"
xr --output default --mode 1024x768 >>"$LOG" 2>&1 || fail "could not restore the mode"
sleep 0.5
check "_NET_DESKTOP_GEOMETRY follows the grow" \
    desktop_geometry_is '1024, 768'
check "wmaker alive after growing the framebuffer" ipc_ready

# --- 6. configured wallpapers follow geometry with hotplug disabled -------
if command -v xwd >/dev/null 2>&1 && command -v convert >/dev/null 2>&1 &&
   command -v stdbuf >/dev/null 2>&1 && [ -x "$WDWRITE" ]; then
note "refresh a single workspace background after mode changes"
wdwrite WindowMaker HotplugMonitor NO || fail "could not disable hotplug"
wdwrite WindowMaker WorkspaceBack '(hgradient, "black", "white")' \
    || fail "could not set WorkspaceBack"
msg -q reconfigure || fail "reconfigure rejected"
wait_for 50 background_in_range 90 110 || fail "initial background was not rendered"
check "single background uses 1024-wide geometry" background_in_range 90 110
xr --output default --mode 800x600 >>"$LOG" 2>&1 || fail "could not shrink for wallpaper"
wait_for 50 background_in_range 118 138 || fail "single background did not refresh on shrink"
check "single background follows shrink" background_in_range 118 138
xr --output default --mode 1024x768 >>"$LOG" 2>&1 || fail "could not grow for wallpaper"
wait_for 50 background_in_range 90 110 || fail "single background did not refresh on grow"
check "single background follows grow" background_in_range 90 110

note "refresh shared workspace textures and default fallback"
msg -q workspace-new 2 || fail "could not create workspaces"
wdwrite WindowMaker WorkspaceSpecificBack \
    '((hgradient, "white", "black"), (hgradient, "white", "black"))' \
    || fail "could not set workspace backgrounds"
msg -q reconfigure || fail "reconfigure rejected"
wait_for 50 background_in_range 145 165 || fail "workspace background was not rendered"
check "workspace background uses 1024-wide geometry" background_in_range 145 165
xr --output default --mode 800x600 >>"$LOG" 2>&1 || fail "could not shrink for helper"
wait_for 50 background_in_range 118 138 || fail "helper background did not refresh"
check "shared workspace texture follows shrink" background_in_range 118 138
msg -q workspace 1 || fail "could not switch to shared texture"
wait_for 50 background_in_range 118 138 || fail "shared texture was not preserved"
check "second workspace retains refreshed shared texture" background_in_range 118 138
msg -q workspace 2 || fail "could not switch to default background"
wait_for 50 background_in_range 118 138 || fail "default background did not refresh"
check "default workspace fallback follows shrink" background_in_range 118 138
xr --output default --mode 1024x768 >>"$LOG" 2>&1 || fail "could not grow for helper"
wait_for 50 background_in_range 90 110 || fail "default background did not refresh on grow"
check "default workspace fallback follows grow" background_in_range 90 110
msg -q workspace 0 || fail "could not return to first workspace"
wait_for 50 background_in_range 145 165 || fail "shared texture did not refresh on grow"
check "shared workspace texture follows grow" background_in_range 145 165
check "wmaker alive after wallpaper refresh" ipc_ready

note "refresh wallpaper after a same-size monitor rearrangement"
convert -size 32x32 xc:black -fill white -draw 'rectangle 16,0 31,31' \
    "$RUNTIME/split.png" || fail "could not create wallpaper image"
wdwrite WindowMaker WorkspaceSpecificBack \
    "((spixmap, \"$RUNTIME/split.png\", black), (spixmap, \"$RUNTIME/split.png\", black))" \
    || fail "could not set per-head test wallpaper"
msg -q reconfigure || fail "reconfigure rejected"
msg -q workspace 1 || fail "could not select updated workspace texture"
msg -q workspace 0 || fail "could not return to first workspace texture"
wait_for 50 background_in_range 0 20 || fail "single-head image was not rendered"
check "image is scaled to the original monitor" background_in_range 0 20
sleep 0.5
old_pixmap=$(root_pixmap)
[ -n "$old_pixmap" ] || fail "root wallpaper pixmap was not published"
xr --setmonitor left 512/135x768/203+0+0 default >>"$LOG" 2>&1 \
    || fail "could not create left monitor for wallpaper"
xr --setmonitor right 512/135x768/203+512+0 none >>"$LOG" 2>&1 \
    || fail "could not create right monitor for wallpaper"
xr --output default --primary >>"$LOG" 2>&1 || fail "could not notify monitor change"
wait_for 50 pixmap_changed "$old_pixmap" || fail "same-size layout did not refresh wallpaper"
check "same-size monitor layout refreshes wallpaper" pixmap_changed "$old_pixmap"
wait_for 50 background_in_range 235 255 || fail "image was not scaled to the new heads"
check "image is scaled separately to the new heads" background_in_range 235 255
check "monitor rearrangement preserved framebuffer size" desktop_geometry_is '1024, 768'
sleep 0.5
DISPLAY=$DPY stdbuf -oL xprop -spy -root _NET_WORKAREA _XROOTPMAP_ID \
    >"$RUNTIME/property-watch" 2>>"$LOG" &
XPROP_PID=$!
record_pid "$XPROP_PID" xprop "$DPY"
wait_for 50 property_count_at_least _NET_WORKAREA 1 || fatal "property watcher did not start"
wait_for 50 property_count_at_least _XROOTPMAP_ID 1 || fatal "pixmap watcher did not start"
old_pixmap=$(root_pixmap)
xr --noprimary >>"$LOG" 2>&1 || fail "could not change primary output"
wait_for 50 property_count_at_least _NET_WORKAREA 2 \
    || fatal "wmaker did not process the primary-only RandR change"
msg -q get_version >/dev/null || fatal "wmaker IPC barrier failed"
sleep 2
check "primary-only change does not rerender wallpaper" \
    sh -c '[ "$(grep -c "^_XROOTPMAP_ID(" "$1")" -eq 1 ] && [ "$2" = "$3" ]' \
    sh "$RUNTIME/property-watch" "$old_pixmap" "$(root_pixmap)"

note "leave externally managed wallpaper alone when backgrounds are disabled"
xr --delmonitor right >>"$LOG" 2>&1 || fail "could not remove right wallpaper monitor"
xr --delmonitor left >>"$LOG" 2>&1 || fail "could not remove left wallpaper monitor"
wdwrite WindowMaker WorkspaceSpecificBack '()' || fail "could not disable workspace backgrounds"
wdwrite WindowMaker WorkspaceBack '()' || fail "could not disable default background"
msg -q reconfigure || fail "reconfigure rejected"
wait_for 50 helper_stopped || fail "wallpaper helper did not exit"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DISPLAY=$DPY "$ROOT/util/.libs/wmsetbg" -p '(hgradient, "white", "black")' \
    >>"$LOG" 2>&1 || fail "could not set external wallpaper"
old_pixmap=$(root_pixmap)
[ -n "$old_pixmap" ] || fail "external wallpaper pixmap was not published"
xr --output default --mode 800x600 >>"$LOG" 2>&1 || fail "could not shrink for external wallpaper"
wait_for 50 desktop_geometry_is '800, 600' || fail "desktop geometry did not shrink"
sleep 0.5
check "disabled backgrounds do not replace external wallpaper" [ "$(root_pixmap)" = "$old_pixmap" ]
else
    note "SKIP: wallpaper refresh checks need xwd, convert, stdbuf and wdwrite"
fi

# --- teardown --------------------------------------------------------------
note "randr drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
