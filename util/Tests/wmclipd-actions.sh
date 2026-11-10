#!/bin/sh

set -u

top_builddir=${top_builddir:-../..}
builddir=${builddir:-.}
DPY=${WMCLIPD_ACTIONS_DISPLAY:-:171}
RUNTIME=$(mktemp -d /tmp/wmclipd-actions.XXXXXX) || exit 99
PIDFILE="$RUNTIME/pids"
LOG="$RUNTIME/log"
HIST="$RUNTIME/history"
SOCKET="$RUNTIME/ipc.sock"
WMCLIPD="$top_builddir/util/wmclipd"
CLIPTEST="$builddir/wmclipd_cliptest"
WMLIBS="$top_builddir/WINGs/.libs:$top_builddir/wrlib/.libs"
XSVR=
DAEMON=
SOURCE=
SOURCE2=
CLIPSOURCE=
RECEIVER=
IPC=

: > "$PIDFILE"
: > "$LOG"

stop_owned()
{
	pid=$1
	[ -n "$pid" ] || return 0
	grep -qx "$pid" "$PIDFILE" || return 1
	name=$(ps -p "$pid" -o comm= | tr -d ' ')
	[ -n "$name" ] || return 0
	pgrep -a -x "$name" | awk -v pid="$pid" '$1 == pid { found=1 } END { exit !found }' || return 0
	if [ "$name" = Xvfb ]; then
		pgrep -a -x "$name" | awk -v pid="$pid" -v display="$DPY" \
			'$1 == pid && $3 == display { found=1 } END { exit !found }' || return 1
	else
		tr '\0' '\n' < "/proc/$pid/environ" | grep -qx "DISPLAY=$DPY" || return 1
	fi
	kill "$pid" 2>/dev/null || return 0
	wait "$pid" 2>/dev/null || true
}

cleanup()
{
	stop_owned "$IPC" || true
	stop_owned "$SOURCE" || true
	stop_owned "$SOURCE2" || true
	stop_owned "$CLIPSOURCE" || true
	stop_owned "$RECEIVER" || true
	stop_owned "$DAEMON" || true
	stop_owned "$XSVR" || true
	rm -f "$PIDFILE" "$LOG" "$HIST" "$SOCKET" "$RUNTIME/base64-history" \
		"$RUNTIME/config/WindowMaker" \
		"$RUNTIME/data/WindowMaker/Clipboard/export.txt" \
		"$RUNTIME/data/WindowMaker/Clipboard/export_1.txt" \
		"$RUNTIME/data/WindowMaker/Clipboard/reloaded.txt" \
		"$RUNTIME/original.png" "$RUNTIME/large.png" "$RUNTIME/oversized.png" \
		"$RUNTIME/compound" "$RUNTIME/bad-compound" \
		"$RUNTIME/uri" "$RUNTIME/expected" "$RUNTIME/encoded" "$RUNTIME/retrieved"
	rmdir "$RUNTIME/data/WindowMaker/Clipboard" "$RUNTIME/data/WindowMaker" \
		"$RUNTIME/data" "$RUNTIME/config" "$RUNTIME" 2>/dev/null || true
}
trap cleanup EXIT

skip_all()
{
	printf 'SKIP: %s\n' "$*"
	exit 77
}

fail()
{
	printf 'FAIL: %s\n' "$*"
	tail -n 80 "$LOG"
	exit 1
}

wait_log()
{
	n=0
	while [ "$n" -lt 100 ]; do
		grep -qF "$1" "$LOG" && return 0
		sleep 0.1
		n=$((n + 1))
	done
	return 1
}

wait_added()
{
	n=0
	while [ "$n" -lt 100 ]; do
		count=$(grep -cF "history: added entry" "$LOG" || true)
		[ "$count" -ge "$1" ] && return 0
		sleep 0.1
		n=$((n + 1))
	done
	return 1
}

for tool in Xvfb xdpyinfo xdotool xclip pgrep ps awk grep sed stat cmp dd base64 tr; do
	command -v "$tool" >/dev/null 2>&1 || skip_all "$tool not installed"
done
[ -x "$WMCLIPD" ] || skip_all "wmclipd not built"
[ -x "$CLIPTEST" ] || skip_all "wmclipd_cliptest not built"
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	skip_all "display $DPY is already in use"
fi

mkdir "$RUNTIME/config" || fail "cannot create configuration directory"
printf '{ ClipboardFilenameTemplate = export; }\n' > "$RUNTIME/config/WindowMaker"
Xvfb "$DPY" -screen 0 800x600x24 -ac >>"$LOG" 2>&1 &
XSVR=$!
echo "$XSVR" >> "$PIDFILE"
n=0
while ! DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; do
	n=$((n + 1))
	[ "$n" -lt 100 ] || fail "Xvfb did not start"
	sleep 0.1
