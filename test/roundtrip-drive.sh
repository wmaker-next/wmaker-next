#!/bin/sh
# Cold-boot reply-count regression check. Proxies the just-built wmaker
# through xtrace on a private Xvfb with no clients managed, counts the
# replies on the main connection, prints the histogram and asserts the
# total stays under ROUNDTRIP_LIMIT. The bulk of the count is the
# pipelined InternAtom and per-screen prefetch batches (one round-trip
# for many replies), the assertion catches a serial GetProperty or
# XSync sneaking back into the manage path.
#
# The keymap prefetches (src/wxkb.c and its WINGs mirror) resolve every
# keymap atom through one GetAtomName each. libxkbcommon's volume there
# scales with the server keymap and the library version, not with
# wmaker's own round-trip discipline, so those replies are excluded from
# the asserted count (they stay in the histogram and the printed total).
# libxkbcommon issues them while parsing the keymap it fetched, between
# its XKB GetControls and GetState requests, so that window identifies
# the replies to exclude; wmaker's own serial wXGetAtomName() calls stay
# in the asserted budget. The window depends on libxkbcommon's request
# order, not just its reply count, so a library change there can move
# replies into or out of the exclusion.
#
# In bench mode (BENCH=1) five clients are also managed and the
# per-window GetProperty and reply deltas are printed, no assertion.
#
# xtrace listens on a filesystem socket under /tmp/.X11-unix/ only, so
# on a host where that directory is a read-only mount the script
# re-execs itself inside a user+mount namespace with a private writable
# tmpfs over that path. Exits 77 (SKIP) when xtrace is not installed or
# the namespace overlay cannot be created.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
. "$ROOT/test/drive-common.sh"
REAL=${ROUNDTRIP_DRIVE_DISPLAY:-:$(drive_default_display)}
PROXY=${ROUNDTRIP_DRIVE_PROXY:-:$(drive_default_display 1)}
# Counted-reply baseline, re-measure and re-baseline on toolchain drift.
# xcb-screensaver adds one pipelined QueryExtension reply at startup.
# The checked pixmap and GC creations in RConvertImage and
# RConvertImageMask cost one barrier per converted image, the mask path
# batches its three checks onto a single barrier.
if grep -q '^#define USE_XCB_SCREENSAVER' "$ROOT/config.h" 2>/dev/null; then
    LIMIT=${ROUNDTRIP_LIMIT:-294}
else
    LIMIT=${ROUNDTRIP_LIMIT:-293}
fi
TRACE=/tmp/roundtrip-drive.trace
LOG=/tmp/roundtrip-drive.log
BENCH=${BENCH:-0}

command -v Xvfb   >/dev/null 2>&1 || { echo "SKIP: Xvfb not installed";   exit 77; }
command -v xtrace >/dev/null 2>&1 || { echo "SKIP: xtrace not installed"; exit 77; }
[ -x "$ROOT/src/.libs/wmaker" ] || { echo "FAIL: src/.libs/wmaker not built"; exit 1; }

# probe whether xtrace can bind a fake-display socket directly, if not
# re-exec inside a user+mount namespace with a writable overlay so the
# host /tmp/.X11-unix is untouched
if [ "${_ROUNDTRIP_NS:-}" != 1 ]; then
    if ! touch /tmp/.X11-unix/.roundtrip-probe 2>/dev/null; then
        command -v unshare >/dev/null 2>&1 \
            || { echo "SKIP: /tmp/.X11-unix is read-only and unshare is unavailable"; exit 77; }
        exec unshare --user --map-root-user --mount \
            env _ROUNDTRIP_NS=1 sh "$0" "$@"
    fi
    rm -f /tmp/.X11-unix/.roundtrip-probe
fi
if [ "${_ROUNDTRIP_NS:-}" = 1 ]; then
    mount -t tmpfs -o mode=1777 none /tmp/.X11-unix \
        || { echo "SKIP: cannot overlay /tmp/.X11-unix"; exit 77; }
fi

PIDFILE=$(mktemp /tmp/roundtrip-drive.pids.XXXXXX)
RT=$(mktemp -d /tmp/roundtrip-drive-rt.XXXXXX)
WMHOME="$RT/home"
: > "$PIDFILE"
: > "$LOG"

note() { printf '%s\n' "$*" | tee -a "$LOG"; }
fail() { printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; exit 1; }

cleanup() {
    while read -r p; do
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done < "$PIDFILE"
    rm -f "$PIDFILE"
    case "$RT" in /tmp/roundtrip-drive-rt.*) rm -rf "$RT";; esac
}
trap cleanup EXIT

wait_for() {
    wf_n=$1; shift
    while [ "$wf_n" -gt 0 ]; do
        "$@" && return 0
        sleep 0.1
        wf_n=$((wf_n - 1))
    done
    return 1
}

mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
printf '{\n}\n' > "$WMHOME/Defaults/WindowMaker"
printf '("Test", ("XTerm", EXEC, "xterm"))\n' > "$WMHOME/Defaults/WMRootMenu"

if DISPLAY=$REAL xdpyinfo >/dev/null 2>&1; then
    echo "display $REAL already in use, set ROUNDTRIP_DRIVE_DISPLAY" >&2
    exit 77
fi

Xvfb "$REAL" -screen 0 1280x800x24 -ac -nolisten tcp >>"$LOG" 2>&1 &
echo $! >> "$PIDFILE"
wait_for 40 sh -c "DISPLAY=$REAL xdpyinfo >/dev/null 2>&1" || fail "Xvfb failed to start"

# xtrace -o appends so truncate first
: > "$TRACE"
xtrace -n -k -d "$REAL" -D "$PROXY" -o "$TRACE" >>"$LOG" 2>&1 &
echo $! >> "$PIDFILE"
wait_for 40 test -S "/tmp/.X11-unix/X${PROXY#:}" || fail "xtrace failed to bind $PROXY"

LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
ASAN_OPTIONS="$(drive_asan_options)" \
DISPLAY=$PROXY \
XDG_RUNTIME_DIR="$RT" \
WMAKER_USER_ROOT="$WMHOME" \
"$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMPID=$!
echo "$WMPID" >> "$PIDFILE"

unset WMAKER_IPC_SOCKET
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS" DISPLAY=$PROXY XDG_RUNTIME_DIR="$RT" \
    "$WMAKERMSG" --get-socketpath)
ipc_ready() {
    LD_LIBRARY_PATH="$WMLIBS" "$WMAKERMSG" -s "$SOCKET" -q get_version >/dev/null 2>&1
}
wait_for 80 ipc_ready || fail "wmaker not ready, see $LOG"
sleep 0.3

# the main connection is the one that grabbed SubstructureRedirect on
# root, additional connections (xkbcommon keymap, IM probe) may exist
WMCONN=$(grep -m1 'SubstructureRedirect' "$TRACE" | sed -n 's/^\([0-9]\{3\}\):.*/\1/p')
[ -n "$WMCONN" ] || fail "could not identify the main connection in $TRACE"

reply_count() {
    grep -c "^$WMCONN:>:.*: Reply to " "$TRACE"
}
atomname_count() {
    grep -c "^$WMCONN:>:.*: Reply to GetAtomName:" "$TRACE"
}
# GetAtomName replies libxkbcommon asked for while parsing its keymap,
# i.e. those answered between its XKB GetControls and GetState requests
keymap_atomname_count() {
    awk -v conn="$WMCONN" '
        index($0, conn ":<:") == 1 \
            && index($0, "XKEYBOARD-Request(135,6): GetControls") > 0 { ink = 1; next }
        index($0, conn ":<:") == 1 \
            && index($0, "XKEYBOARD-Request(135,4): GetState") > 0 { ink = 0; next }
        ink && index($0, conn ":>:") == 1 \
            && index($0, ": Reply to GetAtomName:") > 0 { n++ }
        END { print n + 0 }
    ' "$TRACE"
}
getprop_count() {
    grep -c "^$WMCONN:<:.*: GetProperty " "$TRACE"
}

COLD_REPLIES=$(reply_count)
COLD_ATOMNAMES=$(atomname_count)
COLD_KEYMAP_ATOMNAMES=$(keymap_atomname_count)
COLD_COUNTED=$((COLD_REPLIES - COLD_KEYMAP_ATOMNAMES))
COLD_GETPROP=$(getprop_count)

note "cold-boot conn $WMCONN: replies=$COLD_REPLIES GetAtomName=$COLD_ATOMNAMES keymap=$COLD_KEYMAP_ATOMNAMES counted=$COLD_COUNTED GetProperty=$COLD_GETPROP (limit counted<=$LIMIT)"
note "--- reply histogram ---"
grep "^$WMCONN:>:.*: Reply to " "$TRACE" \
    | sed -E 's/.*Reply to ([A-Za-z0-9_:-]+).*/\1/' \
    | sort | uniq -c | sort -rn | tee -a "$LOG"

if [ "$BENCH" = 1 ]; then
    # per-managed-window cost: spawn five clients on the real display
    # so xtrace only records wmaker's side, then take the delta
    command -v xlogo >/dev/null 2>&1 || { note "(xlogo not installed, skipping per-window bench)"; exit 0; }
    for i in 1 2 3 4 5; do
        DISPLAY=$REAL xlogo -geometry 100x100+50+$((50 + i*110)) >>"$LOG" 2>&1 &
        echo $! >> "$PIDFILE"
        sleep 0.15
    done
    have_n_managed() {
        LD_LIBRARY_PATH="$WMLIBS" "$WMAKERMSG" -s "$SOCKET" get_windows 2>/dev/null \
            | grep -o 'class = XLogo' | wc -l | grep -qx "$1"
    }
    wait_for 80 have_n_managed 5 || note "(warn: did not see 5 managed clients)"
    sleep 0.4
    HOT_REPLIES=$(reply_count)
    HOT_GETPROP=$(getprop_count)
    note ""
    note "--- per-managed-window ---"
    note "5 clients: replies $COLD_REPLIES -> $HOT_REPLIES, GetProperty $COLD_GETPROP -> $HOT_GETPROP"
    note "per window: replies +$(( (HOT_REPLIES - COLD_REPLIES) / 5 )), GetProperty +$(( (HOT_GETPROP - COLD_GETPROP) / 5 ))"
    note "--- per-window GetProperty by atom (first client) ---"
    FIRST=$(grep -m1 "^$WMCONN:>:.*MapRequest" "$TRACE" | sed -n 's/.*window=\(0x[0-9a-f]*\).*/\1/p')
    if [ -n "$FIRST" ]; then
        grep "^$WMCONN:<:.*: GetProperty .*window=$FIRST " "$TRACE" \
            | grep -oE 'property=0x[0-9a-f]+\("[^"]+"\)' | sort | uniq -c | tee -a "$LOG"
    fi
    exit 0
fi

LD_LIBRARY_PATH="$WMLIBS" "$WMAKERMSG" -s "$SOCKET" -q exit >/dev/null 2>&1

if [ "$COLD_COUNTED" -gt "$LIMIT" ]; then
    fail "cold-boot counted reply total $COLD_COUNTED (replies $COLD_REPLIES minus keymap GetAtomName $COLD_KEYMAP_ATOMNAMES) exceeds $LIMIT, a serial round-trip has regressed"
fi
note "ok: counted $COLD_COUNTED <= $LIMIT"
exit 0
