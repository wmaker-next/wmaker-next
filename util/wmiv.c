/*
 *  Window Maker window manager
 *
 *  Copyright (c) 2014-2026 Window Maker Team - David Maciejak
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <xcb/randr.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <wraster.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <poll.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <ftw.h>
#include <libgen.h>
#include "config.h"

#ifdef HAVE_EXIF
#include <libexif/exif-data.h>
#endif

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#ifdef HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

#include <cairo-xcb.h>

#include "wmiv_icon.h"

#ifdef DEBUG
#define WMIV_DEBUG 1
#else
#define WMIV_DEBUG 0
#endif

#define FILE_SEPARATOR '/'

/* CHUNK size for INCR (bytes). Tune if desired. */
#include "clip_common.h"
#include "xcconn.h"

#define CLIPBOARD_PASTE_TIMEOUT_MS 5000

xcb_connection_t *xc;
int screen_num;
xcb_window_t root_window;
xcb_window_t win;
RContext *ctx;
RImage *img;
xcb_pixmap_t pix;

/* XDND drag-out support */
static bool button1_pressed = false;
static int drag_start_x, drag_start_y;
static int drag_threshold = 5; /* pixels */

/* XDND protocol atoms, interned once in wmiv_xdnd_init_atoms() */
#define WMIV_XDND_VERSION 5
#define WMIV_XDND_MAX_TYPES 64
static xcb_atom_t atom_XdndAware, atom_XdndEnter, atom_XdndPosition, atom_XdndStatus;
static xcb_atom_t atom_XdndLeave, atom_XdndDrop, atom_XdndFinished, atom_XdndSelection;
static xcb_atom_t atom_XdndTypeList, atom_XdndActionCopy;
static xcb_atom_t atom_uri_list, atom_text_plain;

/* XDND drop-target state, updated as ClientMessages arrive */
static struct {
	xcb_window_t source;
	xcb_atom_t   want_type;
	int          version;
} xdnd_target;

/* file:// URI for the current image, served on XdndSelection requests */
static char *xdnd_drag_uri = NULL;

const char *APPNAME = "wmiv";
int NEXT = 0;
int PREV = 1;
float zoom_factor = 0;
int max_width = 0;
int max_height = 0;

/* Archive support */
typedef struct TempDirEntry {
	char *path;
	struct TempDirEntry *next;
} TempDirEntry;

static TempDirEntry *temp_dirs;
static char *temp_dir = NULL;
static char *paste_temp_dir = NULL;
static bool is_archive = false;

bool fullscreen_flag = false;
bool focus = true;
bool back_from_fullscreen = false;
bool ignore_unknown_file_format = false;

#ifdef HAVE_PTHREAD
static pthread_mutex_t slideshow_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t slideshow_cond = PTHREAD_COND_INITIALIZER;
static bool slideshow_running;
static uint32_t slideshow_token[2];
bool slideshow_flag = false;
int slideshow_delay = 5;
pthread_t tid = 0;
#endif
static int sig_pipe[2] = { -1, -1 };
static volatile sig_atomic_t caught_signal;
unsigned current_index = 1;
unsigned max_index = 1;

RColor lightGray;
RColor darkGray;
RColor black;
RColor red;

/* Structure to hold the frame extents */
typedef struct {
	unsigned long left;
	unsigned long right;
	unsigned long top;
	unsigned long bottom;
} FrameExtents;

FrameExtents extents = {0, 0, 0, 0};

typedef struct link link_t;
struct link {
	const void *data;
	link_t *prev;
	link_t *next;
};

typedef struct linked_list {
	int count;
	link_t *first;
	link_t *last;
} linked_list_t;

linked_list_t list;
link_t *current_link;

/* --- Clipboard/PNG + JPEG + INCR globals --- */
static xcb_atom_t clipboard_atom = XCB_ATOM_NONE;
static xcb_atom_t targets_atom = XCB_ATOM_NONE;
static xcb_atom_t png_atom = XCB_ATOM_NONE;
static xcb_atom_t jpeg_atom = XCB_ATOM_NONE;
static xcb_atom_t property_atom = XCB_ATOM_NONE;
static xcb_atom_t incr_atom = XCB_ATOM_NONE;

/* --- xkbcommon-x11 keyboard mapping --- */
static struct xkb_context *xkb_ctx;
static struct xkb_keymap *xkb_keymap;

/* --- Thin app-owned event queue (mirrors WINGs/wxevq.c) --- */
typedef struct wmiv_event_queue_node {
	xcb_generic_event_t *ev;
	struct wmiv_event_queue_node *next;
} wmiv_event_queue_node_t;
static wmiv_event_queue_node_t *wmiv_q_head;
static wmiv_event_queue_node_t *wmiv_q_tail;

static unsigned char *clipboard_png_data = NULL;
static size_t clipboard_png_size = 0;
static unsigned char *clipboard_jpeg_data = NULL;
static size_t clipboard_jpeg_size = 0;
static bool we_own_clipboard = false;

static ClipIncrSend incr;

/* End clipboard globals */

static void wmiv_set_deadline(struct timespec *deadline, int timeout_ms)
{
	clock_gettime(CLOCK_MONOTONIC, deadline);
	deadline->tv_sec += timeout_ms / 1000;
	deadline->tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
	if (deadline->tv_nsec >= 1000000000L) {
		deadline->tv_sec++;
		deadline->tv_nsec -= 1000000000L;
	}
}

static int wmiv_deadline_remaining_ms(const struct timespec *deadline)
{
	struct timespec now;
	time_t sec;
	long nsec;

	clock_gettime(CLOCK_MONOTONIC, &now);
	sec = deadline->tv_sec - now.tv_sec;
	nsec = deadline->tv_nsec - now.tv_nsec;
	if (nsec < 0) {
		sec--;
		nsec += 1000000000L;
	}
	if (sec < 0)
		return 0;
	if (sec > INT_MAX / 1000)
		return INT_MAX;
	return (int)(sec * 1000 + (nsec + 999999L) / 1000000L);
}

/* --- Event queue (mirrors WINGs/wxevq.c, simplified for xcb-native wmiv) --- */

typedef int (*wmiv_event_pred)(const xcb_generic_event_t *ev, void *arg);

static void wmiv_q_pump(void)
{
	xcb_generic_event_t *ev;

	/* xcb_poll_for_event does not flush the output buffer, so flush
	 * explicitly before polling, matching what xcb_wait_for_event does
	 * internally — without this, pending write requests (e.g.
	 * xcb_configure_window from resize_window) would sit in the output
	 * buffer and the server would never generate events in response */
	xcb_flush(xc);
	while ((ev = xcb_poll_for_event(xc)) != NULL) {
		wmiv_event_queue_node_t *n = malloc(sizeof(*n));

		if (!n) {
			fprintf(stderr, "wmiv: out of memory in event queue\n");
			exit(EXIT_FAILURE);
		}
		n->ev = ev;
		n->next = NULL;
		if (wmiv_q_tail)
			wmiv_q_tail->next = n;
		else
			wmiv_q_head = n;
		wmiv_q_tail = n;
	}
}

static xcb_generic_event_t *wmiv_q_pop_head(void)
{
	wmiv_event_queue_node_t *n;
	xcb_generic_event_t *ev;

	n = wmiv_q_head;
	if (!n)
		return NULL;
	wmiv_q_head = n->next;
	if (!wmiv_q_head)
		wmiv_q_tail = NULL;
	ev = n->ev;
	free(n);
	return ev;
}

static xcb_generic_event_t *wmiv_q_take_if(wmiv_event_pred pred, void *arg)
{
	wmiv_event_queue_node_t **pp;

	for (pp = &wmiv_q_head; *pp; pp = &(*pp)->next) {
		if (pred((*pp)->ev, arg)) {
			wmiv_event_queue_node_t *n = *pp;
			xcb_generic_event_t *ev = n->ev;

			*pp = n->next;
			if (n == wmiv_q_tail) {
				wmiv_event_queue_node_t *t = wmiv_q_head;

				while (t && t->next)
					t = t->next;
				wmiv_q_tail = t;
			}
			free(n);
			return ev;
		}
	}
	return NULL;
}

static xcb_generic_event_t *wmiv_q_wait(int timeout_ms)
{
	struct pollfd pfd[2];
	int nfds, rc;

	for (;;) {
		wmiv_q_pump();
		if (wmiv_q_head)
			return wmiv_q_pop_head();
		if (xcb_connection_has_error(xc))
			return NULL;

		nfds = 1;
		pfd[0].fd = xcb_get_file_descriptor(xc);

		pfd[0].events = POLLIN;
		pfd[0].revents = 0;
		if (sig_pipe[0] >= 0) {
			pfd[nfds].fd = sig_pipe[0];
			pfd[nfds].events = POLLIN;
			pfd[nfds].revents = 0;
			nfds++;
		}
		rc = poll(pfd, nfds, timeout_ms);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			return NULL;
		}
		if (rc == 0)
			return NULL;
		if (pfd[0].revents & (POLLERR | POLLHUP | POLLNVAL))
			return NULL;
		if (nfds > 1 && (pfd[1].revents & POLLIN))
			return NULL;
	}
}

static xcb_generic_event_t *wmiv_q_match(wmiv_event_pred pred, void *arg,
					 int timeout_ms)
{
	struct timespec deadline;

	wmiv_set_deadline(&deadline, timeout_ms);

	for (;;) {
		wmiv_q_pump();

		xcb_generic_event_t *ev = wmiv_q_take_if(pred, arg);

		if (ev)
			return ev;

		if (xcb_connection_has_error(xc))
			return NULL;

		int remaining = wmiv_deadline_remaining_ms(&deadline);

		if (remaining <= 0)
			return NULL;

		struct pollfd pfd[2];
		int nfds = 1;

		pfd[0].fd = xcb_get_file_descriptor(xc);

		pfd[0].events = POLLIN;
		pfd[0].revents = 0;
		if (sig_pipe[0] >= 0) {
			pfd[nfds].fd = sig_pipe[0];
			pfd[nfds].events = POLLIN;
			pfd[nfds].revents = 0;
			nfds++;
		}
		int rc = poll(pfd, nfds, remaining);

		if (rc < 0) {
			if (errno == EINTR)
				continue;
			return NULL;
		}
		if (rc == 0)
			return NULL;
		if (pfd[0].revents & (POLLERR | POLLHUP | POLLNVAL))
			return NULL;
		if (nfds > 1 && (pfd[1].revents & POLLIN))
			return NULL;
	}
}

static void wmiv_q_put_back(xcb_generic_event_t *ev)
{
	wmiv_event_queue_node_t *n = malloc(sizeof(*n));

	if (!n) {
		fprintf(stderr, "wmiv: out of memory in event queue\n");
		exit(EXIT_FAILURE);
	}
	n->ev = ev;
	n->next = wmiv_q_head;
	wmiv_q_head = n;
	if (!wmiv_q_tail)
		wmiv_q_tail = n;
}

static void wmiv_q_drain_to(uint8_t event_type)
{
	wmiv_event_queue_node_t *last_match = NULL;
	wmiv_event_queue_node_t *p;

	wmiv_q_pump();

	for (p = wmiv_q_head; p; p = p->next) {
		if ((p->ev->response_type & 0x7f) == event_type)
			last_match = p;
	}
	if (!last_match)
		return;

	p = wmiv_q_head;
	while (p) {
		wmiv_event_queue_node_t *n = p;

		p = p->next;
		free(n->ev);
		free(n);
		if (n == last_match)
			break;
	}
	wmiv_q_head = p;
	if (!p)
		wmiv_q_tail = NULL;
	else {
		wmiv_event_queue_node_t *t = p;

		while (t->next)
			t = t->next;
		wmiv_q_tail = t;
	}
}

/* xcb helpers replacing common Xlib one-liners */
static void resize_window(xcb_window_t w, int width, int height)
{
	uint32_t values[2] = { (uint32_t)width, (uint32_t)height };

	xcb_configure_window(xc, w,
			     XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
			     values);
}

static void clear_window(xcb_window_t w)
{
	xcb_clear_area(xc, 0, w, 0, 0, 0, 0);
}

static void send_client_message(xcb_window_t dest, uint32_t mask,
	xcb_window_t about, xcb_atom_t type,
	uint32_t l0, uint32_t l1, uint32_t l2, uint32_t l3, uint32_t l4)
{
	xcb_client_message_event_t ev;

	memset(&ev, 0, sizeof(ev));
	ev.response_type = XCB_CLIENT_MESSAGE;
	ev.format = 32;
	ev.window = about;
	ev.type = type;
	ev.data.data32[0] = l0;
	ev.data.data32[1] = l1;
	ev.data.data32[2] = l2;
	ev.data.data32[3] = l3;
	ev.data.data32[4] = l4;
	xcb_send_event(xc, 0, dest, mask, (const char *)&ev);
	xcb_flush(xc);
}

/* Additional atoms interned at startup */
static xcb_atom_t atom_net_frame_extents;
static xcb_atom_t atom_net_wm_state;
static xcb_atom_t atom_net_wm_state_fullscreen;
static xcb_atom_t atom_wm_protocols;
static xcb_atom_t atom_wm_delete_window;
static xcb_atom_t atom_wmiv_slideshow_tick;
static xcb_atom_t atom_utf8_string;
static xcb_atom_t atom_net_wm_icon;

static xcb_atom_t intern_atom(const char *name)
{
	xcb_intern_atom_cookie_t ck;
	xcb_intern_atom_reply_t *rep;
	xcb_atom_t atom;

	ck = xcb_intern_atom(xc, 0, strlen(name), name);
	rep = xcb_intern_atom_reply(xc, ck, NULL);

	atom = rep ? rep->atom : XCB_ATOM_NONE;
	free(rep);
	return atom;
}

static bool get_cardinal(xcb_window_t w, xcb_atom_t prop, uint32_t *out)
{
	xcb_get_property_cookie_t ck;
	xcb_get_property_reply_t *rep;
	bool ok = false;

	ck = xcb_get_property(xc, 0, w, prop, XCB_ATOM_CARDINAL, 0, 1);
	rep = xcb_get_property_reply(xc, ck, NULL);

	if (rep && rep->type == XCB_ATOM_CARDINAL && rep->format == 32 &&
	    rep->value_len >= 1) {
		*out = ((uint32_t *)xcb_get_property_value(rep))[0];
		ok = true;
	}
	free(rep);
	return ok;
}