done

DISPLAY=$DPY HOME="$RUNTIME" XDG_RUNTIME_DIR="$RUNTIME" \
XDG_DATA_HOME="$RUNTIME/data" XDG_STATE_HOME="$RUNTIME/state" \
WMAKER_USER_ROOT="$RUNTIME/config" WMAKER_USER_XDG=1 WMAKER_IPC_SOCKET="$SOCKET" \
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	"$WMCLIPD" --clipboard --history 3 --history-file "$HIST" --debug >>"$LOG" 2>&1 &
DAEMON=$!
echo "$DAEMON" >> "$PIDFILE"
wait_log "monitoring CLIPBOARD and PRIMARY" || fail "wmclipd did not start"

DISPLAY=$DPY "$CLIPTEST" receive-paste >>"$LOG" 2>&1 &
RECEIVER=$!
echo "$RECEIVER" >> "$PIDFILE"
wait_log "paste receiver ready" || fail "save-action receiver did not start"
DISPLAY=$DPY "$CLIPTEST" serve-string "exported text" >>"$LOG" 2>&1 &
SOURCE=$!
echo "$SOURCE" >> "$PIDFILE"
wait_log "history: added entry" || fail "source was not captured"

: > "$LOG"
DISPLAY=$DPY WMAKER_IPC_SOCKET="$SOCKET" "$CLIPTEST" fake-tick unkeyed >>"$LOG" 2>&1 &
IPC=$!
echo "$IPC" >> "$PIDFILE"
wait_log "keyboard grab succeeded" || fail "popup did not open"
DISPLAY=$DPY "$CLIPTEST" serve-string "pending primary" >>"$LOG" 2>&1 &
SOURCE2=$!
echo "$SOURCE2" >> "$PIDFILE"
wait_log "deferring PRIMARY capture" || fail "pending PRIMARY capture did not start"
DISPLAY=$DPY xdotool key s || fail "cannot send save key"
wait_log "saved clipboard entry and copied its path" || fail "save action did not complete"
DISPLAY=$DPY "$CLIPTEST" stale-clear clipboard || fail "cannot send stale CLIPBOARD clear"
DISPLAY=$DPY "$CLIPTEST" stale-clear primary || fail "cannot send stale PRIMARY clear"
sleep 0.5

path=$(DISPLAY=$DPY xclip -selection clipboard -target UTF8_STRING -o) ||
	fail "cannot request saved path"
[ "$path" = "$RUNTIME/data/WindowMaker/Clipboard/export.txt" ] ||
	fail "unexpected clipboard path"
[ "$(cat "$path")" = "exported text" ] || fail "exported bytes differ"
[ "$(stat -c %a "$path")" = 600 ] || fail "export mode is not 0600"
[ "$(DISPLAY=$DPY xclip -selection primary -target UTF8_STRING -o)" = "$path" ] ||
	fail "PRIMARY path differs"
DISPLAY=$DPY xclip -selection clipboard -target text/uri-list -o > "$RUNTIME/uri" ||
	fail "cannot request URI target"
printf 'file://%s\r\n' "$path" > "$RUNTIME/expected"
cmp "$RUNTIME/uri" "$RUNTIME/expected" || fail "URI target differs"
DISPLAY=$DPY xclip -selection primary -target text/uri-list -o > "$RUNTIME/uri" ||
	fail "cannot request PRIMARY URI target"
cmp "$RUNTIME/uri" "$RUNTIME/expected" || fail "PRIMARY URI target differs"
if grep -qF "paste key state=" "$LOG"; then
	fail "save action submitted a paste"
fi

stop_owned "$DAEMON" || fail "cannot stop test daemon"
DAEMON=
wait_log "history: saved 1 entry to $HIST" || fail "save action changed history"
printf 'PASS: save published both path targets without pasting or changing history\n'

