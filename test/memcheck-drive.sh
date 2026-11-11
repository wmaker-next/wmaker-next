#!/bin/sh
# Run the just-built wmaker under Valgrind on a private headless X
# server, drive it through the menu / window-list / inspector / EWMH
# paths, then fail if Valgrind reports an unsuppressed memory error or
# leak. Process-lifetime allocations that cannot be released (fonts,
# the screen render context, fontconfig TLS) are listed in
# test/memcheck-suppressions.txt.
#
# Every process this starts is recorded in $PIDFILE and only those are
# killed. The fixed private display is refused if already in use, so a
# live :0 session is never touched.
#
# Written for POSIX sh + BSD userland: no bash arrays, no process
# substitution, no GNU-only flags. Exits 77 (SKIP) when valgrind or an
# X-drive tool is missing.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${MEMCHECK_DRIVE_DISPLAY:-:$(drive_default_display)}
LOG=${MEMCHECK_DRIVE_LOG:-/tmp/wmaker-memcheck.valgrind.log}
PIDFILE=/tmp/memcheck-drive.pids
SUPPRESSIONS="$ROOT/test/memcheck-suppressions.txt"
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"

skip() { printf '%s\n' "$*" >&2; exit 77; }
die()  { printf 'memcheck: %s\n' "$*" >&2; exit 1; }

command -v valgrind >/dev/null 2>&1 || skip "valgrind not installed"
command -v Xvfb     >/dev/null 2>&1 || skip "Xvfb not installed"
for t in xdotool wmctrl xterm xclock xmessage xdpyinfo; do
    command -v "$t" >/dev/null 2>&1 || skip "$t not installed"
done
[ -x "$ROOT/src/.libs/wmaker" ] || die "src/.libs/wmaker not built (run make first)"
[ -x "$WMAKERMSG" ]             || die "util/.libs/wmakermsg not built"
[ -f "$SUPPRESSIONS" ]          || die "missing $SUPPRESSIONS"

: > "$PIDFILE"
: > "$LOG"

# isolate config, caches and the IPC socket from the user's live session
WMHOME=$(mktemp -d /tmp/memcheck-drive-wmhome.XXXXXX)
RUNTIME=$(mktemp -d /tmp/memcheck-drive-rt.XXXXXX)
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"
printf '{\n}\n' > "$WMHOME/Defaults/WindowMaker"
unset WMAKER_USER_ROOT WMAKER_USER_XDG WMAKER_USER_DATA WMAKER_USER_CACHE \
      WMAKER_IPC_SOCKET WMAKER_BIN_NAME XDG_CONFIG_HOME XDG_DATA_HOME \
      XDG_CACHE_HOME

cleanup() {
    while read -r p; do
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done < "$PIDFILE"
    rm -rf "$WMHOME" "$RUNTIME"
}
trap cleanup EXIT INT TERM

x() { DISPLAY=$DPY "$@"; }
msg() {
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$WMAKERMSG" -s "$SOCKET" "$@"
}

if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY already in use, set MEMCHECK_DRIVE_DISPLAY" >&2; exit 77
fi

Xvfb "$DPY" -screen 0 1280x800x24 -ac -noreset >/dev/null 2>&1 &
XVFB_PID=$!; echo "$XVFB_PID" >> "$PIDFILE"
n=0
while [ "$n" -lt 50 ]; do
    x xdpyinfo >/dev/null 2>&1 && break
    sleep 0.1; n=$((n + 1))
done
x xdpyinfo >/dev/null 2>&1 || die "Xvfb failed to start"

LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
DISPLAY=$DPY XDG_RUNTIME_DIR="$RUNTIME" WMAKER_USER_ROOT="$WMHOME" \
valgrind --tool=memcheck \
    --leak-check=full \
    --show-leak-kinds=definite,indirect,possible \
    --errors-for-leak-kinds=definite,indirect,possible \
    --track-origins=yes --num-callers=30 \
    --error-exitcode=99 \
    --suppressions="$SUPPRESSIONS" \
    --log-file="$LOG" \
    "$ROOT/src/.libs/wmaker" --for-real >/dev/null 2>&1 &
WMPID=$!; echo "$WMPID" >> "$PIDFILE"

SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DISPLAY=$DPY XDG_RUNTIME_DIR="$RUNTIME" \
    "$WMAKERMSG" --get-socketpath 2>/dev/null)
case "$SOCKET" in
"$RUNTIME"/wmaker/ipc.*) ;;
*) die "resolved IPC socket '$SOCKET' is not under $RUNTIME" ;;
esac

ready() { msg -q get_version >/dev/null 2>&1; }
n=0
while [ "$n" -lt 600 ]; do ready && break; sleep 0.2; n=$((n + 1)); done
ready || die "wmaker under Valgrind did not answer get_version, see $LOG"

spawn() {
    DISPLAY=$DPY "$@" >/dev/null 2>&1 &
    echo $! >> "$PIDFILE"
}

