/*
 *  WXCore.h
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, see <https://www.gnu.org/licenses/>.
 */

/* WXCore.h - composed core-protocol helpers (xcb backend)
 *
 * Small multi-request compositions over the core protocol that more than
 * one caller needs: barriers, cut buffers, XBM bitmaps, window restack/
 * withdraw. Property and ICCCM I/O stay in WXProp/WXHints; this header
 * holds only the core-request layer so a backend seam has one home.
 */

#ifndef WINGS_WXCORE_H
#define WINGS_WXCORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

/* Colour value, the Xlib XColor 16-bit component convention (wXParseColor
 * and wXAllocColor fill it, W_SetSourceXColor consumes it). */
typedef struct {
	uint32_t pixel;		/* server pixel value */
	uint16_t red, green, blue;
} WXColor;

/* Window-attribute list for create/change-window-attributes, Xlib field
 * order and names so positional initialisers stay valid. .visual is a
 * resolved xcb_visualtype_t* (wXWindowAttrValues writes its visual_id). */
typedef struct {
	xcb_pixmap_t background_pixmap;
	uint32_t background_pixel;
	xcb_pixmap_t border_pixmap;
	uint32_t border_pixel;
	uint32_t bit_gravity;
	uint32_t win_gravity;
	uint32_t backing_store;
	uint32_t backing_planes;
	uint32_t backing_pixel;
	bool save_under;
	uint32_t event_mask;
	uint32_t do_not_propagate_mask;
	bool override_redirect;
	xcb_colormap_t colormap;
	xcb_cursor_t cursor;
	xcb_visualtype_t *visual;
} WXSetWindowAttributes;

/* XWindowChanges equivalent for wXConfigureWindow. */
typedef struct {
	int x, y;
	uint32_t width, height;
	uint32_t border_width;
	xcb_window_t sibling;
	uint32_t stack_mode;
} WXWindowChanges;

/* XGetWindowAttributes equivalent, the fields the callers actually use. */
typedef struct {
	int x, y;
	unsigned int width, height;
	unsigned int border_width;
	unsigned int depth;
	int class;		/* XCB_WINDOW_CLASS_INPUT_OUTPUT / _INPUT_ONLY */
	int map_state;		/* XCB_MAP_STATE_* */
	xcb_colormap_t colormap;
	xcb_visualtype_t *visual;
	xcb_window_t root;
	uint32_t all_event_masks;
	uint32_t your_event_mask;
	bool override_redirect;
} WXWindowAttributes;

/* XBM loader status, the Xutil.h Bitmap* codes under project spellings. */
typedef enum {
	WXBitmapSuccess = 0,
	WXBitmapOpenFailed,
	WXBitmapFileInvalid,
	WXBitmapNoMemory
} WXBitmapStatus;

/* the parsed XBM file contents: rows are byte-packed ((width+7)/8
 * bytes per row), data is wmalloc'd and the caller wfrees it */
typedef struct {
	unsigned int width, height;
	int x_hot, y_hot;	/* -1 when the #define is absent */
	unsigned char *data;
	size_t data_len;
} WXBitmapData;

/* Parse the X11 XBM file format from an open FILE: the four #define
 * lines, the one static data array and the X10 `short' rejection.
 * Zeroes *out first; on FileInvalid only the zeroed fields are
 * meaningful (no data to free). */
WXBitmapStatus WXBitmapParseFile(FILE *f, WXBitmapData *out);

/* Repack XBM byte-packed rows (width bits -> (width+7)/8 bytes) into
 * XYPixmap scanlines padded to the server's 32-bit scanline pad.
 * rows rows are read from bits; returns a wmalloc'd buffer of
 * rows * ((width+31)/32*4) bytes, caller wfrees. */
uint8_t *WXBMPackWire(const char *bits, unsigned int width,
		      unsigned int rows, size_t *wire_len);

/* Barrier: push the outgoing buffer, then round-trip so everything
 * queued before the call is known to have reached the server (the
 * semantics XSync(dpy, False) had while both libraries shared the
 * socket; the Xlib half of the flush is long gone). */
void wXSync(xcb_connection_t *dpy);

/* Push the outgoing buffer without waiting. */
void wXFlush(xcb_connection_t *dpy);

/* Map/stack family, Xlib argument shapes. wXMapRaised keeps libX11's
 * two-request XMapRaised shape (configure Above, then map). */