stop_owned "$IPC" || fail "cannot stop test IPC server"
IPC=
stop_owned "$SOURCE" || fail "cannot stop test source"
SOURCE=
stop_owned "$SOURCE2" || fail "cannot stop pending PRIMARY source"
SOURCE2=
stop_owned "$RECEIVER" || fail "cannot stop save-action receiver"
RECEIVER=
rm -f "$SOCKET"
: > "$LOG"
DISPLAY=$DPY HOME="$RUNTIME" XDG_RUNTIME_DIR="$RUNTIME" \
XDG_DATA_HOME="$RUNTIME/data" WMAKER_USER_ROOT="$RUNTIME/config" WMAKER_USER_XDG=1 \
WMAKER_IPC_SOCKET="$SOCKET" LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	"$WMCLIPD" --clipboard --history-file "$HIST" --debug >>"$LOG" 2>&1 &
DAEMON=$!
echo "$DAEMON" >> "$PIDFILE"
wait_log "history: loaded 1 entry" || fail "original history did not reload"
DISPLAY=$DPY WMAKER_IPC_SOCKET="$SOCKET" "$CLIPTEST" fake-tick unkeyed >>"$LOG" 2>&1 &
IPC=$!
echo "$IPC" >> "$PIDFILE"
wait_log "keyboard grab succeeded" || fail "restored history popup did not open"
DISPLAY=$DPY xdotool key Return || fail "cannot activate restored entry"
wait_log "activated history entry 0" || fail "restored entry was not activated"
[ "$(DISPLAY=$DPY xclip -selection clipboard -target STRING -o)" = "exported text" ] ||
	fail "restored history does not contain the original entry"
stop_owned "$IPC" || fail "cannot stop restored history IPC server"
IPC=
rm -f "$SOCKET"
: > "$LOG"
printf '{ ClipboardFilenameTemplate = reloaded; }\n' > "$RUNTIME/config/WindowMaker"
DISPLAY=$DPY WMAKER_IPC_SOCKET="$SOCKET" "$CLIPTEST" fake-tick unkeyed >>"$LOG" 2>&1 &
IPC=$!
echo "$IPC" >> "$PIDFILE"
wait_log "keyboard grab succeeded" || fail "template reload popup did not open"
DISPLAY=$DPY xdotool key s || fail "cannot save using reloaded template"
wait_log "saved clipboard entry and copied its path" || fail "reloaded template save failed"
[ "$(DISPLAY=$DPY xclip -selection clipboard -target UTF8_STRING -o)" = \
	"$RUNTIME/data/WindowMaker/Clipboard/reloaded.txt" ] || fail "template edit was not reloaded"
[ "$(cat "$RUNTIME/data/WindowMaker/Clipboard/reloaded.txt")" = "exported text" ] ||
	fail "reloaded template export differs"
stop_owned "$IPC" || fail "cannot stop reload IPC server"
IPC=
stop_owned "$DAEMON" || fail "cannot stop restored history daemon"
DAEMON=
printf 'PASS: original history survived restart and filename template reloaded\n'

rm -f "$SOCKET"
: > "$LOG"
printf '{ ClipboardFilenameTemplate = export; }\n' > "$RUNTIME/config/WindowMaker"
DISPLAY=$DPY HOME="$RUNTIME" XDG_RUNTIME_DIR="$RUNTIME" \
XDG_DATA_HOME="$RUNTIME/data" XDG_STATE_HOME="$RUNTIME/state" \
WMAKER_USER_ROOT="$RUNTIME/config" WMAKER_USER_XDG=1 WMAKER_IPC_SOCKET="$SOCKET" \
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	"$WMCLIPD" --clipboard --history 1 --debug >>"$LOG" 2>&1 &
DAEMON=$!
echo "$DAEMON" >> "$PIDFILE"
wait_log "monitoring CLIPBOARD and PRIMARY" || fail "capacity-one daemon did not start"
printf 'original image' > "$RUNTIME/original.png"
DISPLAY=$DPY "$CLIPTEST" serve-clipboard-file "$RUNTIME/original.png" >>"$LOG" 2>&1 &
CLIPSOURCE=$!
echo "$CLIPSOURCE" >> "$PIDFILE"
wait_added 1 || fail "clipboard image was not captured"
DISPLAY=$DPY "$CLIPTEST" serve-string "single entry" >>"$LOG" 2>&1 &
SOURCE=$!
echo "$SOURCE" >> "$PIDFILE"
wait_added 2 || fail "capacity-one text was not captured"
DISPLAY=$DPY WMAKER_IPC_SOCKET="$SOCKET" "$CLIPTEST" fake-tick unkeyed >>"$LOG" 2>&1 &
IPC=$!
echo "$IPC" >> "$PIDFILE"
wait_log "keyboard grab succeeded" || fail "capacity-one popup did not open"
DISPLAY=$DPY xdotool key p s || fail "cannot pin and save capacity-one entry"
wait_log "saved clipboard entry and copied its path" || fail "pinned capacity-one save failed"
[ "$(cat "$RUNTIME/data/WindowMaker/Clipboard/export_1.txt")" = "single entry" ] ||
	fail "capacity-one export was not retained"
path="$RUNTIME/data/WindowMaker/Clipboard/export_1.txt"
[ "$(DISPLAY=$DPY xclip -selection clipboard -target UTF8_STRING -o)" = "$path" ] ||
	fail "capacity-one CLIPBOARD path differs"
