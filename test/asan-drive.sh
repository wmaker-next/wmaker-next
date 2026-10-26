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

# `make asan` runs this through the build system, so exit 77 (SKIP)
# rather than fail when a required X tool is absent
for _t in Xvfb Xephyr xdotool wmctrl xterm xdpyinfo xprop; do
    command -v "$_t" >/dev/null 2>&1 || {
        printf '%s not installed\n' "$_t" >&2
        exit 77
    }
done
unset _t

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${ASAN_DRIVE_DISPLAY:-:$(drive_default_display)}
LOG=/tmp/wmaker-asan.log
PIDFILE=/tmp/asan-drive.pids
# freshly built ASan-instrumented libWINGs/libWUtil/libwraster, not the
# installed copies in /lib, applied only to the wmaker process below
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
: > "$PIDFILE"
: > "$LOG"

# isolate everything we run from the real config root so the driver never
# touches the user's defaults, dock state, session or cached pixmaps
WMHOME=$(mktemp -d /tmp/asan-drive-wmhome.XXXXXX)
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
for seed in "$HOME/GNUstep/Defaults" \
            "${XDG_CONFIG_HOME:-$HOME/.config}/WindowMaker/Defaults"; do
    if [ -d "$seed" ]; then
        cp -Rp "$seed/." "$WMHOME/Defaults/" 2>/dev/null
        break
    fi
done
# the session may inherit the live desktop's whole wmaker environment
# (WMAKER_USER_XDG flips the defaults layout, WMAKER_IPC_SOCKET points at
# the real IPC socket) — drop every one of them so the drive and the tools
# it runs stay on the legacy layout of the isolated $WMHOME
unset WMAKER_USER_ROOT WMAKER_USER_XDG WMAKER_USER_DATA WMAKER_USER_CACHE \
      WMAKER_IPC_SOCKET WMAKER_BIN_NAME XDG_CONFIG_HOME XDG_DATA_HOME \
      XDG_CACHE_HOME
export WMAKER_USER_ROOT="$WMHOME"

cleanup() {
    while read -r p; do
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done < "$PIDFILE"
    rm -rf "$WMHOME"
}
trap cleanup EXIT

FAIL=0
note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
# print a '  -> ...' warning to stdout and the log and mark the run as
# failed without aborting, so later coverage and the clean-exit LSan
# report still happen and the final exit status is nonzero
warn() { printf '  -> %s\n' "$*" | tee -a "$LOG"; FAIL=1; }
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

# set TBX/TBY to a point on the titlebar of the given client window in
# root coordinates: xdotool getwindowgeometry --shell double-counts the
# frame offset for a reparented client, so take the absolute client
# origin from xwininfo and step half the _NET_FRAME_EXTENTS top into
# the titlebar
titlebar_of() {
    local w cx cy ft
    w=$1
    set -- $(DISPLAY=$DPY xwininfo -id "$w" \
        | awk '/Absolute upper-left X/{x=$NF} /Absolute upper-left Y/{y=$NF} END{print x,y}')
    cx=${1:-0}; cy=${2:-0}
    ft=$(DISPLAY=$DPY xprop -id "$w" _NET_FRAME_EXTENTS \
        | awk -F', *' '{print $3}')
    TBX=$((cx+60)); TBY=$((cy - ${ft:-20}/2 - 1))
}