static bool get_cardinal_n(xcb_window_t w, xcb_atom_t prop, uint32_t *out, int n)
{
	xcb_get_property_cookie_t ck;
	xcb_get_property_reply_t *rep;
	bool ok = false;

	ck = xcb_get_property(xc, 0, w, prop, XCB_ATOM_CARDINAL, 0, n);
	rep = xcb_get_property_reply(xc, ck, NULL);

	if (rep && rep->type == XCB_ATOM_CARDINAL && rep->format == 32 &&
	    rep->value_len >= (uint32_t)n) {
		memcpy(out, xcb_get_property_value(rep), sizeof(uint32_t) * n);

		ok = true;
	}
	free(rep);
	return ok;
}

typedef struct FrameExtentsWaitData {
	xcb_window_t window;
	xcb_atom_t atom;
} FrameExtentsWaitData;

static int frame_extents_notify_matches(const xcb_generic_event_t *ev, void *arg)
{
	FrameExtentsWaitData *wait = (FrameExtentsWaitData *)arg;

	if ((ev->response_type & 0x7f) != XCB_PROPERTY_NOTIFY)
		return 0;
	xcb_property_notify_event_t *pn = (xcb_property_notify_event_t *)ev;

	return pn->window == wait->window &&
	       pn->atom == wait->atom &&
	       pn->state == XCB_PROPERTY_NEW_VALUE;
}

/*
	Wait for the window manager to set the _NET_FRAME_EXTENTS property
	by listening for PropertyNotify events
	Returns 1 on success, 0 on timeout
*/
int wait_for_frame_extents(xcb_window_t win_id, int timeout_ms)
{
	FrameExtentsWaitData wait;

	if (atom_net_frame_extents == XCB_ATOM_NONE)
		return 0;

	wait.window = win_id;
	wait.atom = atom_net_frame_extents;

	for (;;) {
		uint32_t tmp;

		if (get_cardinal(win_id, atom_net_frame_extents, &tmp))
			return 1;
		xcb_generic_event_t *ev = wmiv_q_match(frame_extents_notify_matches,
						       &wait, timeout_ms);
		if (ev) {
			free(ev);
			return 1;
		}
		return 0;
	}
}

/*
	Get the frame extents (decorations) of a window
	Returns 1 on success, 0 on failure
*/
int get_window_decor_size(xcb_window_t win_id, FrameExtents *extents)
{
	uint32_t ext[4];

	if (atom_net_frame_extents == XCB_ATOM_NONE) {
		fprintf(stderr, "Warning: _NET_FRAME_EXTENTS atom not supported\n");
		return 0;
	}

	if (get_cardinal_n(win_id, atom_net_frame_extents, ext, 4)) {
		extents->left = ext[0];
		extents->right = ext[1];
		extents->top = ext[2];
		extents->bottom = ext[3];

		if (WMIV_DEBUG) {
			fprintf(stderr, "Successfully got frame extents: left=%lu, right=%lu, top=%lu, bottom=%lu\n",
				extents->left, extents->right, extents->top, extents->bottom);
		}
		return 1;
	}

	if (WMIV_DEBUG) {
		fprintf(stderr, "Error: failed to read _NET_FRAME_EXTENTS (wrong type/format/count or missing)\n");
	}

	return 0;
}

#ifdef HAVE_LIBARCHIVE
/*
	Create directory path recursively
	Returns 0 on success, -1 on failure
*/
static int create_directories(const char *path)
{
	char *path_copy = strdup(path);
	char *p;
	int result = 0;

	if (!path_copy)
		return -1;

	/* Skip leading slash if present */
	p = path_copy;
	if (*p == '/')
		p++;

	/* Create each directory component */
	while ((p = strchr(p, '/')) != NULL) {
		*p = '\0';
		if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
			result = -1;
			break;
		}
		*p = '/';
		p++;
	}

	/* Create final directory component */
	if (result == 0 && mkdir(path_copy, 0755) != 0 && errno != EEXIST)
		result = -1;

	free(path_copy);
	return result;
}
#endif

/*
	Callback function for nftw to remove files and directories
*/
static int remove_temp_file(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
	(void)sb;
	(void)typeflag;
	(void)ftwbuf;

	return remove(fpath);
}

static void remove_temp_tree(char *path)
{
	if (WMIV_DEBUG)
		fprintf(stderr, "Cleaning up temporary directory: %s\n", path);
	nftw(path, remove_temp_file, 64, FTW_DEPTH | FTW_PHYS);
	free(path);
}

/*
	Remove all archived-image temporary directories and their contents
*/
static void cleanup_temp_dir(void)
{
	TempDirEntry *entry = temp_dirs;

	while (entry) {
		TempDirEntry *next = entry->next;

		remove_temp_tree(entry->path);
		free(entry);
		entry = next;
	}
	temp_dirs = NULL;
	temp_dir = NULL;
	is_archive = false;
}

/* remove the newest extraction tree after a failed extraction */
static void discard_temp_dir(void)
{
	TempDirEntry *entry = temp_dirs;

	if (entry) {
		temp_dirs = entry->next;
		remove_temp_tree(entry->path);
		free(entry);
	}
	temp_dir = NULL;
}

static void cleanup_paste_temp_dir(void)
{
	if (paste_temp_dir) {
		if (WMIV_DEBUG)
			fprintf(stderr, "Cleaning up pasted clipboard directory: %s\n", paste_temp_dir);
		nftw(paste_temp_dir, remove_temp_file, 64, FTW_DEPTH | FTW_PHYS);
		free(paste_temp_dir);
		paste_temp_dir = NULL;
	}
}

static void on_quit_signal(int sig)
{
	unsigned char byte = 1;
	ssize_t rc;

	caught_signal = sig;
	rc = write(sig_pipe[1], &byte, 1);
	(void)rc;
}

static void install_signal_handlers(void)
{
	struct sigaction sa;

	if (pipe(sig_pipe) < 0) {
		sig_pipe[0] = sig_pipe[1] = -1;
		return;
	}
	fcntl(sig_pipe[0], F_SETFL, O_NONBLOCK);
	fcntl(sig_pipe[1], F_SETFL, O_NONBLOCK);
	fcntl(sig_pipe[0], F_SETFD, FD_CLOEXEC);
	fcntl(sig_pipe[1], F_SETFD, FD_CLOEXEC);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_quit_signal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
}

#ifdef HAVE_PTHREAD
static void init_slideshow_token(void)
{
	int rfd = open("/dev/urandom", O_RDONLY);

	slideshow_token[0] = (uint32_t)getpid() ^ (uint32_t)time(NULL);
	slideshow_token[1] = (uint32_t)clock();
	if (rfd >= 0) {
		if (read(rfd, slideshow_token, sizeof(slideshow_token)) !=
		    (ssize_t)sizeof(slideshow_token)) {
			/* keep the mixed fallback */
		}
		close(rfd);
	}
}
#else
static void init_slideshow_token(void)
{
}
#endif

/*
	Check if file has archive extension
	Returns true if it is an archive, false otherwise
*/
static bool has_suffix_ci(const char *s, const char *suffix)
{
	size_t sl = strlen(s), xl = strlen(suffix);

	return sl >= xl && strcasecmp(s + sl - xl, suffix) == 0;
}

static bool is_archive_file(const char *filename)
{
	static const char *exts[] = {
		".cbz", ".zip", ".cbr", ".rar", ".cbt", ".cb7", ".7z",
		".tar", ".tar.gz", ".tgz", ".tar.bz2", ".tbz2", ".tar.xz", ".txz"
	};
	size_t i;

	for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++)
		if (has_suffix_ci(filename, exts[i]))
			return true;
	return false;
}

#ifdef HAVE_LIBARCHIVE

static bool archive_entry_path_is_safe(const char *path)
{
	const char *component;

	if (!path || path[0] == '\0' || path[0] == '/' || path[0] == '\\')
		return false;

	component = path;
	while (*component) {
		const char *slash = component;
		size_t length;

		while (*slash && *slash != '/' && *slash != '\\')
			slash++;

		length = slash - component;
		if ((length == 1 && component[0] == '.') ||
		    (length == 2 && component[0] == '.' && component[1] == '.'))
			return false;

		if (*slash == '\0')
			break;
		component = slash + 1;
	}

	return true;
}

/*
	Recursively find directory containing image files
	Returns path to directory with images, or NULL if not found
*/
static char *find_image_directory_depth(const char *start_dir, int depth);

static char *find_image_directory(const char *start_dir)
{
	return find_image_directory_depth(start_dir, 0);
}

static char *find_image_directory_depth(const char *start_dir, int depth)
{
	DIR *dir;
	struct dirent *entry;
	struct stat st;

	char full_path[PATH_MAX];
	int image_count = 0;
	char *result = NULL;

	if (depth >= 32)
		return NULL;

	dir = opendir(start_dir);
	if (!dir)
		return NULL;


	/* First pass: count image files in current directory */
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;

		if (snprintf(full_path, sizeof(full_path), "%s/%s", start_dir, entry->d_name) >= (int)sizeof(full_path))
			continue;
		if (stat(full_path, &st) == 0 && S_ISREG(st.st_mode))
			image_count++;
	}
	closedir(dir);

	/* If current directory has images, return it */
	if (image_count > 0) {
		return strdup(start_dir);
	}

	/* Second pass: recursively check subdirectories */
	dir = opendir(start_dir);
	if (!dir)
		return NULL;

	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;

		if (snprintf(full_path, sizeof(full_path), "%s/%s", start_dir, entry->d_name) >= (int)sizeof(full_path))
			continue;
		if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
			result = find_image_directory_depth(full_path, depth + 1);
			if (result)
				break;
		}
	}
	closedir(dir);
	return result;
}

/*
	Extract archive using libarchive
	Returns 0 on success, -1 on failure
*/
static int extract_archive_libarchive(const char *filename, const char *dest_dir)
{
	struct archive *a;
	struct archive_entry *entry;

	char dest_path[PATH_MAX];
	char *buffer = NULL;
	const size_t buffer_size = 8192;
	int r;

	/* Create archive reader */
	a = archive_read_new();
	if (!a) {
		fprintf(stderr, "Error: failed to create archive reader\n");
		return -1;
	}

	/* Enable support for all formats and filters */
	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);

	/* Open the archive file */
	r = archive_read_open_filename(a, filename, 10240);
	if (r != ARCHIVE_OK) {
		fprintf(stderr, "Error: failed to open archive: %s\n", filename);
		archive_read_free(a);
		return -1;
	}

	buffer = malloc(buffer_size);
	if (!buffer) {
		fprintf(stderr, "Error: failed to allocate memory\n");
		archive_read_free(a);
		return -1;
	}

	/* Extract each entry. Cap the total at 256 MiB so a hostile archive
	 * cannot fill the temporary directory. */
	const la_int64_t budget_max = (la_int64_t)256 * 1024 * 1024;
	la_int64_t budget_used = 0;

	while ((r = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
		const char *entry_name = archive_entry_pathname(entry);

		la_int64_t entry_size = archive_entry_size(entry);
		mode_t entry_mode = archive_entry_mode(entry);
		FILE *output_file;

		la_ssize_t bytes_read;
		int write_failed = 0;
		int out_fd;

		if (!entry_name)
			continue;

		if (!archive_entry_path_is_safe(entry_name))
			continue;

		/* Skip directories and special files */
		if (!S_ISREG(entry_mode) || entry_size <= 0)
			continue;

		/* Stay within the extraction budget, the declared entry size
		 * may also understate the decoded stream so the inner loop
		 * checks again as bytes arrive */
		if (entry_size > budget_max - budget_used) {
			fprintf(stderr, "Error: archive exceeds extraction limit, stopping\n");
			break;
		}

		/* Create destination path */
		r = snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, entry_name);
		if (r < 0 || r >= (int)sizeof(dest_path))
			continue;

		/* Create directory structure if needed */
		char *dir_end = strrchr(dest_path, '/');

		if (dir_end) {
			*dir_end = '\0';
			if (create_directories(dest_path) != 0) {
				if (WMIV_DEBUG)
					fprintf(stderr, "Warning: failed to create directory: %s\n", dest_path);
			}
			*dir_end = '/';
		}

		/* Create output file. O_EXCL refuses to clobber an entry a
		 * previous header already produced, and O_NOFOLLOW keeps the
		 * leaf write inside the mkdtemp tree even if a symlink is
		 * raced into place. */
		out_fd = open(dest_path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
		if (out_fd < 0) {
			if (WMIV_DEBUG)
				fprintf(stderr, "Warning: failed to create file: %s\n", dest_path);
			continue;
		}
		output_file = fdopen(out_fd, "wb");
		if (!output_file) {
			close(out_fd);
			continue;
		}

		/* Extract file content */
		while ((bytes_read = archive_read_data(a, buffer, buffer_size)) > 0) {
			budget_used += bytes_read;
			if (budget_used > budget_max) {
				fprintf(stderr, "Error: archive exceeds extraction limit, stopping\n");
				write_failed = 1;
				break;
			}
			if (fwrite(buffer, 1, bytes_read, output_file) != (size_t)bytes_read) {
				fprintf(stderr, "Error: short write extracting %s\n", entry_name);

				write_failed = 1;
				break;
			}
		}

		if (bytes_read < 0) {
			if (WMIV_DEBUG)
				fprintf(stderr, "Warning: failed to read data for: %s\n", entry_name);
		}

		fclose(output_file);

		if (write_failed) {
			unlink(dest_path);
			break;
		}
	}

	if (r != ARCHIVE_EOF)
		fprintf(stderr, "Warning: archive reading ended with error\n");

	free(buffer);
	archive_read_free(a);

	return 0;
}
#endif