[ "$(DISPLAY=$DPY xclip -selection primary -target UTF8_STRING -o)" = "$path" ] ||
	fail "capacity-one PRIMARY path differs"
DISPLAY=$DPY xclip -selection primary -target text/uri-list -o > "$RUNTIME/uri" ||
	fail "capacity-one PRIMARY URI unavailable"
printf 'file://%s\r\n' "$path" > "$RUNTIME/expected"
cmp -s "$RUNTIME/uri" "$RUNTIME/expected" || fail "capacity-one PRIMARY URI differs"
[ ! -e "$RUNTIME/state" ] || fail "non-persistent save created a history state directory"
stop_owned "$IPC" || fail "cannot stop capacity-one save IPC server"
IPC=
rm -f "$SOCKET"
: > "$LOG"
DISPLAY=$DPY WMAKER_IPC_SOCKET="$SOCKET" "$CLIPTEST" fake-tick unkeyed >>"$LOG" 2>&1 &
IPC=$!
echo "$IPC" >> "$PIDFILE"
wait_log "keyboard grab succeeded" || fail "capacity-one verification popup did not open"
DISPLAY=$DPY xdotool key Return || fail "cannot activate pinned original"
wait_log "activated history entry 0" || fail "pinned original was not retained"
[ "$(DISPLAY=$DPY xclip -selection clipboard -target STRING -o)" = "single entry" ] ||
	fail "capacity-one history was changed by save"
printf 'PASS: pinned capacity-one save published both selections without persistence\n'

stop_owned "$IPC" || fail "cannot stop capacity-one IPC server"
IPC=
stop_owned "$SOURCE" || fail "cannot stop capacity-one text source"
SOURCE=
stop_owned "$CLIPSOURCE" || fail "cannot stop capacity-one image source"
CLIPSOURCE=
stop_owned "$DAEMON" || fail "cannot stop capacity-one daemon"
DAEMON=
rm -f "$SOCKET"
: > "$LOG"
DISPLAY=$DPY HOME="$RUNTIME" XDG_RUNTIME_DIR="$RUNTIME" \
XDG_DATA_HOME="$RUNTIME/data" XDG_STATE_HOME="$RUNTIME/state" \
WMAKER_USER_ROOT="$RUNTIME/config" WMAKER_USER_XDG=1 WMAKER_IPC_SOCKET="$SOCKET" \
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	"$WMCLIPD" --clipboard --history 3 --history-file "$RUNTIME/base64-history" --debug >>"$LOG" 2>&1 &
DAEMON=$!
echo "$DAEMON" >> "$PIDFILE"
wait_log "monitoring CLIPBOARD and PRIMARY" || fail "Base64 daemon did not start"
DISPLAY=$DPY "$CLIPTEST" receive-paste >>"$LOG" 2>&1 &
RECEIVER=$!
echo "$RECEIVER" >> "$PIDFILE"
wait_log "paste receiver ready" || fail "paste receiver did not start"
DISPLAY=$DPY "$CLIPTEST" serve-string "base64 text" >>"$LOG" 2>&1 &
SOURCE=$!
echo "$SOURCE" >> "$PIDFILE"
wait_log "history: added entry" || fail "Base64 source was not captured"
DISPLAY=$DPY WMAKER_IPC_SOCKET="$SOCKET" "$CLIPTEST" fake-tick unkeyed >>"$LOG" 2>&1 &
IPC=$!
echo "$IPC" >> "$PIDFILE"
wait_log "keyboard grab succeeded" || fail "Base64 popup did not open"
DISPLAY=$DPY xdotool key b || fail "cannot send Base64 key"
wait_log "published Base64 text to CLIPBOARD and PRIMARY" || fail "Base64 publication failed"
wait_log "paste key state=0x1 synthetic=0" || fail "Shift+Insert was not delivered"
[ "$(DISPLAY=$DPY xclip -selection clipboard -target UTF8_STRING -o)" = \
	"data:text/plain;charset=utf-8;base64,YmFzZTY0IHRleHQ=" ] || fail "CLIPBOARD Base64 differs"
[ "$(DISPLAY=$DPY xclip -selection primary -target UTF8_STRING -o)" = \
	"data:text/plain;charset=utf-8;base64,YmFzZTY0IHRleHQ=" ] || fail "PRIMARY Base64 differs"
stop_owned "$IPC" || fail "cannot stop Base64 IPC server"
IPC=
stop_owned "$SOURCE" || fail "cannot stop Base64 source"
SOURCE=
stop_owned "$DAEMON" || fail "cannot stop Base64 daemon"
DAEMON=
wait_log "history: saved 1 entry to $RUNTIME/base64-history" || fail "Base64 changed history"
printf 'PASS: Base64 published both selections and submitted Shift+Insert without changing history\n'