# set RBX/RBY to a point on the middle resizebar segment of the given
# client window in root coordinates, same xwininfo + _NET_FRAME_EXTENTS
# approach as titlebar_of so the click lands inside the bottom extent
resizebar_of() {
    local w cx cy cw ch fb
    w=$1
    set -- $(DISPLAY=$DPY xwininfo -id "$w" | awk \
        '/Absolute upper-left X/{x=$NF} /Absolute upper-left Y/{y=$NF}
         /Width:/{w=$NF} /Height:/{h=$NF} END{print x,y,w,h}')
    cx=${1:-0}; cy=${2:-0}; cw=${3:-0}; ch=${4:-0}
    fb=$(DISPLAY=$DPY xprop -id "$w" _NET_FRAME_EXTENTS \
        | awk -F', *' '{print $4}')
    RBX=$((cx + cw/2)); RBY=$((cy + ch + ${fb:-8}/2))
}

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
    exit 77
fi
# Xephyr is a nested server: on the session display its host pointer
# tracks the live mouse and can override the XTEST warps the drive
# sends. Run it on a private headless parent whose virtual pointer
# only moves when Xephyr mirrors a warp.
PARENT_DISPLAY=${ASAN_DRIVE_PARENT_DISPLAY:-":$(( ${DPY#:} + 1 ))"}
Xvfb "$PARENT_DISPLAY" -screen 0 2400x1600x24 -ac >/dev/null 2>&1 &
XVFB_PARENT_PID=$!
echo "$XVFB_PARENT_PID" >> "$PIDFILE"
xvfb_parent_ready() {
    kill -0 "$XVFB_PARENT_PID" 2>/dev/null || { echo "parent Xvfb exited early" >&2; exit 1; }
    DISPLAY=$PARENT_DISPLAY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_parent_ready || { echo "parent Xvfb failed to start" >&2; exit 1; }

note "start Xephyr $DPY on parent $PARENT_DISPLAY"
# -noreset: like Xvfb, Xephyr resets when the last client disconnects and
# every property write and root background vanishes
Xephyr "$DPY" -display "$PARENT_DISPLAY" -screen 1280x800 -ac -noreset >/tmp/xephyr93.log 2>&1 &
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

# pick one managed client window to drive, xdotool search --class can
# return a dock appicon when the copied user defaults have xterm docked,
# so take the toplevel via wmctrl (which lists _NET_CLIENT_LIST only)
WIDX=$(DISPLAY=$DPY wmctrl -lx 2>/dev/null \
        | awk 'tolower($3) ~ /xterm/ { print $1; exit }')
WID=$( [ -n "$WIDX" ] && printf '%d' "$WIDX" \
        || xdo search --onlyvisible --class XTerm | head -n 1 )
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

# --- move/resize bumps the focus-stealing baseline (wNETWMBumpUserTime) -----
# wMouseMoveWindow()/wMouseResizeWindow() advance scr->net_last_user_time to
# the terminating ButtonRelease timestamp so a window that maps while the
# user is dragging cannot steal focus once the loop exits. The check lives
# here because it needs a real server timestamp on both sides of the
# comparison and this driver pushes no synthetic _NET_WM_USER_TIME values
# above the server clock, see ewmh-layout-drive.sh for the timestamp-faked
# exemption checks.
EWMH_CLIENT="$WMHOME/ewmh-client"
if ${CC:-cc} -std=c99 "$ROOT/test/ewmh-client.c" -o "$EWMH_CLIENT" -lX11 \
        >>"$LOG" 2>&1; then
    note "move/resize ButtonRelease bumps net_last_user_time"
    active_is() {
        DISPLAY=$DPY xprop -root _NET_ACTIVE_WINDOW 2>/dev/null \
            | grep -iF "$1" >/dev/null
    }
    has_demands_attention() {
        DISPLAY=$DPY xprop -id "$1" _NET_WM_STATE 2>/dev/null \
            | grep -F _NET_WM_STATE_DEMANDS_ATTENTION >/dev/null
    }
    map_baseline_client() {
        : >"$WMHOME/baseline.xid"
        DISPLAY=$DPY env EWMH_CLIENT_NAME="$1" EWMH_CLIENT_CLASS="$2" \
            EWMH_CLIENT_USER_TIME="$3" "$EWMH_CLIENT" \
            >"$WMHOME/baseline.xid" 2>>"$LOG" &
        BL_PID=$!; echo "$BL_PID" >> "$PIDFILE"
        wait_for 30 test -s "$WMHOME/baseline.xid" || return 1
        BL_XID=$(head -n 1 "$WMHOME/baseline.xid")
        case "$BL_XID" in 0x[0-9a-fA-F]*) return 0 ;; *) return 1 ;; esac
    }
    close_baseline_client() {
        DISPLAY=$DPY wmctrl -i -c "$BL_XID" 2>/dev/null
        wait_for 30 pid_gone "$BL_PID" || kill "$BL_PID" 2>/dev/null
    }
    titlebar_of "$WID"
    # capture the server clock now: every prior baseline source in this
    # driver is a real (past) server stamp so net_last_user_time <= this,
    # the same value becomes stale only once the drag below releases
    STALE_TS=$(DISPLAY=$DPY EWMH_CLIENT_PRINT_SERVER_TIME=1 "$EWMH_CLIENT" 2>>"$LOG")
    case "$STALE_TS" in
    [0-9]*)
        # positive control: stamped at the captured time the client is
        # fresh and must take focus, otherwise the denial below cannot
        # be attributed to the bump
        if map_baseline_client baseline-fresh AsanBaselineFresh "$STALE_TS"; then
            wait_for 20 active_is "$BL_XID" \
                || warn "baseline control: fresh-stamped client did not take focus"
            close_baseline_client
        fi
        # titlebar drag of $WID: ButtonRelease at the end of
        # wMouseMoveWindow() bumps the screen baseline strictly past
        # STALE_TS, two motion steps because the first only arms
        # started=1 and updateWindowPosition() runs from the second,
        # so the started branch is exercised as well as the bump
        xdo mousemove "$TBX" "$TBY"; sleep 0.1
        xdo mousedown 1; sleep 0.1
        xdo mousemove $((TBX+40)) $((TBY+20)); sleep 0.1
        xdo mousemove $((TBX+80)) $((TBY+40)); sleep 0.1
        xdo mouseup 1; sleep 0.2
        # same stamp is now stale: a distinct-class client carrying it
        # must be denied and flagged DEMANDS_ATTENTION (distinct class
        # so SharedAppIcon grouping cannot leak the same-app exemption)
        if map_baseline_client baseline-move-stale AsanBaselineMove "$STALE_TS"; then
            wait_for 20 has_demands_attention "$BL_XID" \
                || warn "move bump: denied client lacks _NET_WM_STATE_DEMANDS_ATTENTION"
            active_is "$BL_XID" \
                && warn "move bump: stale-stamped client stole focus after drag"
            close_baseline_client
        fi
        # second round for wMouseResizeWindow(): the move release just
        # advanced the baseline past STALE_TS so capture a fresh server
        # timestamp, drag the resizebar, and assert again
        STALE_TS=$(DISPLAY=$DPY EWMH_CLIENT_PRINT_SERVER_TIME=1 "$EWMH_CLIENT" 2>>"$LOG")
        resizebar_of "$WID"
        xdo mousemove "$RBX" "$RBY"; sleep 0.1
        xdo mousedown 1; sleep 0.1
        xdo mousemove "$RBX" $((RBY+20)); sleep 0.1
        xdo mousemove "$RBX" $((RBY+40)); sleep 0.1
        xdo mouseup 1; sleep 0.2
        if map_baseline_client baseline-resize-stale AsanBaselineResize "$STALE_TS"; then
            wait_for 20 has_demands_attention "$BL_XID" \
                || warn "resize bump: denied client lacks _NET_WM_STATE_DEMANDS_ATTENTION"
            active_is "$BL_XID" \
                && warn "resize bump: stale-stamped client stole focus after drag"
            close_baseline_client
        fi
        # restore $WID for the blocks below
        xdo windowmove "$WID" 100 100; sleep 0.2
        xdo windowsize "$WID" 400 300; sleep 0.2
        ;;
    *)
        warn "could not read server timestamp for baseline-bump check"
        ;;
    esac