/*
	Extract archive to temporary directory
	Returns path to extracted directory on success, NULL on failure
*/
static char *extract_archive(const char *filename)
{
#ifdef HAVE_LIBARCHIVE
	TempDirEntry *entry;
	char *temp_template = NULL;
	int result;

	/* Create temporary directory template */
	if (asprintf(&temp_template, "/tmp/wmiv_XXXXXX") == -1) {
		fprintf(stderr, "Error: failed to allocate memory\n");
		return NULL;
	}

	/* Create temporary directory */
	temp_dir = mkdtemp(temp_template);
	if (!temp_dir) {
		perror("mkdtemp");
		free(temp_template);
		return NULL;
	}

	entry = malloc(sizeof(*entry));
	if (!entry) {
		perror("malloc");
		remove_temp_tree(temp_dir);
		temp_dir = NULL;
		return NULL;
	}
	entry->path = temp_dir;
	entry->next = temp_dirs;
	temp_dirs = entry;

	if (WMIV_DEBUG)
		fprintf(stderr, "Created temporary directory: %s\n", temp_dir);

	/* Extract the archive */
	if (is_archive_file(filename)) {
		result = extract_archive_libarchive(filename, temp_dir);
	} else {
		fprintf(stderr, "Error: unknown archive type\n");
		discard_temp_dir();
		return NULL;
	}

	if (result != 0) {
		discard_temp_dir();
		return NULL;
	}

	/* Check if extraction resulted in nested directories - find the one with actual images */
	char *image_dir = find_image_directory(temp_dir);

	if (image_dir && strcmp(image_dir, temp_dir) != 0) {
		if (WMIV_DEBUG)
			fprintf(stderr, "Archive contains nested directories, using image directory: %s\n", image_dir);

		char *result_dir = strdup(image_dir);

		free(image_dir);
		is_archive = true;
		return result_dir;
	}

	if (image_dir)
		free(image_dir);

	is_archive = true;
	return strdup(temp_dir);
#else
	(void) filename;
	fprintf(stderr, "Error: archive support not available\n");

	return NULL;
#endif
}

/*
	Convert current RImage to specified format using RSaveRawImage
	Returns 0 on success (buffer & size filled), -1 on failure
*/
#if defined(USE_PNG) || defined(USE_JPEG)
static int convert_image_to_format(RImage *image, const char *format, unsigned char **out_buf, size_t *out_size)
{
	if (!image || !format || !out_buf || !out_size) {
		if (WMIV_DEBUG)
			fprintf(stderr, "Error: convert_image_to_format invalid parameters\n");
		return -1;
	}

	if (!RSaveRawImage(image, format, out_buf, out_size)) {
		if (WMIV_DEBUG)
			fprintf(stderr, "Error: RSaveRawImage failed for %s, RErrorCode=%d\n", format, RErrorCode);
		return -1;
	}

	return 0;
}
#endif

/*
	Free current clipboard data (both PNG and JPEG).
	Cancels any in-progress INCR transfer first, since xfer->data
	points into one of these buffers.
*/
static void free_clipboard_data(void)
{
	clip_incr_send_cancel(xc, &incr);
	if (clipboard_png_data) {
		free(clipboard_png_data);
		clipboard_png_data = NULL;
	}
	clipboard_png_size = 0;
	if (clipboard_jpeg_data) {
		free(clipboard_jpeg_data);
		clipboard_jpeg_data = NULL;
	}
	clipboard_jpeg_size = 0;
}

static void send_selection_notify(xcb_selection_request_event_t *req,
				  xcb_atom_t property)
{
	/* the union zeroes the full 32 wire bytes: xcb_send_event
	 * copies 32 bytes and the typed struct is only 24 */
	union {
		xcb_selection_notify_event_t ev;
		char buf[32];
	} u;

	memset(&u, 0, sizeof(u));
	u.ev.response_type = XCB_SELECTION_NOTIFY;
	u.ev.time = req->time;
	u.ev.requestor = req->requestor;
	u.ev.selection = req->selection;
	u.ev.target = req->target;
	u.ev.property = property;
	xcb_send_event(xc, 0, req->requestor, 0, u.buf);
	xcb_flush(xc);
}

static xcb_window_t selection_owner(xcb_atom_t selection)
{
	xcb_get_selection_owner_cookie_t ck;
	xcb_get_selection_owner_reply_t *rep;
	xcb_window_t owner;

	ck = xcb_get_selection_owner(xc, selection);
	rep = xcb_get_selection_owner_reply(xc, ck, NULL);

	owner = rep ? rep->owner : 0;
	free(rep);
	return owner;
}

static char *atom_name(xcb_atom_t atom)
{
	xcb_get_atom_name_cookie_t ck;
	xcb_get_atom_name_reply_t *rep;
	char *name;
	int len;

	if (atom == XCB_ATOM_NONE)
		return NULL;
	ck = xcb_get_atom_name(xc, atom);
	rep = xcb_get_atom_name_reply(xc, ck, NULL);

	if (!rep)
		return NULL;
	len = xcb_get_atom_name_name_length(rep);

	name = malloc(len + 1);
	if (name) {
		memcpy(name, xcb_get_atom_name_name(rep), len);

		name[len] = '\0';
	}
	free(rep);
	return name;
}

/*
	Called when Ctrl+C pressed: encode current image to PNG and JPEG and claim CLIPBOARD
*/
static void copy_current_image_to_clipboard(void)
{
	if (!xc || !win || !img)
		return;

	/* free previous */
	free_clipboard_data();

	if (WMIV_DEBUG)
		fprintf(stderr, "Attempting to copy image to clipboard (size: %dx%d)\n", img->width, img->height);

	/* Convert image to both formats */
#ifdef USE_PNG
	if (convert_image_to_format(img, "PNG", &clipboard_png_data, &clipboard_png_size) != 0) {
		fprintf(stderr, "Error: failed to convert image to PNG\n");
		return;
	}
#endif
#ifdef USE_JPEG
	if (convert_image_to_format(img, "JPEG", &clipboard_jpeg_data, &clipboard_jpeg_size) != 0) {
		fprintf(stderr, "Error: failed to convert image to JPEG\n");
		free_clipboard_data();
		return;
	}
#endif
	if (clipboard_png_data == NULL && clipboard_jpeg_data == NULL) {
		fprintf(stderr, "Error: no image formats available to copy to the clipboard\n");
		return;
	}

	/* claim CLIPBOARD */
	xcb_set_selection_owner(xc, win, clipboard_atom, XCB_TIME_CURRENT_TIME);

	if (selection_owner(clipboard_atom) != win) {
		fprintf(stderr, "Error: failed to own clipboard selection\n");
		free_clipboard_data();
		we_own_clipboard = false;
		return;
	}
	we_own_clipboard = true;
	xcb_flush(xc);

	if (WMIV_DEBUG)
		fprintf(stderr, "Copied image to clipboard (PNG: %zu bytes, JPEG: %zu bytes)\n", clipboard_png_size, clipboard_jpeg_size);
}

/*
	Handle SelectionRequest events: serve TARGETS and image formats (with INCR when needed)
*/
static void handle_selection_request(xcb_selection_request_event_t *req)
{
	xcb_atom_t property = req->property == XCB_ATOM_NONE ? req->target : req->property;
	xcb_atom_t types[3];
	int ntypes = 0;
	unsigned char *data = NULL;
	size_t size = 0;

	if (WMIV_DEBUG) {
		char *target_name = atom_name(req->target);
		char *selection_name = atom_name(req->selection);

		fprintf(stderr, "SelectionRequest received: target=%s, selection=%s\n",
				target_name ? target_name : "unknown",
				selection_name ? selection_name : "unknown");
		free(target_name);
		free(selection_name);
	}

	if (req->selection == atom_XdndSelection) {
		/* serve the file:// URI prepared by initiate_file_drag() */
		if (xdnd_drag_uri &&
		    (req->target == atom_uri_list || req->target == atom_text_plain)) {
			xcb_change_property(xc, XCB_PROP_MODE_REPLACE, req->requestor,
					    property, req->target, 8,
					    strlen(xdnd_drag_uri),
					    xdnd_drag_uri);
			send_selection_notify(req, property);
		} else {
			send_selection_notify(req, XCB_ATOM_NONE);
		}
		return;
	}

	if (req->selection != clipboard_atom) {
		send_selection_notify(req, XCB_ATOM_NONE);
		return;
	}

	if (req->target == targets_atom) {
		/* advertise supported targets, ICCCM requires TARGETS to list itself */
		types[ntypes++] = targets_atom;
#ifdef USE_PNG
		types[ntypes++] = png_atom;
#endif
#ifdef USE_JPEG
		types[ntypes++] = jpeg_atom;
#endif
		xcb_change_property(xc, XCB_PROP_MODE_REPLACE, req->requestor,
				    property, XCB_ATOM_ATOM, 32, ntypes, types);
		send_selection_notify(req, property);
		return;
	}

	if (req->target == png_atom && clipboard_png_data && clipboard_png_size > 0) {
		data = clipboard_png_data;
		size = clipboard_png_size;
	} else if (req->target == jpeg_atom && clipboard_jpeg_data && clipboard_jpeg_size > 0) {
		data = clipboard_jpeg_data;
		size = clipboard_jpeg_size;
	}

	if (!data) {
		send_selection_notify(req, XCB_ATOM_NONE);
		return;
	}

	if (size > CLIP_DEFAULT_DIRECT_LIMIT) {
		if (!clip_incr_send_start(xc, &incr, req, incr_atom, req->target,
					   data, size, CLIP_DEFAULT_CHUNK_SIZE)) {
			if (WMIV_DEBUG)
				fprintf(stderr, "INCR: refusing concurrent transfer to 0x%x\n",
					(unsigned int)req->requestor);
			send_selection_notify(req, XCB_ATOM_NONE);
			return;
		}
		if (WMIV_DEBUG)
			fprintf(stderr, "Starting INCR for %s %zu bytes\n",
				req->target == png_atom ? "PNG" : "JPEG", size);
		send_selection_notify(req, property);
		return;
	}

	xcb_change_property(xc, XCB_PROP_MODE_REPLACE, req->requestor,
			    property, req->target, 8, size, data);
	send_selection_notify(req, property);
}

/*
	Called when the owner loses the selection
*/
static void handle_selection_clear(xcb_selection_clear_event_t *ev)
{
	if (ev->selection != clipboard_atom)
		return;
	clip_incr_send_cancel(xc, &incr);
	if (we_own_clipboard) {
		we_own_clipboard = false;
		free_clipboard_data();
	}
}

typedef struct {
	xcb_atom_t selection;
	xcb_atom_t target;
	xcb_atom_t property;
} ClipboardSelectionWait;

static int clipboard_selection_notify_matches(const xcb_generic_event_t *ev, void *arg)
{
	ClipboardSelectionWait *wait = (ClipboardSelectionWait *)arg;

	if ((ev->response_type & 0x7f) != XCB_SELECTION_NOTIFY)
		return 0;
	xcb_selection_notify_event_t *sn = (xcb_selection_notify_event_t *)ev;

	return sn->selection == wait->selection &&
	       sn->requestor == win &&
	       sn->target == wait->target &&
	       (sn->property == wait->property || sn->property == XCB_ATOM_NONE);
}

static int clipboard_service_event_matches(const xcb_generic_event_t *ev, void *arg)
{
	uint8_t rt;

	(void)arg;
	rt = ev->response_type & 0x7f;
	if (rt == XCB_SELECTION_REQUEST || rt == XCB_SELECTION_CLEAR)
		return 1;
	if (rt == XCB_PROPERTY_NOTIFY && incr.active) {
		xcb_property_notify_event_t *pn = (xcb_property_notify_event_t *)ev;

		return pn->window == incr.requestor &&
		       pn->atom == incr.property &&
		       pn->state == XCB_PROPERTY_DELETE;
	}
	return 0;
}

static void dispatch_clipboard_service_event(xcb_generic_event_t *event)
{
	uint8_t rt = event->response_type & 0x7f;

	if (rt == XCB_SELECTION_REQUEST) {
		handle_selection_request((xcb_selection_request_event_t *)event);
	} else if (rt == XCB_SELECTION_CLEAR) {
		handle_selection_clear((xcb_selection_clear_event_t *)event);
	} else if (rt == XCB_PROPERTY_NOTIFY) {
		clip_incr_send_property_notify(xc, &incr,
			(xcb_property_notify_event_t *)event);
	}
}