void wXMapWindow(xcb_connection_t *dpy, xcb_window_t w);
void wXMapRaised(xcb_connection_t *dpy, xcb_window_t w);
void wXMapSubwindows(xcb_connection_t *dpy, xcb_window_t w);
void wXUnmapSubwindows(xcb_connection_t *dpy, xcb_window_t w);
void wXRaiseWindow(xcb_connection_t *dpy, xcb_window_t w);
void wXLowerWindow(xcb_connection_t *dpy, xcb_window_t w);
void wXClearArea(xcb_connection_t *dpy, xcb_window_t w, int x, int y,
		 unsigned int width, unsigned int height, bool exposures);

void wXFreePixmap(xcb_connection_t *dpy, xcb_pixmap_t pixmap);

/* XConfigureWindow equivalent: Xlib CW_* mask and XWindowChanges. */
int wXConfigureWindow(xcb_connection_t *dpy, xcb_window_t w, unsigned int mask, WXWindowChanges *wc);

/* XGetGeometry equivalent, Xlib argument shape. */
bool wXGetGeometry(xcb_connection_t *dpy, xcb_drawable_t d, xcb_window_t *root, int *x, int *y,
		   unsigned int *width, unsigned int *height,
		   unsigned int *border_width, unsigned int *depth);

void wXDeleteProperty(xcb_connection_t *dpy, xcb_window_t w, xcb_atom_t prop);
void wXFreeColors(xcb_connection_t *dpy, xcb_colormap_t cmap, uint32_t *pixels,
		  int npixels, uint32_t planes);

/* XQueryTree equivalent. The children array is wmalloc()ed and must be
 * released with wfree(); children/root/parent may be NULL. */
bool wXQueryTree(xcb_connection_t *dpy, xcb_window_t w, xcb_window_t *root, xcb_window_t *parent,
		 xcb_window_t **children, unsigned int *nchildren);

/* GetWindowAttributes + GetGeometry composed on WXProp and assembled
 * into a WXWindowAttributes. .visual is resolved through the xcb_setup
 * walk. vid_ret still carries the raw xcb_visualid_t and may be NULL. */
bool wXGetWindowAttributes(xcb_connection_t *dpy, xcb_window_t w, WXWindowAttributes *out,
			   xcb_visualid_t *vid_ret);

/* XTranslateCoordinates equivalent; child may be NULL. */
bool wXTranslateCoordinates(xcb_connection_t *dpy, xcb_window_t src, xcb_window_t dst,
			    int src_x, int src_y,
			    int *dest_x, int *dest_y, xcb_window_t *child);

/* Compose an XSetWindowAttributes + CW_* mask (numerically identical to
 * the XCB_CW_* mask) into the xcb value list in wire order. vals must
 * hold at least 15 slots; the return value is the xcb value mask. */
uint32_t wXWindowAttrValues(const WXSetWindowAttributes *attr, uint32_t mask,
			    uint32_t *vals);

/* Grab status codes, the values the xcb grab replies carry. Xlib's
 * spellings only agree for the first two (its GrabFrozen has no xcb
 * counterpart), so callers must use these against the helpers. */
typedef enum {
	WXGrabSuccess = 0,
	WXGrabAlreadyGrabbed = 1,
	WXGrabInvalidTime = 2,
	WXGrabNotViewable = 3
} WXGrabStatus;

/* Grabs. The grab requests are round trips; the helpers return the
 * WXGrabStatus codes so call sites keep their checks. */
int wXGrabPointer(xcb_connection_t *dpy, xcb_window_t grab, bool owner_events,
		  unsigned int event_mask, int pointer_mode, int keyboard_mode,
		  xcb_window_t confine_to, xcb_cursor_t cursor, xcb_timestamp_t time);
void wXUngrabPointer(xcb_connection_t *dpy, xcb_timestamp_t time);
int wXGrabKeyboard(xcb_connection_t *dpy, xcb_window_t grab, bool owner_events,
		   int pointer_mode, int keyboard_mode, xcb_timestamp_t time);
void wXUngrabKeyboard(xcb_connection_t *dpy, xcb_timestamp_t time);
void wXChangeActivePointerGrab(xcb_connection_t *dpy, unsigned int event_mask,
			       xcb_cursor_t cursor, xcb_timestamp_t time);
void wXAllowEvents(xcb_connection_t *dpy, int mode, xcb_timestamp_t time);
void wXWarpPointer(xcb_connection_t *dpy, xcb_window_t src, xcb_window_t dst,
		   int src_x, int src_y, unsigned int src_w, unsigned int src_h,
		   int dst_x, int dst_y);

/* XQueryPointer equivalent, same argument shape. */
bool wXQueryPointer(xcb_connection_t *dpy, xcb_window_t w, xcb_window_t *root, xcb_window_t *child,
		    int *root_x, int *root_y, int *win_x, int *win_y,
		    unsigned int *mask);

/* XQueryPointer's cross-screen refusal: false when the pointer is not
 * on the same screen as `w` (or on error), the button state in `mask`
 * otherwise. */
