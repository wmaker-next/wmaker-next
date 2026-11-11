#!/bin/sh
# Drive the window switch panel: flat cycle (Alt+Tab), app-group cycle
# (Mod4+Tab), intra-app cycle (Mod4+grave), letter-key lookup by WM_CLASS
# initial, Escape cancel, skip_switchpanel exclusion via
# _NET_WM_STATE_SKIP_TASKBAR, SwitchPanelIconSize runtime reconfigure,
# and the SwitcherOrder runtime flip (Focus/MRU vs Age/creation).
#
# Harness conventions follow tabgroup-drive.sh: Xephyr, isolated
# WMAKER_USER_ROOT and XDG_RUNTIME_DIR, PID tracking, no pkill.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/test/drive-common.sh"
DPY=${SWITCHPANEL_DRIVE_DISPLAY:-:$(drive_default_display)}
W=1280
H=800
PIDFILE=$(mktemp /tmp/switchpanel-drive.pids.XXXXXX)
WMHOME=$(mktemp -d /tmp/switchpanel-drive-wmhome.XXXXXX)
RUNTIME=$(mktemp -d /tmp/switchpanel-drive-run.XXXXXX)
LOG=/tmp/switchpanel-drive.log
WMLIBS="$ROOT/WINGs/.libs:$ROOT/wrlib/.libs"
WMAKERMSG="$ROOT/util/.libs/wmakermsg"
WDWRITE="$ROOT/util/.libs/wdwrite"
SOCKET=
: > "$PIDFILE"
: > "$LOG"

PASSED=0
FAILED=0

cleanup() {
    # release any cycling modifier an aborted test left logically held:
    # XTEST presses outlive the client that sent them and would pin
    # StartWindozeCycle in its event wait with the panel still mapped
    DISPLAY=$DPY xdotool keyup alt 2>/dev/null
    DISPLAY=$DPY xdotool keyup super 2>/dev/null
    while read -r p; do
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done < "$PIDFILE"
    rm -f "$PIDFILE"
    rm -rf "$WMHOME" "$RUNTIME"
}
trap cleanup EXIT

note() { printf '\n=== %s ===\n' "$*" | tee -a "$LOG"; }
ok()   { PASSED=$((PASSED + 1)); printf 'PASS: %s\n' "$*" | tee -a "$LOG"; }
fail() { FAILED=$((FAILED + 1)); printf 'FAIL: %s\n' "$*" | tee -a "$LOG"; }
fatal() { printf 'FATAL: %s\n' "$*" | tee -a "$LOG"; exit 1; }
skip_all() { printf 'SKIP: %s\n' "$*" | tee -a "$LOG"; exit 77; }

check() {
    cd_desc=$1
    shift
    if "$@"; then
        ok "$cd_desc"
    else
        fail "$cd_desc"
    fi
}

wait_for() {
    n=$1; shift
    while [ "$n" -gt 0 ]; do
        "$@" && return 0
        sleep 0.1
        n=$((n - 1))
    done
    return 1
}
pid_gone() { ! kill -0 "$1" 2>/dev/null; }

xdo() { DISPLAY=$DPY xdotool "$@"; }

msg() {
    LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$WMAKERMSG" -s "$SOCKET" "$@"
}
ipc_ready() { msg -q get_version >/dev/null 2>&1; }

# _NET_ACTIVE_WINDOW from root, returns the decimal XID or 0x0
active_window() {
    DISPLAY=$DPY xprop -root _NET_ACTIVE_WINDOW 2>/dev/null \
        | sed -n 's/.*_NET_ACTIVE_WINDOW(WINDOW): window id #//p' \
        | tr -d ' '
}
# _NET_WM_STATE_FOCUSED on a client
is_focused() {
    DISPLAY=$DPY xprop -id "$1" _NET_WM_STATE 2>/dev/null \
        | grep -q '_NET_WM_STATE_FOCUSED'
}
wm_class_of() {
    DISPLAY=$DPY xprop -id "$1" WM_CLASS 2>/dev/null \
        | sed -n 's/.*WM_CLASS(STRING) = "[^"]*", "\([^"]*\)".*/\1/p'
}
panel_window() {
    DISPLAY=$DPY xdotool search --name '^Window Maker Switch Panel$' 2>/dev/null \
        | tail -n 1
}
panel_width() {
    panel=$(panel_window)
    [ -n "$panel" ] || return 1
    DISPLAY=$DPY xwininfo -id "$panel" 2>/dev/null \
        | sed -n 's/^  Width: \([0-9][0-9]*\)$/\1/p'
}
panel_height() {
    panel=$(panel_window)
    [ -n "$panel" ] || return 1
    DISPLAY=$DPY xwininfo -id "$panel" 2>/dev/null \
        | sed -n 's/^  Height: \([0-9][0-9]*\)$/\1/p'
}

for tool in Xvfb Xephyr xdotool xterm xprop xwininfo wmctrl; do
    command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not found"
