# Window Maker

Window Maker is a fast X11 window manager emulating the NeXTSTEP look
and feel. It is easy to configure and the entire appearance is
controlled through WPrefs.

> This is the `next` branch, where active development happens.

## Highlights

- **Modern text rendering** — every string is shaped and drawn through
  Pango and cairo, so complex scripts (Arabic, Devanagari, CJK, ...)
  and per-script font fallback just work.
- **HiDPI that works** — decorations, dialogs and WPrefs scale with
  `Xft.dpi`, and window icons load natively from SVG with freedesktop
  icon-theme fallback, so nothing turns blurry at high pixel density.
- **libxcb only** — the window manager, libWINGs, WPrefs and the
  utilities talk to the X server through libxcb and libxkbcommon, with
  optional input-method support (fcitx5, ibus). libX11 is not used.
- **Zero idle CPU** — the event loop sleeps until there is real work.
- **Scriptable IPC** — a Unix-socket control channel drives commands,
  typed state queries and an event stream, with `wmakermsg` as the
  command-line client.
- **Multi-monitor** through RandR.
- **Modern window management** — per-workspace tiling layouts, windows
  on several workspaces at once, tab groups, regex-based window rules,
  and an optional virtual desktop larger than the screen.
- **Session integration** — XSMP session management, display-off
  hooks, and the XDG data/cache directory split.
- **Drag a file onto the dock** to create a launcher.
- **Sound events** through an optional daemon.

## New utilities

| Binary | What it does |
|---|---|
| `wmclipd` | clipboard daemon |
| `wmglyph` | Unicode glyph selector |
| `wmlock` | screen locker |
| `wmsoundd` | sound event daemon (PipeWire) |
| `wmtouchd` | touchpad gesture daemon (libinput) |

## Driving the window manager

The running window manager exposes a Unix-socket IPC channel.
`wmakermsg` sends commands, queries state, and subscribes to events:

```sh
wmakermsg workspace next
wmakermsg set-layout 2 master-stack
wmakermsg action Hide
wmakermsg get_window_tree
wmakermsg -m workspace window
```

The wire format is documented in `doc/wmaker-ipc.txt`, the client
helpers live in `<WINGs/WIPC.h>`, and `wmakermsg(1)` covers the
command-line interface.

## Build

```sh
./autogen.sh
./configure
make -j$(nproc)
make install
```

Build dependencies include libxcb and its utility modules, xkbcommon,
libev, cairo with the xcb backend and Pango. The complete list is in
`INSTALL-WMAKER`.

## Test

```sh
make check          # unit suites — no X server needed
make check-drive    # integration drivers on a private X server
make checkpatch     # style + commit-message lint
```

Every feature ships with a driver that runs it under AddressSanitizer
on a private X server and asserts the observable result.

## Documentation

- **`NEWS`** — the changelog. Read this first.
- **`INSTALL-WMAKER`** — build dependencies and configure options.
- **`FAQ`** — frequently asked questions.
- **`doc/wmaker-ipc.txt`** — the IPC wire protocol.
- **`README.i18n`** — internationalization notes.
- **`BUGFORM`** — bug-report template.
- **`TODO`** — plans for the future.

## Contact

- Web: <http://windowmaker.org>

## Copyright

Window Maker is copyrighted by the Window Maker Team, Alfredo K.
Kojima and Dan Pascu and is licensed through the GNU General Public
License. Read the `COPYING` file for the complete license. NeXT,
OpenStep and NEXTSTEP are trademarks of NeXT Computer, Inc. All other
trademarks are property of their respective owners.
