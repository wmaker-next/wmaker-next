#!/bin/sh
# Drive the Accent Picker under an isolated Xvfb + wmaker.
# The held trigger uses the Menu key, as recommended in the defaults
# template. All spawned processes are tracked in a private PID file and
# never touch the live display.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${ACCENTPICKER_DRIVE_DISPLAY:-:$(drive_default_display)}
RUNTIME=$(mktemp -d /tmp/wmaker-accent-picker-test.XXXXXX)
WMHOME="$RUNTIME/home"
PIDFILE="$RUNTIME/pids"
LOG=/tmp/accent-picker-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKER="$ROOT/src/.libs/wmaker"
CLIENT="$RUNTIME/accent-picker-client"
OUT="$RUNTIME/out"
XVFB_PID=
WMAKER_PID=
CLIENT_PID=
CLIENT_WID=
BASE=0
PASSED=0
FAILED=0

: > "$PIDFILE"
: > "$LOG"
: > "$OUT"

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
    qa_desc=$1; shift
    if "$@"; then ok "$qa_desc"; else fail "$qa_desc"; fi
}

wait_for() {
    n=$1; shift
    while [ "$n" -gt 0 ]; do "$@" && return 0; sleep 0.1; n=$((n - 1)); done
    return 1
}
pid_gone() { ! kill -0 "$1" 2>/dev/null; }

xdo() { DISPLAY=$DPY xdotool "$@"; sleep 0.15; }
popup_id() { DISPLAY=$DPY xwininfo -root -tree 2>/dev/null | awk '/"accentPicker"/ { print $1; exit }'; }

for tool in Xvfb xdpyinfo xwininfo xdotool xprop gcc pkg-config; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$WMAKER" ] || skip_all "src/.libs/wmaker not built"
pkg-config --exists xcb xcb-xkb xkbcommon-x11 || skip_all "xcb/xkbcommon-x11 development files not installed"

${CC:-cc} -std=gnu11 "$ROOT/test/accent-picker-client.c" -o "$CLIENT" \
    $(pkg-config --cflags --libs xcb xcb-xkb xkbcommon-x11) >>"$LOG" 2>&1 \
    || skip_all "could not build test/accent-picker-client.c"

export WMAKER_USER_ROOT="$WMHOME"
unset WMAKER_USER_XDG
export XDG_RUNTIME_DIR="$RUNTIME"
unset WMAKER_IPC_SOCKET
mkdir -p "$WMHOME/Defaults" "$WMHOME/Library/WindowMaker"

write_domain() { # MODIFIER SETS KEY
    cat >"$WMHOME/Defaults/WindowMaker" <<DOMAIN
{
  DisableDock = YES;
  DisableClip = YES;
  DisableDrawers = YES;
  DisableSound = YES;
  AccentPickerKey = "$3";
  AccentPickerModifier = "$1";
  AccentPickerSets = $2;
}
DOMAIN
}

wm_ready() { DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q "window id"; }

start_xvfb() {
    if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
        fatal "display $DPY is already in use"
    fi
    note "start Xvfb $DPY"
    Xvfb "$DPY" -screen 0 1024x768x24 -ac >>"$LOG" 2>&1 &
    XVFB_PID=$!
    echo "$XVFB_PID" >> "$PIDFILE"
    wait_for 60 sh -c "kill -0 $XVFB_PID 2>/dev/null && DISPLAY=$DPY xdpyinfo >/dev/null 2>&1" \
        || fatal "Xvfb failed to start"
}

start_wmaker() {
    note "start wmaker"
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        ASAN_OPTIONS="$(drive_asan_options)" \
        DISPLAY=$DPY "$WMAKER" --for-real >>"$LOG" 2>&1 &
    WMAKER_PID=$!
    echo "$WMAKER_PID" >> "$PIDFILE"
    wait_for 100 wm_ready || fatal "wmaker did not start"
    sleep 0.4
}

stop_wmaker() {
    [ -n "${WMAKER_PID:-}" ] && kill "$WMAKER_PID" 2>/dev/null
    [ -n "${WMAKER_PID:-}" ] && wait_for 50 pid_gone "$WMAKER_PID"
    WMAKER_PID=
}

