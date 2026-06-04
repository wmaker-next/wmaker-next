#!/bin/sh
# Drive wmaker under Xephyr with ASan/UBSan/LSan and exercise as many
# code paths as possible. Tracks every PID it spawns and kills only those.
#
# Expects an --enable-asan build. Run from anywhere.
#
# Written for POSIX sh + BSD userland: no bash arrays, no process
# substitution, no GNU-only flags. 'local', fractional 'sleep' and
# 'mktemp -d' are accepted on every target listed in section 16.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${ASAN_DRIVE_DISPLAY:-:93}
LOG=/tmp/wmaker-asan.log
PIDFILE=/tmp/asan-drive.pids
# freshly built ASan-instrumented libWINGs/libWUtil/libwraster, not the
# installed copies in /lib, applied only to the wmaker process below
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
: > "$PIDFILE"
: > "$LOG"

# isolate everything we run from the real ~/GNUstep so the driver never
# touches the user's defaults, dock state, session or cached pixmaps
WMHOME=$(mktemp -d /tmp/asan-drive-wmhome.XXXXXX)
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
if [ -d "$HOME/GNUstep/Defaults" ]; then
    cp -Rp "$HOME/GNUstep/Defaults/." "$WMHOME/Defaults/" 2>/dev/null
fi
export WMAKER_USER_ROOT="$WMHOME"

cleanup() {
    while read -r p; do
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done < "$PIDFILE"
    rm -rf "$WMHOME"
}
trap cleanup EXIT

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
# xdotool's search/activate probes _NET_WM_DESKTOP on every window and
# whines for override-redirect ones that do not carry it, drop just that
# (route stderr through a grep -v while leaving stdout untouched, no
# bash >() needed)
xdo()  {
    { DISPLAY=$DPY xdotool "$@" 2>&1 1>&3 \
        | grep -v '^XGetWindowProperty\[_NET_WM_DESKTOP\] failed' >&2; } 3>&1
}

# poll until "$@" succeeds or N tenths of a second pass
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

# dump the Xephyr root window as a PNG when WMAKER_SHOTS_DIR is set,
# ad-hoc debugging aid (e.g. `WMAKER_SHOTS_DIR=/tmp/shots ./asan-drive.sh`)
shot() {
    [ -n "${WMAKER_SHOTS_DIR:-}" ] || return 0
    command -v xwd >/dev/null 2>&1 && command -v convert >/dev/null 2>&1 || return 0
    mkdir -p "$WMAKER_SHOTS_DIR"
    sleep 0.3
    xwd -display "$DPY" -root -silent | convert xwd:- "png:$WMAKER_SHOTS_DIR/$1.png"
}

# launch a libWINGs GUI under ASan, wait for its window, optionally drive
# it with extra xdotool commands, then close it cleanly so LSan reports
run_gui() {
    local bin=$1 search=$2; shift 2
    note "  $bin $*"
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    ASAN_OPTIONS="detect_leaks=1:abort_on_error=0:halt_on_error=0:symbolize=1:fast_unwind_on_malloc=0" \
    UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0" \
    LSAN_OPTIONS="suppressions=$ROOT/test/lsan-suppressions.txt:print_suppressions=0" \
    WMAKER_BIN_NAME="$ROOT/src/.libs/wmaker" \
    DISPLAY=$DPY "$bin" "$@" >>"$LOG" 2>&1 &
    local gpid=$!
    echo "$gpid" >> "$PIDFILE"
    local gw="" n=40
    while [ "$n" -gt 0 ]; do
        gw=$(DISPLAY=$DPY xdotool search --name "$search" 2>/dev/null | head -n 1)
        [ -n "$gw" ] && break
        kill -0 "$gpid" 2>/dev/null || break
        sleep 0.1
        n=$((n - 1))
    done
    sleep 0.5
    GUI_WIN="$gw" GUI_PID="$gpid"
}