fi

# --- titlebar interaction (framewin.c, actions.c) ---------------------------
note "titlebar double-click (shade), window menu"
titlebar_of "$WID"
xdo mousemove "$TBX" "$TBY"; sleep 0.1
xdo click --repeat 2 --delay 50 1; sleep 0.3             # shade
DISPLAY=$DPY xprop -id "$WID" _NET_WM_STATE 2>/dev/null \
    | grep -q _NET_WM_STATE_SHADED \
    || warn "titlebar double-click did not shade $WID"
xdo click --repeat 2 --delay 50 1; sleep 0.3             # unshade
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

# --- _NET_RESTACK_WINDOW / _NET_REQUEST_FRAME_EXTENTS (wmspec.c) ------------
# wmctrl does not send either message, use python-xlib if present
if command -v python3 >/dev/null 2>&1 && python3 -c 'import Xlib' 2>/dev/null; then
    note "EWMH restack + request-frame-extents"
    DISPLAY=$DPY python3 - <<'PYEOF' >>"$LOG" 2>&1 \
        || warn "restack/frame-extents check failed"
import sys, time
from Xlib import X, Xatom, display
from Xlib.protocol import event
d = display.Display(); scr = d.screen(); root = scr.root
RESTACK = d.intern_atom('_NET_RESTACK_WINDOW')
REQ_EXT = d.intern_atom('_NET_REQUEST_FRAME_EXTENTS')
EXTENTS = d.intern_atom('_NET_FRAME_EXTENTS')
STACK   = d.intern_atom('_NET_CLIENT_LIST_STACKING')
mask = X.SubstructureRedirectMask | X.SubstructureNotifyMask
def send(win, atom, data):
    root.send_event(event.ClientMessage(window=win, client_type=atom,
                                        data=(32, data)), event_mask=mask)
    d.flush(); time.sleep(0.2)