start_client() {
    : > "$OUT"
    DISPLAY=$DPY "$CLIENT" 60 >>"$OUT" 2>>"$LOG" &
    CLIENT_PID=$!
    echo "$CLIENT_PID" >> "$PIDFILE"
    wait_for 80 grep -qx ready "$OUT" || fatal "accent-picker client did not start"
    CLIENT_WID=$(DISPLAY=$DPY xdotool search --name '^AccentPickerClient$' 2>/dev/null | head -n 1)
    [ -n "$CLIENT_WID" ] || fatal "accent-picker client window not found"
    DISPLAY=$DPY xdotool windowactivate --sync "$CLIENT_WID" >/dev/null 2>&1
    DISPLAY=$DPY xdotool windowfocus --sync "$CLIENT_WID" >/dev/null 2>&1
    sleep 0.3
    BASE=$(wc -l < "$OUT")
}

stop_client() {
    [ -n "${CLIENT_PID:-}" ] && kill "$CLIENT_PID" 2>/dev/null
    [ -n "${CLIENT_PID:-}" ] && wait_for 40 pid_gone "$CLIENT_PID"
    CLIENT_PID=
}

case_begin() {
    BASE=$(wc -l < "$OUT")
}

new_lines() {
    tail -n +"$((BASE + 1))" "$OUT" 2>/dev/null | tr -d '\r' | sed '/^$/d'
}

expect_only() { # WANT
    qa_want=$1
    qa_n=25
    while [ "$qa_n" -gt 0 ]; do
        qa_got=$(new_lines)
        [ "$qa_got" = "$qa_want" ] && return 0
        # a stable non-empty value that differs is a leak, stop waiting
        if [ -n "$qa_got" ] && [ "$qa_n" -lt 18 ]; then
            break
        fi
        sleep 0.1
        qa_n=$((qa_n - 1))
    done
    qa_got=$(new_lines)
    if [ "$qa_got" != "$qa_want" ]; then
        printf 'want <%s> got <%s>\n' "$qa_want" "$qa_got" | tee -a "$LOG" >&2
        return 1
    fi
    return 0
}

expect_none() {
    sleep 0.6
    qa_got=$(new_lines)
    if [ -n "$qa_got" ]; then
        printf 'want <nothing> got <%s>\n' "$qa_got" | tee -a "$LOG" >&2
        return 1
    fi
    return 0
}

run_menu_cases() {
    note "held trigger: Menu key"
    case_begin
    xdo keydown Menu
    xdo key e
    xdo keyup Menu
    check "Menu+e commits the first variant" expect_only "é"

    case_begin
    xdo keydown Menu
    xdo key e
    xdo key e
    xdo keyup Menu
    check "tapping e again selects the next variant" expect_only "è"

    case_begin
    xdo keydown Menu
    xdo key e
    xdo key space
    xdo keyup Menu
    check "Space cycles the selected variant" expect_only "è"

    case_begin
    xdo keydown Menu
    xdo keydown e
    sleep 0.7
    xdo keyup e
    xdo keyup Menu
    check "holding e does not leak or cycle" expect_only "é"

    case_begin
    xdo keydown Menu
    xdo key s
    xdo keyup Menu
    check "a character present in the keymap uses the direct path" expect_only "$"

    case_begin
    xdo keydown Menu
    xdo key q
    xdo keyup Menu
    check "a base with no variants inserts the base" expect_only "q"

    case_begin
    xdo keydown Menu
    xdo keyup Menu
    check "Menu alone inserts nothing" expect_none

    case_begin
    xdo keydown Menu
    xdo key e
    xdo key Escape
    xdo keyup Menu
    check "Escape cancels the held trigger" expect_none

    case_begin
    xdo mousemove 10 10
    xdo keydown Menu
    xdo key e
    xdo click 1
    xdo keyup Menu
    xdo mousemove 512 384
    check "clicking outside the popup cancels" expect_none

    case_begin
    xdo keydown Menu
    xdo key e
    qa_popup=$(popup_id)
    if [ -n "$qa_popup" ]; then
        xdo mousemove --window "$qa_popup" 8 8
        xdo click 1
    fi
    xdo keyup Menu
    check "clicking a popup cell commits" expect_only "é"
}

