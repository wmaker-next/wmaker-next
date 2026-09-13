#!/bin/sh
# Drive MWM hints decoding beyond the decorations=0 case that
# moveres-drive.sh §3 covers: partial decoration masks asserted through
# _NET_FRAME_EXTENTS, and MWM_HINTS_FUNCTIONS bits asserted through
# _NET_WM_ALLOWED_ACTIONS.
#
# src/motif.c setupMWMHints() turns every decoration off when
# MWM_HINTS_DECORATIONS is present, then re-enables each part whose
# MWM_DECOR_* bit is set, so the frame extents show which parts
# survived: a missing titlebar drops the top extent, a missing
# resizebar drops the bottom extent, a missing border drops the side
# extents. MWM_HINTS_FUNCTIONS likewise sets no_closable/
# no_miniaturizable/no_resizable and re-enables the matching actions
# per MWM_FUNC_* bit, which _NET_WM_ALLOWED_ACTIONS reflects.
#
# Runs against an isolated WMAKER_USER_ROOT and tracks every PID it
# spawns, the live :0 wmaker is the same binary so no pkill.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DPY=${MWM_HINTS_DRIVE_DISPLAY:-:118}
RUNTIME=$(mktemp -d /tmp/wmaker-mwmhints-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/mwm-hints-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
SOCKET=
MOTH="$RUNTIME/mothints-client"
MOTH_PID=
MOTH_WID=
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
    mh_desc=$1; shift
    if "$@"; then ok "$mh_desc"; else fail "$mh_desc"; fi
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

extents_of() {
    DISPLAY=$DPY xprop -id "$1" _NET_FRAME_EXTENTS 2>/dev/null \
        | sed -n 's/.*_NET_FRAME_EXTENTS(CARDINAL) = //p' | tr ',' ' '
}
allowed_has() {
    DISPLAY=$DPY xprop -id "$1" _NET_WM_ALLOWED_ACTIONS 2>/dev/null \
        | grep -q "$2"
}
allowed_lacks() { ! allowed_has "$1" "$2"; }

moth_found() {
    MOTH_WID=$(DISPLAY=$DPY xdotool search --class "$moth_name" 2>/dev/null | tail -n 1)
    [ -n "$MOTH_WID" ]
}

moth_managed() {
    [ -n "$MOTH_WID" ] \
    && DISPLAY=$DPY xprop -id "$MOTH_WID" _NET_FRAME_EXTENTS 2>/dev/null | grep -q '=' \
    && DISPLAY=$DPY xprop -id "$MOTH_WID" _NET_WM_ALLOWED_ACTIONS 2>/dev/null | grep -q '='
}

# start_moth <name> <flags> <funcs> <decor>, sets MOTH_PID and MOTH_WID
start_moth() {
    moth_name=$1
    MOTH_WID=
    DISPLAY=$DPY env MOTH_NAME="$moth_name" MOTH_FLAGS="$2" \
        MOTH_FUNCS="$3" MOTH_DECOR="$4" \
        "$MOTH" >>"$LOG" 2>&1 &
    MOTH_PID=$!
    echo "$MOTH_PID" >> "$PIDFILE"
    wait_for 40 moth_found || return 1
    wait_for 40 moth_managed
}

stop_moth() {
    [ -n "$MOTH_PID" ] || return 0
    kill "$MOTH_PID" 2>/dev/null
    wait_for 30 pid_gone "$MOTH_PID" || kill -9 "$MOTH_PID" 2>/dev/null
    MOTH_PID=
    MOTH_WID=
}

# decor_case <name> <decor>, sets DC_L/DC_R/DC_T/DC_B
decor_case() {
    start_moth "$1" 2 0 "$2" || return 1
    set -- $(extents_of "$MOTH_WID")
    [ $# -eq 4 ] || return 1
    DC_L=$1
    DC_R=$2
    DC_T=$3
    DC_B=$4
}

# func_case <name> <functions>, sets MOTH_PID and MOTH_WID
func_case() {
    start_moth "$1" 1 "$2" 0
}

side_zero() { [ "$DC_L" = 0 ] && [ "$DC_R" = 0 ]; }
title_between() { [ "$DC_T" -gt 0 ] && [ "$DC_T" -lt "$BASE_T" ]; }
resize_between() { [ "$DC_B" -gt 0 ] && [ "$DC_B" -lt "$BASE_B" ]; }

for tool in Xvfb xdpyinfo xdotool xprop; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"

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
  FrameBorderWidth = 4;
}
EOF

${CC:-cc} -std=gnu11 "$ROOT/test/mothints-client.c" -o "$MOTH" -lX11 >>"$LOG" 2>&1 \
    || fatal "could not build test/mothints-client.c"

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use" >&2; exit 1
fi
note "start Xvfb $DPY"
Xvfb "$DPY" -screen 0 1024x768x24 -ac >>"$LOG" 2>&1 &
XVFB_PID=$!
echo "$XVFB_PID" >> "$PIDFILE"
wait_for 40 sh -c "kill -0 $XVFB_PID 2>/dev/null && DISPLAY=$DPY xdpyinfo >/dev/null 2>&1" \
    || fatal "Xvfb failed to start"

note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    ASAN_OPTIONS="abort_on_error=0:halt_on_error=0:detect_leaks=0" \
    DISPLAY=$DPY \
    "$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
wait_for 80 ipc_ready || fatal "wmaker IPC did not come up"
sleep 0.5

# --- 1. baseline decorations = MWM_DECOR_ALL --------------------------------
note "baseline: decorations = MWM_DECOR_ALL"
if decor_case base 1; then
    BASE_L=$DC_L; BASE_R=$DC_R; BASE_T=$DC_T; BASE_B=$DC_B
    note "baseline extents L=$BASE_L R=$BASE_R T=$BASE_T B=$BASE_B"
    check "baseline shows a titlebar (top extent > 0)" [ "$BASE_T" -gt 0 ]
    check "baseline shows a resizebar (bottom extent > 0)" [ "$BASE_B" -gt 0 ]
    check "baseline shows a side border (left extent > 0)" [ "$BASE_L" -gt 0 ]
    check "baseline side extents are symmetric" [ "$BASE_L" = "$BASE_R" ]
else
    fatal "baseline mothints client did not map/manage"
fi
stop_moth

# --- 2. partial decoration masks -------------------------------------------
note "no titlebar: decor = BORDER|RESIZEH"
if decor_case notitle 6; then
    check "no-titlebar keeps the side border" [ "$DC_L" = "$BASE_L" ]
    check "no-titlebar keeps the resizebar" [ "$DC_B" = "$BASE_B" ]
    check "no-titlebar drops the top extent below baseline" [ "$DC_T" -lt "$BASE_T" ]
else
    fail "no-titlebar client did not map/manage"
fi
stop_moth

note "no resizebar: decor = BORDER|TITLE"
if decor_case noresize 10; then
    check "no-resizebar keeps the side border" [ "$DC_L" = "$BASE_L" ]
    check "no-resizebar keeps the titlebar" [ "$DC_T" = "$BASE_T" ]
    check "no-resizebar drops the bottom extent below baseline" [ "$DC_B" -lt "$BASE_B" ]
else
    fail "no-resizebar client did not map/manage"
fi
stop_moth

note "no border: decor = TITLE|RESIZEH"
if decor_case noborder 12; then
    check "no-border drops both side extents to zero" side_zero
    check "no-border keeps a titlebar without its border share" title_between
    check "no-border keeps a resizebar without its border share" resize_between
else
    fail "no-border client did not map/manage"
fi
stop_moth

check "wmaker alive after the decoration matrix" ipc_ready

# --- 3. MWM_HINTS_FUNCTIONS bits -------------------------------------------
note "functions = MWM_FUNC_ALL"
if func_case fall 1; then
    check "FUNC_ALL allows close" allowed_has "$MOTH_WID" _NET_WM_ACTION_CLOSE
    check "FUNC_ALL allows minimize" allowed_has "$MOTH_WID" _NET_WM_ACTION_MINIMIZE
    check "FUNC_ALL allows resize" allowed_has "$MOTH_WID" _NET_WM_ACTION_RESIZE
    check "FUNC_ALL allows horizontal maximize" allowed_has "$MOTH_WID" _NET_WM_ACTION_MAXIMIZE_HORZ
    check "FUNC_ALL allows vertical maximize" allowed_has "$MOTH_WID" _NET_WM_ACTION_MAXIMIZE_VERT
else
    fail "FUNC_ALL client did not map/manage"
fi
stop_moth

note "functions = 0"
if func_case none 0; then
    check "empty functions hides close" allowed_lacks "$MOTH_WID" _NET_WM_ACTION_CLOSE
    check "empty functions hides minimize" allowed_lacks "$MOTH_WID" _NET_WM_ACTION_MINIMIZE
    check "empty functions hides resize" allowed_lacks "$MOTH_WID" _NET_WM_ACTION_RESIZE
    check "empty functions hides horizontal maximize" allowed_lacks "$MOTH_WID" _NET_WM_ACTION_MAXIMIZE_HORZ
    check "empty functions hides vertical maximize" allowed_lacks "$MOTH_WID" _NET_WM_ACTION_MAXIMIZE_VERT
    check "empty functions keeps move" allowed_has "$MOTH_WID" _NET_WM_ACTION_MOVE
else
    fail "empty-functions client did not map/manage"
fi
stop_moth

note "functions = MWM_FUNC_CLOSE"
if func_case close 32; then
    check "FUNC_CLOSE allows close" allowed_has "$MOTH_WID" _NET_WM_ACTION_CLOSE
    check "FUNC_CLOSE hides minimize" allowed_lacks "$MOTH_WID" _NET_WM_ACTION_MINIMIZE
    check "FUNC_CLOSE hides resize" allowed_lacks "$MOTH_WID" _NET_WM_ACTION_RESIZE
else
    fail "FUNC_CLOSE client did not map/manage"
fi
stop_moth

note "functions = MWM_FUNC_MINIMIZE"
if func_case mini 8; then
    check "FUNC_MINIMIZE allows minimize" allowed_has "$MOTH_WID" _NET_WM_ACTION_MINIMIZE
    check "FUNC_MINIMIZE hides close" allowed_lacks "$MOTH_WID" _NET_WM_ACTION_CLOSE
    check "FUNC_MINIMIZE hides resize" allowed_lacks "$MOTH_WID" _NET_WM_ACTION_RESIZE
else
    fail "FUNC_MINIMIZE client did not map/manage"
fi
stop_moth

note "functions = MWM_FUNC_RESIZE"
if func_case resize 2; then
    check "FUNC_RESIZE allows resize" allowed_has "$MOTH_WID" _NET_WM_ACTION_RESIZE
    check "FUNC_RESIZE allows horizontal maximize" allowed_has "$MOTH_WID" _NET_WM_ACTION_MAXIMIZE_HORZ
    check "FUNC_RESIZE allows vertical maximize" allowed_has "$MOTH_WID" _NET_WM_ACTION_MAXIMIZE_VERT
    check "FUNC_RESIZE hides close" allowed_lacks "$MOTH_WID" _NET_WM_ACTION_CLOSE
    check "FUNC_RESIZE hides minimize" allowed_lacks "$MOTH_WID" _NET_WM_ACTION_MINIMIZE
else
    fail "FUNC_RESIZE client did not map/manage"
fi
stop_moth

note "functions = MWM_FUNC_MAXIMIZE"
if func_case maxi 16; then
    check "FUNC_MAXIMIZE implies resize" allowed_has "$MOTH_WID" _NET_WM_ACTION_RESIZE
    check "FUNC_MAXIMIZE allows horizontal maximize" allowed_has "$MOTH_WID" _NET_WM_ACTION_MAXIMIZE_HORZ
    check "FUNC_MAXIMIZE allows vertical maximize" allowed_has "$MOTH_WID" _NET_WM_ACTION_MAXIMIZE_VERT
    check "FUNC_MAXIMIZE hides close" allowed_lacks "$MOTH_WID" _NET_WM_ACTION_CLOSE
    check "FUNC_MAXIMIZE hides minimize" allowed_lacks "$MOTH_WID" _NET_WM_ACTION_MINIMIZE
else
    fail "FUNC_MAXIMIZE client did not map/manage"
fi
stop_moth

check "wmaker alive after the function matrix" ipc_ready

# --- teardown ----------------------------------------------------------------
note "mwm-hints drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