def stacking():
    p = root.get_full_property(STACK, Xatom.WINDOW)
    return list(p.value) if p else []
# pick both windows from the managed-client list, xdotool search --class
# can return a dock appicon or subwindow that wWindowFor() will not
# resolve, the topmost two stacking entries are guaranteed real clients
s = stacking()
if len(s) < 2:
    print('SKIP restack: fewer than two managed clients'); sys.exit(0)
w1, w2 = s[-1], s[-2]
def w1_above_w2():
    s = stacking()
    return s.index(w1) > s.index(w2) if w1 in s and w2 in s else None

# _NET_RESTACK_WINDOW: put w1 below w2, then back above, and verify
# _NET_CLIENT_LIST_STACKING tracks each step (regression cover for the
# previously-unsubscribed WMNResetStacking observer)
fail = 0
send(w1, RESTACK, [2, w2, 1, 0, 0])      # Below
if w1_above_w2():
    print('FAIL restack Below: w1 still above w2'); fail = 1
send(w1, RESTACK, [2, w2, 0, 0, 0])      # Above
if not w1_above_w2():
    print('FAIL restack Above: w1 not above w2'); fail = 1
send(w1, RESTACK, [2, 0, 99, 0, 0])      # hostile detail, must be refused
send(w1, RESTACK, [2, w1, 0, 0, 0])      # self as sibling, must be dropped
send(w1, RESTACK, [2, 0x7fffffff, 0, 0, 0])  # garbage sibling, must be dropped
send(w1, RESTACK, [2, 0, 0, 0, 0])       # raise without sibling

# _NET_REQUEST_FRAME_EXTENTS on a fresh unmapped window
uw = root.create_window(0, 0, 50, 50, 0, scr.root_depth,
                        X.InputOutput, X.CopyFromParent)
send(uw.id, REQ_EXT, [0, 0, 0, 0, 0])
ext = uw.get_full_property(EXTENTS, Xatom.CARDINAL)
if not ext or len(ext.value) != 4 or ext.value[2] == 0:
    print('FAIL request-frame-extents:', ext and list(ext.value)); fail = 1
else:
    print('frame-extents estimate', list(ext.value))
uw.destroy(); d.flush()
sys.exit(fail)
PYEOF
fi

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

