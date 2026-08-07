# Window Maker — `next` branch

> **This is where Window Maker is happening.** The `next` branch carries
> the active development.

---

## Why `next`

### Zero idle CPU

When nothing is happening, Window Maker does **nothing**. The libev
event loop blocks in `epoll_wait()` and only returns when the X fd, the
IPC socket, an inotify watch, or a timer fires.

```
idle: 0 event-loop wakeups in 3s = 0.0/s   (limit 15)
```

No root `PointerMotion` selection. No polling timer. No wakeups while
your desktop sits there. `master` never measured this; `next` has a
[regression test](test/idle-wakeups-drive.sh) that asserts it stays at
zero.

### Bounded cold-boot cost

Startup does **274 X11 replies** total, with only **7 `GetProperty`
round-trips** in the manage path (limit ≤ 350). The bulk is a pipelined
`InternAtom` batch — one round-trip for many replies. A serial
`GetProperty` or `XSync` sneaking back into the manage path trips the
[regression assertion](test/roundtrip-drive.sh).

### Modern text rendering

Every pixel of text — window titles, menus, OSD popups, help balloons,
WINGs widget labels — is shaped and rendered through **PangoCairo**.
Per-script font fallback and complex-script shaping (Arabic, Devanagari,
CJK, ...) work out of the box. `master` uses legacy Xft with no
fallback.

### HiDPI that works

Window decorations, dialogs, and WPrefs scale with `Xft.dpi`. Set it
once in `~/.Xresources` and everything follows. Change it at runtime and
the stored scale updates live.

### SVG icons

Window icons load natively from SVG at the target pixel size. No raster
pre-conversion, no blurry scaling. Plus freedesktop hicolor-theme
fallback when an icon isn't in the traditional `IconPath`.

### TrueColor-only, libXinerama dropped

The dead PseudoColor/GrayScale/dither rendering paths — the ones that
have not run on real hardware in two decades — are gone. `wrlib` now
requires a TrueColor visual (depth 24 or 32), which every X server in
use today exposes. In return, `convert.c` lost ~1600 lines of
unreachable colormap code and the `WRASTER_COLOR_RESOLUTION` env var is
dropped.

The `libXinerama` link dependency is also gone: multi-head geometry is
authoritative via **RandR 1.5** `GetMonitors`, with the XINERAMA wire
protocol spoken directly (no library) as a fallback for nested servers.
The `USE_XINERAMA` build switch no longer exists —
`_NET_WM_FULLSCREEN_MONITORS`, the per-head workspace-name placement,
and the `-X`/`--xinerama` `wmsetbg` flag are all unconditional now.

---

## Window management you asked for

### Per-workspace tiling

Each workspace can carry an opt-in tiling layout — master-stack,
columns, or rows — with configurable master count and ratio. Ten new
keyboard shortcuts control it; the `set-layout` IPC verb drives it
programmatically. Set `NoTiling = YES` per-window to exempt individual
apps.

### Windows on multiple workspaces

A window can belong to several workspaces at once. Pin your terminal
to every workspace, or toggle membership on the fly with
`ToggleWorkspace1Key..ToggleWorkspace10Key`.

### Tab groups

Tab windows together into a single frame. The titlebar segments paint
through one cairo context for clean visual continuity.

### Dynamic window rules

`WMWindowAttributes` accepts a `DynamicRules` array that matches windows
by regex on class, instance, title, role, command, machine, or window
type — and overlays attributes on top of the usual precedence stack:

```
DynamicRules = (
  { MatchClass = "^Firefox$"; MatchTitle = ".*YouTube.*";
    Attributes = { NoTitlebar = YES; KeepOnTop = YES; }; }
);
```

`$_HOSTNAME`-style env substitution keeps rule files portable across
machines. `MatchTitle` re-evaluates live when a window renames itself.

### Per-window frame borders