done
[ -x "$ROOT/src/.libs/wmaker" ] || skip_all "src/.libs/wmaker not built"
[ -x "$WMAKERMSG" ] || skip_all "util/.libs/wmakermsg not built"
[ -x "$WDWRITE" ] || skip_all "util/.libs/wdwrite not built"

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
  StrictWindozeCycle = YES;
  WrapMenus = NO;
  ScrollableMenus = NO;
}
EOF
cat >"$WMHOME/Defaults/WMWindowAttributes" <<'EOF'
{
  "*" = { NoAppIcon = YES; };
  "AppIcon" = { NoAppIcon = NO; SharedAppIcon = YES; };
}
EOF
# One INFO_PANEL entry so the internal-panel Delete path can be driven.
cat >"$WMHOME/Defaults/WMRootMenu" <<'EOF'
("Root",
 ("Info", INFO_PANEL))
EOF

# --- Xephyr ----------------------------------------------------------------
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
    echo "display $DPY is already in use, set SWITCHPANEL_DRIVE_DISPLAY" >&2
    exit 77
fi
# Xephyr is a nested server: on the session display its host pointer
# tracks the live mouse and can override the XTEST warps the drive
# sends. Run it on a private headless parent whose virtual pointer
# only moves when Xephyr mirrors a warp.
PARENT_DISPLAY=${SWITCHPANEL_DRIVE_PARENT_DISPLAY:-":$(( ${DPY#:} + 1 ))"}
Xvfb "$PARENT_DISPLAY" -screen 0 2400x1600x24 -ac >>"$LOG" 2>&1 &
XVFB_PARENT_PID=$!
echo "$XVFB_PARENT_PID" >> "$PIDFILE"
xvfb_parent_ready() {
    kill -0 "$XVFB_PARENT_PID" 2>/dev/null || fatal "parent Xvfb exited early"
    DISPLAY=$PARENT_DISPLAY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xvfb_parent_ready || fatal "parent Xvfb failed to start"

note "start Xephyr $DPY on parent $PARENT_DISPLAY"
Xephyr "$DPY" -display "$PARENT_DISPLAY" -screen ${W}x${H} -ac >>"$LOG" 2>&1 &
XEPHYR_PID=$!
echo "$XEPHYR_PID" >> "$PIDFILE"
xephyr_ready() {
    kill -0 "$XEPHYR_PID" 2>/dev/null || fatal "Xephyr exited early"
    DISPLAY=$DPY xdpyinfo >/dev/null 2>&1
}
wait_for 40 xephyr_ready || fatal "Xephyr failed to start"

# --- wmaker ----------------------------------------------------------------
note "start wmaker"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    ASAN_OPTIONS="$(drive_asan_options)" \
    DISPLAY=$DPY \
    "$ROOT/src/.libs/wmaker" --for-real >>"$LOG" 2>&1 &
WMAKER_PID=$!
echo "$WMAKER_PID" >> "$PIDFILE"
wm_ready() {
    DISPLAY=$DPY xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null \
        | grep -q 'window id'
}
wait_for 120 wm_ready || fatal "wmaker did not come up"
SOCKET=$(LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    DISPLAY=$DPY "$WMAKERMSG" --get-socketpath)
case "$SOCKET" in
    "$RUNTIME"/*) : ;;
    *) fatal "IPC socket $SOCKET escaped private XDG_RUNTIME_DIR" ;;
esac
wait_for 80 ipc_ready || fatal "wmaker IPC did not come up at $SOCKET"
sleep 0.5

note "configure switch panel label formats"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" DISPLAY=$DPY \
    "$WDWRITE" WindowMaker SwitchPanelLabelFormat '"%I - %T"' || fatal "window label write failed"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" DISPLAY=$DPY \
    "$WDWRITE" WindowMaker SwitchPanelGroupLabelFormat '"%s %I"' || fatal "group label write failed"
msg -q reconfigure || fatal "label format reconfigure rejected"

# --- spawn xterms -----------------------------------------------------------
# Two plain xterms share WM_CLASS "XTerm" for intra-app cycle (CYCLE_CLASS_ONLY).
# Three with distinct -class (Alpha, Beta, Gamma) for app-group cycle and
# letter-key lookup. Distinct -class per xterm so each forms its own
# WApplication (SharedAppIcon coalesces same-instance.class windows).
spawn_term() {
    DISPLAY=$DPY xterm -title "$1" ${2:+-class "$2"} \
        -geometry "$3" -e 'sleep 600' >>"$LOG" 2>&1 &
    p=$!
    echo "$p" >> "$PIDFILE"
}
find_client() {
    DISPLAY=$DPY xdotool search --name "^$1\$" 2>/dev/null | tail -n 1
}

note "spawn five xterms"
spawn_term xtA "" 80x24+50+50
spawn_term xtB "" 80x24+200+50
spawn_term Alpha Alpha 80x24+400+100
spawn_term Beta  Beta  80x24+600+200
spawn_term Gamma Gamma 80x24+800+100
for t in xtA xtB Alpha Beta Gamma; do
    wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^$t\$' 2>/dev/null | grep -q ." \
        || fatal "$t did not map"
done
XA=$(find_client xtA)
XB=$(find_client xtB)
AC=$(find_client Alpha)
BT=$(find_client Beta)
GM=$(find_client Gamma)
GM_HEX=$(printf '0x%x' "$GM")
note "xtA=$XA xtB=$XB Alpha=$AC Beta=$BT Gamma=$GM"

# Establish a deterministic MRU order: activate xtA, xtB, Alpha, Beta, Gamma
# so Gamma is most-recently-focused and the panel list walks Gamma->Beta->
# Alpha->xtB->xtA.
for w in "$XA" "$XB" "$AC" "$BT" "$GM"; do
    xdo windowactivate "$w"; sleep 0.2
done
sleep 0.3

# --- 1. CYCLE_FLAT forward (Alt+Tab) ---------------------------------------
note "CYCLE_FLAT: Alt+Tab changes focus"
before=$(active_window)
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.2
xdo keyup alt; sleep 0.3
after=$(active_window)
check "Alt+Tab moved focus away from $before" [ "$before" != "$after" ]
check "Alt+Tab target is focused" is_focused "$after"
# cycle again: Alt+Tab from the new window
before2=$after
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.2
xdo keyup alt; sleep 0.3
after2=$(active_window)
check "second Alt+Tab moved focus away from $before2" [ "$before2" != "$after2" ]
check "wmaker alive after flat cycle" ipc_ready

# --- 2. CYCLE_FLAT backward (Alt+Shift+Tab) -------------------------------
note "CYCLE_FLAT: Alt+Shift+Tab backward"
before=$after2
xdo keydown alt; sleep 0.1
xdo keydown shift; sleep 0.1
xdo key Tab; sleep 0.2
xdo keyup shift; sleep 0.1
xdo keyup alt; sleep 0.3
after=$(active_window)
check "Alt+Shift+Tab moved focus away from $before" [ "$before" != "$after" ]
check "wmaker alive after backward cycle" ipc_ready

# --- 3. CYCLE_GROUP (Mod4+Tab) ----------------------------------------------
note "CYCLE_GROUP: Mod4+Tab cycles app groups"
# Re-focus Gamma so the MRU is deterministic.
xdo windowactivate "$GM"; sleep 0.3
before=$(active_window)
before_class=$(wm_class_of "$before")
xdo keydown super; sleep 0.1
xdo key Tab; sleep 0.2
xdo keyup super; sleep 0.3
after=$(active_window)
after_class=$(wm_class_of "$after")
check "Mod4+Tab moved focus away from $before" [ "$before" != "$after" ]
check "Mod4+Tab changed WM_CLASS ($before_class -> $after_class)" \
    [ "$before_class" != "$after_class" ]
check "wmaker alive after app-group cycle" ipc_ready

note "CYCLE_GROUP: held Super survives auto-drill and modifier repeat"
xdo windowactivate "$XA"; sleep 0.3
xdo keydown super; sleep 0.1
xdo key Tab; sleep 0.2
xdo key a; sleep 1.0
xdo keydown super; sleep 0.1
xdo key Tab; sleep 0.2
xdo keyup super; sleep 0.3
check "wmaker alive after single-window auto-drill" ipc_ready

xdo windowactivate "$GM"; sleep 0.3
xdo keydown super; sleep 0.1
xdo key Tab; sleep 0.2
xdo key x; sleep 1.0
drill_height=$(panel_height)
check "multi-window auto-drill expanded the switch panel (height $drill_height)" \
    [ "${drill_height:-0}" -gt 100 ]
xdo keydown super; sleep 0.1
xdo key Tab; sleep 0.2
xdo keyup super; sleep 0.3
check "wmaker alive after multi-window auto-drill" ipc_ready

# --- 4. Letter-key lookup on app-group panel --------------------------------
note "letter-key: Mod4+Tab then 'b' selects a Beta-class window"
# Focus xtA (class XTerm) so the app-group panel starts from a non-Beta class.
xdo windowactivate "$XA"; sleep 0.3
xdo keydown super; sleep 0.1
xdo key Tab; sleep 0.2
xdo key b; sleep 0.2
xdo keyup super; sleep 0.3
after=$(active_window)
after_class=$(wm_class_of "$after")
class_starts_with() { case "$2" in "$1"*) true;; *) false;; esac; }
check "letter 'b' focused a window" [ -n "$after" ]
check "letter 'b' selected class starting with B (got $after_class)" \
    class_starts_with B "$after_class"
check "wmaker alive after letter lookup" ipc_ready

# --- 5. CYCLE_CLASS_ONLY (Mod4+grave) --------------------------------------
note "CYCLE_CLASS_ONLY: Mod4+grave cycles within same WM_CLASS"
# Focus xtA (class XTerm). The intra-app panel filters to same-WM_CLASS, so
# it should cycle between xtA and xtB (both XTerm) and skip Alpha/Beta/Gamma.
xdo windowactivate "$XA"; sleep 0.3
before=$(active_window)
xdo keydown super; sleep 0.1
xdo key grave; sleep 0.2
xdo keyup super; sleep 0.3
after=$(active_window)
check "Mod4+grave moved focus away from $before" [ "$before" != "$after" ]
check "Mod4+grave stayed within XTerm class" \
    [ "$(wm_class_of "$after")" = "XTerm" ]
check "wmaker alive after intra-app cycle" ipc_ready

# --- 6. Escape cancels cycle ------------------------------------------------
note "Escape cancels Alt+Tab cycle"
xdo windowactivate "$GM"; sleep 0.3
before=$(active_window)
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.2
xdo key Escape; sleep 0.3
xdo keyup alt; sleep 0.2
after=$(active_window)
check "Escape restored focus to $before (got $after)" [ "$before" = "$after" ]
check "wmaker alive after Escape cancel" ipc_ready

# --- 7. skip_switchpanel exclusion -----------------------------------------
note "skip_switchpanel: SKIP_TASKBAR excludes from cycle"
# Mark Gamma with _NET_WM_STATE_SKIP_TASKBAR, which mirrors skip_switchpanel
# (wmspec.c:1429-1434). Then Alt+Tab from Beta should never land on Gamma.
DISPLAY=$DPY wmctrl -i -r "$GM" -b add,skip_taskbar
sleep 0.3
# verify the property is set
DISPLAY=$DPY xprop -id "$GM" _NET_WM_STATE 2>/dev/null | grep -q SKIP_TASKBAR \
    || fatal "could not set skip_taskbar on Gamma"
# focus Beta, then Alt+Tab multiple times — Gamma must never get focus
xdo windowactivate "$BT"; sleep 0.3
hit_gamma=0
i=0
while [ "$i" -lt 6 ]; do
    xdo keydown alt; sleep 0.1
    xdo key Tab; sleep 0.2
    xdo keyup alt; sleep 0.3
    cur=$(active_window)
    [ "$cur" = "$GM_HEX" ] && hit_gamma=1
    i=$((i + 1))
done
check "Gamma with skip_taskbar never focused during 6-cycle" [ "$hit_gamma" = "0" ]
check "wmaker alive after skip_switchpanel cycle" ipc_ready
# clean up the flag
DISPLAY=$DPY wmctrl -i -r "$GM" -b remove,skip_taskbar
sleep 0.2

# --- 8. SwitchPanelIconSize runtime reconfigure -----------------------------
note "SwitchPanelIconSize runtime reconfigure"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" DISPLAY=$DPY \
    "$WDWRITE" WindowMaker SwitchPanelIconSize 96
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3
# open the panel — wmaker must survive the size change
xdo windowactivate "$AC"; sleep 0.3
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.3
xdo keyup alt; sleep 0.3
check "wmaker alive after SwitchPanelIconSize reconfigure + cycle" ipc_ready
# restore default
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" DISPLAY=$DPY \
    "$WDWRITE" WindowMaker SwitchPanelIconSize 64
msg -q reconfigure || true
sleep 0.2

note "switch panel label format runtime reconfigure"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" DISPLAY=$DPY \
    "$WDWRITE" WindowMaker SwitchPanelLabelFormat '"%S %-20T %i"' || fatal "window label write failed"
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" DISPLAY=$DPY \
    "$WDWRITE" WindowMaker SwitchPanelGroupLabelFormat '"%I [%W]"' || fatal "group label write failed"
msg -q reconfigure || fatal "label format reconfigure rejected"
xdo windowactivate "$AC"; sleep 0.3
before=$(active_window)
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.3
check "panel appears after label format reconfigure" test -n "$(panel_window)"
xdo key Tab; sleep 0.2
xdo keyup alt; sleep 0.3
after=$(active_window)
check "cycle after label format reconfigure changes selection" [ "$before" != "$after" ]
check "panel closes after label format reconfigure" test -z "$(panel_window)"
check "wmaker alive after label format reconfigure" ipc_ready

# --- 9. Drill into a group's windows (grave/Down + Right) ---------------------
note "CYCLE_GROUP drill: Down drills, Right cycles within the sub-strip"
# Re-establish MRU so the panel order is deterministic: Alpha, Beta, Gamma,
# then the two XTerms. xtB is the most-recent XTerm so it is the group rep.
for w in "$AC" "$BT" "$GM" "$XA" "$XB"; do
    xdo windowactivate "$w"; sleep 0.2
done
before=$(active_window)
xdo keydown super; sleep 0.1
xdo key Tab; sleep 0.2
# Jump to the XTerm group (class starts with 'X').
xdo key x; sleep 0.2
# Drill into the XTerm sub-strip with Down.
xdo key Down; sleep 0.2
# Advance to the next window in the sub-strip with Right.
xdo key Right; sleep 0.2
xdo keyup super; sleep 0.3
after=$(active_window)
check "drill+Right focused a window" [ -n "$after" ]
check "drill+Right stayed within XTerm (got $(wm_class_of "$after"))" \
    [ "$(wm_class_of "$after")" = "XTerm" ]
check "drill+Right selected a different window than the pre-panel focus ($before -> $after)" \
    [ "$before" != "$after" ]
check "wmaker alive after drill+Right" ipc_ready

# --- 10. Group switch while drilled (Tab) --------------------------------------
note "CYCLE_GROUP drill: Tab switches groups while drilled"
for w in "$AC" "$BT" "$GM" "$XA" "$XB"; do
    xdo windowactivate "$w"; sleep 0.2
done
xdo keydown super; sleep 0.1
xdo key Tab; sleep 0.2
xdo key x; sleep 0.2
xdo key Down; sleep 0.2
# Now drilled into XTerm. Press Tab to switch to the next group.
xdo key Tab; sleep 0.2
xdo keyup super; sleep 0.3
after=$(active_window)
after_class=$(wm_class_of "$after")
check "group switch while drilled left XTerm (got $after_class)" \
    [ "$after_class" != "XTerm" ]
check "group switch while drilled focused a real group window" [ -n "$after" ]
check "wmaker alive after drilled group switch" ipc_ready

# --- 11. Confirm and cancel while drilled -------------------------------------
note "CYCLE_GROUP drill: confirm and cancel while drilled"
# Confirm: drill + Right + release Mod4 focuses the selected XTerm window.
for w in "$AC" "$BT" "$GM" "$XA" "$XB"; do
    xdo windowactivate "$w"; sleep 0.2
done
before=$(active_window)
xdo keydown super; sleep 0.1
xdo key Tab; sleep 0.2
xdo key x; sleep 0.2
xdo key Down; sleep 0.2
xdo key Right; sleep 0.2
xdo keyup super; sleep 0.3
after=$(active_window)
check "drill confirm focused an XTerm window (got $(wm_class_of "$after"))" \
    [ "$(wm_class_of "$after")" = "XTerm" ]
check "drill confirm moved focus from $before to $after" [ "$before" != "$after" ]

# Cancel: drill + Escape restores focus to the pre-panel window.
xdo windowactivate "$XB"; sleep 0.3
before=$(active_window)
xdo keydown super; sleep 0.1
xdo key Tab; sleep 0.2
xdo key x; sleep 0.2
xdo key Down; sleep 0.2
xdo key Escape; sleep 0.3
xdo keyup super; sleep 0.2
after=$(active_window)
check "drill Escape restored focus to $before (got $after)" [ "$before" = "$after" ]
check "wmaker alive after drill cancel" ipc_ready

# --- 12. Up undrills back to the main strip ------------------------------------
note "CYCLE_GROUP drill: Up undrills back to the main strip"
for w in "$AC" "$BT" "$GM" "$XA" "$XB"; do
    xdo windowactivate "$w"; sleep 0.2
done
xdo keydown super; sleep 0.1
xdo key Tab; sleep 0.2
xdo key x; sleep 0.2
# Drill into XTerm sub-strip.
xdo key Down; sleep 0.2
# Up undrills back to the main strip.
xdo key Up; sleep 0.2
# Now on the main strip, Right should switch to a different group.
xdo key Right; sleep 0.2
xdo keyup super; sleep 0.3
after=$(active_window)
after_class=$(wm_class_of "$after")
check "undrill+Right switched groups away from XTerm (got $after_class)" \
    [ "$after_class" != "XTerm" ]
check "wmaker alive after Up-undrill" ipc_ready

# --- 13. Re-drill after undrill shows window icons -------------------------------
note "CYCLE_GROUP drill: re-drill after undrill shows window icons"
for w in "$AC" "$BT" "$GM" "$XA" "$XB"; do
    xdo windowactivate "$w"; sleep 0.2
done
xdo keydown super; sleep 0.1
xdo key Tab; sleep 0.2
xdo key x; sleep 0.2
# Drill into XTerm, undrill, switch group, re-drill into the new group.
xdo key Down; sleep 0.2
xdo key Up; sleep 0.2
xdo key Right; sleep 0.2
xdo key Down; sleep 0.2
# The re-drilled sub-strip must show windows: Right should cycle to a
# different window within the new group.
xdo key Right; sleep 0.2
xdo keyup super; sleep 0.3
after=$(active_window)
after_class=$(wm_class_of "$after")
check "re-drill after undrill focused a window" [ -n "$after" ]
check "re-drill after undrill did not stay on XTerm (got $after_class)" \
    [ "$after_class" != "XTerm" ]
check "wmaker alive after re-drill" ipc_ready

# --- 14. Single-window group does not drill -------------------------------------
note "CYCLE_GROUP drill: single-window group does not drill"
# Alpha is a single-window group (class Alpha). Drill into it with Down
# should be a no-op: the sub-panel stays closed, and Right still acts on
# the main strip (switching groups) instead of cycling a sub-strip.
for w in "$XA" "$XB" "$AC"; do
    xdo windowactivate "$w"; sleep 0.2
done
xdo keydown super; sleep 0.1
xdo key Tab; sleep 0.2
xdo key a; sleep 0.2
# Down on a single-window group: no drill, stay on main strip.
xdo key Down; sleep 0.2
# Right should still switch groups (main strip active), not cycle a sub.
xdo key Right; sleep 0.2
xdo keyup super; sleep 0.3
after=$(active_window)
after_class=$(wm_class_of "$after")
check "single-window drill+Right switched groups away from Alpha (got $after_class)" \
    [ "$after_class" != "Alpha" ]
check "wmaker alive after single-window drill" ipc_ready

# --- 15. Mixed app groups survive delayed auto-drill ------------------------
note "CYCLE_GROUP: mixed multi-window and standalone groups with auto-drill"
# XTerm has two windows. Alpha, Beta, and Gamma are standalone groups.
xdo windowactivate "$XB"; sleep 0.3
before=$(active_window)
before_class=$(wm_class_of "$before")
xdo keydown super
xdo key Tab
sleep 1.1
# A repeated modifier press must not close the panel after auto-drill.
xdo keydown super
xdo key Tab
sleep 0.2
xdo keyup super
sleep 0.3
after=$(active_window)
after_class=$(wm_class_of "$after")
check "mixed-group delayed auto-drill moved focus away from $before" [ "$before" != "$after" ]
check "mixed-group delayed auto-drill selected another app group ($before_class -> $after_class)" \
    [ "$before_class" != "$after_class" ]
check "wmaker alive after mixed-group delayed auto-drill" ipc_ready

# --- 16. Forward/backward cycling after repeated auto-drill -----------------
note "CYCLE_GROUP: forward and backward after repeated auto-drill waits"
xdo windowactivate "$AC"; sleep 0.3
xdo keydown super
xdo key Tab
xdo key x
sleep 1.0
# Move to a standalone group, wait, then return to the multi-window group.
xdo key Tab
sleep 1.0
xdo keydown shift
xdo key Tab
sleep 0.2
xdo keyup shift
# Repeat both directions after the drill preview has been refreshed.
xdo key Tab
sleep 1.0
xdo keydown shift
xdo key Tab
sleep 0.2
xdo keyup shift
xdo keyup super
sleep 0.3
check "repeated forward/backward Super+Tab kept XTerm selected" \
    [ "$(wm_class_of "$(active_window)")" = "XTerm" ]
check "wmaker alive after repeated forward/backward auto-drill" ipc_ready

# --- 17. SwitcherOrder runtime flip ------------------------------------------
note "SwitcherOrder: Focus (MRU) vs Age (creation order)"
# Close the earlier xterms so exactly three clients remain and the cycle
# array is fully known ([ordA, ...] with no older windows ahead).
for w in "$XA" "$XB" "$AC" "$BT" "$GM"; do
    DISPLAY=$DPY wmctrl -i -c "$w"
done
all_gone() {
    DISPLAY=$DPY xdotool search --name '^(xtA|xtB|Alpha|Beta|Gamma)$' 2>/dev/null \
        | grep -q . && return 1
    return 0
}
wait_for 40 all_gone || fatal "old xterms did not close"
check "wmaker alive after closing five xterms" ipc_ready

# Three fresh xterms with distinct classes, spawned one at a time so the
# manage order (and therefore creation order) is OA < OB < OC. Distinct
# -class keeps each in its own WApplication so tab-groups and the group
# dedup stay out of the way.
spawn_term ordA OA 80x24+50+450
wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^ordA\$' 2>/dev/null | grep -q ." \
    || fatal "ordA did not map"
spawn_term ordB OB 80x24+250+450
wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^ordB\$' 2>/dev/null | grep -q ." \
    || fatal "ordB did not map"
spawn_term ordC OC 80x24+450+450
wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^ordC\$' 2>/dev/null | grep -q ." \
    || fatal "ordC did not map"
sleep 0.3
# active_window() reports hex, xdotool reports decimal: normalize to hex.
OA=$(printf '0x%x' "$(find_client ordA)")
OB=$(printf '0x%x' "$(find_client ordB)")
OC=$(printf '0x%x' "$(find_client ordC)")
note "ordA=$OA ordB=$OB ordC=$OC"

# Activate B, C, then A so the recency chain is A, C, B while creation
# order stays A < B < C. Re-run before every probe: each cycle moves
# focus and the Focus-mode expectations depend on exact recency.
mru_chain() {
    for w in "$OB" "$OC" "$OA"; do
        xdo windowactivate "$w"; sleep 0.2
    done
}

# Baseline Focus assertion: Alt+Tab forward from A selects C.
mru_chain
sleep 0.3
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.2
xdo keyup alt; sleep 0.3
after=$(active_window)
check "Focus order: Alt+Tab from most-recent picks next-most-recent ($after = $OC)" \
    [ "$after" = "$OC" ]
check "wmaker alive after Focus-order cycle" ipc_ready

# Flip to Age at runtime.
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" DISPLAY=$DPY \
    "$WDWRITE" WindowMaker SwitcherOrder Age
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3

# Age assertion: same array position math but creation-ordered list.
mru_chain
sleep 0.3
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.2
xdo keyup alt; sleep 0.3
after=$(active_window)
check "Age order: Alt+Tab from oldest of three picks next-created ($after = $OB)" \
    [ "$after" = "$OB" ]
check "wmaker alive after Age-order cycle" ipc_ready

# Backward discriminator: Alt+Shift+Tab from A wraps to the newest window C
# in Age mode (it would wrap past B to an old window in Focus mode).
mru_chain
sleep 0.3
xdo keydown alt; sleep 0.1
xdo keydown shift; sleep 0.1
xdo key Tab; sleep 0.2
xdo keyup shift; sleep 0.1
xdo keyup alt; sleep 0.3
after=$(active_window)
check "Age order: backward cycle wraps to newest created ($after = $OC)" \
    [ "$after" = "$OC" ]
check "wmaker alive after Age-order backward cycle" ipc_ready

# Flip back and confirm the round trip restores MRU behaviour.
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" DISPLAY=$DPY \
    "$WDWRITE" WindowMaker SwitcherOrder Focus
msg -q reconfigure || fail "reconfigure rejected"
sleep 0.3
mru_chain
sleep 0.3
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.2
xdo keyup alt; sleep 0.3
after=$(active_window)
check "Focus order after flip-back: Alt+Tab picks next-most-recent again ($after = $OC)" \
    [ "$after" = "$OC" ]
check "wmaker alive after SwitcherOrder round-trip" ipc_ready

# --- 18. Delete closes the highlighted window --------------------------------
note "Delete: closes the highlighted window mid-cycle"
# Two disposable xterms. Activate delA then delB so the flat panel opened
# from delB highlights delA and Delete closes delA.
spawn_term delA "" 80x24+50+600
spawn_term delB "" 80x24+250+600
wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^delA\$' 2>/dev/null | grep -q ." \
    || fatal "delA did not map"
wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^delB\$' 2>/dev/null | grep -q ." \
    || fatal "delB did not map"
DA=$(find_client delA)
DB=$(find_client delB)
DB_HEX=$(printf '0x%x' "$DB")
note "delA=$DA delB=$DB"
xdo windowactivate "$DA"; sleep 0.2
xdo windowactivate "$DB"; sleep 0.3
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.2
xdo key Delete; sleep 0.3
# the cycle stays open after the close and the selection moves on
xdo key Tab; sleep 0.2
xdo keyup alt; sleep 0.5
check "Delete closed the highlighted window delA" \
    wait_for 60 sh -c "! DISPLAY=$DPY xdotool search --name '^delA\$' 2>/dev/null | grep -q ."
check "cycle continued after Delete (focus moved off delB, got $(active_window))" \
    [ "$(active_window)" != "$DB_HEX" ]
check "delB survived the Delete of its neighbour" \
    wait_for 5 sh -c "DISPLAY=$DPY xdotool search --name '^delB\$' 2>/dev/null | grep -q ."
check "wmaker alive after Delete close" ipc_ready

# --- 19. Delete peels an app-group tile ---------------------------------------
note "Delete: app-group tile keeps its slot while the group has windows"
# Three xterms share the class Peeled, so the app-group panel shows them
# as one tile. Activate peelC last: it becomes the group representative.
spawn_term peelA Peeled 80x24+450+600
spawn_term peelB Peeled 80x24+650+600
spawn_term peelC Peeled 80x24+850+600
for t in peelA peelB peelC; do
    wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^$t\$' 2>/dev/null | grep -q ." \
        || fatal "$t did not map"
done
PA=$(find_client peelA)
PB=$(find_client peelB)
PC=$(find_client peelC)
PC_HEX=$(printf '0x%x' "$PC")
note "peelA=$PA peelB=$PB peelC=$PC"
xdo windowactivate "$PA"; sleep 0.2
xdo windowactivate "$PB"; sleep 0.2
xdo windowactivate "$PC"; sleep 0.3
xdo keydown super; sleep 0.1
xdo key Tab; sleep 0.2
# Jump to the Peeled group tile and delete its representative.
xdo key p; sleep 0.2
xdo key Delete; sleep 0.3
xdo keyup super; sleep 0.5
check "group Delete closed the group rep peelC" \
    wait_for 60 sh -c "! DISPLAY=$DPY xdotool search --name '^peelC\$' 2>/dev/null | grep -q ."
check "group tile peeled to a remaining Peeled window" \
    [ "$(wm_class_of "$(active_window)")" = "Peeled" ]
check "peel selection is no longer the closed rep" \
    [ "$(active_window)" != "$PC_HEX" ]
check "wmaker alive after group Delete" ipc_ready

# --- 20. Delete on a drilled sub-strip entry ---------------------------------
note "Delete: drilled sub-strip entry closes and the cycle continues"
# peelA and peelB remain, drill into the Peeled group and delete the
# sub-strip entry the selection moves to.
xdo windowactivate "$PA"; sleep 0.2
xdo windowactivate "$PB"; sleep 0.3
xdo keydown super; sleep 0.1
xdo key Tab; sleep 0.2
xdo key p; sleep 0.2
xdo key Down; sleep 0.2
xdo key Right; sleep 0.2
xdo key Delete; sleep 0.3
xdo keyup super; sleep 0.5
peel_left() {
    DISPLAY=$DPY xdotool search --name '^peel[AB]$' 2>/dev/null | grep -c .
}
check "sub-strip Delete closed exactly one Peeled window (left $(peel_left))" \
    [ "$(peel_left)" = 1 ]
check "wmaker alive after sub-strip Delete" ipc_ready

# --- 21. Delete closes an internal panel (Info) ------------------------------
note "Delete: closes wmaker's own Info panel"
info_open() { msg get_windows 2>/dev/null | grep -q 'Info'; }
info_gone() { ! msg get_windows 2>/dev/null | grep -q 'Info'; }
# Button3 opens the root menu at the pointer, its entry list is the only
# child below the menu title (geometry probe follows rootmenu-drive.sh).
CX=1200
CY=100
xdo mousemove "$CX" "$CY"
xdo mousedown 3
ENTRY_WIN=$(DISPLAY=$DPY xwininfo -root -tree 2>/dev/null \
    | awk -v cx="$CX" -v cy="$CY" '
        /^     0x/ { fx=""; fy=""; fw="";
            if (match($0, /[0-9]+x[0-9]+\+-?[0-9]+\+-?[0-9]+/)) {
                split(substr($0,RSTART,RLENGTH), g, /[x+]/);
                fw=g[1]; fx=g[3]; fy=g[4];
            }
            inframe = (fx!="" && fy==cy && fx<=cx && cx<=fx+fw);
            next; }
        inframe && /^        0x/ {
            if (match($0, /[0-9]+x[0-9]+\+-?[0-9]+\+-?[0-9]+  \+-?[0-9]+\+-?[0-9]+$/)) {
                split(substr($0,RSTART,RLENGTH), g, /[x+ ]+/);
                if (g[6] > cy) { print g[5] " " g[6] " " g[2]; exit; }
            } }')
if [ -n "$ENTRY_WIN" ]; then
    set -- $ENTRY_WIN
    EX=$1; EY=$2; EH=$3
    xdo mousemove "$((EX + 6))" "$((EY + (EH + 1) / 2))"
fi
xdo mouseup 3
check "Info panel opened from the root menu" wait_for 40 info_open
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.2
xdo key i; sleep 0.2
xdo key Delete; sleep 0.3
xdo keyup alt; sleep 0.5
check "Delete closed the Info panel" wait_for 60 info_gone
check "wmaker alive after internal-panel Delete" ipc_ready

# --- 22. Delete shrinks the panel and unmanages at once ---------------------
note "Delete: panel shrinks and the closed window leaves the managed list"
spawn_term shrinkA "" 80x24+50+350
spawn_term shrinkB "" 80x24+300+350
spawn_term shrinkC "" 80x24+550+350
for t in shrinkA shrinkB shrinkC; do
    wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^$t\$' 2>/dev/null | grep -q ." \
        || fatal "$t did not map"
done
SA=$(find_client shrinkA)
SB=$(find_client shrinkB)
SC=$(find_client shrinkC)
note "shrinkA=$SA shrinkB=$SB shrinkC=$SC"
shrink_b_gone() { ! msg get_windows 2>/dev/null | grep -q 'shrinkB'; }
xdo windowactivate "$SA"; sleep 0.2
xdo windowactivate "$SB"; sleep 0.2
xdo windowactivate "$SC"; sleep 0.3
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.2
w3=$(panel_width)
xdo key Delete; sleep 0.3
check "closed window unmanaged while the panel is still open" wait_for 40 shrink_b_gone
w2=$(panel_width)
width_shrank() { [ -n "$w3" ] && [ -n "$w2" ] && [ "$w2" -lt "$w3" ]; }
check "panel width shrank with its entries ($w3 -> $w2)" width_shrank
xdo keyup alt; sleep 0.5
check "wmaker alive after Delete shrink" ipc_ready

# --- 23. Delete tears the application icon down mid-cycle -------------------
note "Delete: app icon goes away with the closed window"
icon_tiles() { DISPLAY=$DPY xwininfo -root -tree 2>/dev/null | grep ' 64x64+'; }
icon_tiles_count() { icon_tiles | grep -c .; }
t0=$(icon_tiles_count)
spawn_term iconapp AppIcon 80x24+50+350
wait_for 40 sh -c "DISPLAY=$DPY xdotool search --name '^iconapp\$' 2>/dev/null | grep -q ." \
    || fatal "iconapp did not map"
IA=$(find_client iconapp)
note "iconapp=$IA base tiles=$t0"
xdo windowminimize "$IA"; sleep 0.5
check "app icon and miniwindow present while minimized" [ "$(icon_tiles_count)" -ge "$((t0 + 2))" ]
xdo keydown alt; sleep 0.1
xdo key Tab; sleep 0.2
# letter lookup jumps to the minimized window (title iconapp)
xdo key i; sleep 0.2
xdo key Delete; sleep 0.3
icon_tiles_gone() { [ "$(icon_tiles_count)" -le "$t0" ]; }
check "app icon and miniwindow gone before the panel is released" \
    wait_for 40 icon_tiles_gone
xdo keyup alt; sleep 0.5
check "wmaker alive after app icon Delete" ipc_ready

# --- teardown --------------------------------------------------------------
note "switchpanel drive summary"
printf 'PASS: %d, FAIL: %d\n' "$PASSED" "$FAILED" | tee -a "$LOG"
total=$((PASSED + FAILED))

msg -q exit >>"$LOG" 2>&1
wait_for 50 pid_gone "$WMAKER_PID"

[ "$FAILED" -eq 0 ] && [ "$total" -gt 0 ]