# --- keymap reload (wXkbRefresh -> wReadKeybindings) ------------------------
# setxkbmap on the test display pushes XCB_XKB_MAP_NOTIFY plus a core
# MappingNotify, both routes land in refreshKeybindings() which reloads
# the xkbcommon keymap, re-derives the modifier masks and re-resolves
# every WShortKey, then Alt+Tab proves the regrabbed binding still fires
note "keymap reload"
DISPLAY=$DPY setxkbmap us 2>/dev/null; sleep 0.4
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.2
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

# --- show-desktop sweep with a client dying mid-animation -------------------
# wIconifyWindow() drains pending events after each animation, so a client
# that exits during the sweep is freed by wUnmanageWindow(); the snapshot
# must be re-resolved instead of dereferenced. ASan flags the stale read.
note "show-desktop sweep with a client destroyed mid-sweep"
if command -v wmctrl >/dev/null 2>&1; then
    ASAN_LOG_MARK=$(wc -l < "$LOG")
    SD_HEAD=$(xdo search --onlyvisible --pid "$XT2" 2>/dev/null | head -n 1)
    SD_TAIL=$(xdo search --onlyvisible --pid "$XT1" 2>/dev/null | head -n 1)
    if [ -n "$SD_HEAD" ] && [ -n "$SD_TAIL" ]; then
        # head-first the sweep is SD_HEAD then SD_TAIL, so killing SD_TAIL
        # just after the trigger lands its DestroyNotify in SD_HEAD's
        # animation drain, before the sweep reaches the dead snapshot entry
        xdo windowactivate "$SD_TAIL"; sleep 0.3
        xdo windowactivate "$SD_HEAD"; sleep 0.3
        DISPLAY=$DPY wmctrl -k on 2>/dev/null
        sleep 0.05
        kill "$XT1" 2>/dev/null
        sleep 2
        DISPLAY=$DPY wmctrl -k off 2>/dev/null
        sleep 0.5
        if tail -n +"$((ASAN_LOG_MARK + 1))" "$LOG" \
                | grep -qE 'ERROR: AddressSanitizer|runtime error:'; then
            warn "mid-sweep client destroy tripped a sanitizer report"
        fi
    else
        note "  could not resolve the xterm window ids, skipping the mid-sweep leg"
    fi
else
    note "  wmctrl not installed, skipping the mid-sweep destroy leg"
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
    "(\"printf %s \\\"\\\$DESKTOP_STARTUP_ID\\\" > $HC_MARK/sn-id; touch $HC_MARK/tl\", None, None, None)"