close_gui() {
    [ -n "$GUI_WIN" ] && DISPLAY=$DPY wmctrl -i -c "$GUI_WIN" 2>/dev/null
    wait_for 40 pid_gone "$GUI_PID"
    if kill -0 "$GUI_PID" 2>/dev/null; then
        kill -TERM "$GUI_PID" 2>/dev/null; sleep 0.5
        kill -0 "$GUI_PID" 2>/dev/null && kill -9 "$GUI_PID" 2>/dev/null
        printf '  -> %s did not exit on WM_DELETE\n' "$1" | tee -a "$LOG"
    fi
    sleep 0.3   # let wmaker process the DestroyNotify
}

# --- Xephyr -----------------------------------------------------------------
# refuse to attach to an existing display, otherwise Xephyr can fail and
# the readiness probe below would happily talk to whatever was already
# listening on $DPY
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use, set ASAN_DRIVE_DISPLAY to a free one" >&2
    exit 1
fi
note "start Xephyr $DPY"
Xephyr "$DPY" -screen 1280x800 -ac >/tmp/xephyr93.log 2>&1 &
XEPHYR_PID=$!
echo "$XEPHYR_PID" >> "$PIDFILE"
xephyr_ready() {
    kill -0 "$XEPHYR_PID" 2>/dev/null || { echo "Xephyr exited early" >&2; exit 1; }
    DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xephyr_ready || { echo "Xephyr failed to start"; exit 1; }

# --- pre-map clients so manageAllWindows / wManageWindow batch path runs ----
note "pre-map clients before wmaker starts"
for c in xlogo xclock xeyes; do
    DISPLAY=$DPY "$c" >/dev/null 2>&1 &
    echo $! >> "$PIDFILE"
done
sleep 0.5

# --- wmaker under ASan ------------------------------------------------------
note "start wmaker (ASan, --for-real, no monitor)"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
ASAN_OPTIONS="detect_leaks=1:abort_on_error=0:halt_on_error=0:fast_unwind_on_malloc=0:print_stacktrace=1:symbolize=1" \
UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0" \
LSAN_OPTIONS="print_suppressions=0" \
DISPLAY=$DPY \
"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
wm_ready() {
    DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null \
        | grep 'window id' >/dev/null
}
wait_for 120 wm_ready
note "wmaker pid=$WMAKER_PID, root props:"
DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK _NET_NUMBER_OF_DESKTOPS _NET_DESKTOP_NAMES 2>&1 | tee -a "$LOG"

# --- map more clients while wmaker is running (MapRequest path) -------------
note "map clients post-startup"
DISPLAY=$DPY xterm -geometry 80x24+50+50 -e 'sleep 300' >/dev/null 2>&1 &
XT1=$!; echo "$XT1" >> "$PIDFILE"
DISPLAY=$DPY xterm -geometry 80x24+400+200 -e 'sleep 300' >/dev/null 2>&1 &
XT2=$!; echo "$XT2" >> "$PIDFILE"
DISPLAY=$DPY xlogo -geometry 200x200+700+100 >/dev/null 2>&1 &
echo $! >> "$PIDFILE"
DISPLAY=$DPY xeyes -geometry 150x100+700+400 >/dev/null 2>&1 &
echo $! >> "$PIDFILE"
sleep 1

# pick one managed client window to drive
WID=$(xdo search --class XTerm | head -n 1)
note "driving window $WID"
shot clients-mapped

# --- focus / move / resize --------------------------------------------------
note "focus / move / resize"
xdo windowactivate "$WID"; sleep 0.2
xdo windowmove "$WID" 100 100; sleep 0.2
xdo windowmove "$WID" 300 250; sleep 0.2
xdo windowsize "$WID" 500 350; sleep 0.2
xdo windowsize "$WID" 700 450; sleep 0.2
xdo windowsize "$WID" 400 300; sleep 0.2
shot xterm-focused

# --- titlebar interaction (framewin.c, actions.c) ---------------------------
note "titlebar double-click (shade), drag, button menu"
# move pointer onto titlebar of WID (frame y is window y - titlebar height)
eval "$(xdo getwindowgeometry --shell "$WID")"
xdo mousemove $((X+40)) $((Y-10)); sleep 0.1
xdo click 1; sleep 0.05; xdo click 1; sleep 0.3          # shade
xdo click 1; sleep 0.05; xdo click 1; sleep 0.3          # unshade
xdo click 3; sleep 0.3; xdo key Escape; sleep 0.2        # window menu

# --- maximise / minimise via EWMH (wmspec.c handlers) ----------------------
note "EWMH state toggles"
DISPLAY=$DPY wmctrl -i -r "$WID" -b add,maximized_vert,maximized_horz 2>/dev/null; sleep 0.3
shot xterm-maximized
DISPLAY=$DPY wmctrl -i -r "$WID" -b remove,maximized_vert,maximized_horz 2>/dev/null; sleep 0.3
DISPLAY=$DPY wmctrl -i -r "$WID" -b add,fullscreen 2>/dev/null; sleep 0.3
DISPLAY=$DPY wmctrl -i -r "$WID" -b remove,fullscreen 2>/dev/null; sleep 0.3
DISPLAY=$DPY wmctrl -i -r "$WID" -b add,hidden 2>/dev/null; sleep 0.3
xdo windowactivate "$WID"; sleep 0.3

# --- workspace switching (workspace.c, wsmap.c) -----------------------------
note "workspace switch"
DISPLAY=$DPY wmctrl -s 1 2>/dev/null; sleep 0.3
DISPLAY=$DPY wmctrl -s 0 2>/dev/null; sleep 0.3
DISPLAY=$DPY wmctrl -i -r "$WID" -t 1 2>/dev/null; sleep 0.3
DISPLAY=$DPY wmctrl -s 1 2>/dev/null; sleep 0.3
DISPLAY=$DPY wmctrl -s 0 2>/dev/null; sleep 0.3
DISPLAY=$DPY wmctrl -i -r "$WID" -t 0 2>/dev/null; sleep 0.3

# --- root menu (rootmenu.c, menu.c) -----------------------------------------
note "root menu open/navigate/close"
xdo mousemove 640 500; sleep 0.1
xdo click 3; sleep 0.4
xdo key Down Down Down; sleep 0.2
xdo key Right; sleep 0.3
shot rootmenu-open
xdo key Escape; sleep 0.2
xdo key Escape; sleep 0.2
# window list menu (middle click on root)
xdo click 2; sleep 0.3
xdo key Escape; sleep 0.2

# --- switchpanel (Alt+Tab) --------------------------------------------------
note "switchpanel"
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.2; xdo key Tab; sleep 0.2; xdo key Tab; sleep 0.2
shot switchpanel
xdo keyup alt; sleep 0.3

# --- iconify / deiconify (icon.c) -------------------------------------------
note "iconify / deiconify"
xdo windowminimize "$WID"; sleep 0.4
xdo windowactivate "$WID"; sleep 0.3

# --- property churn on a client (client.c wClientCheckProperty) -------------
note "property churn"
DISPLAY=$DPY xprop -id "$WID" -f _NET_WM_NAME 8u -set _NET_WM_NAME "renamed-by-drive"; sleep 0.2
DISPLAY=$DPY xprop -id "$WID" -remove _NET_WM_NAME; sleep 0.2
DISPLAY=$DPY xprop -id "$WID" -f WM_NAME 8s -set WM_NAME "renamed-again"; sleep 0.2

# --- late-set _NET_WM_ICON (updateIconImage main_window_desc propagation) ---
# Electron/Chromium clients set _NET_WM_ICON after mapping, then change it.
# This exercises the release-old/retain-new pair in updateIconImage() across
# move/resize/miniaturize so a refcount mistake shows up under ASan.
if command -v python3 >/dev/null 2>&1 && python3 -c 'import Xlib' 2>/dev/null; then
    note "late-set _NET_WM_ICON"
    DISPLAY=$DPY python3 - >/tmp/icon-ids.txt 2>&1 <<'PYEOF' &
import sys, time
from Xlib import X, display, Xatom
d = display.Display(); scr = d.screen(); root = scr.root
NET_WM_ICON = d.intern_atom('_NET_WM_ICON')
leader = root.create_window(0, 0, 1, 1, 0, scr.root_depth, X.InputOutput, X.CopyFromParent)
leader.set_wm_class("lateicon", "LateIcon")
leader.set_wm_hints(flags=64, window_group=leader.id)
vis = root.create_window(150, 150, 320, 240, 1, scr.root_depth, X.InputOutput, X.CopyFromParent,
                         background_pixel=scr.white_pixel)
vis.set_wm_name("LateIcon"); vis.set_wm_class("lateicon", "LateIcon")
vis.set_wm_hints(flags=65, input=1, window_group=leader.id)
vis.change_property(d.intern_atom('WM_CLIENT_LEADER'), Xatom.WINDOW, 32, [leader.id])
vis.map(); d.flush(); time.sleep(0.4)
vis.change_property(NET_WM_ICON, Xatom.CARDINAL, 32, [32,32]+[0xFFFF0000]*1024); d.flush()
time.sleep(0.3)
vis.change_property(NET_WM_ICON, Xatom.CARDINAL, 32, [32,32]+[0xFF00FF00]*1024); d.flush()
print(hex(vis.id)); sys.stdout.flush()
time.sleep(30)
PYEOF
    LIPID=$!; echo "$LIPID" >> "$PIDFILE"
    sleep 1.2
    LIW=$(cat /tmp/icon-ids.txt 2>/dev/null)
    if [ -n "$LIW" ]; then
        xdo windowmove "$LIW" 300 250; sleep 0.2
        xdo windowsize "$LIW" 420 300; sleep 0.2
        xdo windowminimize "$LIW"; sleep 0.3
        xdo windowactivate "$LIW"; sleep 0.3
    fi
    kill "$LIPID" 2>/dev/null; sleep 0.3
    rm -f /tmp/icon-ids.txt
fi

# --- kill clients while managed (wUnmanageWindow path) ----------------------
note "client deaths"
kill "$XT2" 2>/dev/null; sleep 0.3
for p in $(xdo search --class xeyes); do
    DISPLAY=$DPY wmctrl -i -c "$p" 2>/dev/null
done
sleep 0.5

# --- defaults reload (wDefaultsCheckDomains) --------------------------------
note "touch isolated user domain to exercise the reload swap"
touch "$WMHOME/Defaults/WindowMaker"
sleep 1.2

# --- _WINDOWMAKER_COMMAND (event.c receiver) --------------------------------
note "Reconfigure via _WINDOWMAKER_COMMAND"
DISPLAY=$DPY xdotool key super+shift+r 2>/dev/null
sleep 0.5

# --- dock interaction (if present) ------------------------------------------
note "dock click"
xdo mousemove 1250 30; sleep 0.1
xdo click 3; sleep 0.3; xdo key Escape; sleep 0.2

# --- hot corners (hotcorner.c InputOnly windows + stacking hooks) -----------
note "hot corners over root and over a covering window"
HC_MARK=$(mktemp -d /tmp/asan-drive-hc.XXXXXX)
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" DISPLAY=$DPY \
    "$ROOT/util/.libs/wdwrite" WindowMaker HotCorners YES
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" DISPLAY=$DPY \
    "$ROOT/util/.libs/wdwrite" WindowMaker HotCornerDelay 100
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" DISPLAY=$DPY \
    "$ROOT/util/.libs/wdwrite" WindowMaker HotCornerEdge 10
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" DISPLAY=$DPY \
    "$ROOT/util/.libs/wdwrite" WindowMaker HotCornerActions \
    "(\"touch $HC_MARK/tl\", None, None, None)"
sleep 1.2   # wait for the defaults reload to apply
xdo mousemove 640 400; sleep 0.1
xdo mousemove 1 1; sleep 0.3
[ -f "$HC_MARK/tl" ] || printf '  -> hot corner over root did not fire\n' | tee -a "$LOG"
rm -f "$HC_MARK/tl"
DISPLAY=$DPY xterm -geometry 200x60+0+0 -e 'sleep 60' >/dev/null 2>&1 &
HC_XT=$!; echo "$HC_XT" >> "$PIDFILE"; sleep 0.5
xdo mousemove 640 400; sleep 0.1
xdo mousemove 1 1; sleep 0.3
[ -f "$HC_MARK/tl" ] || printf '  -> hot corner over window did not fire\n' | tee -a "$LOG"
kill "$HC_XT" 2>/dev/null; sleep 0.3
rm -rf "$HC_MARK"

# --- util/ tools under ASan -------------------------------------------------
# These do not carry the built-in __lsan_default_suppressions(), so pass
# the file-based suppressions for third-party noise.
note "util/ tools"
util_env() {
    env DISPLAY="$DPY" \
        LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        ASAN_OPTIONS="detect_leaks=1:abort_on_error=0:halt_on_error=0:symbolize=1" \
        UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0" \
        LSAN_OPTIONS="suppressions=$ROOT/test/lsan-suppressions.txt:print_suppressions=0" \
        "$@"
}

run_util() {
    local name=$1; shift
    note "  $name $*"
    util_env "$ROOT/util/.libs/$name" "$@" >>"$LOG" 2>&1
    local rc=$?
    [ "$rc" -eq 0 ] || printf '  -> exit %d\n' "$rc" | tee -a "$LOG"
    return "$rc"
}

# all of these run with WMAKER_USER_ROOT pointing at the isolated copy
# above, so wdwrite/setstyle/seticons can be exercised safely
run_util wdread WindowMaker WorkspaceBack
run_util wdwrite WindowMaker AsanDriveProbe '"ok"'
run_util getstyle -t "$WMHOME/asan-style"
run_util setstyle "$WMHOME/asan-style"
# minimal style without IconTitleBack/ResizebarBack so hackStyle()'s
# getColor() fallback runs against a solid texture (borrowed-return path)
cat >"$WMHOME/asan-style-min" <<'EOS'
{
  FTitleBack = (solid, "#112233");
  UTitleBack = (solid, "#445566");
}
EOS
run_util setstyle "$WMHOME/asan-style-min"
run_util geticonset "$WMHOME/asan-icons"
run_util seticons "$WMHOME/asan-icons"
run_util wmgenmenu
run_util wmsetbg -p '(solid, "#404040")'
run_util convertfonts "$WMHOME/asan-style"

# --- GUI tools under ASan (wmagnify, wmiv, WPrefs) --------------------------
note "GUI tools"
run_gui "$ROOT/util/.libs/wmagnify" "Magnify"
xdo mousemove 400 300; sleep 0.3; xdo mousemove 500 400; sleep 0.3
close_gui wmagnify

run_gui "$ROOT/util/.libs/wmiv" "wmiv" "$ROOT/wrlib/Tests/test.png"
[ -n "$GUI_WIN" ] && xdo key --window "$GUI_WIN" Right Left plus minus 2>/dev/null
sleep 0.3
[ -n "$GUI_WIN" ] && xdo key --window "$GUI_WIN" q 2>/dev/null
wait_for 30 pid_gone "$GUI_PID"
kill -0 "$GUI_PID" 2>/dev/null && close_gui wmiv

run_gui "$ROOT/WPrefs.app/.libs/WPrefs" "Preferences"
if [ -n "$GUI_WIN" ]; then
    xdo windowactivate "$GUI_WIN"; sleep 0.3
    shot wprefs-open
    # click through several section icons along the top toolbar
    for x in 60 110 160 210 260 310 360; do
        xdo mousemove --window "$GUI_WIN" "$x" 50 click 1; sleep 0.4
    done
fi
close_gui WPrefs

# --- clean exit so LSan runs ------------------------------------------------
note "request clean exit"
# SIGTERM goes through handleExitSig -> Shutdown(WSExitMode) -> Exit(),
# so LSan sees the steady-state heap.
kill -TERM "$WMAKER_PID"
wait_for 100 pid_gone "$WMAKER_PID"
kill -0 "$WMAKER_PID" 2>/dev/null && { note "wmaker still up, SIGKILL"; kill -9 "$WMAKER_PID"; }

note "done; log at $LOG"