rm -f "$SOCKET"
: > "$LOG"
DISPLAY=$DPY HOME="$RUNTIME" XDG_RUNTIME_DIR="$RUNTIME" \
XDG_DATA_HOME="$RUNTIME/data" XDG_STATE_HOME="$RUNTIME/state" \
WMAKER_USER_ROOT="$RUNTIME/config" WMAKER_USER_XDG=1 WMAKER_IPC_SOCKET="$SOCKET" \
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	"$WMCLIPD" --clipboard --history 3 --history-file "$RUNTIME/base64-history" --debug >>"$LOG" 2>&1 &
DAEMON=$!
echo "$DAEMON" >> "$PIDFILE"
wait_log "history: loaded 1 entry" || fail "Base64 history did not reload"
DISPLAY=$DPY xdotool keydown Super_L || fail "cannot hold invocation modifier"
DISPLAY=$DPY WMAKER_IPC_SOCKET="$SOCKET" "$CLIPTEST" fake-tick keyed >>"$LOG" 2>&1 &
IPC=$!
echo "$IPC" >> "$PIDFILE"
wait_log "keyboard grab succeeded" || fail "held-modifier Base64 popup did not open"
DISPLAY=$DPY xdotool key b || fail "cannot send held-modifier Base64 key"
wait_log "published Base64 text to CLIPBOARD and PRIMARY" || fail "held Base64 publication failed"
sleep 0.6
if grep -qF "paste key state=" "$LOG"; then
	fail "Base64 paste was not deferred while the modifier was down"
fi
DISPLAY=$DPY xdotool keyup Super_L || fail "cannot release invocation modifier"
wait_log "paste key state=0x1 synthetic=0" || fail "deferred Shift+Insert was not delivered"
printf 'PASS: Base64 paste waited for the invocation modifier to be released\n'

stop_owned "$IPC" || fail "cannot stop held-modifier IPC server"
IPC=
rm -f "$SOCKET"
: > "$LOG"
DISPLAY=$DPY xdotool keydown Super_L || fail "cannot hold modifier for ownership-loss test"
DISPLAY=$DPY WMAKER_IPC_SOCKET="$SOCKET" "$CLIPTEST" fake-tick keyed >>"$LOG" 2>&1 &
IPC=$!
echo "$IPC" >> "$PIDFILE"
wait_log "keyboard grab succeeded" || fail "ownership-loss popup did not open"
DISPLAY=$DPY xdotool key b || fail "cannot send ownership-loss Base64 key"
wait_log "published Base64 text to CLIPBOARD and PRIMARY" || fail "ownership-loss publication failed"
DISPLAY=$DPY "$CLIPTEST" serve-string "new primary owner" >>"$LOG" 2>&1 &
SOURCE=$!
echo "$SOURCE" >> "$PIDFILE"
wait_log "Base64 paste cancelled: generated selection ownership was lost" ||
	fail "selection loss did not cancel deferred paste"
DISPLAY=$DPY xdotool keyup Super_L || fail "cannot release ownership-loss modifier"
sleep 0.4
if grep -qF "paste key state=" "$LOG"; then
	fail "paste was submitted after selection ownership was lost"
fi
printf 'PASS: losing a generated selection cancels deferred paste\n'

stop_owned "$IPC" || fail "cannot stop ownership-loss IPC server"
IPC=
stop_owned "$SOURCE" || fail "cannot stop replacement selection owner"
SOURCE=
rm -f "$SOCKET"
: > "$LOG"
DISPLAY=$DPY xdotool keydown Super_L || fail "cannot hold modifier for target-loss test"
DISPLAY=$DPY WMAKER_IPC_SOCKET="$SOCKET" "$CLIPTEST" fake-tick keyed >>"$LOG" 2>&1 &
IPC=$!
echo "$IPC" >> "$PIDFILE"
wait_log "keyboard grab succeeded" || fail "target-loss popup did not open"
DISPLAY=$DPY xdotool key b || fail "cannot send target-loss Base64 key"
wait_log "published Base64 text to CLIPBOARD and PRIMARY" || fail "target-loss publication failed"
stop_owned "$RECEIVER" || fail "cannot destroy pending paste target"
RECEIVER=
DISPLAY=$DPY xdotool keyup Super_L || fail "cannot release target-loss modifier"
wait_log "Base64 paste cancelled: focus target no longer exists" ||
	fail "destroyed target did not cancel deferred paste"
printf 'PASS: destroying the deferred paste target cancels submission\n'