# four wdwrite calls land within one wall-clock second, the inotify
# reload now compares st_mtim at nanosecond resolution so all four are
# applied before this poll returns
hit_corner() {
    rm -f "$HC_MARK/tl"
    xdo mousemove 640 400; sleep 0.1
    xdo mousemove 1 1
    wait_for 10 test -f "$HC_MARK/tl"
}
wait_for 15 hit_corner || warn "hot corner over root did not fire"
DISPLAY=$DPY xterm -geometry 200x60+0+0 -e 'sleep 60' >/dev/null 2>&1 &
HC_XT=$!; echo "$HC_XT" >> "$PIDFILE"; sleep 0.5
hit_corner || warn "hot corner over window did not fire"
kill "$HC_XT" 2>/dev/null; sleep 0.3
# launcher half of startup-notification, ExecuteShellCommand() is the
# hot-corner action path so the child must inherit DESKTOP_STARTUP_ID,
# only meaningful when the action above actually ran
if [ -f "$HC_MARK/tl" ] \
   && grep -q '^#define HAVE_STARTUP_NOTIFICATION 1' "$ROOT/config.h" 2>/dev/null; then
    if [ -s "$HC_MARK/sn-id" ]; then
        case "$(cat "$HC_MARK/sn-id")" in
            wmaker/*_TIME*) note "startup-notification: launcher exported DESKTOP_STARTUP_ID" ;;
            *) warn "startup-notification: DESKTOP_STARTUP_ID present but malformed: $(cat "$HC_MARK/sn-id")" ;;
        esac
    else
        warn "startup-notification: launcher did not export DESKTOP_STARTUP_ID"
    fi
fi
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

# exec so $! is the tool, not a wrapper subshell.
util_env_exec() {
    exec env DISPLAY="$DPY" \
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

# Start a long-running util, then SIGTERM it so LSan sees the
# steady-state heap.
run_daemon() {
    local name=$1 settle=$2; shift 2
    note "  $name $*"
    util_env_exec "$ROOT/util/.libs/$name" "$@" >>"$LOG" 2>&1 &
    local dpid=$!
    echo "$dpid" >> "$PIDFILE"
    sleep "$settle"
    kill -TERM "$dpid" 2>/dev/null
    if wait_for 40 pid_gone "$dpid"; then
        return 0
    fi
    kill -9 "$dpid" 2>/dev/null
    printf '  -> %s did not exit on SIGTERM\n' "$name" | tee -a "$LOG"
    return 1
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
if [ -x "$ROOT/util/.libs/wmtouchd" ]; then
    printf 'begin 3\nupdate 3 80 0\nend 3\n' >"$WMHOME/asan-gestures"
    printf '{ ThreeFingerRight = "NextWorkspace"; }\n' >"$WMHOME/asan-wmtouch"
    run_util wmtouchd --source-events "$WMHOME/asan-gestures" \
        --config "$WMHOME/asan-wmtouch"
fi
if [ -x "$ROOT/util/.libs/wmclipd" ]; then
    run_daemon wmclipd 1 --clipboard --debug
fi
if [ -x "$ROOT/util/.libs/wmsoundd" ]; then
    run_daemon wmsoundd 2 -d "$DPY" --debug
fi

# --- resource realisation failures (resource_retry_default call sites) ------
# After startup scr->flags.startup is clear, so an invalid spec must warn
# and keep the previously realised resource instead of retrying the
# schema default, and never crash or poison the reload. Fonts are not
# exercisable this way any more: WMCreateFont() on Pango falls back
# internally and cannot fail on a bogus spec.
note "invalid specs keep previous resources (runtime reload)"
run_util wdwrite WindowMaker FTitleBack '(totally, bogus, spec)'
sleep 0.4
grep -q 'Error in texture specification for key "FTitleBack"' "$LOG" \
    || warn "garbage FTitleBack did not hit the texture failure path"
run_util wdwrite WindowMaker FTitleColor '"#zz1234"'
sleep 0.4
grep -q 'could not get color for key "FTitleColor"' "$LOG" \
    || warn "invalid FTitleColor did not hit the color failure path"
run_util wdwrite WindowMaker NormalCursor \
    '(bitmap, "asan-no-such.xbm", "asan-no-such.xbm")'
sleep 0.4
grep -q 'Error in cursor specification for key "NormalCursor"' "$LOG" \
    || warn "bad NormalCursor did not hit the cursor failure path"
kill -0 "$WMAKER_PID" 2>/dev/null \
    || warn "wmaker died after invalid resource specs"
# a valid style must still apply cleanly afterwards
run_util setstyle "$WMHOME/asan-style"

# --- GUI tools under ASan (wmagnify, wmiv, WPrefs) --------------------------
note "GUI tools"
run_gui "$ROOT/util/.libs/wmagnify" "Magnify"
xdo mousemove 400 300; sleep 0.3; xdo mousemove 500 400; sleep 0.3
close_gui wmagnify

run_gui "$ROOT/util/.libs/wmglyph" "wmglyph"
close_gui wmglyph

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

# asan-drive keeps abort_on_error=0 so its assertions still run, which
# means a report does not change the process status by itself. Treat any
# AddressSanitizer, LeakSanitizer or UBSan report as a failure here,
# including the LeakSanitizer output written after the clean exit above.
if grep -qE 'ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:' "$LOG"; then
    printf '\n=== sanitizer reports in %s ===\n' "$LOG"
    grep -nE 'ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:' "$LOG"
    FAIL=1
fi

note "done; log at $LOG"
exit $FAIL
