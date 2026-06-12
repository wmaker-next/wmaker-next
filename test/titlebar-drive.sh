#!/bin/sh
# Verify the configurable titlebar button layout: that a one-sided
# (Close,Miniaturize,Maximize)/() cluster places three button cores
# on the left and none on the right, that the Maximize glyph flips
# on maximize/unmaximize, that
# ToggleFullscreenKey round-trips and the post-fullscreen titlebar
# survives the wWindowConfigureBorders rebuild, that the Icon role
# draws and clears on _NET_WM_ICON / WM_HINTS removal, and that the
# per-role and surface-based dont-fit thresholds drop the right
# buttons when narrowed. Runs wmaker under Xephyr on an isolated
# display and WMAKER_USER_ROOT, tracks every PID it spawns and kills
# only those.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${TITLEBAR_DRIVE_DISPLAY:-:96}
W=1280
H=800
PIDFILE=$(mktemp /tmp/titlebar-drive.pids.XXXXXX)
WMHOME=$(mktemp -d /tmp/titlebar-drive-wmhome.XXXXXX)
SHOTDIR=$(mktemp -d /tmp/titlebar-drive-shots.XXXXXX)
LOG=/tmp/titlebar-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
ECLIENT="$WMHOME/ewmh-client"
: > "$PIDFILE"
: > "$LOG"