stop_owned "$IPC" || fail "cannot stop target-loss IPC server"
IPC=
rm -f "$SOCKET"
: > "$LOG"
DISPLAY=$DPY "$CLIPTEST" receive-paste >>"$LOG" 2>&1 &
RECEIVER=$!
echo "$RECEIVER" >> "$PIDFILE"
wait_log "paste receiver ready" || fail "replacement paste receiver did not start"
DISPLAY=$DPY "$CLIPTEST" serve-clipboard-file "$RUNTIME/original.png" >>"$LOG" 2>&1 &
CLIPSOURCE=$!
echo "$CLIPSOURCE" >> "$PIDFILE"
wait_log "history: added entry" || fail "Base64 image was not captured"
DISPLAY=$DPY WMAKER_IPC_SOCKET="$SOCKET" "$CLIPTEST" fake-tick unkeyed >>"$LOG" 2>&1 &
IPC=$!
echo "$IPC" >> "$PIDFILE"
wait_log "keyboard grab succeeded" || fail "Base64 image popup did not open"
DISPLAY=$DPY xdotool key b || fail "cannot send image Base64 key"
wait_log "paste key state=0x1 synthetic=0" || fail "image Base64 did not use Shift+Insert"
[ "$(DISPLAY=$DPY xclip -selection clipboard -target UTF8_STRING -o)" = \
	"data:image/png;base64,b3JpZ2luYWwgaW1hZ2U=" ] || fail "image Base64 output differs"
printf 'PASS: image Base64 uses the text paste chord\n'

stop_owned "$IPC" || fail "cannot stop image Base64 IPC server"
IPC=
stop_owned "$CLIPSOURCE" || fail "cannot stop image Base64 source"
CLIPSOURCE=
rm -f "$SOCKET"
: > "$LOG"
printf '\033$(B\044\042\033(B\000Z' > "$RUNTIME/compound"
DISPLAY=$DPY "$CLIPTEST" serve-clipboard-typed COMPOUND_TEXT "$RUNTIME/compound" >>"$LOG" 2>&1 &
CLIPSOURCE=$!
echo "$CLIPSOURCE" >> "$PIDFILE"
wait_log "history: added entry" || fail "compound text was not captured"
DISPLAY=$DPY WMAKER_IPC_SOCKET="$SOCKET" "$CLIPTEST" fake-tick unkeyed >>"$LOG" 2>&1 &
IPC=$!
echo "$IPC" >> "$PIDFILE"
wait_log "keyboard grab succeeded" || fail "compound text popup did not open"
DISPLAY=$DPY xdotool key b || fail "cannot send compound text Base64 key"
wait_log "paste key state=0x1 synthetic=0" || fail "compound text paste was not submitted"
[ "$(DISPLAY=$DPY xclip -selection clipboard -target UTF8_STRING -o)" = \
	"data:text/plain;charset=utf-8;base64,44GCAFo=" ] || fail "compound text Base64 differs"
printf 'PASS: compound text is decoded without truncating embedded NULs\n'

stop_owned "$IPC" || fail "cannot stop compound text IPC server"
IPC=
stop_owned "$CLIPSOURCE" || fail "cannot stop compound text source"
CLIPSOURCE=
rm -f "$SOCKET"
: > "$LOG"
printf '\033$(B\044' > "$RUNTIME/bad-compound"
DISPLAY=$DPY "$CLIPTEST" serve-clipboard-typed COMPOUND_TEXT "$RUNTIME/bad-compound" >>"$LOG" 2>&1 &
CLIPSOURCE=$!
echo "$CLIPSOURCE" >> "$PIDFILE"
wait_log "history: added entry" || fail "malformed compound text was not captured"
DISPLAY=$DPY xclip -selection primary -target UTF8_STRING -o > "$RUNTIME/encoded" ||
	fail "cannot read PRIMARY before compound text rejection"
DISPLAY=$DPY WMAKER_IPC_SOCKET="$SOCKET" "$CLIPTEST" fake-tick unkeyed >>"$LOG" 2>&1 &
IPC=$!
echo "$IPC" >> "$PIDFILE"
wait_log "keyboard grab succeeded" || fail "malformed compound text popup did not open"
DISPLAY=$DPY xdotool key b || fail "cannot send malformed compound text Base64 key"
wait_log "invalid or unsupported compound text" || fail "malformed compound text was not rejected"
DISPLAY=$DPY xclip -selection clipboard -target COMPOUND_TEXT -o > "$RUNTIME/retrieved" ||
	fail "cannot read rejected compound text"
cmp -s "$RUNTIME/bad-compound" "$RUNTIME/retrieved" || fail "rejection changed CLIPBOARD"
DISPLAY=$DPY xclip -selection primary -target UTF8_STRING -o > "$RUNTIME/retrieved" ||
	fail "cannot read PRIMARY after compound text rejection"