# --- clients ----------------------------------------------------------------
spawn xterm -geometry 80x24+80+60   -T memcheck-a -e 'sleep 600'
spawn xterm -geometry 80x24+520+320 -T memcheck-b -e 'sleep 600'
spawn xclock -geometry 150x150+900+70
sleep 3

# --- focus / EWMH state churn ----------------------------------------------
x wmctrl -a memcheck-a 2>/dev/null; sleep 1
x wmctrl -r memcheck-a -b add,maximized_vert,maximized_horz 2>/dev/null; sleep 1
x wmctrl -r memcheck-a -b remove,maximized_vert,maximized_horz 2>/dev/null; sleep 1
x wmctrl -r memcheck-a -b add,shaded 2>/dev/null; sleep 1
x wmctrl -r memcheck-a -b remove,shaded 2>/dev/null; sleep 1

# --- root menu (F12), window list / switch panel (F11) ----------------------
x xdotool key F12 2>/dev/null; sleep 2
x xdotool key Down Down Down 2>/dev/null; sleep 1
x xdotool key Right 2>/dev/null; sleep 1
x xdotool key Escape 2>/dev/null; sleep 1
x xdotool key Escape 2>/dev/null; sleep 1
x xdotool key F11 2>/dev/null; sleep 2
x xdotool key Escape 2>/dev/null; sleep 1

# --- held Alt+Tab switch panel ---------------------------------------------
x xdotool keydown alt 2>/dev/null; sleep 0.5
x xdotool key Tab 2>/dev/null; sleep 1
x xdotool key Tab 2>/dev/null; sleep 1
x xdotool keyup alt 2>/dev/null; sleep 1

# --- window menu, cascade, and the Attributes inspector --------------------
x wmctrl -a memcheck-a 2>/dev/null; sleep 1
x xdotool key ctrl+Escape 2>/dev/null; sleep 2
n=0; while [ "$n" -lt 9 ]; do
    x xdotool key Down 2>/dev/null; sleep 0.3; n=$((n + 1))
done
x xdotool key Return 2>/dev/null; sleep 3
if msg get_windows 2>/dev/null | grep -q 'Inspecting'; then
    echo "  inspector opened"
else
    echo "  -> inspector did not open (coverage only)"
fi
# second window menu: open the Region snap cascade
x xdotool key ctrl+Escape 2>/dev/null; sleep 2
x xdotool key Down 2>/dev/null; sleep 1
x xdotool key Right 2>/dev/null; sleep 1
x xdotool key Down 2>/dev/null; sleep 1
x xdotool key Escape 2>/dev/null; sleep 1
x xdotool key Escape 2>/dev/null; sleep 1

# --- window ops via default bindings ---------------------------------------
x wmctrl -a memcheck-a 2>/dev/null; sleep 1
x xdotool key alt+h 2>/dev/null; sleep 1; msg -q action ShowAll >/dev/null 2>&1; sleep 1
x xdotool key alt+m 2>/dev/null; sleep 1
x xdotool key alt+Up 2>/dev/null; sleep 1
x xdotool key alt+Down 2>/dev/null; sleep 1
x xdotool key alt+2 2>/dev/null; sleep 1
x xdotool key alt+1 2>/dev/null; sleep 1

# --- show-desktop sweep with a client dying mid-animation -------------------
# wIconifyWindow() drains pending events after each animation, so a client
# that exits during the sweep is freed by wUnmanageWindow(); the snapshot
# must be re-resolved, not dereferenced (Valgrind flags the stale read).
x wmctrl -a memcheck-b 2>/dev/null; sleep 1
x wmctrl -a memcheck-a 2>/dev/null; sleep 1
x wmctrl -k on 2>/dev/null; sleep 0.05
x wmctrl -c memcheck-b 2>/dev/null
sleep 3
x wmctrl -k off 2>/dev/null; sleep 1

# --- dialog client + teardown ----------------------------------------------
spawn xmessage -geometry 400x150+300+200 "memcheck-dialog"
sleep 3
x wmctrl -c xmessage 2>/dev/null; sleep 1
x wmctrl -c memcheck-b 2>/dev/null; sleep 1

# --- clean exit so Valgrind writes its report ------------------------------
msg -q exit >/dev/null 2>&1
n=0
while [ "$n" -lt 300 ]; do
    kill -0 "$WMPID" 2>/dev/null || break
    sleep 0.2; n=$((n + 1))
done
wait "$WMPID" 2>/dev/null
rc=$?
kill "$WMPID" 2>/dev/null

# valgrind --error-exitcode=99, so 99 means at least one unsuppressed
# error or leak; anything but 0 and 99 means the run itself went wrong
echo "--- valgrind report ($LOG) ---"
grep -E 'ERROR SUMMARY|definitely lost:|indirectly lost:|possibly lost:' "$LOG" 2>/dev/null
case "$rc" in
0)  echo "memcheck: clean" ;;
99) echo "memcheck: unsuppressed errors/leaks found" ;;
*)  echo "memcheck: valgrind exited $rc" ;;
esac
[ "$rc" -eq 0 ]