bool wXQueryPointerSameScreen(xcb_connection_t *dpy, xcb_window_t w,
			      unsigned int *mask);

/* Server grab: the xcb request is checked so a failure is visible
 * here rather than on the event stream (Xlib's XGrabServer was async). */
bool wXGrabServer(xcb_connection_t *dpy);
void wXUngrabServer(xcb_connection_t *dpy);

/* checked void requests: request_check()s the cookie, frees the
 * error and reports failure. Typed on the void cookie so a
 * reply-bearing cookie cannot end up in a request_check. */
bool WXCheckVoid(xcb_connection_t *dpy, xcb_void_cookie_t ck);

/* Passive grabs, Xlib argument order; errors stay asynchronous on the
 * event stream exactly as with Xlib. */
void wXGrabKey(xcb_connection_t *dpy, int keycode, unsigned int modifiers,
	       xcb_window_t grab_window, bool owner_events, int pointer_mode,
	       int keyboard_mode);
void wXUngrabKey(xcb_connection_t *dpy, int keycode, unsigned int modifiers,
		 xcb_window_t grab_window);
void wXGrabButton(xcb_connection_t *dpy, unsigned int button, unsigned int modifiers,
		  xcb_window_t grab_window, bool owner_events, unsigned int event_mask,
		  int pointer_mode, int keyboard_mode, xcb_window_t confine_to,
		  xcb_cursor_t cursor);
void wXUngrabButton(xcb_connection_t *dpy, unsigned int button, unsigned int modifiers,
		    xcb_window_t grab_window);

/* Colours. XColor keeps the 16-bit Xlib component convention so
 * callers stay unchanged. */
bool wXAllocColor(xcb_connection_t *dpy, xcb_colormap_t cmap, WXColor *color);
bool wXParseColor(xcb_connection_t *dpy, xcb_colormap_t cmap, const char *spec, WXColor *color);
bool wXQueryColor(xcb_connection_t *dpy, xcb_colormap_t cmap, WXColor *color);

/* The client-side numeric colour grammar of XParseColor: #RRR... and
 * rgb:RR/GG/BB. No connection needed, so tools and tests can use it
 * standalone; false when the spec is not one of the two forms or is
 * malformed (wXParseColor keeps Xlib's no-LookupColor-fallback for
 * malformed numeric specs). */
bool WXParseNumericColor(const char *spec, WXColor *color);

/* Walk the xcb_setup depth/visual lists for a visual id. */
xcb_visualtype_t *wXVisualTypeForID(xcb_connection_t *dpy, xcb_visualid_t vid);

/* The screen's default visual from the xcb_setup. */
xcb_visualtype_t *wXDefaultVisualType(xcb_connection_t *dpy, int screen_number);

/* Theme cursor load (xcb-cursor, core-font fallback included). The
 * name is the core cursor-font glyph name, which coincides with the
 * theme cursor name. Returns XCB_CURSOR_NONE on failure. */
xcb_cursor_t wXLoadCursor(xcb_connection_t *dpy, int screen_number, const char *name);

/* XCreatePixmapCursor equivalent, Xlib argument shape. */
xcb_cursor_t wXCreatePixmapCursor(xcb_connection_t *dpy, xcb_pixmap_t source, xcb_pixmap_t mask,
				  const WXColor *foreground,
				  const WXColor *background,
				  unsigned int x, unsigned int y);

/* Set (or clear with XCB_CURSOR_NONE) a window's cursor. */
void wXDefineCursor(xcb_connection_t *dpy, xcb_window_t w, xcb_cursor_t cursor);

/* XRecolorCursor equivalent. */
void wXRecolorCursor(xcb_connection_t *dpy, xcb_cursor_t cursor, const WXColor *fg,
		     const WXColor *bg);

/* The numbered screen record from the xcb_setup, NULL when out of
 * range (the ScreenOfDisplay/DefaultScreenOfDisplay stand-in). */
xcb_screen_t *wXScreenOf(xcb_connection_t *dpy, int screen_number);

/* Number of screens on the connection (XScreenCount). */
int wXScreenCount(xcb_connection_t *dpy);

/* Root window of the numbered screen (Xlib's RootWindow spelling). */
xcb_window_t wXRootWindow(xcb_connection_t *dpy, int screen_number);

/* Selection ownership and conversion, Xlib argument shape. */
xcb_window_t wXGetSelectionOwner(xcb_connection_t *dpy, xcb_atom_t selection);
void wXSetSelectionOwner(xcb_connection_t *dpy, xcb_atom_t selection, xcb_window_t owner, xcb_timestamp_t time);
bool wXConvertSelection(xcb_connection_t *dpy, xcb_atom_t selection, xcb_atom_t target, xcb_atom_t property,
			xcb_window_t requestor, xcb_timestamp_t time);