cmp -s "$RUNTIME/encoded" "$RUNTIME/retrieved" || fail "rejection changed PRIMARY"
if grep -qF "paste key state=" "$LOG"; then
	fail "malformed compound text submitted a paste"
fi
printf 'PASS: malformed compound text leaves both selections unchanged\n'

stop_owned "$IPC" || fail "cannot stop malformed compound IPC server"
IPC=
stop_owned "$CLIPSOURCE" || fail "cannot stop malformed compound source"
CLIPSOURCE=
rm -f "$SOCKET"
: > "$LOG"
dd if=/dev/zero of="$RUNTIME/large.png" bs=1024 count=160 2>/dev/null ||
	fail "cannot create large image fixture"
DISPLAY=$DPY "$CLIPTEST" serve-clipboard-file "$RUNTIME/large.png" >>"$LOG" 2>&1 &
CLIPSOURCE=$!
echo "$CLIPSOURCE" >> "$PIDFILE"
wait_log "history: added entry" || fail "large image was not captured"
DISPLAY=$DPY WMAKER_IPC_SOCKET="$SOCKET" "$CLIPTEST" fake-tick unkeyed >>"$LOG" 2>&1 &
IPC=$!
echo "$IPC" >> "$PIDFILE"
wait_log "keyboard grab succeeded" || fail "large Base64 popup did not open"
DISPLAY=$DPY xdotool key b || fail "cannot send large Base64 key"
wait_log "published Base64 text to CLIPBOARD and PRIMARY" || fail "large Base64 publication failed"
printf 'data:image/png;base64,' > "$RUNTIME/encoded"
base64 < "$RUNTIME/large.png" | tr -d '\n' >> "$RUNTIME/encoded"
DISPLAY=$DPY xclip -selection clipboard -target UTF8_STRING -o > "$RUNTIME/retrieved" ||
	fail "large CLIPBOARD transfer failed"
cmp -s "$RUNTIME/encoded" "$RUNTIME/retrieved" || fail "large CLIPBOARD transfer differed"
DISPLAY=$DPY xclip -selection primary -target UTF8_STRING -o > "$RUNTIME/retrieved" ||
	fail "large PRIMARY transfer failed"
cmp -s "$RUNTIME/encoded" "$RUNTIME/retrieved" || fail "large PRIMARY transfer differed"
wait_log "starting INCR transfer" || fail "large Base64 did not use INCR"
printf 'PASS: both Base64 selections support exact INCR-sized transfers\n'

stop_owned "$IPC" || fail "cannot stop large Base64 IPC server"
IPC=
stop_owned "$CLIPSOURCE" || fail "cannot stop large Base64 source"
CLIPSOURCE=
rm -f "$SOCKET"
: > "$LOG"
dd if=/dev/zero of="$RUNTIME/oversized.png" bs=1048577 count=1 2>/dev/null ||
	fail "cannot create oversized image fixture"
DISPLAY=$DPY "$CLIPTEST" serve-clipboard-file-incr "$RUNTIME/oversized.png" >>"$LOG" 2>&1 &
CLIPSOURCE=$!
echo "$CLIPSOURCE" >> "$PIDFILE"
wait_log "history: added entry" || fail "oversized image was not captured"
before=$(DISPLAY=$DPY "$CLIPTEST" read-clipboard) || fail "cannot read oversized CLIPBOARD"
DISPLAY=$DPY xclip -selection primary -target UTF8_STRING -o > "$RUNTIME/encoded" ||
	fail "cannot read previous PRIMARY"
DISPLAY=$DPY WMAKER_IPC_SOCKET="$SOCKET" "$CLIPTEST" fake-tick unkeyed >>"$LOG" 2>&1 &
IPC=$!
echo "$IPC" >> "$PIDFILE"
wait_log "keyboard grab succeeded" || fail "oversized Base64 popup did not open"
DISPLAY=$DPY xdotool key b || fail "cannot send oversized Base64 key"
wait_log "cannot encode missing or oversized clipboard data" || fail "oversized input was not rejected"
after=$(DISPLAY=$DPY "$CLIPTEST" read-clipboard) || fail "cannot read unchanged oversized CLIPBOARD"
[ "$before" = "$after" ] || fail "oversized Base64 changed CLIPBOARD"
DISPLAY=$DPY xclip -selection primary -target UTF8_STRING -o > "$RUNTIME/retrieved" ||
	fail "cannot read unchanged PRIMARY"
cmp -s "$RUNTIME/encoded" "$RUNTIME/retrieved" || fail "oversized Base64 changed PRIMARY"
if grep -qF "paste key state=" "$LOG"; then
	fail "oversized Base64 submitted a paste"