run_modifier_cases() {
    note "modifier mode: Super"
    case_begin
    xdo keydown Super_L
    xdo key e
    xdo key space
    xdo keyup Super_L
    check "Super+e+Space commits the first variant" expect_only "é"

    case_begin
    xdo keydown Super_L
    xdo key e
    xdo key space
    xdo key space
    xdo keyup Super_L
    check "a second Space selects the second variant" expect_only "è"

    case_begin
    xdo keydown Super_L
    xdo keydown Shift_L
    xdo key e
    xdo keyup Shift_L
    xdo keyup Super_L
    check "Super+Shift+e uses uppercase variants" expect_only "É"

    case_begin
    xdo keydown Super_L
    xdo key q
    xdo keyup Super_L
    check "a base with no variants inserts the base" expect_only "q"

    case_begin
    xdo keydown Super_L
    xdo keydown e
    sleep 0.7
    xdo keyup e
    xdo keyup Super_L
    check "holding the base key never leaks e" expect_only "e"

    case_begin
    xdo keydown Super_L
    xdo key e
    xdo key Escape
    xdo keyup Super_L
    check "Escape cancels modifier mode" expect_none

    case_begin
    xdo mousemove 10 10
    xdo keydown Super_L
    xdo key e
    xdo click 1
    xdo keyup Super_L
    xdo mousemove 512 384
    check "clicking outside cancels modifier mode" expect_none

    case_begin
    xdo keydown Super_L
    xdo key e
    qa_popup=$(popup_id)
    if [ -n "$qa_popup" ]; then
        xdo mousemove --window "$qa_popup" 8 8
        xdo click 1
    fi
    xdo keyup Super_L
    check "clicking a popup cell commits in modifier mode" expect_only "é"
}

run_auto_cases() {
    command -v setxkbmap >/dev/null 2>&1 || {
        note "setxkbmap not installed, skipping auto-layout cases"
        return
    }
    note "auto expansion from the configured XKB layouts"
    DISPLAY=$DPY setxkbmap -layout us,fr >>"$LOG" 2>&1
    sleep 0.5
    if ! DISPLAY=$DPY setxkbmap -query 2>/dev/null | grep -q 'fr'; then
        note "X server did not apply the requested layouts, skipping auto cases"
        return
    fi
    case_begin
    xdo keydown Super_L
    xdo key e
    xdo key space
    xdo keyup Super_L
    check "auto with us,fr enables French" expect_only "é"

    DISPLAY=$DPY setxkbmap -layout us >>"$LOG" 2>&1
    sleep 0.5
    case_begin
    xdo keydown Super_L
    xdo key e
    xdo key space
    xdo keyup Super_L
    check "us-only auto still enables Currency" expect_only "€"

    note "keymap change while the picker is open cancels"
    case_begin
    xdo keydown Super_L
    xdo key e
    DISPLAY=$DPY setxkbmap -layout us,fr >>"$LOG" 2>&1
    sleep 0.4
    xdo keyup Super_L
    check "layout change cancels without inserting" expect_none
}

start_xvfb

# --- held trigger, the recommended Menu setup ----------------------------
write_domain None '("auto", "French", "Currency")' Menu
start_wmaker
start_client
run_menu_cases
stop_client
stop_wmaker

# --- modifier mode -------------------------------------------------------
write_domain Super '("auto", "French", "Currency")' None
start_wmaker
start_client
run_modifier_cases
stop_client

# --- auto layout expansion ----------------------------------------------
write_domain Super '("auto", "Currency")' None
stop_wmaker
start_wmaker
start_client
run_auto_cases
stop_client
stop_wmaker

note "result"
printf '%d passed, %d failed\n' "$PASSED" "$FAILED" | tee -a "$LOG"
[ "$FAILED" -eq 0 ]