cleanup() {
    while read -r p; do
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done < "$PIDFILE"
    rm -f "$PIDFILE"
    rm -rf "$WMHOME" "$SHOTDIR"
}
trap cleanup EXIT

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
fail() { printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
skip() { printf 'SKIP: %s\n' "$*" | tee -a "$LOG"; exit 77; }
xdo()  { DISPLAY=$DPY xdotool "$@"; }

for tool in Xephyr xdotool xterm xprop xwininfo import; do
    command -v "$tool" >/dev/null 2>&1 || skip "$tool not found"
done
${CC:-cc} -std=c99 "$ROOT/test/ewmh-client.c" -o "$ECLIENT" -lX11 >>"$LOG" 2>&1 \
    || skip "could not build test/ewmh-client.c"

wait_for() {
    n=$1; shift
    while [ "$n" -gt 0 ]; do
        "$@" && return 0
        sleep 0.1
        n=$((n - 1))
    done
    return 1
}

unset WMAKER_IPC_SOCKET
export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"

write_defaults() {
    # stop any previous wmaker before truncating its domain file so
    # inotify cannot fire on a partial write and wedge the old process
    # mid-reload with signals blocked
    stop_clients
    cat >"$WMHOME/Defaults/WindowMaker" <<EOF
{
  NewStyle = $1;
  TitlebarLeft = $2;
  TitlebarRight = $3;
  ToggleFullscreenKey = "Mod1+F11";
  WindowTitleFont = "Sans:bold:pixelsize=12";
}
EOF
}

# --- Xephyr ----------------------------------------------------------------
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use, set TITLEBAR_DRIVE_DISPLAY to a free one" >&2
    exit 1
fi
note "start Xephyr $DPY"
Xephyr "$DPY" -screen ${W}x${H} -ac >>"$LOG" 2>&1 &
XEPHYR_PID=$!
echo "$XEPHYR_PID" >> "$PIDFILE"
xephyr_ready() {
    kill -0 "$XEPHYR_PID" 2>/dev/null || fail "Xephyr exited early"
    DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xephyr_ready || fail "Xephyr failed to start"

WMAKER_PID=
XTPID=
XTW=
THEIGHT=

stop_clients() {
    if [ -n "$XTPID" ]; then
        kill "$XTPID" 2>/dev/null
        wait "$XTPID" 2>/dev/null
        XTPID=
    fi
    if [ -n "$WMAKER_PID" ]; then
        kill "$WMAKER_PID" 2>/dev/null
        sn=0
        while kill -0 "$WMAKER_PID" 2>/dev/null && [ $sn -lt 30 ]; do
            sleep 0.1; sn=$((sn + 1))
        done
        kill -9 "$WMAKER_PID" 2>/dev/null
        wait "$WMAKER_PID" 2>/dev/null
        WMAKER_PID=
    fi
}

start_wmaker() {
    stop_clients
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    ASAN_OPTIONS="abort_on_error=0:halt_on_error=0:detect_leaks=0" \
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
}

start_xterm() {
    DISPLAY=$DPY xterm -geometry 80x24+100+100 -e 'sleep 600' >>"$LOG" 2>&1 &
    XTPID=$!
    echo "$XTPID" >> "$PIDFILE"
    XTW=
    ROOTW=$(DISPLAY=$DPY xwininfo -root 2>/dev/null \
                | awk '/Window id:/ {print $4; exit}')
    xterm_mapped() {
        # search --class also matches the wmaker app-icon (it carries
        # the client's WM_CLASS and is a direct child of root), pick
        # the reparented client so frame_top resolves the frame core
        # and xprop -id $XTW reaches the watched PropertyNotify path
        for w in $(DISPLAY=$DPY xdotool search --class XTerm 2>/dev/null); do
            wp=$(frame_of "$w")
            [ -n "$wp" ] && [ "$wp" != "$ROOTW" ] && [ "$wp" != "0x0" ] || continue
            XTW=$w
            return 0
        done
        return 1
    }
    wait_for 50 xterm_mapped || fail "xterm did not map"
    xdo windowactivate "$XTW"; sleep 0.4
}

frame_of() {
    DISPLAY=$DPY xwininfo -tree -id "$1" 2>/dev/null \
        | awk '/Parent window id:/ {print $4; exit}'
}

frame_top() {
    # walk parents until the next hop is root, return the child-of-root
    # frame core, works whether $1 is the client or already the frame
    # and never emits 0x0 (root's parent) which hangs xwininfo -id
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

capture_titlebar() {
    out=$1
    frame=$(frame_top "$XTW")
    [ -n "$frame" ] || fail "could not resolve frame for $XTW"
    set -- $(DISPLAY=$DPY xwininfo -id "$frame" 2>/dev/null \
                 | awk '/Absolute upper-left X:/ {x=$4}
                        /Absolute upper-left Y:/ {y=$4}
                        /Width:/ && !w {w=$2}
                        /Border width:/ {b=$3}
                        END {print x, y, w, b}')
    fx=$1; fy=$2; fw=$3; fb=$4
    # client absolute Y minus frame inside Y (frame Y + border) is
    # the client's relative Y inside the frame core, which is top_width
    cy=$(DISPLAY=$DPY xwininfo -id "$XTW" \
             | awk '/Absolute upper-left Y:/ {print $4; exit}')
    THEIGHT=$((cy - fy - fb))
    fx=$((fx + fb)); fy=$((fy + fb))
    [ "$THEIGHT" -gt 0 ] || fail "computed titlebar height $THEIGHT <= 0"
    DISPLAY=$DPY import -window root -crop "${fw}x${THEIGHT}+${fx}+${fy}" \
        +repage "png:$out" >>"$LOG" 2>&1 \
        || fail "import failed for $out"
}

count_button_children() {
    # count mapped square children sized (4, THEIGHT] anywhere in the
    # frame subtree on the given side, in TS_NEW the button cores are
    # THEIGHTxTHEIGHT children of the frame, in TS_OLD/TS_NEXT they
    # are (THEIGHT-7)/(THEIGHT-8) children of the titlebar core
    side=$1
    frame=$(frame_top "$XTW")
    set -- $(DISPLAY=$DPY xwininfo -id "$frame" \
                 | awk '/Absolute upper-left X:/ {x=$4}
                        /Width:/ && !w {w=$2}
                        /Border width:/ {b=$3}
                        END {print x+b, w}')
    cfx=$1; cfw=$2
    cn=0
    for cw in $(DISPLAY=$DPY xwininfo -tree -id "$frame" 2>/dev/null \
                    | awk -v th="$THEIGHT" '
                        /^ *0x/ {
                            g = $(NF-1)
                            if (split(g, a, /[x+]/) < 4) next
                            if (a[1]+0 != a[2]+0) next
                            if (a[1]+0 <= 4 || a[1]+0 > th+0) next
                            print $1
                        }'); do
        DISPLAY=$DPY xwininfo -id "$cw" 2>/dev/null \
            | grep -q 'Map State: IsViewable' || continue
        cax=$(DISPLAY=$DPY xwininfo -id "$cw" \
                  | awk '/Absolute upper-left X:/ {print $4; exit}')
        crel=$((cax - cfx))
        if [ "$side" = left ]; then
            [ "$crel" -lt "$((cfw / 2))" ] && cn=$((cn + 1))
        else
            [ "$crel" -ge "$((cfw / 2))" ] && cn=$((cn + 1))
        fi
    done
    printf '%s\n' "$cn"
}

button_mapped() {
    # is there a mapped button-sized child at x-offset $1 on the frame
    bx=$1
    frame=$(frame_top "$XTW")
    DISPLAY=$DPY xwininfo -children -id "$frame" 2>/dev/null \
        | awk -v th="$THEIGHT" -v want="$bx" '
            /^ *0x/ {
                g = $(NF-1)
                if (split(g, a, /[x+]/) < 4) next
                if (a[1] + 0 == th + 0 && a[2] + 0 == th + 0 && a[3] + 0 == want + 0)
                    print $1
            }' \
        | while read -r wid; do
            DISPLAY=$DPY xwininfo -id "$wid" 2>/dev/null \
                | grep -q 'Map State: IsViewable' && echo y && break
        done | grep -q y
}

# --- 1. one-sided (Close,Miniaturize,Maximize) / () at every NewStyle ------
for style in new old next; do
    note "macOS-style cluster NewStyle=$style"
    write_defaults "$style" '(Close, Miniaturize, Maximize)' '()'
    start_wmaker
    start_xterm
    capture_titlebar "$SHOTDIR/macos-$style.png"
    nl=$(count_button_children left)
    nr=$(count_button_children right)
    [ "$nl" = 3 ] || fail "[$style] expected 3 left buttons, got $nl"
    [ "$nr" = 0 ] || fail "[$style] expected 0 right buttons, got $nr"
    note "ok: [$style] 3 left / 0 right button cores"

    # centring clamp keeps a long title clear of the cluster, the
    # cluster region must not change between short and long title
    xdo set_window --name "$(head -c 200 </dev/zero | tr '\0' X)" "$XTW"
    sleep 0.3
    DISPLAY=$DPY import -window root -crop \
        "$((THEIGHT*3))x${THEIGHT}+${fx}+${fy}" \
        +repage "png:$SHOTDIR/cluster-$style-long.png" >>"$LOG" 2>&1
    xdo set_window --name "xterm" "$XTW"; sleep 0.3
    DISPLAY=$DPY import -window root -crop \
        "$((THEIGHT*3))x${THEIGHT}+${fx}+${fy}" \
        +repage "png:$SHOTDIR/cluster-$style-short.png" >>"$LOG" 2>&1
    cmp -s "$SHOTDIR/cluster-$style-long.png" \
           "$SHOTDIR/cluster-$style-short.png" \
        || fail "[$style] centred long title overlapped left cluster"
    note "ok: [$style] centring clamp"

    # dont-fit smoke, narrow drops at least one core, widen restores
    xdo windowsize "$XTW" $((2 * THEIGHT)) 100; sleep 0.4
    nl=$(count_button_children left)
    [ "$nl" -lt 3 ] || fail "[$style] expected <3 mapped at width=2*top_width, got $nl"
    xdo windowsize "$XTW" 600 200; sleep 0.4
    nl=$(count_button_children left)
    [ "$nl" = 3 ] || fail "[$style] expected 3 mapped after widen, got $nl"
    note "ok: [$style] dont-fit drops and restores"

    stop_clients
done

# section 2 onward exercises TS_NEW-specific button geometry
# (bsize == top_width, button cores are direct frame children at
# x = N*top_width), the per-style smoke above covers TS_OLD/TS_NEXT
write_defaults new '(Close, Miniaturize, Maximize)' '()'
start_wmaker
start_xterm
capture_titlebar "$SHOTDIR/macos.png"

# --- 2. Maximize glyph flips and fullscreen round-trip ----------------------
note "maximize glyph flip"
xdo windowactivate "$XTW"; sleep 0.4
frame=$(frame_top "$XTW")
fx=$(DISPLAY=$DPY xwininfo -id "$frame" | awk '/Absolute upper-left X:/ {print $4; exit}')
fy=$(DISPLAY=$DPY xwininfo -id "$frame" | awk '/Absolute upper-left Y:/ {print $4; exit}')
maxslot="${THEIGHT}x${THEIGHT}+$((fx + 2*THEIGHT))+${fy}"
DISPLAY=$DPY import -window root -crop "$maxslot" +repage "png:$SHOTDIR/max-pre.png"
xdo mousemove $((fx + 2*THEIGHT + THEIGHT/2)) $((fy + THEIGHT/2))
sleep 0.1
xdo mousedown 1
sleep 0.1
xdo mouseup 1
sleep 0.5
frame=$(frame_top "$XTW")
fx=$(DISPLAY=$DPY xwininfo -id "$frame" | awk '/Absolute upper-left X:/ {print $4; exit}')
fy=$(DISPLAY=$DPY xwininfo -id "$frame" | awk '/Absolute upper-left Y:/ {print $4; exit}')
maxslot="${THEIGHT}x${THEIGHT}+$((fx + 2*THEIGHT))+${fy}"
DISPLAY=$DPY import -window root -crop "$maxslot" +repage "png:$SHOTDIR/max-post.png"
cmp -s "$SHOTDIR/max-pre.png" "$SHOTDIR/max-post.png" \
    && fail "Maximize glyph did not change after click"
xdo mousemove $((fx + 2*THEIGHT + THEIGHT/2)) $((fy + THEIGHT/2))
sleep 0.1
xdo mousedown 1
sleep 0.1
xdo mouseup 1
sleep 0.5
note "ok: Maximize glyph flips"

note "fullscreen toggle round-trip"
capture_titlebar "$SHOTDIR/pre-fs.png"
xdo windowactivate "$XTW"; sleep 0.2
xdo key alt+F11; sleep 0.5
g=$(DISPLAY=$DPY xwininfo -id "$XTW" | awk '/Width:/ {w=$2} /Height:/ {h=$2} END {print w"x"h}')
[ "$g" = "${W}x${H}" ] || fail "fullscreen geometry $g != ${W}x${H}"
xdo key alt+F11; sleep 0.5
capture_titlebar "$SHOTDIR/post-fs.png"
cmp -s "$SHOTDIR/pre-fs.png" "$SHOTDIR/post-fs.png" \
    || fail "titlebar changed across fullscreen round-trip"
note "ok: fullscreen round-trip preserves titlebar"

# second pass: maximized-while-fullscreen leaves Restore glyph on exit
xdo key alt+F11; sleep 0.4
EWMH_CLIENT_STATE_TARGET=$XTW \
EWMH_CLIENT_STATE_ACTION=1 \
EWMH_CLIENT_STATE_ATOM1=_NET_WM_STATE_MAXIMIZED_VERT \
EWMH_CLIENT_STATE_ATOM2=_NET_WM_STATE_MAXIMIZED_HORZ \
DISPLAY=$DPY "$ECLIENT" >>"$LOG" 2>&1 \
    || fail "ewmh-client _NET_WM_STATE injection failed"
sleep 0.3
xdo key alt+F11; sleep 0.5
frame=$(frame_top "$XTW")
fx=$(DISPLAY=$DPY xwininfo -id "$frame" | awk '/Absolute upper-left X:/ {print $4; exit}')
fy=$(DISPLAY=$DPY xwininfo -id "$frame" | awk '/Absolute upper-left Y:/ {print $4; exit}')
maxslot="${THEIGHT}x${THEIGHT}+$((fx + 2*THEIGHT))+${fy}"
DISPLAY=$DPY import -window root -crop "$maxslot" +repage "png:$SHOTDIR/max-after-fs.png"
cmp -s "$SHOTDIR/max-pre.png" "$SHOTDIR/max-after-fs.png" \
    && fail "Maximize glyph not Restore after maximized-while-fullscreen"
note "ok: Restore glyph after _NET_WM_STATE while fullscreen"
stop_clients

# --- 3. Icon role draws and clears -----------------------------------------
note "Icon role"
write_defaults new '(Icon)' '(Miniaturize, Close)'
start_wmaker
start_xterm
capture_titlebar "$SHOTDIR/icon-with.png"
frame=$(frame_top "$XTW")
fx=$(DISPLAY=$DPY xwininfo -id "$frame" | awk '/Absolute upper-left X:/ {print $4; exit}')
fy=$(DISPLAY=$DPY xwininfo -id "$frame" | awk '/Absolute upper-left Y:/ {print $4; exit}')
isz=$((THEIGHT - 4))
iconslot="${isz}x${isz}+$((fx + 3))+$((fy + 2))"
# strip any client-supplied icon first so the bare reference is
# deterministic, then install a known _NET_WM_ICON via the helper so
# the draw/clear assertion does not depend on xterm build options
DISPLAY=$DPY xprop -id "$XTW" -remove _NET_WM_ICON 2>/dev/null
DISPLAY=$DPY xprop -id "$XTW" -remove WM_HINTS 2>/dev/null
sleep 0.4
DISPLAY=$DPY import -window root -crop "$iconslot" +repage "png:$SHOTDIR/iconslot-bare.png"
EWMH_CLIENT_SETICON_TARGET=$XTW DISPLAY=$DPY "$ECLIENT" >>"$LOG" 2>&1
sleep 0.4
DISPLAY=$DPY import -window root -crop "$iconslot" +repage "png:$SHOTDIR/iconslot.png"
cmp -s "$SHOTDIR/iconslot.png" "$SHOTDIR/iconslot-bare.png" \
    && fail "Icon role did not draw after _NET_WM_ICON set"
DISPLAY=$DPY xprop -id "$XTW" -remove _NET_WM_ICON 2>/dev/null
sleep 0.4
DISPLAY=$DPY import -window root -crop "$iconslot" +repage "png:$SHOTDIR/iconslot-after.png"
cmp -s "$SHOTDIR/iconslot-bare.png" "$SHOTDIR/iconslot-after.png" \
    || fail "icon region not cleared to bare after _NET_WM_ICON removal"
note "ok: icon region drawn and cleared"
stop_clients

# --- 4. dont-fit drop order -------------------------------------------------
note "dont-fit thresholds"
write_defaults new '(Close, Miniaturize, Maximize)' '()'
start_wmaker
start_xterm
capture_titlebar "$SHOTDIR/dontfit-pre.png"
xdo windowsize "$XTW" $((3 * THEIGHT)) 100; sleep 0.4
button_mapped 0 || fail "Close (k=2) unmapped at width=3*theight"
button_mapped "$THEIGHT" && fail "Miniaturize (k=4) still mapped at width=3*theight"
xdo windowsize "$XTW" 600 200; sleep 0.4
button_mapped 0 || fail "Close did not remap after widen"
button_mapped "$THEIGHT" || fail "Miniaturize did not remap after widen"
button_mapped $((2 * THEIGHT)) || fail "Maximize did not remap after widen"
note "ok: per-role dont-fit drop order"

note "icon surface-based dont-fit"
write_defaults new '(Icon, Miniaturize, Maximize)' '(Close)'
start_wmaker
start_xterm
capture_titlebar "$SHOTDIR/iconfit-pre.png"
DISPLAY=$DPY xprop -id "$XTW" -remove _NET_WM_ICON 2>/dev/null
DISPLAY=$DPY xprop -id "$XTW" -remove WM_HINTS 2>/dev/null
frame=$(frame_top "$XTW")
isz=$((THEIGHT - 4))
# at width = 4*theight all three button cores still fit (k=4 boundary),
# leaving theight of surface, the icon (theight-4) plus a theight of
# slack does not fit so it must be hidden even though
# width >= 4*top_width
xdo windowsize "$XTW" $((4 * THEIGHT)) 100; sleep 0.4
fx=$(DISPLAY=$DPY xwininfo -id "$frame" | awk '/Absolute upper-left X:/ {print $4; exit}')
fy=$(DISPLAY=$DPY xwininfo -id "$frame" | awk '/Absolute upper-left Y:/ {print $4; exit}')
narrowslot="${isz}x${isz}+$((fx + 2*THEIGHT + 3))+$((fy + 2))"
DISPLAY=$DPY import -window root -crop "$narrowslot" +repage \
    "png:$SHOTDIR/iconfit-narrow-bare.png"
# install the icon while already narrow, this is the SetIcon-without-
# resize path, updateIconFit() must keep dont_fit set so the slot
# stays identical to bare
EWMH_CLIENT_SETICON_TARGET=$XTW DISPLAY=$DPY "$ECLIENT" >>"$LOG" 2>&1
sleep 0.4
DISPLAY=$DPY import -window root -crop "$narrowslot" +repage \
    "png:$SHOTDIR/iconfit-narrow.png"
cmp -s "$SHOTDIR/iconfit-narrow-bare.png" "$SHOTDIR/iconfit-narrow.png" \
    || fail "icon drawn at width=4*top_width despite surface dont-fit"
xdo windowsize "$XTW" 600 200; sleep 0.4
fx=$(DISPLAY=$DPY xwininfo -id "$frame" | awk '/Absolute upper-left X:/ {print $4; exit}')
fy=$(DISPLAY=$DPY xwininfo -id "$frame" | awk '/Absolute upper-left Y:/ {print $4; exit}')
wideslot="${isz}x${isz}+$((fx + 2*THEIGHT + 3))+$((fy + 2))"
DISPLAY=$DPY import -window root -crop "$wideslot" +repage \
    "png:$SHOTDIR/iconfit-wide.png"
DISPLAY=$DPY xprop -id "$XTW" -remove _NET_WM_ICON 2>/dev/null
sleep 0.4
DISPLAY=$DPY import -window root -crop "$wideslot" +repage \
    "png:$SHOTDIR/iconfit-wide-bare.png"
cmp -s "$SHOTDIR/iconfit-wide.png" "$SHOTDIR/iconfit-wide-bare.png" \
    && fail "icon not drawn after widen (dont-fit not cleared)"
note "ok: surface-based icon dont-fit hides when narrow, draws when wide"
stop_clients

note "all checks passed"
exit 0