fi
printf 'PASS: oversized Base64 input leaves both selections unchanged\n'

stop_owned "$IPC" || fail "cannot stop oversized IPC server"
IPC=
stop_owned "$RECEIVER" || fail "cannot stop paste receiver"
RECEIVER=
rm -f "$SOCKET"
: > "$LOG"
DISPLAY=$DPY WMAKER_IPC_SOCKET="$SOCKET" "$CLIPTEST" fake-tick unkeyed >>"$LOG" 2>&1 &
IPC=$!
echo "$IPC" >> "$PIDFILE"
wait_log "keyboard grab succeeded" || fail "unfocused Base64 popup did not open"
DISPLAY=$DPY xdotool key Down b || fail "cannot send unfocused Base64 key"
wait_log "published Base64 text to CLIPBOARD and PRIMARY" || fail "unfocused publication failed"
wait_log "Base64 paste cancelled: no usable focus target" ||
	fail "unusable Base64 focus target was not reported"
printf 'PASS: publication survives an unusable paste target\n'

stop_owned "$IPC" || fail "cannot stop unfocused IPC server"
IPC=
stop_owned "$CLIPSOURCE" || fail "cannot stop oversized image source"
CLIPSOURCE=
stop_owned "$DAEMON" || fail "cannot stop Base64 daemon"
DAEMON=
stop_owned "$XSVR" || fail "cannot stop first test display"
XSVR=
DPY=${WMCLIPD_ACTIONS_FALLBACK_DISPLAY:-:172}
if DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; then
	printf 'SKIP: fallback display %s is already in use\n' "$DPY"
	exit 0
fi
rm -f "$SOCKET"
: > "$LOG"
Xvfb "$DPY" -screen 0 800x600x24 -ac -extension XTEST >>"$LOG" 2>&1 &
XSVR=$!
echo "$XSVR" >> "$PIDFILE"
n=0
while ! DISPLAY=$DPY xdpyinfo >/dev/null 2>&1; do
	n=$((n + 1))
	[ "$n" -lt 100 ] || fail "fallback Xvfb did not start"
	sleep 0.1
done
if DISPLAY=$DPY xdpyinfo | grep -qw XTEST; then
	fail "fallback X server did not disable XTest"
fi
DISPLAY=$DPY HOME="$RUNTIME" XDG_RUNTIME_DIR="$RUNTIME" \
XDG_DATA_HOME="$RUNTIME/data" XDG_STATE_HOME="$RUNTIME/state" \
WMAKER_USER_ROOT="$RUNTIME/config" WMAKER_USER_XDG=1 WMAKER_IPC_SOCKET="$SOCKET" \
LD_LIBRARY_PATH="$WMLIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
	"$WMCLIPD" --clipboard --history 3 --debug >>"$LOG" 2>&1 &
DAEMON=$!
echo "$DAEMON" >> "$PIDFILE"
wait_log "monitoring CLIPBOARD and PRIMARY" || fail "fallback daemon did not start"
DISPLAY=$DPY "$CLIPTEST" receive-paste >>"$LOG" 2>&1 &
RECEIVER=$!
echo "$RECEIVER" >> "$PIDFILE"
wait_log "paste receiver ready" || fail "fallback receiver did not start"
DISPLAY=$DPY "$CLIPTEST" serve-string "fallback" >>"$LOG" 2>&1 &
SOURCE=$!
echo "$SOURCE" >> "$PIDFILE"
wait_log "history: added entry" || fail "fallback source was not captured"
DISPLAY=$DPY WMAKER_IPC_SOCKET="$SOCKET" "$CLIPTEST" fake-tick unkeyed >>"$LOG" 2>&1 &
IPC=$!
echo "$IPC" >> "$PIDFILE"
wait_log "keyboard grab succeeded" || fail "fallback popup did not open"
popup=$(sed -n 's/.*popup window \(0x[0-9a-f]*\).*/\1/p' "$LOG" | tail -n 1)
[ -n "$popup" ] || fail "fallback popup window was not reported"
DISPLAY=$DPY "$CLIPTEST" send-popup-b "$popup" || fail "cannot send fallback Base64 key"
wait_log "paste key state=0x1 synthetic=1" || fail "synthetic Shift+Insert was not delivered"
[ "$(DISPLAY=$DPY xclip -selection clipboard -target UTF8_STRING -o)" = \
	"data:text/plain;charset=utf-8;base64,ZmFsbGJhY2s=" ] || fail "fallback Base64 output differs"
printf 'PASS: Base64 paste falls back to checked synthetic events without XTest\n'