All seven `Frame*BorderColor` / `Frame*BorderWidth` keys, plus a new
**inner frame border**, are now settable per window — not just globally.
Pin a window class to a specific geometry with `StartGeometry =
"800x600+0+0"`.

---

## Integration that respects your session

### XSMP session manager support

Started under a session manager, Window Maker opens an XSMP connection
so logout is orderly and restart works. No more "wmaker didn't save my
workspace layout."

### Display-off awareness

Decorative animations pause when no display is showing. Two hooks fire
on transitions:

```
DisplayOffCommand = "lock-screen --immediately";
DisplayOnCommand  = "";
```

### XDG data / cache split

On the XDG layout your per-user tree is split across
`XDG_DATA_HOME` / `XDG_CONFIG_HOME` / `XDG_CACHE_HOME` instead of
nesting everything under one directory. `~/GNUstep` and explicit
`WMAKER_USER_ROOT` keep working unchanged.

### Smarter Run dialog

The Run dialog completes against freedesktop application names and
document paths, with a popup when Tab finds multiple candidates:

```
RunDialogXDGCompletion = YES;
RunDialogDocumentPaths = ("~/Documents", "~/Downloads");
```

### Drag a file onto the dock

Drop a single file from any XDND-capable file manager onto the dock and
it becomes a launcher. No configuration needed.

---

## New utilities

| Binary | What it does |
|---|---|
| `wmclipd` | Clipboard daemon with `MAX_CLIP_BYTES` bounded transfers |
| `wmglyph` | Unicode glyph selector that copies a clicked character to PRIMARY and CLIPBOARD |
| `wmlock` | Screen locker with idle clock view and DPMS blanking |

---

## Drive it from the command line

`wmakermsg` speaks the in-tree IPC socket. New verbs:

| Verb | Effect |
|---|---|
| `hide-application` | hide the focused app |
| `hide-others` | hide all but the focused app |
| `show-all` | unhide everything |
| `arrange-icons` | arrange icons |
| `window-workspace` | move window between workspaces |
| `set-layout` | set a workspace's tiling layout |
| `display-power` | blank / unblank via ScreenSaver notify |

```sh
wmakermsg hide-application
wmakermsg set-layout 2 master-stack
wmakermsg workspace next
```

---

## Performance

`make bench` runs the benchmarks below:

**Cold-boot X11 round-trips** — 274 replies, 7 `GetProperty` (limit 350).
The pipelined `InternAtom`/`GetAtomName` batches keep the startup path
round-trip-free; the regression test catches any serial `GetProperty`
that creeps back in.

**Idle wakeups** — 0.0/s. The event loop blocks in `epoll_wait()` and
does not wake unless there is real work.

---

## Build

```sh
./autogen.sh
./configure --enable-debug
make -j$(nproc)
```

**New build dependencies** (vs `master`): `cairo` (with `cairo-xcb`) and
`pangocairo`. Both are standard on modern Linux distributions. See
`INSTALL-WMAKER` for the full list.

---

## Test

```sh
make check          # unit suites — no X server needed
make check-drive    # integration drivers on a private X server
make checkpatch     # style + commit-message lint
```

Every feature on `next` has a drive-test that runs it under
AddressSanitizer on a private Xvfb and asserts the observable result
through EWMH atoms — not internal state.

---

## Documentation

- **`NEWS`** — the full changelog with per-feature detail. Read this first.
- **`INSTALL-WMAKER`** — build dependencies and configure options.
- **`FAQ`** / **`FAQ.I18N`** — frequently asked questions.
- **`BUGFORM`** — bug-report template.
- **`TODO`** — plans for the future.

---

## Contact

- Web: <http://windowmaker.org>

---

## Copyright

Window Maker is copyrighted by the Window Maker Team, Alfredo K.
Kojima and Dan Pascu and is licensed through the GNU General Public
License. Read the `COPYING` file for the complete license. NeXT,
OpenStep and NEXTSTEP are trademarks of NeXT Computer, Inc. All other
trademarks are property of their respective owners.