/* Atom name, malloc()ed, release with free(). */
char *wXGetAtomName(xcb_connection_t *dpy, xcb_atom_t atom);

/* Atom interning with a per-connection cache (xcb has no server- or
 * client-side intern cache; Xlib's is what the pre-conversion code
 * relied on). Returns XCB_ATOM_NONE when the connection fails. The
 * cache keeps its own copy of the name, so the argument may be a
 * stack buffer. */
xcb_atom_t WXInternAtom(xcb_connection_t *dpy, const char *name);

/* Batch form of XInternAtoms: names[i] -> atoms[i] in order. The
 * cache keeps its own copies of the names; the arguments may be
 * stack buffers and are not retained. */
void WXInternAtomBatch(xcb_connection_t *dpy, char **names, int count, xcb_atom_t *atoms);

/*
 * The static atom table. Every atom the tree addresses with a literal
 * name is listed once here and resolved by one WXInternAtomBatch at
 * startup into wx_static_atoms, an enum-indexed array. Call sites
 * read wx_static_atoms[W_ATOM_...] instead of re-interning the name.
 * WXInternAtom stays only for client-supplied names (Xdnd type lists,
 * per-screen selections).
 *
 * To add an atom add one row and the enum and the name array follow.
 */
#define WX_STATIC_ATOMS(_) \
	_(W_ATOM_WM_STATE,			"WM_STATE") \
	_(W_ATOM_WM_CHANGE_STATE,		"WM_CHANGE_STATE") \
	_(W_ATOM_WM_PROTOCOLS,			"WM_PROTOCOLS") \
	_(W_ATOM_WM_TAKE_FOCUS,			"WM_TAKE_FOCUS") \
	_(W_ATOM_WM_DELETE_WINDOW,		"WM_DELETE_WINDOW") \
	_(W_ATOM_WM_SAVE_YOURSELF,		"WM_SAVE_YOURSELF") \
	_(W_ATOM_WM_CLIENT_LEADER,		"WM_CLIENT_LEADER") \
	_(W_ATOM_WM_COLORMAP_WINDOWS,		"WM_COLORMAP_WINDOWS") \
	_(W_ATOM_WM_COLORMAP_NOTIFY,		"WM_COLORMAP_NOTIFY") \
	_(W_ATOM_MOTIF_WM_HINTS,		"_MOTIF_WM_HINTS") \
	_(W_ATOM_WM_IGNORE_FOCUS_EVENTS,	"WM_IGNORE_FOCUS_EVENTS") \
	_(W_ATOM_WM_WINDOW_ROLE,		"WM_WINDOW_ROLE") \
	_(W_ATOM_WINDOWMAKER_MENU,		"_WINDOWMAKER_MENU") \
	_(W_ATOM_WINDOWMAKER_STATE,		"_WINDOWMAKER_STATE") \
	_(W_ATOM_WINDOWMAKER_WM_PROTOCOLS,	"_WINDOWMAKER_WM_PROTOCOLS") \
	_(W_ATOM_WINDOWMAKER_WM_FUNCTION,	"_WINDOWMAKER_WM_FUNCTION") \
	_(W_ATOM_WINDOWMAKER_NOTICEBOARD,	"_WINDOWMAKER_NOTICEBOARD") \
	_(W_ATOM_WINDOWMAKER_COMMAND,		"_WINDOWMAKER_COMMAND") \
	_(W_ATOM_WINDOWMAKER_ICON_SIZE,		"_WINDOWMAKER_ICON_SIZE") \
	_(W_ATOM_WINDOWMAKER_ICON_TILE,		"_WINDOWMAKER_ICON_TILE") \
	_(W_ATOM_WINDOWMAKER_MARK_KEY,		"_WINDOWMAKER_MARK_KEY") \
	_(W_ATOM_WINDOWMAKER_TABGROUP,		"_WINDOWMAKER_TABGROUP") \
	_(W_ATOM_WINDOWMAKER_XDNDEXCHANGE,	"_WINDOWMAKER_XDNDEXCHANGE") \
	_(W_ATOM_GNUSTEP_WM_ATTR,		"_GNUSTEP_WM_ATTR") \
	_(W_ATOM_GNUSTEP_WM_MINIATURIZE_WINDOW,	"_GNUSTEP_WM_MINIATURIZE_WINDOW") \
	_(W_ATOM_GNUSTEP_WM_HIDE_APP,		"_GNUSTEP_WM_HIDE_APP") \
	_(W_ATOM_GNUSTEP_TITLEBAR_STATE,	"_GNUSTEP_TITLEBAR_STATE") \
	_(W_ATOM_GTK_APPLICATION_OBJECT_PATH,	"_GTK_APPLICATION_OBJECT_PATH") \
	_(W_ATOM_WINGS_DND_MOUSE_OFFSET,	"_WINGS_DND_MOUSE_OFFSET") \
	_(W_ATOM_NET_SUPPORTED,			"_NET_SUPPORTED") \
	_(W_ATOM_NET_CLIENT_LIST,		"_NET_CLIENT_LIST") \
	_(W_ATOM_NET_CLIENT_LIST_STACKING,	"_NET_CLIENT_LIST_STACKING") \
	_(W_ATOM_NET_NUMBER_OF_DESKTOPS,	"_NET_NUMBER_OF_DESKTOPS") \
	_(W_ATOM_NET_DESKTOP_GEOMETRY,		"_NET_DESKTOP_GEOMETRY") \
	_(W_ATOM_NET_DESKTOP_VIEWPORT,		"_NET_DESKTOP_VIEWPORT") \
	_(W_ATOM_NET_CURRENT_DESKTOP,		"_NET_CURRENT_DESKTOP") \
	_(W_ATOM_NET_DESKTOP_NAMES,		"_NET_DESKTOP_NAMES") \
	_(W_ATOM_NET_ACTIVE_WINDOW,		"_NET_ACTIVE_WINDOW") \
	_(W_ATOM_NET_WORKAREA,			"_NET_WORKAREA") \
	_(W_ATOM_NET_SUPPORTING_WM_CHECK,	"_NET_SUPPORTING_WM_CHECK") \
	_(W_ATOM_NET_VIRTUAL_ROOTS,		"_NET_VIRTUAL_ROOTS") \
	_(W_ATOM_NET_DESKTOP_LAYOUT,		"_NET_DESKTOP_LAYOUT") \
	_(W_ATOM_NET_SHOWING_DESKTOP,		"_NET_SHOWING_DESKTOP") \
	_(W_ATOM_NET_CLOSE_WINDOW,		"_NET_CLOSE_WINDOW") \
	_(W_ATOM_NET_MOVERESIZE_WINDOW,		"_NET_MOVERESIZE_WINDOW") \
	_(W_ATOM_NET_WM_MOVERESIZE,		"_NET_WM_MOVERESIZE") \
	_(W_ATOM_NET_RESTACK_WINDOW,		"_NET_RESTACK_WINDOW") \
	_(W_ATOM_NET_REQUEST_FRAME_EXTENTS,	"_NET_REQUEST_FRAME_EXTENTS") \
	_(W_ATOM_NET_WM_NAME,			"_NET_WM_NAME") \
	_(W_ATOM_NET_WM_VISIBLE_NAME,		"_NET_WM_VISIBLE_NAME") \
	_(W_ATOM_NET_WM_ICON_NAME,		"_NET_WM_ICON_NAME") \
	_(W_ATOM_NET_WM_VISIBLE_ICON_NAME,	"_NET_WM_VISIBLE_ICON_NAME") \
	_(W_ATOM_NET_WM_DESKTOP,		"_NET_WM_DESKTOP") \
	_(W_ATOM_NET_WM_FULLSCREEN_MONITORS,	"_NET_WM_FULLSCREEN_MONITORS") \
	_(W_ATOM_NET_WM_WINDOW_TYPE,		"_NET_WM_WINDOW_TYPE") \
	_(W_ATOM_NET_WM_WINDOW_TYPE_DESKTOP,	"_NET_WM_WINDOW_TYPE_DESKTOP") \
	_(W_ATOM_NET_WM_WINDOW_TYPE_DOCK,	"_NET_WM_WINDOW_TYPE_DOCK") \
	_(W_ATOM_NET_WM_WINDOW_TYPE_TOOLBAR,	"_NET_WM_WINDOW_TYPE_TOOLBAR") \
	_(W_ATOM_NET_WM_WINDOW_TYPE_MENU,	"_NET_WM_WINDOW_TYPE_MENU") \
	_(W_ATOM_NET_WM_WINDOW_TYPE_UTILITY,	"_NET_WM_WINDOW_TYPE_UTILITY") \
	_(W_ATOM_NET_WM_WINDOW_TYPE_SPLASH,	"_NET_WM_WINDOW_TYPE_SPLASH") \
	_(W_ATOM_NET_WM_WINDOW_TYPE_DIALOG,	"_NET_WM_WINDOW_TYPE_DIALOG") \
	_(W_ATOM_NET_WM_WINDOW_TYPE_DROPDOWN_MENU, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU") \
	_(W_ATOM_NET_WM_WINDOW_TYPE_POPUP_MENU,	"_NET_WM_WINDOW_TYPE_POPUP_MENU") \
	_(W_ATOM_NET_WM_WINDOW_TYPE_TOOLTIP,	"_NET_WM_WINDOW_TYPE_TOOLTIP") \
	_(W_ATOM_NET_WM_WINDOW_TYPE_NOTIFICATION, "_NET_WM_WINDOW_TYPE_NOTIFICATION") \
	_(W_ATOM_NET_WM_WINDOW_TYPE_COMBO,	"_NET_WM_WINDOW_TYPE_COMBO") \
	_(W_ATOM_NET_WM_WINDOW_TYPE_DND,	"_NET_WM_WINDOW_TYPE_DND") \
	_(W_ATOM_NET_WM_WINDOW_TYPE_NORMAL,	"_NET_WM_WINDOW_TYPE_NORMAL") \
	_(W_ATOM_NET_WM_STATE,			"_NET_WM_STATE") \
	_(W_ATOM_NET_WM_STATE_MODAL,		"_NET_WM_STATE_MODAL") \
	_(W_ATOM_NET_WM_STATE_STICKY,		"_NET_WM_STATE_STICKY") \
	_(W_ATOM_NET_WM_STATE_MAXIMIZED_VERT,	"_NET_WM_STATE_MAXIMIZED_VERT") \
	_(W_ATOM_NET_WM_STATE_MAXIMIZED_HORZ,	"_NET_WM_STATE_MAXIMIZED_HORZ") \
	_(W_ATOM_NET_WM_STATE_SHADED,		"_NET_WM_STATE_SHADED") \
	_(W_ATOM_NET_WM_STATE_SKIP_TASKBAR,	"_NET_WM_STATE_SKIP_TASKBAR") \
	_(W_ATOM_NET_WM_STATE_SKIP_PAGER,	"_NET_WM_STATE_SKIP_PAGER") \
	_(W_ATOM_NET_WM_STATE_HIDDEN,		"_NET_WM_STATE_HIDDEN") \
	_(W_ATOM_NET_WM_STATE_FULLSCREEN,	"_NET_WM_STATE_FULLSCREEN") \
	_(W_ATOM_NET_WM_STATE_ABOVE,		"_NET_WM_STATE_ABOVE") \
	_(W_ATOM_NET_WM_STATE_BELOW,		"_NET_WM_STATE_BELOW") \
	_(W_ATOM_NET_WM_STATE_FOCUSED,		"_NET_WM_STATE_FOCUSED") \
	_(W_ATOM_NET_WM_STATE_DEMANDS_ATTENTION, "_NET_WM_STATE_DEMANDS_ATTENTION") \
	_(W_ATOM_NET_WM_ALLOWED_ACTIONS,	"_NET_WM_ALLOWED_ACTIONS") \
	_(W_ATOM_NET_WM_ACTION_MOVE,		"_NET_WM_ACTION_MOVE") \
	_(W_ATOM_NET_WM_ACTION_RESIZE,		"_NET_WM_ACTION_RESIZE") \
	_(W_ATOM_NET_WM_ACTION_MINIMIZE,	"_NET_WM_ACTION_MINIMIZE") \
	_(W_ATOM_NET_WM_ACTION_SHADE,		"_NET_WM_ACTION_SHADE") \
	_(W_ATOM_NET_WM_ACTION_STICK,		"_NET_WM_ACTION_STICK") \
	_(W_ATOM_NET_WM_ACTION_MAXIMIZE_HORZ,	"_NET_WM_ACTION_MAXIMIZE_HORZ") \
	_(W_ATOM_NET_WM_ACTION_MAXIMIZE_VERT,	"_NET_WM_ACTION_MAXIMIZE_VERT") \
	_(W_ATOM_NET_WM_ACTION_FULLSCREEN,	"_NET_WM_ACTION_FULLSCREEN") \
	_(W_ATOM_NET_WM_ACTION_CHANGE_DESKTOP,	"_NET_WM_ACTION_CHANGE_DESKTOP") \
	_(W_ATOM_NET_WM_ACTION_CLOSE,		"_NET_WM_ACTION_CLOSE") \
	_(W_ATOM_NET_WM_ACTION_ABOVE,		"_NET_WM_ACTION_ABOVE") \
	_(W_ATOM_NET_WM_ACTION_BELOW,		"_NET_WM_ACTION_BELOW") \
	_(W_ATOM_NET_WM_STRUT,			"_NET_WM_STRUT") \
	_(W_ATOM_NET_WM_STRUT_PARTIAL,		"_NET_WM_STRUT_PARTIAL") \
	_(W_ATOM_NET_WM_ICON_GEOMETRY,		"_NET_WM_ICON_GEOMETRY") \
	_(W_ATOM_NET_WM_ICON,			"_NET_WM_ICON") \
	_(W_ATOM_NET_WM_PID,			"_NET_WM_PID") \
	_(W_ATOM_NET_WM_HANDLED_ICONS,		"_NET_WM_HANDLED_ICONS") \
	_(W_ATOM_NET_WM_WINDOW_OPACITY,		"_NET_WM_WINDOW_OPACITY") \
	_(W_ATOM_NET_FRAME_EXTENTS,		"_NET_FRAME_EXTENTS") \
	_(W_ATOM_NET_WM_USER_TIME,		"_NET_WM_USER_TIME") \
	_(W_ATOM_NET_WM_USER_TIME_WINDOW,	"_NET_WM_USER_TIME_WINDOW") \
	_(W_ATOM_NET_WM_FULL_PLACEMENT,		"_NET_WM_FULL_PLACEMENT") \
	_(W_ATOM_NET_WM_PING,			"_NET_WM_PING") \
	_(W_ATOM_NET_WM_SYNC_REQUEST,		"_NET_WM_SYNC_REQUEST") \
	_(W_ATOM_NET_WM_SYNC_REQUEST_COUNTER,	"_NET_WM_SYNC_REQUEST_COUNTER") \
	_(W_ATOM_NET_STARTUP_ID,		"_NET_STARTUP_ID") \
	_(W_ATOM_UTF8_STRING,			"UTF8_STRING") \
	_(W_ATOM_XDND_AWARE,			"XdndAware") \
	_(W_ATOM_XDND_SELECTION,		"XdndSelection") \
	_(W_ATOM_XDND_ENTER,			"XdndEnter") \
	_(W_ATOM_XDND_LEAVE,			"XdndLeave") \
	_(W_ATOM_XDND_POSITION,			"XdndPosition") \
	_(W_ATOM_XDND_DROP,			"XdndDrop") \
	_(W_ATOM_XDND_FINISHED,			"XdndFinished") \
	_(W_ATOM_XDND_TYPE_LIST,		"XdndTypeList") \
	_(W_ATOM_XDND_ACTION_LIST,		"XdndActionList") \
	_(W_ATOM_XDND_ACTION_DESCRIPTION,	"XdndActionDescription") \
	_(W_ATOM_XDND_STATUS,			"XdndStatus") \
	_(W_ATOM_XDND_ACTION_COPY,		"XdndActionCopy") \
	_(W_ATOM_XDND_ACTION_MOVE,		"XdndActionMove") \
	_(W_ATOM_XDND_ACTION_LINK,		"XdndActionLink") \
	_(W_ATOM_XDND_ACTION_ASK,		"XdndActionAsk") \
	_(W_ATOM_XDND_ACTION_PRIVATE,		"XdndActionPrivate") \
	_(W_ATOM_CLIPBOARD,			"CLIPBOARD") \
	_(W_ATOM_INCR,				"INCR") \
	_(W_ATOM_TARGETS,			"TARGETS") \
	_(W_ATOM_TEXT,				"TEXT") \
	_(W_ATOM_COMPOUND_TEXT,			"COMPOUND_TEXT") \
	_(W_ATOM_TEXT_URI_LIST,			"text/uri-list") \
	_(W_ATOM_VERSION,			"VERSION") \
	_(W_ATOM_RGBA_IMAGE,			"_RGBA_IMAGE") \
	_(W_ATOM_XKB_RULES_NAMES,		"_XKB_RULES_NAMES") \
	_(W_ATOM_XSETTINGS_SETTINGS,		"_XSETTINGS_SETTINGS") \
	_(W_ATOM_MANAGER,			"MANAGER")

#define WX_ATOM_ENUM_(id, name)	id,
enum {
	WX_STATIC_ATOMS(WX_ATOM_ENUM_)
	W_ATOM_COUNT
};
#undef WX_ATOM_ENUM_

/* the resolved static atoms, filled once per process by
 * WXStaticAtomsInit(), XCB_ATOM_NONE entries when a name did not
 * resolve. Zero before the first call. */
extern xcb_atom_t wx_static_atoms[W_ATOM_COUNT];

/* resolve every entry of wx_static_atoms with one batch round trip,
 * a no-op after the first call */
void WXStaticAtomsInit(xcb_connection_t *dpy);

/* XBM bitmaps. wXCreateBitmapFromData mirrors XCreateBitmapFromData;
 * wXReadBitmapFile mirrors XReadBitmapFile and returns the
 * WXBitmapStatus codes. */
xcb_pixmap_t wXCreateBitmapFromData(xcb_connection_t *dpy, xcb_drawable_t d, const char *bits,
			      unsigned int width, unsigned int height);
WXBitmapStatus wXReadBitmapFile(xcb_connection_t *dpy, xcb_drawable_t d, const char *filename,
				unsigned int *width, unsigned int *height,
				xcb_pixmap_t *bitmap, int *x_hot, int *y_hot);

/* Cut buffers on the default screen's root (CUT_BUFFER0..7). Store
 * mirrors XStoreBuffer (no-op on negative byte counts or buffer > 7).
 * Fetch returns a NUL-terminated wmalloc()ed copy of at most *nbytes
 * bytes, NULL when the buffer is unset (free with wfree()); it is the
 * XFetchBuffer byte contract, the caller does not XFree(). */
void wXStoreBuffer(xcb_connection_t *dpy, int screen, const char *bytes, int nbytes, int buffer);
char *wXFetchBuffer(xcb_connection_t *dpy, int screen, int *nbytes, int buffer);
void wXRotateBuffers(xcb_connection_t *dpy, int screen, int rotate);

/* ICCCM 4.1.4 withdraw: unmap, then a synthetic UnmapNotify to the
 * root (the XWithdrawWindow wire behaviour). */
bool wXWithdrawWindow(xcb_connection_t *dpy, xcb_window_t w, int screen_number);

/* XRestackWindows equivalent: the list is top to bottom, windows[i]
 * is stacked directly below windows[i-1]. */
void wXRestackWindows(xcb_connection_t *dpy, xcb_window_t *windows, int n);

/* XSetClassHint equivalent: WM_CLASS = res_name NUL res_class NUL. */
void wXSetClassHint(xcb_connection_t *dpy, xcb_window_t w, const char *res_name,
		    const char *res_class);

/* XBell equivalent (percent -100..100). */
void wXBell(xcb_connection_t *dpy, int percent);

/* XChangePointerControl equivalent, Xlib argument shape. */
void wXChangePointerControl(xcb_connection_t *dpy, bool do_accel, bool do_threshold,
			    int acc_num, int acc_denom, int threshold);

/* XGetPointerControl equivalent, Xlib argument shape. */
void wXGetPointerControl(xcb_connection_t *dpy, int *acc_num, int *acc_denom,
			 int *threshold);

/* xcb-native twin of WXProp.h's Xlib-shaped WXSendWmakerCommand(): a
 * format-8 _WINDOWMAKER_COMMAND ClientMessage to the root window of
 * the given screen, the legacy fallback for callers with no IPC
 * socket to an older window manager. */
void WXSendWmakerCommandXC(xcb_connection_t *dpy, int screen, const char *cmd);

/* Send a core event packet. xcb_send_event always serialises a full
 * 32 bytes from the buffer, while the typed event structs are smaller
 * (SelectionNotify 24, UnmapNotify 16, ...), so passing the struct
 * alone puts adjacent stack bytes on the wire. Pack into a zeroed
 * 32-byte buffer first. */
static inline void WXSendEvent32(xcb_connection_t *dpy, const void *event,
				 size_t len, uint8_t propagate,
				 xcb_window_t destination, uint32_t event_mask)
{
	char packet[32];

	if (len > sizeof(packet))
		return;
	memset(packet, 0, sizeof(packet));
	memcpy(packet, event, len);
	xcb_send_event(dpy, propagate, destination, event_mask, packet);
}

/* Synthesize a queue-dispatchable event: a zeroed generic-size buffer
 * (36 bytes: the 32 wire bytes plus full_sequence at offset 32, so
 * handlers reading it stay in bounds; zero full_sequence is the
 * artificial-event marker). Fill the typed fields on the returned
 * buffer; ownership follows the queue's event free. */
static inline xcb_generic_event_t *W_SynthEvent(uint8_t response_type)
{
	xcb_generic_event_t *ev = calloc(1, sizeof(xcb_generic_event_t));

	ev->response_type = response_type;
	return ev;
}

/* Screen geometry from the xcb_setup (the Display*-bound Xlib macros
 * are gone). */
unsigned int wXDisplayWidth(xcb_connection_t *dpy, int screen_number);
unsigned int wXDisplayHeight(xcb_connection_t *dpy, int screen_number);

xcb_pixmap_t wXCreatePixmap(xcb_connection_t *dpy, xcb_drawable_t d, unsigned int width,
		      unsigned int height, unsigned int depth);

/* XCreatePixmapFromBitmapData equivalent, Xlib argument shape. */
xcb_pixmap_t wXCreatePixmapFromBitmapData(xcb_connection_t *dpy, xcb_drawable_t d, const char *bits,
				    unsigned int width, unsigned int height,
				    unsigned long fg, unsigned long bg,
				    unsigned int depth);

#endif /* WINGS_WXCORE_H */