static bool wait_for_clipboard_event(wmiv_event_pred pred, void *arg,
				     xcb_generic_event_t **out, int timeout_ms)
{
	struct timespec deadline;

	wmiv_set_deadline(&deadline, timeout_ms);
	for (;;) {
		wmiv_q_pump();

		/* dispatch service events so we keep answering requestors */
		for (;;) {
			xcb_generic_event_t *svc = wmiv_q_take_if(
				clipboard_service_event_matches, NULL);
			if (!svc)
				break;
			dispatch_clipboard_service_event(svc);
			free(svc);
		}

		*out = wmiv_q_take_if(pred, arg);
		if (*out)
			return true;

		if (xcb_connection_has_error(xc))
			return false;

		int remaining = wmiv_deadline_remaining_ms(&deadline);

		if (remaining <= 0)
			return false;

		struct pollfd pfd;

		pfd.fd = xcb_get_file_descriptor(xc);

		pfd.events = POLLIN;
		pfd.revents = 0;
		int rc = poll(&pfd, 1, remaining);

		if (rc < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		if (rc == 0)
			return false;
		if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
			return false;
	}
}

static bool request_clipboard_selection(xcb_atom_t selection, xcb_atom_t target,
		xcb_atom_t property, xcb_timestamp_t timestamp,
		xcb_selection_notify_event_t *out)
{
	ClipboardSelectionWait wait;
	xcb_generic_event_t *event;

	wait.selection = selection;
	wait.target = target;
	wait.property = property;

	xcb_delete_property(xc, win, property);
	xcb_convert_selection(xc, win, selection, target, property, timestamp);
	xcb_flush(xc);

	if (!wait_for_clipboard_event(clipboard_selection_notify_matches,
			&wait, &event, CLIPBOARD_PASTE_TIMEOUT_MS)) {
		return false;
	}

	*out = *(xcb_selection_notify_event_t *)event;
	free(event);
	return out->property != XCB_ATOM_NONE;
}

static int clipboard_candidate_targets(xcb_atom_t *targets)
{
	int count = 0;

#if !defined(USE_PNG) && !defined(USE_JPEG)
	(void)targets;
#endif
#ifdef USE_PNG
	targets[count++] = png_atom;
#endif
#ifdef USE_JPEG
	targets[count++] = jpeg_atom;
#endif
	return count;
}

static const char *clipboard_target_name(xcb_atom_t target)
{
	if (target == png_atom)
		return "image/png";
	if (target == jpeg_atom)
		return "image/jpeg";
	return "unknown";
}

static xcb_atom_t choose_clipboard_target(void)
{
	xcb_selection_notify_event_t selection;
	xcb_get_property_cookie_t ck;
	xcb_get_property_reply_t *rep;
	xcb_atom_t candidates[2];
	xcb_atom_t result = XCB_ATOM_NONE;
	int candidate_count;
	int i;
	uint32_t j;

	if (!request_clipboard_selection(clipboard_atom, targets_atom, property_atom,
			XCB_TIME_CURRENT_TIME, &selection)) {
		return XCB_ATOM_NONE;
	}

	ck = xcb_get_property(xc, 0, win, property_atom, XCB_ATOM_ATOM, 0, 1024);
	rep = xcb_get_property_reply(xc, ck, NULL);
	xcb_delete_property(xc, win, property_atom);

	if (!rep || rep->type != XCB_ATOM_ATOM || rep->format != 32 ||
	    rep->value_len == 0 || rep->bytes_after != 0) {
		free(rep);
		return XCB_ATOM_NONE;
	}

	candidate_count = clipboard_candidate_targets(candidates);
	xcb_atom_t *targets = (xcb_atom_t *)xcb_get_property_value(rep);

	for (i = 0; i < candidate_count && result == XCB_ATOM_NONE; i++) {
		for (j = 0; j < rep->value_len; j++) {
			if (targets[j] == candidates[i]) {
				result = candidates[i];
				break;
			}
		}
	}

	free(rep);
	return result;
}

/* During an INCR receive the helper hands us every event that is not
 * the awaited PropertyNotify. Service selection events so we keep
 * answering requestors, and put everything else back into the queue
 * so the main loop sees it once the paste is done. */
static void paste_dispatch(xcb_generic_event_t *event, void *udata)
{
	(void)udata;
	if (clipboard_service_event_matches(event, NULL)) {
		dispatch_clipboard_service_event(event);
		free(event);
	} else {
		wmiv_q_put_back(event);
	}
}

static int request_clipboard_data(xcb_atom_t target, xcb_atom_t *type_out,
		unsigned char **data_out, size_t *len_out)
{
	xcb_selection_notify_event_t selection;
	unsigned char *data;
	xcb_atom_t type;
	size_t len;

	*type_out = XCB_ATOM_NONE;
	*data_out = NULL;
	*len_out = 0;

	if (!request_clipboard_selection(clipboard_atom, target, property_atom,
			XCB_TIME_CURRENT_TIME, &selection)) {
		return 0;
	}

	data = clip_retrieve_property(xc, win, property_atom, incr_atom,
					CLIP_DEFAULT_MAX_BYTES,
					CLIPBOARD_PASTE_TIMEOUT_MS,
					paste_dispatch, NULL, &type, &len);

	if (!data)
		return 0;
	if (len == 0 || type != target) {
		free(data);
		return 0;
	}

	*type_out = type;
	*data_out = data;
	*len_out = len;
	return 1;
}

static char *ensure_paste_temp_dir(void)
{
	char *temp_template;

	if (paste_temp_dir)
		return paste_temp_dir;

	if (asprintf(&temp_template, "/tmp/wmiv_paste_XXXXXX") == -1)
		return NULL;

	paste_temp_dir = mkdtemp(temp_template);
	if (!paste_temp_dir) {
		perror("mkdtemp");
		free(temp_template);
		return NULL;
	}
	return paste_temp_dir;
}

static int write_full(int fd, const unsigned char *data, size_t size)
{
	size_t offset = 0;

	while (offset < size) {
		size_t remaining = size - offset;
		ssize_t written;

		if (remaining > (size_t)SSIZE_MAX)
			remaining = (size_t)SSIZE_MAX;
		written = write(fd, data + offset, remaining);
		if (written < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (written == 0)
			return -1;
		offset += (size_t)written;
	}
	return 0;
}

static char *write_clipboard_image_temp_file(const unsigned char *data, size_t size)
{
	char *dir;
	char *path;
	int fd;
	int failed = 0;

	dir = ensure_paste_temp_dir();
	if (!dir)
		return NULL;

	if (asprintf(&path, "%s/clipboard-XXXXXX", dir) == -1)
		return NULL;

	fd = mkstemp(path);
	if (fd < 0) {
		perror("mkstemp");
		free(path);
		return NULL;
	}

	if (write_full(fd, data, size) != 0) {
		perror("write");
		failed = 1;
	}
	if (close(fd) != 0) {
		perror("close");
		failed = 1;
	}
	if (failed) {
		unlink(path);
		free(path);
		return NULL;
	}
	return path;
}

/*
	Load an image and optionally get its orientation if libexif is available
	Returns the image on success, NULL on failure
*/
RImage *load_oriented_image(RContext *context, const char *file, int index)
{
	RImage *image;
#ifdef HAVE_EXIF
	int orientation = 0;
#endif
	image = RLoadImage(context, file, index);
	if (!image)
		return NULL;
#ifdef HAVE_EXIF
	ExifData *exifData = exif_data_new_from_file(file);

	if (exifData) {
		ExifByteOrder byteOrder = exif_data_get_byte_order(exifData);
		ExifEntry *exifEntry = exif_data_get_entry(exifData, EXIF_TAG_ORIENTATION);

		if (exifEntry && exifEntry->data && exifEntry->size >= 2)
			orientation = exif_get_short(exifEntry->data, byteOrder);

		exif_data_free(exifData);
	}

/*
	0th Row      0th Column
	1  top          left side
	2  top          right side
	3  bottom     right side
	4  bottom     left side
	5  left side    top
	6  right side  top
	7  right side  bottom
	8  left side    bottom
*/

	if (image && (orientation > 1)) {
		RImage *tmp = NULL;

		switch (orientation) {
		case 2:
			tmp = RFlipImage(image, RHorizontalFlip);
			break;
		case 3:
			tmp = RRotateImage(image, 180);
			break;
		case 4:
			tmp = RFlipImage(image, RVerticalFlip);
			break;
		case 5: {
				RImage *tmp2;

				tmp2 = RFlipImage(image, RVerticalFlip);
				if (tmp2) {
					tmp = RRotateImage(tmp2, 90);
					RReleaseImage(tmp2);
				}
			}
			break;
		case 6:
			tmp = RRotateImage(image, 90);
			break;
		case 7: {
				RImage *tmp2;

				tmp2 = RFlipImage(image, RVerticalFlip);
				if (tmp2) {
					tmp = RRotateImage(tmp2, 270);
					RReleaseImage(tmp2);
				}
			}
			break;
		case 8:
			tmp = RRotateImage(image, 270);
			break;
		}
		if (tmp) {
			RReleaseImage(image);
			image = tmp;
		}
	}
#endif
	return image;
}

/*
	Change window title
	Returns EXIT_SUCCESS on success, EXIT_FAILURE on failure
*/
int change_title(char *filename)
{
	char *combined_title = NULL;

	if (ignore_unknown_file_format) {
		if (asprintf(&combined_title, "%s - %s", APPNAME, filename) < 0)
			return EXIT_FAILURE;
	}
	else {
		if (asprintf(&combined_title, "%s - %u/%u - %s", APPNAME, current_index, max_index, filename) < 0)
			if (asprintf(&combined_title, "%s - %u/%u", APPNAME, current_index, max_index) < 0)
				return EXIT_FAILURE;
	}
	xcb_icccm_set_wm_name(xc, win, atom_utf8_string, 8,
			      strlen(combined_title), combined_title);
	free(combined_title);
	return EXIT_SUCCESS;
}

/* the old pixmap is kept when the conversion fails */
static int set_image_pixmap(RImage *image)
{
	xcb_pixmap_t new_pix;

	if (!RConvertImage(ctx, image, &new_pix))
		return 0;
	if (pix)
		xcb_free_pixmap(xc, pix);
	pix = new_pix;
	return 1;
}

/*
	Rescale the current image based on the screen size
	Returns EXIT_SUCCESS on success, EXIT_FAILURE on failure
*/
int rescale_image(void)
{
	long final_width;
	long final_height;
	int max_width_tmp = max_width;
	int max_height_tmp = max_height;

	if (!img)
		return EXIT_FAILURE;
	final_width = img->width;
	final_height = img->height;

	if (!fullscreen_flag) {
		max_width_tmp -= extents.left + extents.right;
		max_height_tmp -= extents.top + extents.bottom;
	}

	if ((max_width_tmp < final_width) || (max_height_tmp < final_height)) {
		double val = 0;

		if (final_width > final_height) {
			val = final_height * max_width_tmp / final_width;
			final_width = ceil(final_width * val / final_height);
			final_height = ceil(val);
			if (val > max_height_tmp) {
				val = final_width * max_height_tmp / final_height;
				final_height = ceil(final_height * val / final_width);
				final_width = ceil(val);
			}
		} else {
			val = final_width * max_height_tmp / final_height;
			final_height = ceil(final_height * val / final_width);
			final_width = ceil(val);
			if (val > max_width_tmp) {
				val = final_height * max_width_tmp / final_width;
				final_width = ceil(final_width * val / final_height);
				final_height = ceil(val);
			}
		}
	}
	if ((final_width != img->width) || (final_height != img->height)) {
		RImage *old_img = img;

		img = RScaleImage(img, final_width, final_height);
		if (!img) {
			img = old_img;
			return EXIT_FAILURE;
		}
		RReleaseImage(old_img);
	}
	if (!set_image_pixmap(img)) {
		fprintf(stderr, "Error: %s\n", RMessageForError(RErrorCode));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
	Blit the rendered pixmap onto the display window at (0,0), or
	centred inside the max_width x max_height fullscreen frame.
*/
static void redraw_window(int centred)
{
	cairo_surface_t *ws, *ps;
	cairo_t *cr;

	int ww, wh, dx, dy;

	if (!img || !win || !pix)
		return;
	ww = centred ? max_width : img->width;
	wh = centred ? max_height : img->height;
	dx = centred ? max_width / 2 - img->width / 2 : 0;
	dy = centred ? max_height / 2 - img->height / 2 : 0;

	ws = cairo_xcb_surface_create(ctx->xc, (xcb_drawable_t)win,
				      ctx->xvisual, ww, wh);
	ps = cairo_xcb_surface_create(ctx->xc, (xcb_drawable_t)pix,
				      ctx->xvisual, img->width, img->height);
	cr = cairo_create(ws);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface(cr, ps, dx, dy);
	cairo_rectangle(cr, dx, dy, img->width, img->height);
	cairo_fill(cr);
	cairo_destroy(cr);
	cairo_surface_flush(ws);
	cairo_surface_destroy(ps);
	cairo_surface_destroy(ws);
}

/*
	Find the best image size for the current display
	Returns EXIT_SUCCESS on success
*/
int maximize_image(void)
{
	rescale_image();
	redraw_window(1);
	return EXIT_SUCCESS;
}

/*
	Merge the current image with a checkerboard background
	Returns EXIT_SUCCESS on success, EXIT_FAILURE on failure
*/
int merge_with_background(RImage *i)
{
	RImage *back;
	unsigned char *data;
	int x, y;

	if (!i)
		return EXIT_FAILURE;

	/* the checkerboard can only show through transparency */
	if (i->format != RRGBAFormat)
		return EXIT_SUCCESS;

	back = RCreateImage(i->width, i->height, true);
	if (!back)
		return EXIT_FAILURE;

	RFillImage(back, &lightGray);
	for (x = 0; x <= i->width; x += 8) {
		if (x/8 % 2)
			y = 8;
		else
			y = 0;
		for (; y <= i->height; y += 16)
			ROperateRectangle(back, RCopyOperation, x, y, x+8, y+8, &darkGray);
	}

	RCombineImages(back, i);
	data = i->data;
	i->data = back->data;
	back->data = data;
	RReleaseImage(back);
	return EXIT_SUCCESS;
}

/*
	Rotate the image by the angle passed
	Returns EXIT_SUCCESS on success, EXIT_FAILURE on failure
*/
int rotate_image(float angle)
{
	RImage *tmp;

	if (!img)
		return EXIT_FAILURE;

	tmp = RRotateImage(img, angle);
	if (!tmp)
		return EXIT_FAILURE;

	if (!fullscreen_flag) {
		if (img->width != tmp->width || img->height != tmp->height)
			resize_window(win, tmp->width, tmp->height);
	}

	RReleaseImage(img);
	img = tmp;

	rescale_image();
	if (fullscreen_flag)
		clear_window(win);
	redraw_window(fullscreen_flag);

	return EXIT_SUCCESS;
}

/*
	Rotate the image by 90 degrees to the right
	Returns EXIT_SUCCESS on success, EXIT_FAILURE on failure
*/
int rotate_image_right(void)
{
	return rotate_image(90.0);
}

/*
	Rotate the image by -90 degrees to the left
	Returns EXIT_SUCCESS on success, EXIT_FAILURE on failure
*/
int rotate_image_left(void)
{
	return rotate_image(-90.0);
}

/*
	Create a red crossed image to indicate an error loading file
	Returns the image on success, NULL on failure
*/
RImage *draw_failed_image(void)
{
	RImage *failed_image = NULL;
	xcb_get_geometry_cookie_t ck;
	xcb_get_geometry_reply_t *geo;

	ck = xcb_get_geometry(xc, win);
	geo = xcb_get_geometry_reply(xc, ck, NULL);

	if (win && geo)
		failed_image = RCreateImage(geo->width, geo->height, false);
	else
		failed_image = RCreateImage(50, 50, false);
	free(geo);
	if (!failed_image)
		return NULL;

	RFillImage(failed_image, &black);

	/* Calculate 80% size cross centered in the image */
	int cross_width = (int)(failed_image->width * 0.6);
	int cross_height = (int)(failed_image->height * 0.6);
	int offset_x = (failed_image->width - cross_width) / 2;
	int offset_y = (failed_image->height - cross_height) / 2;

	/* Calculate line thickness based on image size (minimum 3, maximum 8 pixels) */
	int thickness = (failed_image->width + failed_image->height) / 100;

	if (thickness < 3) thickness = 3;
	if (thickness > 8) thickness = 8;

	/* Draw thick diagonal lines for the cross using 80% of the image size */
	for (int i = -thickness/2; i <= thickness/2; i++) {
		/* First diagonal (top-left to bottom-right) */
		ROperateLine(failed_image, RAddOperation,
			offset_x + i, offset_y,
			offset_x + cross_width + i, offset_y + cross_height, &red);
		ROperateLine(failed_image, RAddOperation,
			offset_x, offset_y + i,
			offset_x + cross_width, offset_y + cross_height + i, &red);

		/* Second diagonal (top-right to bottom-left) */
		ROperateLine(failed_image, RAddOperation,
			offset_x + i, offset_y + cross_height,
			offset_x + cross_width + i, offset_y, &red);
		ROperateLine(failed_image, RAddOperation,
			offset_x, offset_y + cross_height - i,
			offset_x + cross_width, offset_y - i, &red);
	}

	return failed_image;
}

/*
	Send event to the window manager to switch from/to full screen mode
	Returns EXIT_SUCCESS on success, EXIT_FAILURE on failure
*/
int full_screen(void)
{
	if (!fullscreen_flag && !current_link)
		return EXIT_FAILURE;

	if (fullscreen_flag) {
		fullscreen_flag = false;
		zoom_factor = 0;
		back_from_fullscreen = true;
	} else {
		fullscreen_flag = true;
		zoom_factor = 0;
		RReleaseImage(img);
		img = load_oriented_image(ctx, current_link->data, 0);
		if (!img) {
			img = draw_failed_image();
			if (!img)
				return EXIT_FAILURE;
		} else {
			merge_with_background(img);
		}
	}

	send_client_message(root_window, XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
			    win, atom_net_wm_state, fullscreen_flag,
			    atom_net_wm_state_fullscreen, 0, 0, 0);
	return EXIT_SUCCESS;
}

/*
	Init the linked list
*/
void linked_list_init(linked_list_t *list)
{
	list->first = list->last = 0;
	list->count = 0;
}

/*
	Add an element to the linked list
	Returns EXIT_SUCCESS on success, EXIT_FAILURE otherwise
*/
int linked_list_add(linked_list_t *list, const void *data)
{
	link_t *link;

	/* calloc sets the "next" field to zero. */
	link = calloc(1, sizeof(link_t));
	if (!link) {
		fprintf(stderr, "Error: failed to allocate memory\n");
		return EXIT_FAILURE;
	}
	link->data = data;
	if (list->last) {
		/* Join the two final links together. */
		list->last->next = link;
		link->prev = list->last;
		list->last = link;
	} else {
		list->first = link;
		list->last = link;
	}
	list->count++;
	return EXIT_SUCCESS;
}

static int show_pasted_image(char *path)
{
	RImage *new_img;

	new_img = load_oriented_image(ctx, path, 0);
	if (!new_img) {
		fprintf(stderr, "Error: clipboard image %s\n", RMessageForError(RErrorCode));
		return EXIT_FAILURE;
	}

	merge_with_background(new_img);
	if (linked_list_add(&list, path) != EXIT_SUCCESS) {
		RReleaseImage(new_img);
		return EXIT_FAILURE;
	}

	current_link = list.last;
	max_index = (unsigned)list.count;
	current_index = max_index;
	zoom_factor = 0;

	if (img)
		RReleaseImage(img);
	img = new_img;

	if (rescale_image() != EXIT_SUCCESS && WMIV_DEBUG)
		fprintf(stderr, "Warning: failed to rescale pasted clipboard image\n");

	change_title((char *)current_link->data);
	if (win) {
		if (!fullscreen_flag)
			resize_window(win, img->width, img->height);
		clear_window(win);
		redraw_window(fullscreen_flag);
		free(xcb_get_input_focus_reply(xc, xcb_get_input_focus(xc), NULL));
		xcb_flush(xc);

	}
	return EXIT_SUCCESS;
}

static int paste_clipboard_image(void)
{
	xcb_atom_t candidates[2];
	xcb_atom_t target;
	xcb_atom_t actual_type = XCB_ATOM_NONE;
	unsigned char *data = NULL;
	size_t size = 0;
	char *path;
	int candidate_count;
	int i;

	if (selection_owner(clipboard_atom) == 0) {
		fprintf(stderr, "Error: clipboard is empty\n");
		return EXIT_FAILURE;
	}

	candidate_count = clipboard_candidate_targets(candidates);
	if (candidate_count == 0) {
		fprintf(stderr, "Error: this wmiv build cannot load PNG or JPEG clipboard data\n");
		return EXIT_FAILURE;
	}

	target = choose_clipboard_target();
	if (target != XCB_ATOM_NONE && request_clipboard_data(target, &actual_type, &data, &size)) {
		/* data filled */
	} else {
		for (i = 0; i < candidate_count; i++) {
			if (candidates[i] == target)
				continue;
			if (request_clipboard_data(candidates[i], &actual_type, &data, &size))
				break;
		}
	}

	if (!data || size == 0) {
		fprintf(stderr, "Error: clipboard does not contain a supported image\n");
		return EXIT_FAILURE;
	}

	if (WMIV_DEBUG)
		fprintf(stderr, "Pasting clipboard image: %zu bytes as %s\n",
			size, clipboard_target_name(actual_type));

	path = write_clipboard_image_temp_file(data, size);
	free(data);
	if (!path)
		return EXIT_FAILURE;

	if (show_pasted_image(path) != EXIT_SUCCESS) {
		unlink(path);
		free(path);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
	Delete an element from the linked list
	Returns EXIT_SUCCESS on success, EXIT_FAILURE otherwise
*/
int linked_list_del(linked_list_t *list, link_t *link)
{
	if (!list || !link) {
		return EXIT_FAILURE;
	}

	/* Update previous link's next pointer */
	if (link->prev) {
		link->prev->next = link->next;
	} else {
		/* This was the first link */
		list->first = link->next;
	}

	/* Update next link's previous pointer */
	if (link->next) {
		link->next->prev = link->prev;
	} else {
		/* This was the last link */
		list->last = link->prev;
	}

	if (link->data)
		free((char *)link->data);
	free(link);

	list->count--;

	return EXIT_SUCCESS;
}

/*
	Deallocate the whole linked list
*/
void linked_list_free(linked_list_t *list)
{
	link_t *link;
	link_t *next;

	for (link = list->first; link; link = next) {
		/* Store the next value so that we don't access freed memory. */
		next = link->next;
		if (link->data)
			free((char *)link->data);
		free(link);
	}
	current_link = NULL;
}

/*
	Apply a zoom factor on the current image
	Arg: 1 to zoom in, 0 to zoom out
	Returns EXIT_SUCCESS on success, EXIT_FAILURE on failure
*/
int zoom_in_out(int z)
{
	RImage *old_img = img;
	RImage *tmp;
	float old_zoom = zoom_factor;
	int new_width, new_height;

	if (!current_link)
		return EXIT_FAILURE;

	tmp = load_oriented_image(ctx, current_link->data, 0);
	if (!tmp)
		return EXIT_FAILURE;

	if (z)
		zoom_factor += 0.2f;
	else
		zoom_factor -= 0.2f;

	new_width = tmp->width + (int)(tmp->width * zoom_factor);
	new_height = tmp->height + (int)(tmp->height * zoom_factor);

	if (!z && ((new_width <= 0) || (new_height <= 0))) {
		zoom_factor = old_zoom;
		RReleaseImage(tmp);
		return EXIT_FAILURE;
	}
	if (new_width > (int)R_IMAGE_MAX_WIDTH)
		new_width = R_IMAGE_MAX_WIDTH;
	if (new_height > (int)R_IMAGE_MAX_HEIGHT)
		new_height = R_IMAGE_MAX_HEIGHT;

	img = RScaleImage(tmp, new_width, new_height);
	RReleaseImage(tmp);
	if (!img) {
		img = old_img;
		zoom_factor = old_zoom;
		return EXIT_FAILURE;
	}

	merge_with_background(img);
	if (!set_image_pixmap(img)) {
		zoom_factor = old_zoom;
		RReleaseImage(img);
		img = old_img;
		return EXIT_FAILURE;
	}
	RReleaseImage(old_img);
	resize_window(win, img->width, img->height);
	return EXIT_SUCCESS;
}

/*
	Transitional function used to call zoom_in_out with zoom in flag
	Returns EXIT_SUCCESS on success, EXIT_FAILURE on failure
*/
int zoom_in(void)
{
	return zoom_in_out(1);
}

/*
	Transitional function used to call zoom_in_out with zoom out flag
	Returns EXIT_SUCCESS on success, EXIT_FAILURE on failure
*/
int zoom_out(void)
{
	return zoom_in_out(0);
}

/*
	Reset the zoom to the actual image size
	Returns EXIT_SUCCESS on success, EXIT_FAILURE on failure
*/
int zoom_reset(void)
{
	RImage *tmp;

	if (!current_link)
		return EXIT_FAILURE;

	if (fullscreen_flag) {
		tmp = load_oriented_image(ctx, current_link->data, 0);
		if (!tmp)
			return EXIT_FAILURE;
		RReleaseImage(img);
		img = tmp;
		zoom_factor = 0;
		merge_with_background(img);
		maximize_image();
		return EXIT_SUCCESS;
	}
	zoom_factor = -0.2f;
	return zoom_in();
}

/*
	Load previous or next image
	Arg: way which could be PREV or NEXT constant
	Returns EXIT_SUCCESS on success, EXIT_FAILURE on failure
*/
int change_image(int way)
{
	if (max_index == 0)
		return EXIT_FAILURE;

	if (img && current_link) {
		int old_img_width = img->width;
		int old_img_height = img->height;

		RReleaseImage(img);
		img = NULL;

		for (;;) {
			link_t *tmp_link;

			if (way == NEXT) {
				current_link = current_link->next;
				current_index++;
			} else {
				current_link = current_link->prev;
				current_index--;
			}
			if (current_link == NULL) {
				if (way == NEXT) {
					current_link = list.first;
					current_index = 1;
				} else {
					current_link = list.last;
					current_index = max_index;
				}
			}
			if (WMIV_DEBUG)
				fprintf(stderr, "Current file is> %s\n", (char *)current_link->data);
			img = load_oriented_image(ctx, current_link->data, 0);
			if (img) {
				merge_with_background(img);
				break;
			}

			if (strlen((char *)current_link->data) == 0)
				fprintf(stderr, "Error: %s\n", RMessageForError(RErrorCode));
			else
				fprintf(stderr, "Error: %s %s\n", (char *)current_link->data, RMessageForError(RErrorCode));

			if (!ignore_unknown_file_format) {
				img = draw_failed_image();
				break;
			}

			/* the entry is dropped without ever being shown, so
			 * continue from the side its successor is reached
			 * from instead of building a placeholder */
			if (WMIV_DEBUG)
				fprintf(stderr, "Skipping file...\n");
			if (way == NEXT)
				if (!current_link->prev)
					tmp_link = list.last;
				else
					tmp_link = current_link->prev;
			else
				if (!current_link->next)
					tmp_link = list.first;
				else
					tmp_link = current_link->next;
			if (tmp_link == current_link)
				tmp_link = NULL;
			linked_list_del(&list, current_link);
			current_link = tmp_link;
			max_index = list.count;
			if (!current_link) {
				img = draw_failed_image();
				return EXIT_FAILURE;
			}
		}

		if (!img)
			return EXIT_FAILURE;
		rescale_image();
		if (win) {
			if (!fullscreen_flag) {
				if ((old_img_width != img->width) || (old_img_height != img->height))
					resize_window(win, img->width, img->height);
				else
					redraw_window(0);
				change_title((char *)current_link->data);
			} else {
				clear_window(win);
				redraw_window(1);
			}
		}
		return EXIT_SUCCESS;
	}
	return EXIT_FAILURE;
}

#ifdef HAVE_PTHREAD
/*
	Tick the main loop to display the next image at every delay set to
	slideshow_delay. The trigger is an internal ClientMessage tagged
	with a per-process token.
*/
void *slideshow(void *arg)
{
	(void) arg;

	pthread_mutex_lock(&slideshow_mutex);
	while (slideshow_flag) {
		struct timespec ts;

		pthread_mutex_unlock(&slideshow_mutex);
		send_client_message(win, 0, win, atom_wmiv_slideshow_tick,
				    slideshow_token[0], slideshow_token[1],
				    0, 0, 0);
		pthread_mutex_lock(&slideshow_mutex);
		if (!slideshow_flag)
			break;
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += slideshow_delay;
		pthread_cond_timedwait(&slideshow_cond, &slideshow_mutex, &ts);
	}
	slideshow_running = false;
	pthread_mutex_unlock(&slideshow_mutex);
	return arg;
}

static void slideshow_stop(void)
{
	pthread_mutex_lock(&slideshow_mutex);
	slideshow_flag = false;
	pthread_cond_signal(&slideshow_cond);
	pthread_mutex_unlock(&slideshow_mutex);
}

static void slideshow_join(void)
{
	slideshow_stop();
	if (tid) {
		pthread_join(tid, NULL);
		tid = 0;
	}
}

/* returns 1 when a slideshow is running or was just started */
static int slideshow_start(void)
{
	int rc;

	pthread_mutex_lock(&slideshow_mutex);
	if (slideshow_running) {
		pthread_mutex_unlock(&slideshow_mutex);
		return 1;
	}
	pthread_mutex_unlock(&slideshow_mutex);

	if (tid) {
		pthread_join(tid, NULL);
		tid = 0;
	}
	if (!current_link)
		return 0;

	pthread_mutex_lock(&slideshow_mutex);
	slideshow_flag = true;
	slideshow_running = true;
	rc = pthread_create(&tid, NULL, &slideshow, NULL);
	if (rc != 0) {
		slideshow_flag = false;
		slideshow_running = false;
		tid = 0;
	}
	pthread_mutex_unlock(&slideshow_mutex);
	return rc == 0;
}

static int slideshow_wants_tick(void)
{
	int on;

	pthread_mutex_lock(&slideshow_mutex);
	on = slideshow_flag;
	pthread_mutex_unlock(&slideshow_mutex);
	return on;
}
#endif

/*
	List and sort by name all files from a given directory
	Arg: the directory path that contains images, the linked list where to add the new file refs
	Returns: the first argument of the list or NULL on failure
*/
link_t *connect_dir(char *dirpath, linked_list_t *list)
{
	struct dirent **dir;
	int dv, idx;
	char path[PATH_MAX] = "";

	if (!dirpath || !*dirpath)
		return NULL;

	dv = scandir(dirpath, &dir, 0, alphasort);
	if (dv < 0) {
		/* maybe it's a file */
		struct stat stDirInfo;

		if (lstat(dirpath, &stDirInfo) == 0)
			linked_list_add(list, strdup(dirpath));
		return list->first;
	}
	for (idx = 0; idx < dv; idx++) {
			struct stat stDirInfo;
			int r;

			if (dirpath[strlen(dirpath)-1] == FILE_SEPARATOR)
				r = snprintf(path, PATH_MAX, "%s%s", dirpath, dir[idx]->d_name);
			else
				r = snprintf(path, PATH_MAX, "%s%c%s", dirpath, FILE_SEPARATOR, dir[idx]->d_name);

			free(dir[idx]);
			if (r < 0 || r >= PATH_MAX)
				continue;
			if ((lstat(path, &stDirInfo) == 0) && !S_ISDIR(stDirInfo.st_mode))
				linked_list_add(list, strdup(path));
	}
	free(dir);
	return list->first;
}

/*
	Retrieve the monitor resolution where the window is located.
	xcb-randr is a hard configure dependency, on a server without
	RandR 1.5 the GetMonitors reply is NULL and the caller keeps
	the DisplayWidth/DisplayHeight fallback it already seeded.
*/
void get_monitor_dimensions(xcb_window_t win_id, int *width, int *height)
{
	xcb_randr_get_monitors_reply_t *mr;
	xcb_randr_monitor_info_iterator_t mi;
	xcb_translate_coordinates_cookie_t tck;
	xcb_translate_coordinates_reply_t *trep;
	int abs_x, abs_y;

	tck = xcb_translate_coordinates(xc, win_id, root_window, 0, 0);
	trep = xcb_translate_coordinates_reply(xc, tck, NULL);

	if (!trep)
		return;
	abs_x = trep->dst_x;
	abs_y = trep->dst_y;
	free(trep);

	mr = xcb_randr_get_monitors_reply(xc,
			xcb_randr_get_monitors(xc, root_window, 1), NULL);

	if (!mr)
		return;
	for (mi = xcb_randr_get_monitors_monitors_iterator(mr);

		 mi.rem;
		 xcb_randr_monitor_info_next(&mi)) {
		if (abs_x >= mi.data->x && abs_x < mi.data->x + mi.data->width &&
			abs_y >= mi.data->y && abs_y < mi.data->y + mi.data->height) {
			*width = mi.data->width;
			*height = mi.data->height;
			break;
		}
	}
	free(mr);
}

/* ----- XDND ---------------------------------------------------------- *
 *
 * Minimal XDND v5 source and target implementation, replacing the
 * previously vendored util/xdnd.{c,h}. Only text/uri-list and text/plain
 * are offered or accepted, which is all wmiv needs to drag the current
 * image file out and to receive image files dropped from a file manager.
 */

static void wmiv_xdnd_init_atoms(void)
{
	static const char *const names[] = {
		"XdndAware", "XdndEnter", "XdndPosition", "XdndStatus",
		"XdndLeave", "XdndDrop", "XdndFinished", "XdndSelection",
		"XdndTypeList", "XdndActionCopy", "text/uri-list", "text/plain"
	};
	xcb_intern_atom_cookie_t cookies[12];
	xcb_intern_atom_reply_t *rep;
	int i;

	for (i = 0; i < 12; i++)
		cookies[i] = xcb_intern_atom(xc, 0, strlen(names[i]), names[i]);

	#define XDND_ATOM(idx, dst) do { \
		rep = xcb_intern_atom_reply(xc, cookies[idx], NULL); \
		dst = rep ? rep->atom : XCB_ATOM_NONE; \
		free(rep); \
	} while (0)

	XDND_ATOM(0, atom_XdndAware);
	XDND_ATOM(1, atom_XdndEnter);
	XDND_ATOM(2, atom_XdndPosition);
	XDND_ATOM(3, atom_XdndStatus);
	XDND_ATOM(4, atom_XdndLeave);
	XDND_ATOM(5, atom_XdndDrop);
	XDND_ATOM(6, atom_XdndFinished);
	XDND_ATOM(7, atom_XdndSelection);
	XDND_ATOM(8, atom_XdndTypeList);
	XDND_ATOM(9, atom_XdndActionCopy);
	XDND_ATOM(10, atom_uri_list);
	XDND_ATOM(11, atom_text_plain);
	#undef XDND_ATOM
}

/* Local copy of the inline percent-decode helper from WINGs/WXProp.h */
static int wmiv_xdnd_hex_nibble(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

static void wmiv_xdnd_decode_uri(char *uri)
{
	char *last;

	int hi, lo;

	if (uri[0] == '\0' || uri[1] == '\0')
		return;
	last = uri + strlen(uri);
	while (uri < last - 2) {
		if (*uri == '%') {
			hi = wmiv_xdnd_hex_nibble((unsigned char)uri[1]);
			lo = wmiv_xdnd_hex_nibble((unsigned char)uri[2]);
			if (hi < 0 || lo < 0)
				break;
			*uri = (char)((hi << 4) | lo);
			memmove(uri + 1, uri + 3, last - (uri + 2));
			last -= 2;
		}
		uri++;
	}
}

/* ----- XDND drop target ---------------------------------------------- */

static xcb_atom_t wmiv_xdnd_pick_type(const xcb_atom_t *types, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		if (types[i] == atom_uri_list)
			return atom_uri_list;
	for (i = 0; i < count; i++)
		if (types[i] == atom_text_plain)
			return atom_text_plain;
	return XCB_ATOM_NONE;
}

static void wmiv_xdnd_send_status(bool accept)
{
	send_client_message(xdnd_target.source, 0,
			    xdnd_target.source, atom_XdndStatus,
			    (uint32_t)win,
			    accept ? 1 : 0,
			    0, 0,
			    accept ? (uint32_t)atom_XdndActionCopy : 0);
}

static void wmiv_xdnd_send_finished(bool ok)
{
	send_client_message(xdnd_target.source, 0,
			    xdnd_target.source, atom_XdndFinished,
			    (uint32_t)win,
			    (xdnd_target.version >= 5 && ok) ? 1 : 0,
			    (xdnd_target.version >= 5 && ok) ? (uint32_t)atom_XdndActionCopy : 0,
			    0, 0);
}

static int wmiv_xdnd_is_selnotify(const xcb_generic_event_t *ev, void *arg)
{
	(void)arg;
	if ((ev->response_type & 0x7f) != XCB_SELECTION_NOTIFY)
		return 0;
	xcb_selection_notify_event_t *sn = (xcb_selection_notify_event_t *)ev;

	return sn->selection == atom_XdndSelection &&
	       sn->requestor == win &&
	       sn->target == xdnd_target.want_type &&
	       (sn->property == atom_XdndSelection || sn->property == XCB_ATOM_NONE);
}

/* bounded wait for the XdndSelection SelectionNotify so a hostile or
 * crashed source cannot wedge the main loop indefinitely */
static bool wmiv_xdnd_wait_selnotify(xcb_selection_notify_event_t *out)
{
	xcb_generic_event_t *ev = wmiv_q_match(wmiv_xdnd_is_selnotify, NULL, 2000);

	if (!ev)
		return false;
	*out = *(xcb_selection_notify_event_t *)ev;
	free(ev);
	return true;
}

static int wmiv_xdnd_is_drag_reply(const xcb_generic_event_t *ev, void *arg)
{
	uint8_t rt;

	(void)arg;
	rt = ev->response_type & 0x7f;
	if (rt == XCB_SELECTION_REQUEST)
		return 1;
	if (rt == XCB_CLIENT_MESSAGE) {
		xcb_client_message_event_t *cm = (xcb_client_message_event_t *)ev;

		return cm->window == win && cm->type == atom_XdndFinished;
	}
	return 0;
}

/*
 * Called from the main loop for every ClientMessage. Advances the XDND
 * target state machine and returns 1 with a NUL-terminated malloc'd copy
 * of the dropped data only when an XdndDrop completes. Returns 0 for
 * non-XDND messages and for intermediate XDND messages so the caller can
 * continue dispatching.
 */
static int wmiv_xdnd_handle_message(xcb_client_message_event_t *ev, unsigned char **out, int *out_len)
{
	/* every XDND message is a format-32 ClientMessage, anything else
	 * has garbage in data.l[] */
	if (ev->format != 32)
		return 0;

	if (ev->type == atom_XdndEnter) {
		xcb_atom_t inline_types[3];

		xdnd_target.source  = (xcb_window_t)ev->data.data32[0];
		xdnd_target.version = (int)((unsigned int)ev->data.data32[1] >> 24);
		xdnd_target.want_type = XCB_ATOM_NONE;

		if (ev->data.data32[1] & 1) {
			/* more than three types, fetch the bounded list from the
			 * source window */
			xcb_get_property_cookie_t ck;
			xcb_get_property_reply_t *rep;

			ck = xcb_get_property(xc, 0, xdnd_target.source,
					      atom_XdndTypeList, XCB_ATOM_ATOM,
					      0, WMIV_XDND_MAX_TYPES);
			rep = xcb_get_property_reply(xc, ck, NULL);

			if (rep && rep->type == XCB_ATOM_ATOM &&
			    rep->format == 32) {
				xdnd_target.want_type =
					wmiv_xdnd_pick_type(
						(xcb_atom_t *)xcb_get_property_value(rep),
						rep->value_len);
			}
			free(rep);
		} else {
			inline_types[0] = (xcb_atom_t)ev->data.data32[2];
			inline_types[1] = (xcb_atom_t)ev->data.data32[3];
			inline_types[2] = (xcb_atom_t)ev->data.data32[4];
			xdnd_target.want_type = wmiv_xdnd_pick_type(inline_types, 3);
		}
		return 0;
	}

	if (ev->type == atom_XdndPosition) {
		if ((xcb_window_t)ev->data.data32[0] == xdnd_target.source)
			wmiv_xdnd_send_status(xdnd_target.want_type != XCB_ATOM_NONE);
		return 0;
	}

	if (ev->type == atom_XdndLeave) {
		xdnd_target.source = 0;
		xdnd_target.want_type = XCB_ATOM_NONE;
		return 0;
	}

	if (ev->type == atom_XdndDrop) {
		xcb_selection_notify_event_t sn;
		size_t len = 0;
		char *data;

		if ((xcb_window_t)ev->data.data32[0] != xdnd_target.source ||
		    xdnd_target.want_type == XCB_ATOM_NONE) {
			wmiv_xdnd_send_finished(false);
			xdnd_target.source = 0;
			return 0;
		}

		xcb_convert_selection(xc, win, atom_XdndSelection,
				      xdnd_target.want_type, atom_XdndSelection,
				      ev->data.data32[2]);
		xcb_flush(xc);

		/* wait for the source to deliver the converted selection,
		 * bounded so an unresponsive source cannot block us */
		if (!wmiv_xdnd_wait_selnotify(&sn)) {
			wmiv_xdnd_send_finished(false);
			xdnd_target.source = 0;
			xdnd_target.want_type = XCB_ATOM_NONE;
			return 0;
		}

		if (sn.property == XCB_ATOM_NONE) {
			wmiv_xdnd_send_finished(false);
			xdnd_target.source = 0;
			return 0;
		}

		{
			xcb_atom_t prop_type;
			size_t prop_len;

			data = (char *)clip_read_property8(xc, win, sn.property,
							   CLIP_DEFAULT_MAX_BYTES,
							   &prop_type, &prop_len);
			len = prop_len;
		}
		xcb_delete_property(xc, win, sn.property);

		wmiv_xdnd_send_finished(data != NULL);
		xdnd_target.source = 0;
		xdnd_target.want_type = XCB_ATOM_NONE;

		if (!data)
			return 0;

		*out = (unsigned char *)data;

		*out_len = (int)len;
		return 1;
	}

	/* not an XDND message */
	return 0;
}

/* ----- XDND drag source ---------------------------------------------- */

static char *wmiv_build_drag_uri(void)
{
	char *full_path, *uri;
	size_t n;

	if (!current_link || !current_link->data)
		return NULL;

	full_path = realpath((char *)current_link->data, NULL);
	if (!full_path)
		return NULL;

	n = strlen("file://") + strlen(full_path) + 1;
	uri = malloc(n);
	if (!uri) {
		free(full_path);
		return NULL;
	}
	snprintf(uri, n, "file://%s", full_path);
	free(full_path);
	return uri;
}

static bool get_atom_prop(xcb_window_t w, xcb_atom_t prop, xcb_atom_t *out)
{
	xcb_get_property_cookie_t ck;
	xcb_get_property_reply_t *rep;
	bool ok = false;

	ck = xcb_get_property(xc, 0, w, prop, XCB_ATOM_ATOM, 0, 1);
	rep = xcb_get_property_reply(xc, ck, NULL);

	if (rep && rep->type == XCB_ATOM_ATOM && rep->format == 32 &&
	    rep->value_len >= 1) {
		*out = ((xcb_atom_t *)xcb_get_property_value(rep))[0];
		ok = true;
	}
	free(rep);
	return ok;
}

/*
 * Walk down from the root to the deepest child under (rx,ry) that carries
 * the XdndAware property. Some toolkits set it on the toplevel only, so
 * remember the highest aware ancestor and keep descending while children
 * exist under the pointer.
 */
static xcb_window_t wmiv_xdnd_find_target(xcb_window_t root, int rx, int ry)
{
	xcb_window_t cur = root, child, aware = 0;
	xcb_atom_t version;

	for (;;) {
		if (cur != root && cur != win &&
		    get_atom_prop(cur, atom_XdndAware, &version))
			aware = cur;
		xcb_translate_coordinates_cookie_t tck;

		tck = xcb_translate_coordinates(xc, root, cur, rx, ry);
		xcb_translate_coordinates_reply_t *trep =
			xcb_translate_coordinates_reply(xc, tck, NULL);

		if (!trep)
			break;
		child = trep->child;
		free(trep);
		if (child == 0)
			break;
		cur = child;
	}
	return aware;
}

static void wmiv_xdnd_send_enter(xcb_window_t target)
{
	send_client_message(target, 0, target, atom_XdndEnter,
			    (uint32_t)win,
			    (uint32_t)WMIV_XDND_VERSION << 24,
			    (uint32_t)atom_uri_list,
			    (uint32_t)atom_text_plain,
			    0);
}

static void wmiv_xdnd_send_position(xcb_window_t target, int rx, int ry, xcb_timestamp_t t)
{
	send_client_message(target, 0, target, atom_XdndPosition,
			    (uint32_t)win, 0,
			    ((uint32_t)rx << 16) | ((uint32_t)ry & 0xffff),
			    (uint32_t)t,
			    (uint32_t)atom_XdndActionCopy);
}

static void wmiv_xdnd_send_leave(xcb_window_t target)
{
	send_client_message(target, 0, target, atom_XdndLeave,
			    (uint32_t)win, 0, 0, 0, 0);
}

static void wmiv_xdnd_send_drop(xcb_window_t target, xcb_timestamp_t t)
{
	send_client_message(target, 0, target, atom_XdndDrop,
			    (uint32_t)win, 0, (uint32_t)t, 0, 0);
}

/*
 * Run the XDND source state machine until button release. The selection
 * data is precomputed into xdnd_drag_uri so the existing
 * handle_selection_request() can serve it when the target asks.
 */
static void initiate_file_drag(void)
{
	xcb_window_t target = 0;
	bool target_accepts = false;
	xcb_grab_pointer_cookie_t gck;
	xcb_grab_pointer_reply_t *grep;

	if (xdnd_drag_uri)
		free(xdnd_drag_uri);
	xdnd_drag_uri = wmiv_build_drag_uri();
	if (!xdnd_drag_uri)
		return;

	xcb_set_selection_owner(xc, win, atom_XdndSelection, XCB_TIME_CURRENT_TIME);

	if (selection_owner(atom_XdndSelection) != win) {
		free(xdnd_drag_uri);
		xdnd_drag_uri = NULL;
		return;
	}

	gck = xcb_grab_pointer(xc, 0, win,
			      XCB_EVENT_MASK_BUTTON_RELEASE |
			      XCB_EVENT_MASK_POINTER_MOTION,
			      XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
			      0, 0, XCB_TIME_CURRENT_TIME);
	grep = xcb_grab_pointer_reply(xc, gck, NULL);

	if (!grep || grep->status != XCB_GRAB_STATUS_SUCCESS) {
		free(grep);
		free(xdnd_drag_uri);
		xdnd_drag_uri = NULL;
		return;
	}
	free(grep);

	if (WMIV_DEBUG)
		fprintf(stderr, "XDND drag started: %s\n", xdnd_drag_uri);

	for (;;) {
		xcb_generic_event_t *ev = wmiv_q_wait(-1);

		if (!ev)
			break;
		uint8_t rt = ev->response_type & 0x7f;

		if ((ev->response_type & 0x80) &&
		    (rt == XCB_MOTION_NOTIFY || rt == XCB_BUTTON_RELEASE)) {
			free(ev);
			continue;
		}

		if (rt == XCB_MOTION_NOTIFY) {
			xcb_motion_notify_event_t *mn = (xcb_motion_notify_event_t *)ev;
			xcb_window_t over = wmiv_xdnd_find_target(root_window,
								  mn->root_x,
								  mn->root_y);
			if (over != target) {
				if (target != 0)
					wmiv_xdnd_send_leave(target);
				target = over;
				target_accepts = false;
				if (target != 0)
					wmiv_xdnd_send_enter(target);
			}
			if (target != 0)
				wmiv_xdnd_send_position(target,
							mn->root_x,
							mn->root_y,
							mn->time);
			xcb_flush(xc);

			free(ev);
			continue;
		}

		if (rt == XCB_CLIENT_MESSAGE) {
			xcb_client_message_event_t *cm = (xcb_client_message_event_t *)ev;

			if (cm->type == atom_XdndStatus) {
				if ((xcb_window_t)cm->data.data32[0] == target)
					target_accepts = (cm->data.data32[1] & 1) != 0;
				free(ev);
				continue;
			}
		}

		if (rt == XCB_SELECTION_REQUEST) {
			handle_selection_request((xcb_selection_request_event_t *)ev);
			free(ev);
			continue;
		}

		if (rt == XCB_BUTTON_RELEASE) {
			xcb_button_release_event_t *br = (xcb_button_release_event_t *)ev;

			xcb_ungrab_pointer(xc, br->time);

			if (target != 0 && target_accepts) {
				wmiv_xdnd_send_drop(target, br->time);
				xcb_flush(xc);

				/* serve the selection and wait briefly for
				 * XdndFinished, but do not block forever if
				 * the target never sends it */
				for (;;) {
					xcb_generic_event_t *fev =
						wmiv_q_match(wmiv_xdnd_is_drag_reply,
							     NULL, 1000);
					if (!fev)
						break;
					if ((fev->response_type & 0x7f) == XCB_SELECTION_REQUEST) {
						handle_selection_request(
							(xcb_selection_request_event_t *)fev);
						free(fev);
						continue;
					}
					free(fev);
					break;
				}
			} else if (target != 0) {
				wmiv_xdnd_send_leave(target);
			}
			xcb_flush(xc);

			free(ev);
			break;
		}

		free(ev);
	}

	free(xdnd_drag_uri);
	xdnd_drag_uri = NULL;
}

/* xkbcommon-x11 keysym resolution */
static xkb_keysym_t keycode_to_keysym(xcb_keycode_t keycode, int level)
{
	const xkb_keysym_t *syms;
	int n;

	if (!xkb_keymap)
		return XKB_KEY_NoSymbol;
	n = xkb_keymap_key_get_syms_by_level(xkb_keymap, keycode, 0, level, &syms);

	return n > 0 ? syms[0] : XKB_KEY_NoSymbol;
}

static void init_xkb(void)
{
	int32_t device_id;

	if (!xkb_x11_setup_xkb_extension(xc,
					 XKB_X11_MIN_MAJOR_XKB_VERSION,
					 XKB_X11_MIN_MINOR_XKB_VERSION,
					 XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
					 NULL, NULL, NULL, NULL))
		return;

	xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

	if (!xkb_ctx)
		return;

	device_id = xkb_x11_get_core_keyboard_device_id(xc);

	if (device_id < 0)
		return;

	xkb_keymap = xkb_x11_keymap_new_from_device(xkb_ctx, xc, device_id,
						    XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!xkb_keymap)
		return;
}

/*
	main
*/
int main(int argc, char **argv)
{
	int option = -1;
	RContextAttributes attr = {};
	xkb_keysym_t keysym;
	char *reading_filename = "";
	int file_i;
	int quit = 0;
	xcb_screen_t *screen;
	uint32_t event_mask;
	uint32_t values[2];
	xcb_atom_t xdnd_version;
	xcb_icccm_wm_hints_t hints;
	char cmd_buf[4096];
	int cmd_pos;
	char wm_class_buf[32];
	int inst_len, cls_len, pos;

	/* Init colors */
	lightGray.red = lightGray.green = lightGray.blue = 211;
	darkGray.red = darkGray.green = darkGray.blue = 169;
	lightGray.alpha = darkGray.alpha = 255;
	black.red = black.green = black.blue = 0;
	red.red = 255;
	red.green = red.blue = 0;

	static struct option long_options[] = {
		{"help", no_argument, 0, 'h'},
		{"ignore-unknown", no_argument, 0, 'i'},
		{"version", no_argument, 0, 'v'},
		{0, 0, 0, 0}
	};
	int option_index = 0;

	option = getopt_long (argc, argv, "hiv", long_options, &option_index);
	if (option != -1) {
		switch (option) {
		case 'h':
			printf("Usage: %s [image(s)|directory|archive]\n"
			"Options:\n"
			"  -h, --help            display this help and exit\n"
			"  -i, --ignore-unknown  ignore unknown image format\n"
			"  -v, --version         print version\n"
			"\nKeys:\n\n"
			"  [+]            zoom in\n"
			"  [-]            zoom out\n"
			"  [▸]            next image\n"
			"  [◂]            previous image\n"
			"  [▴]            first image\n"
			"  [▾]            last image\n"
			"  [Ctrl+C]       copy image to clipboard\n"
			"  [Ctrl+V]       paste image from clipboard\n"
			"  [Ctrl+0]       reset zoom\n"
#ifdef HAVE_PTHREAD
			"  [D]            start slideshow\n"
#endif
			"  [Esc]          actual size\n"
			"  [F]            toggle full-screen mode\n"
			"  [L]            rotate image on the left\n"
			"  [Q]            quit\n"
			"  [R]            rotate image on the right\n",
			argv[0]);
			return EXIT_SUCCESS;
		case 'v':
			printf("%s version %s\n", APPNAME, VERSION);
			return EXIT_SUCCESS;
		case 'i':
			ignore_unknown_file_format = true;
			break;
		case '?':
			return EXIT_FAILURE;
		}
	}

	linked_list_init(&list);
	atexit(cleanup_paste_temp_dir);

#ifdef HAVE_LIBARCHIVE
	/* Register cleanup function for temporary directory */
	atexit(cleanup_temp_dir);
#endif

	install_signal_handlers();
	init_slideshow_token();

	xc = xcconn_open(NULL, APPNAME, 0, &screen_num);

	if (!xc) {
		linked_list_free(&list);
		return EXIT_FAILURE;
	}

	/* Get screen info */
	screen = xcconn_screen(xc, screen_num);

	root_window = screen->root;
	max_width = screen->width_in_pixels;
	max_height = screen->height_in_pixels;

	/* Initialize XKB keyboard mapping */
	init_xkb();

	/* Initialize clipboard atoms */
	clipboard_atom = intern_atom("CLIPBOARD");
	targets_atom = intern_atom("TARGETS");
	png_atom = intern_atom("image/png");
	jpeg_atom = intern_atom("image/jpeg");
	property_atom = intern_atom("WMIV_CLIPBOARD_PROP");
	incr_atom = intern_atom("INCR");
	atom_net_frame_extents = intern_atom("_NET_FRAME_EXTENTS");
	atom_net_wm_state = intern_atom("_NET_WM_STATE");
	atom_net_wm_state_fullscreen = intern_atom("_NET_WM_STATE_FULLSCREEN");
	atom_wm_protocols = intern_atom("WM_PROTOCOLS");
	atom_wm_delete_window = intern_atom("WM_DELETE_WINDOW");
	atom_wmiv_slideshow_tick = intern_atom("_WMIV_SLIDESHOW_TICK");
	atom_utf8_string = intern_atom("UTF8_STRING");
	atom_net_wm_icon = intern_atom("_NET_WM_ICON");

	attr.flags = 0;
	ctx = RCreateContextXCB(xc, screen_num, &attr);

	if (argc < 2) {
		argv[1] = ".";
		argc = 2;
	}

	for (file_i = 1; file_i < argc; file_i++) {
		/* Check if this is an archive file */
		if (is_archive_file(argv[file_i])) {
			char *extracted_dir = extract_archive(argv[file_i]);

			if (extracted_dir) {
				current_link = connect_dir(extracted_dir, &list);
				free(extracted_dir);
				if (current_link) {
				reading_filename = (char *)current_link->data;
				max_index = list.count;
				}
			}
		} else {
			current_link = connect_dir(argv[file_i], &list);
			if (current_link) {
				reading_filename = (char *)current_link->data;
				max_index = list.count;
			}
		}
	}
	img = load_oriented_image(ctx, reading_filename, 0);

	if (!img) {
		if (strlen(reading_filename) == 0)
			fprintf(stderr, "Error: %s\n", RMessageForError(RErrorCode));
		else
			fprintf(stderr, "Error: %s %s\n", reading_filename, RMessageForError(RErrorCode));
		img = draw_failed_image();
		if (!current_link)
			return EXIT_FAILURE;
		if (ignore_unknown_file_format) {
			if (WMIV_DEBUG)
				fprintf(stderr, "Skipping file...\n");
			if (change_image(NEXT) != EXIT_SUCCESS || !img)
				return EXIT_FAILURE;
			if (current_link)
				reading_filename = (char *)current_link->data;
		}
	}

	if (!img)
		return EXIT_FAILURE;
	merge_with_background(img);

	/* Create window */
	screen = xcconn_screen(xc, screen_num);
	event_mask = XCB_EVENT_MASK_KEY_PRESS |
		     XCB_EVENT_MASK_STRUCTURE_NOTIFY |
		     XCB_EVENT_MASK_EXPOSURE |
		     XCB_EVENT_MASK_BUTTON_PRESS |
		     XCB_EVENT_MASK_BUTTON_RELEASE |
		     XCB_EVENT_MASK_POINTER_MOTION |
		     XCB_EVENT_MASK_FOCUS_CHANGE |
		     XCB_EVENT_MASK_PROPERTY_CHANGE;
	values[0] = screen->black_pixel;
	values[1] = event_mask;

	win = xcb_generate_id(xc);
	xcb_create_window(xc, screen->root_depth, win, root_window,
			  0, 0, 10, 10, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
			  screen->root_visual,
			  XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values);

	xcb_icccm_set_wm_protocols(xc, win, atom_wm_protocols, 1,
				   &atom_wm_delete_window);
	change_title(reading_filename);

	wmiv_xdnd_init_atoms();
	xdnd_version = WMIV_XDND_VERSION;

	xcb_change_property(xc, XCB_PROP_MODE_REPLACE, win,
			    atom_XdndAware, XCB_ATOM_ATOM, 32, 1,
			    &xdnd_version);

	/* Set _NET_WM_ICON from the hardcoded ARGB literal */
	xcb_change_property(xc, XCB_PROP_MODE_REPLACE, win,
			    atom_net_wm_icon, XCB_ATOM_CARDINAL, 32,
			    WMIV_ICON_ARGB_LEN, wmiv_icon_argb);

	/* Set WM hints */
	memset(&hints, 0, sizeof(hints));
	xcb_icccm_wm_hints_set_normal(&hints);
	xcb_icccm_wm_hints_set_input(&hints, 1);
	xcb_icccm_wm_hints_set_window_group(&hints, win);
	xcb_icccm_set_wm_hints(xc, win, &hints);

	xcb_icccm_set_wm_icon_name(xc, win, atom_utf8_string, 8,
				   strlen(APPNAME), APPNAME);

	/* Set WM_COMMAND */
	cmd_pos = 0;

	for (int i = 0; i < argc && cmd_pos < (int)sizeof(cmd_buf); i++) {
		int arg_len = strlen(argv[i]);

		if (cmd_pos + arg_len + 1 > (int)sizeof(cmd_buf))
			break;
		memcpy(cmd_buf + cmd_pos, argv[i], arg_len);
		cmd_pos += arg_len;
		cmd_buf[cmd_pos++] = '\0';
	}
	xcb_change_property(xc, XCB_PROP_MODE_REPLACE, win,
			    XCB_ATOM_WM_COMMAND, XCB_ATOM_STRING, 8,
			    cmd_pos, cmd_buf);

	/* Set WM_CLASS */
	inst_len = strlen(APPNAME);
	cls_len = strlen("wmiv");
	pos = 0;

	memcpy(wm_class_buf + pos, APPNAME, inst_len);
	pos += inst_len;
	wm_class_buf[pos++] = '\0';
	memcpy(wm_class_buf + pos, "wmiv", cls_len);
	pos += cls_len;
	wm_class_buf[pos++] = '\0';
	xcb_icccm_set_wm_class(xc, win, pos, wm_class_buf);

	xcb_map_window(xc, win);
	xcb_flush(xc);

	if (wait_for_frame_extents(win, 1000)) { /* Wait up to 1 second */
		if (!get_window_decor_size(win, &extents) && WMIV_DEBUG)
			fprintf(stderr, "Warning: Property found but could not parse frame extents\n");
	} else {
		if (WMIV_DEBUG)
			fprintf(stderr, "Warning: Window manager did not set _NET_FRAME_EXTENTS property within timeout\n");

	}

	get_monitor_dimensions(win, &max_width, &max_height);

	if (WMIV_DEBUG)
		fprintf(stderr, "Display size: %dx%d\n", max_width, max_height);

	rescale_image();
	resize_window(win, img->width, img->height);
	redraw_window(0);
	/* Flush all pending requests and wait for the server to process them,
	 * then discard the accumulated events — the XSync(true) shape */
	xcb_flush(xc);
	free(xcb_get_input_focus_reply(xc, xcb_get_input_focus(xc), NULL));
	wmiv_q_drain_to(XCB_CONFIGURE_NOTIFY);

	/* Main event loop */
	while (!quit) {
		xcb_generic_event_t *ev = wmiv_q_wait(incr.active ?
			CLIP_DEFAULT_INCR_SEND_TIMEOUT_MS : -1);

		if (!ev) {
			if (caught_signal)
				break;
			if (xcb_connection_has_error(xc) || !incr.active)
				break;
			if (clip_incr_send_check(xc, &incr, clip_monotonic_ms(),
						 CLIP_DEFAULT_INCR_SEND_TIMEOUT_MS)) {
				if (WMIV_DEBUG)
					fprintf(stderr, "INCR: transfer stalled, cancelled\n");
				continue;
			}
			break;
		}
		uint8_t rt = ev->response_type & 0x7f;

		/* synthetic key and button events are not real input */
		if ((ev->response_type & 0x80) &&
		    (rt == XCB_KEY_PRESS || rt == XCB_BUTTON_PRESS ||
		     rt == XCB_BUTTON_RELEASE || rt == XCB_MOTION_NOTIFY)) {
			free(ev);
			continue;
		}

		if (rt == 0) {
			if (clip_incr_send_handle_error(&incr,
					((xcb_generic_error_t *)ev)->resource_id) &&
			    WMIV_DEBUG)
				fprintf(stderr, "INCR: cancelled transfer, X error on requestor\n");
			free(ev);
			continue;
		}

		if (rt == XCB_CLIENT_MESSAGE) {
			xcb_client_message_event_t *cm = (xcb_client_message_event_t *)ev;
			unsigned char *dropBuffer;
			int length;

#ifdef HAVE_PTHREAD
			if (cm->type == atom_wmiv_slideshow_tick) {
				if (cm->data.data32[0] == slideshow_token[0] &&
				    cm->data.data32[1] == slideshow_token[1] &&
				    slideshow_wants_tick())
					change_image(NEXT);
				free(ev);
				continue;
			}
#endif
			if (wmiv_xdnd_handle_message(cm, &dropBuffer, &length)) {
				char *handled_uri_header = "file://";
				char *filename_separator = "\n";
				char *ptr = strtok((char *)dropBuffer, filename_separator);

				if (WMIV_DEBUG)
					fprintf(stderr, "Dropped data: %s\n", (char *)dropBuffer);

				linked_list_free(&list);
				linked_list_init(&list);

				bool find_one_file = false;

				while (ptr != NULL) {
					if (strstr(ptr, handled_uri_header) == ptr) {
						char *tmp_file;

						if (ptr[strlen(ptr) - 1] == '\r')
							ptr[strlen(ptr) - 1] = '\0';
						tmp_file = ptr + strlen(handled_uri_header);

						/* drag-and-drop file name format as per the spec is encoded as an URI */
						wmiv_xdnd_decode_uri(tmp_file);
						if (connect_dir(tmp_file, &list))
							find_one_file = true;
					}
					ptr = strtok(NULL, filename_separator);
				}
				free(dropBuffer);

				if (find_one_file) {
					max_index = list.count;
					current_link = list.last;
					change_image(NEXT);
				} else {
					if (WMIV_DEBUG)
						fprintf(stderr, "Error: no valid file found in dropped data\n");
					xcb_clear_area(xc, 1, win, 0, 0, 0, 0);

				}
			}
			if (cm->type == atom_wm_protocols && cm->format == 32 &&
			    cm->data.data32[0] == atom_wm_delete_window)
				quit = 1;
			free(ev);
			continue;
		}
		if (rt == XCB_SELECTION_REQUEST) {
			/* Serve clipboard requests */
			handle_selection_request((xcb_selection_request_event_t *)ev);
			free(ev);
			continue;
		}
		if (rt == XCB_SELECTION_CLEAR) {
			/* we lost ownership */
			handle_selection_clear((xcb_selection_clear_event_t *)ev);
			free(ev);
			continue;
		}
		if (rt == XCB_PROPERTY_NOTIFY) {
			if (clip_incr_send_property_notify(xc, &incr,
					(xcb_property_notify_event_t *)ev)) {
				if (WMIV_DEBUG)
					fprintf(stderr, "INCR: sent chunk %zu/%zu%s\n",
						incr.offset, incr.size,
						incr.active ? "" : " (complete)");
			}
			free(ev);
			continue;
		}
		if (rt == XCB_DESTROY_NOTIFY) {
			clip_incr_send_destroy_notify(xc, &incr,
					(xcb_destroy_notify_event_t *)ev);
			free(ev);
			continue;
		}
		if (rt == XCB_FOCUS_IN) {
			focus = true;
			free(ev);
			continue;
		}
		if (rt == XCB_FOCUS_OUT) {
			focus = false;
			free(ev);
			continue;
		}
		if (!fullscreen_flag && (rt == XCB_EXPOSE)) {
			xcb_expose_event_t *xe = (xcb_expose_event_t *)ev;

			if (xe->count == 0)
				redraw_window(0);
			free(ev);
			continue;
		}
		if (!fullscreen_flag && rt == XCB_CONFIGURE_NOTIFY) {
			xcb_get_geometry_reply_t *geo;
			int win_w, win_h;

			/* a forged event can claim any size, the drawable geometry
			 * is the authority for the rescale below */
			geo = xcb_get_geometry_reply(xc, xcb_get_geometry(xc, win), NULL);
			if (!geo) {
				free(ev);
				continue;
			}
			win_w = geo->width;
			win_h = geo->height;
			free(geo);
			if (win_w < 1)
				win_w = 1;
			if (win_h < 1)
				win_h = 1;
			if (win_w > max_width)
				win_w = max_width;
			if (win_h > max_height)
				win_h = max_height;

			/* there is no file loaded and the window is resized */
			if (!current_link) {
				if (img)
					resize_window(win, img->width, img->height);
				free(ev);
				continue;
			}

			if (img && (win_w != img->width || win_h != img->height)) {
				RImage *old_img = img;
				RImage *loaded;

				loaded = load_oriented_image(ctx, current_link->data, 0);
				if (!loaded) {
					/* keep the old img and window size */
					resize_window(win, img->width, img->height);
				} else {
					RImage *tmp2;

					img = loaded;
					if (!back_from_fullscreen) {
						/* manually resized window */
						tmp2 = RScaleImage(img, win_w, win_h);
					}
					else {
						/* back from fullscreen mode, maybe img was rotated */
						tmp2 = img;
						back_from_fullscreen = false;
						clear_window(win);
					}
					if (!tmp2) {
						RReleaseImage(img);
						img = old_img;
						resize_window(win, img->width, img->height);
					} else {
						merge_with_background(tmp2);
						if (set_image_pixmap(tmp2)) {
							RReleaseImage(old_img);
							if (tmp2 != img)
								RReleaseImage(img);
							img = tmp2;
							change_title((char *)current_link->data);
							xcb_flush(xc);
							free(xcb_get_input_focus_reply(xc, xcb_get_input_focus(xc), NULL));
							wmiv_q_drain_to(XCB_CONFIGURE_NOTIFY);
							rescale_image();
							resize_window(win, img->width, img->height);
							redraw_window(0);
						} else {
							RReleaseImage(tmp2);
							if (tmp2 != img)
								RReleaseImage(img);
							img = old_img;
						}
					}
				}
			}
			free(ev);
			continue;
		}
		if (fullscreen_flag && rt == XCB_CONFIGURE_NOTIFY) {
			xcb_configure_notify_event_t *xce = (xcb_configure_notify_event_t *)ev;

			if (img && (xce->width != img->width || xce->height != img->height))
				maximize_image();
			free(ev);
			continue;
		}
		if (rt == XCB_BUTTON_PRESS) {
			xcb_button_press_event_t *be = (xcb_button_press_event_t *)ev;

			switch (be->detail) {
			case 1: {
				/* Store button press for potential drag operation */
				button1_pressed = true;
				drag_start_x = be->event_x;
				drag_start_y = be->event_y;
			}
			break;
			case 4:
				zoom_in();
				break;
			case 5:
				zoom_out();
				break;
			case 8:
				change_image(PREV);
				break;
			case 9:
				change_image(NEXT);
				break;
			}
			free(ev);
			continue;
		}
		if (rt == XCB_BUTTON_RELEASE) {
			xcb_button_release_event_t *be = (xcb_button_release_event_t *)ev;

			if (be->detail == 1 && button1_pressed) {
				/* Button released without dragging - treat as click for image navigation */
				button1_pressed = false;
				if (focus) {
					if (img && (drag_start_x > img->width/2))
						change_image(NEXT);
					else
						change_image(PREV);
				}
			}
			free(ev);
			continue;
		}
		if (rt == XCB_MOTION_NOTIFY && button1_pressed) {
			xcb_motion_notify_event_t *me = (xcb_motion_notify_event_t *)ev;

			/* Check if we've moved enough to start a drag */
			int dx = me->event_x - drag_start_x;
			int dy = me->event_y - drag_start_y;

			if (abs(dx) > drag_threshold || abs(dy) > drag_threshold) {
				/* Start drag operation */
				button1_pressed = false;
				if (current_link && current_link->data) {
					initiate_file_drag();
				}
			}
			free(ev);
			continue;
		}
		if (rt == XCB_KEY_PRESS) {
			xcb_key_press_event_t *ke = (xcb_key_press_event_t *)ev;
			int level = (ke->state & XCB_KEY_BUT_MASK_SHIFT) ? 1 : 0;

			keysym = keycode_to_keysym(ke->detail, level);
#ifdef HAVE_PTHREAD
			slideshow_stop();
#endif
			/* Detect Ctrl+C: state contains ControlMask */
			if ((ke->state & XCB_KEY_BUT_MASK_CONTROL) && (keysym == XKB_KEY_c || keysym == XKB_KEY_C)) {
				copy_current_image_to_clipboard();
				free(ev);
				continue;
			}
			if ((ke->state & XCB_KEY_BUT_MASK_CONTROL) && (keysym == XKB_KEY_v || keysym == XKB_KEY_V)) {
				paste_clipboard_image();
				free(ev);
				continue;
			}
			if ((ke->state & XCB_KEY_BUT_MASK_CONTROL) && keysym == XKB_KEY_0) {
				zoom_reset();
				free(ev);
				continue;
			}

			switch (keysym) {
			case XKB_KEY_Right:
				change_image(NEXT);
				break;
			case XKB_KEY_Left:
				change_image(PREV);
				break;
			case XKB_KEY_Up:
				if (current_link) {
				current_link = list.last;
				change_image(NEXT);
				}
				break;
			case XKB_KEY_Down:
				if (current_link) {
				current_link = list.first;
				change_image(PREV);
				}
				break;
#ifdef HAVE_PTHREAD
			case XKB_KEY_F5:
			case XKB_KEY_d:
				if (!slideshow_start())
					fprintf(stderr, "Error: can't start slideshow\n");
				break;
#endif
			case XKB_KEY_q:
				quit = 1;
				break;
			case XKB_KEY_Escape:
				if (!fullscreen_flag)
					zoom_reset();
				else
					full_screen();
				break;
			case XKB_KEY_plus:
				zoom_in();
				break;
			case XKB_KEY_minus:
				zoom_out();
				break;
			case XKB_KEY_F11:
			case XKB_KEY_f:
				full_screen();
				break;
			case XKB_KEY_r:
				rotate_image_right();
				break;
			case XKB_KEY_l:
				rotate_image_left();
				break;
			}

			free(ev);
		} else {
			free(ev);
		}
	}

#ifdef HAVE_PTHREAD
	/* the thread uses libxcb, so it must exit first */
	slideshow_join();
#endif

	if (img)
		RReleaseImage(img);
	if (pix)
		xcb_free_pixmap(xc, (xcb_pixmap_t)pix);

	linked_list_free(&list);
	RDestroyContext(ctx);
	RShutdown();

	free_clipboard_data();
	cleanup_temp_dir();

	if (xkb_keymap)
		xkb_keymap_unref(xkb_keymap);

	if (xkb_ctx)
		xkb_context_unref(xkb_ctx);

	xcb_disconnect(xc);

	return EXIT_SUCCESS;
}
