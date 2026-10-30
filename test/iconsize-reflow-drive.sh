#!/bin/sh
# Drive runtime IconSize reconfigure: wdwrite WindowMaker IconSize 96
# + reconfigure, assert the dock tile geometry changes from 64x64 to
# 96x96 via xwininfo on root children.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${ICONSIZE_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-iconsize-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/iconsize-reflow-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
WDWRITE="$ROOT/util/.libs/wdwrite"
SOCKET=
: > "$PIDFILE"
: > "$LOG"

PASSED=0
FAILED=0

cleanup() {
    while read -r p; do
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done < "$PIDFILE"
    rm -rf "$RUNTIME"
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
pid_gone() { ! kill -0 "$1" 2>/dev/null; }

msg() {
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$WMAKERMSG" -s "$SOCKET" "$@"
}
ipc_ready() { msg -q get_version >/dev/null 2>&1; }

wdwrite() {
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        DISPLAY=$DPY "$WDWRITE" "$@"
}

tile_count_by_size() {
    want_w=$1; want_h=$2
    DISPLAY=$DPY xwininfo -root -children 2>/dev/null \
        | awk -v ww="$want_w" -v wh="$want_h" '
            /^ *0x/ { winid=$1 }
            /[0-9]+x[0-9]+\+/ {
                for (i = 1; i <= NF; i++) {
                    if (match($i, /^[0-9]+x[0-9]+\+/)) {
                        geom = $i
                        gsub(/\+.*/, "", geom)
                        split(geom, dims, "x")
                        if (dims[1]+0 == ww && dims[2]+0 == wh) count++
                    }
                }
            }
            END { print count+0 }
        '
}

for tool in Xvfb xdpyinfo xprop xwininfo; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
[ -x "$WDWRITE" ] || skip_all "util/.libs/wdwrite not built"

export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
# Keep the dock enabled (default) so dock tiles exist to measure.
cat >"$WMHOME/Defaults/WindowMaker" <<'EOF'
{
  DisableClip = YES;
  DisableDrawers = YES;
}
EOF
cat >"$WMHOME/Defaults/WMState" <<'EOF'
{
  Workspaces = (
    { Name = "ws-0"; }
  );
  Dock = {
    Lowered = Yes;
    Contents = (
      {
        Name = "Logo.WMDock";
        Lock = No;
        AutoLaunch = No;
        Command = "#wprefs";
        Position = "0,0";
        Forced = No;
        Bounce = No;
      }
    );
  };
}
EOF

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use" >&2; exit 77
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 1024x768x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
wait_for 40 sh -c "kill -0 $XVFB_PID 2>/dev/null && DISPLAY=$DPY xdpyinfo >/dev/null 2>&1" \
    || fatal "Xvfb failed to start"

note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    ASAN_OPTIONS="$(drive_asan_options)" \
    DISPLAY=$DPY \
    "$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
wait_for 80 ipc_ready || fatal "wmaker IPC did not come up"
sleep 0.5

# --- 1. Baseline: dock tile is 64x64 (default IconSize) ---------------------
note "baseline: dock tile at default IconSize 64"
baseline_64=$(tile_count_by_size 64 64)
baseline_96=$(tile_count_by_size 96 96)
note "baseline 64x64 tiles: $baseline_64, 96x96 tiles: $baseline_96"
check "dock tile exists at 64x64 (default)" [ "$baseline_64" -ge 1 ]
check "no 96x96 tiles at baseline" [ "$baseline_96" -eq 0 ]

# --- 2. inotify reload without wmakermsg reconfigure -------------------------
# wdwrite only rewrites Defaults/WindowMaker, the inotify watcher must
# drive wDefaultsCheckDomains() -> wReadDefaults() -> REFRESH_ICON_TILE
# and reflow the dock on its own.
note "inotify reload: IconSize=96 without reconfigure"
wdwrite WindowMaker IconSize 96
inotify_96() { [ "$(tile_count_by_size 96 96)" -ge 1 ]; }
wait_for 50 inotify_96
check "inotify reload applies IconSize=96 without reconfigure" inotify_96
check "no 64x64 tiles after inotify reload" [ "$(tile_count_by_size 64 64)" -eq 0 ]

wdwrite WindowMaker IconSize 64
inotify_64() { [ "$(tile_count_by_size 64 64)" -ge 1 ]; }
wait_for 50 inotify_64
check "inotify reload restores IconSize=64" inotify_64

# --- 3. Reconfigure IconSize=96 ---------------------------------------------
note "reconfigure IconSize=96"
wdwrite WindowMaker IconSize 96
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.5

after_64=$(tile_count_by_size 64 64)
after_96=$(tile_count_by_size 96 96)
note "after reconfigure: 64x64 tiles: $after_64, 96x96 tiles: $after_96"
check "dock tile resized to 96x96 after reconfigure" [ "$after_96" -ge 1 ]
check "no 64x64 tiles remain after reconfigure" [ "$after_64" -eq 0 ]
check "wmaker alive after IconSize reconfigure" ipc_ready

# --- 4. Reconfigure back to 64 ----------------------------------------------
note "reconfigure IconSize back to 64"
wdwrite WindowMaker IconSize 64
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.5

back_64=$(tile_count_by_size 64 64)
back_96=$(tile_count_by_size 96 96)
note "after restore: 64x64 tiles: $back_64, 96x96 tiles: $back_96"
check "dock tile resized back to 64x64" [ "$back_64" -ge 1 ]
check "no 96x96 tiles after restore" [ "$back_96" -eq 0 ]
check "wmaker alive after IconSize restore" ipc_ready

# --- teardown ----------------------------------------------------------------
note "iconsize-reflow drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
