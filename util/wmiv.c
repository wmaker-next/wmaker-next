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

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <WINGs/WINGsP.h>
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
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
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

#ifdef USE_RANDR
#include <X11/extensions/Xrandr.h>
#endif

#ifdef USE_XPM
extern int XpmCreatePixmapFromData(Display *, Drawable, char **, Pixmap *, Pixmap *, void *);
/*	This is the icon from the eog project git.gnome.org/browse/eog */
#include "wmiv.h"
#endif

#ifdef DEBUG
#define WMIV_DEBUG 1
#else
#define WMIV_DEBUG 0
#endif

#define FILE_SEPARATOR '/'

/* CHUNK size for INCR (bytes). Tune if desired. */
#define CHUNK_SIZE 65536
#define CLIPBOARD_PASTE_MAX_BYTES ((size_t)256 * 1024 * 1024)
#define CLIPBOARD_PASTE_TIMEOUT_MS 5000

Display *dpy;
Window win;
RContext *ctx;
RImage *img;
Pixmap pix;

/* XDND drag-out support */
static Bool button1_pressed = False;
static int drag_start_x, drag_start_y;
static int drag_threshold = 5; /* pixels */

/* XDND protocol atoms, interned once in wmiv_xdnd_init_atoms() */
#define WMIV_XDND_VERSION 5
#define WMIV_XDND_MAX_TYPES 64
static Atom atom_XdndAware, atom_XdndEnter, atom_XdndPosition, atom_XdndStatus;
static Atom atom_XdndLeave, atom_XdndDrop, atom_XdndFinished, atom_XdndSelection;
static Atom atom_XdndTypeList, atom_XdndActionCopy;
static Atom atom_uri_list, atom_text_plain;

/* XDND drop-target state, updated as ClientMessages arrive */
static struct {
	Window source;
	Atom   want_type;
	int    version;
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
static char *temp_dir = NULL;
static char *paste_temp_dir = NULL;
static Bool is_archive = False;

Bool fullscreen_flag = False;
Bool focus = True;
Bool back_from_fullscreen = False;
Bool ignore_unknown_file_format = False;

#ifdef HAVE_PTHREAD
Bool slideshow_flag = False;
int slideshow_delay = 5;
pthread_t tid = 0;
#endif
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
static Atom clipboard_atom = None;
static Atom targets_atom = None;
static Atom png_atom = None;
static Atom jpeg_atom = None;
static Atom property_atom = None;
static Atom incr_atom = None;

static unsigned char *clipboard_png_data = NULL;
static size_t clipboard_png_size = 0;
static unsigned char *clipboard_jpeg_data = NULL;
static size_t clipboard_jpeg_size = 0;
static Bool we_own_clipboard = False;

/* INCR transfer state */
typedef struct {
	Bool active;
	Window requestor;
	Atom property;      /* property name the requestor asked for (req->property) */
	Atom target;        /* requested target (png_atom or jpeg_atom) */
	size_t total_size;
	size_t offset;
	size_t chunk_size;
	long saved_event_mask; /* to restore previous event mask on requestor window */
	unsigned char *data; /* pointer to either PNG or JPEG data */
} incr_transfer_t;

static incr_transfer_t incr = { .active = False };

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

static int wmiv_wait_x_until(Display *disp, const struct timespec *deadline)
{
	int remaining = wmiv_deadline_remaining_ms(deadline);
	struct timeval timeout;
	fd_set fdset;
	int ready;

	if (remaining <= 0)
		return 0;

	FD_ZERO(&fdset);
	FD_SET(ConnectionNumber(disp), &fdset);
	timeout.tv_sec = remaining / 1000;
	timeout.tv_usec = (remaining % 1000) * 1000;
	XFlush(disp);

	ready = select(ConnectionNumber(disp) + 1, &fdset, NULL, NULL, &timeout);
	if (ready < 0) {
		if (errno == EINTR)
			return 1;
		return -1;
	}
	return ready > 0;
}

static Bool wmiv_wait_for_matching_event_until(Display *disp, XEvent *event,
		Bool (*predicate)(Display *, XEvent *, XPointer), XPointer arg,
		const struct timespec *deadline)
{
	int ready;

	for (;;) {
		if (XCheckIfEvent(disp, event, predicate, arg))
			return True;

		ready = wmiv_wait_x_until(disp, deadline);
		if (ready <= 0)
			return False;
	}
}

static Bool wmiv_wait_for_matching_event(Display *disp, XEvent *event,
		Bool (*predicate)(Display *, XEvent *, XPointer), XPointer arg,
		int timeout_ms)
{
	struct timespec deadline;

	wmiv_set_deadline(&deadline, timeout_ms);
	return wmiv_wait_for_matching_event_until(disp, event, predicate, arg, &deadline);
}

typedef struct FrameExtentsWaitData {
	Window window;
	Atom atom;
} FrameExtentsWaitData;

static Bool frameExtentsNotifyMatches(Display *disp, XEvent *event, XPointer arg)
{
	FrameExtentsWaitData *wait = (FrameExtentsWaitData *)arg;

	(void)disp;
	return event->type == PropertyNotify &&
		event->xproperty.window == wait->window &&
		event->xproperty.atom == wait->atom &&
		event->xproperty.state == PropertyNewValue;
}

/*
	Wait for the window manager to set the _NET_FRAME_EXTENTS property
	by listening for PropertyNotify events
	Returns 1 on success, 0 on timeout
*/
int wait_for_frame_extents(Display *disp, Window win, int timeout_ms) {
	Atom net_frame_extents = XInternAtom(disp, "_NET_FRAME_EXTENTS", False);
	FrameExtentsWaitData wait;
	struct timespec deadline;
	XEvent event;

	if (net_frame_extents == None)
		return 0;

	wait.window = win;
	wait.atom = net_frame_extents;
	wmiv_set_deadline(&deadline, timeout_ms);

	for (;;) {
		uint32_t tmp;

		if (WXGetCardinal(disp, win, net_frame_extents, &tmp))
			return 1;
		if (XCheckIfEvent(disp, &event, frameExtentsNotifyMatches, (XPointer)&wait))
			return 1;
		if (wmiv_wait_x_until(disp, &deadline) <= 0)
			return 0;
	}
}

/*
	Get the frame extents (decorations) of a window
	Returns 1 on success, 0 on failure
*/
int get_window_decor_size(Display *disp, Window win, FrameExtents *extents) {
	uint32_t ext[4];
	Atom net_frame_extents;

	/* Get the atom for the _NET_FRAME_EXTENTS property */
	net_frame_extents = XInternAtom(disp, "_NET_FRAME_EXTENTS", False);
	if (net_frame_extents == None) {
		fprintf(stderr, "Warning: _NET_FRAME_EXTENTS atom not supported\n");
		return 0;
	}

	/* Retrieve the property value */
	if (WXGetCardinalN(disp, win, net_frame_extents, ext, 4)) {
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

/*
	Remove the temporary directory and its contents
*/
static void cleanup_temp_dir(void)
{
	if (temp_dir) {
		if (WMIV_DEBUG)
			fprintf(stderr, "Cleaning up temporary directory: %s\n", temp_dir);
		nftw(temp_dir, remove_temp_file, 64, FTW_DEPTH | FTW_PHYS);
		free(temp_dir);
		temp_dir = NULL;
	}
	is_archive = False;
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

/*
	Check if file has archive extension
	Returns True if it is an archive, False otherwise
*/
static Bool has_suffix_ci(const char *s, const char *suffix)
{
	size_t sl = strlen(s), xl = strlen(suffix);
	return sl >= xl && strcasecmp(s + sl - xl, suffix) == 0;
}

static Bool is_archive_file(const char *filename)
{
	static const char *exts[] = {
		".cbz", ".zip", ".cbr", ".rar", ".cbt", ".cb7", ".7z",
		".tar", ".tar.gz", ".tgz", ".tar.bz2", ".tbz2", ".tar.xz", ".txz"
	};
	size_t i;

	for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++)
		if (has_suffix_ci(filename, exts[i]))
			return True;
	return False;
}

#ifdef HAVE_LIBARCHIVE

static Bool archive_entry_path_is_safe(const char *path)
{
	const char *component;

	if (!path || path[0] == '\0' || path[0] == '/' || path[0] == '\\')
		return False;

	component = path;
	while (*component) {
		const char *slash = component;
		size_t length;

		while (*slash && *slash != '/' && *slash != '\\')
			slash++;

		length = slash - component;
		if ((length == 1 && component[0] == '.') ||
		    (length == 2 && component[0] == '.' && component[1] == '.'))
			return False;

		if (*slash == '\0')
			break;
		component = slash + 1;
	}

	return True;
}

/*
	Recursively find directory containing image files
	Returns path to directory with images, or NULL if not found
*/
static char *find_image_directory(const char *start_dir)
{
	DIR *dir;
	struct dirent *entry;
	struct stat st;
	char full_path[PATH_MAX];
	int image_count = 0;
	char *result = NULL;

	dir = opendir(start_dir);
	if (!dir)
		return NULL;


	/* First pass: count image files in current directory */
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;

		snprintf(full_path, sizeof(full_path), "%s/%s", start_dir, entry->d_name);
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

		snprintf(full_path, sizeof(full_path), "%s/%s", start_dir, entry->d_name);
		if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
			result = find_image_directory(full_path);
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

	if (WMIV_DEBUG)
		fprintf(stderr, "Created temporary directory: %s\n", temp_dir);

	/* Extract the archive */
	if (is_archive_file(filename)) {
		result = extract_archive_libarchive(filename, temp_dir);
	} else {
		fprintf(stderr, "Error: unknown archive type\n");
		cleanup_temp_dir();
		return NULL;
	}

	if (result != 0) {
		cleanup_temp_dir();
		return NULL;
	}

	/* Check if extraction resulted in nested directories - find the one with actual images */
	char *image_dir = find_image_directory(temp_dir);
	if (image_dir && strcmp(image_dir, temp_dir) != 0) {
		if (WMIV_DEBUG)
			fprintf(stderr, "Archive contains nested directories, using image directory: %s\n", image_dir);

		char *result_dir = strdup(image_dir);
		free(image_dir);
		is_archive = True;
		return result_dir;
	}

	if (image_dir)
		free(image_dir);

	is_archive = True;
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

/*
	Free current clipboard data (both PNG and JPEG)
*/
static void free_clipboard_data(void)
{
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

/*
	Begin an INCR transfer for the given request.
	Selects PropertyChangeMask on the requestor before sending SelectionNotify so
	a fast client cannot delete the INCR property before we are listening.
	Returns True if the INCR handshake was started (caller must not send a second
	SelectionNotify), False if an INCR is already in flight and this request must
	be refused.
*/
static Bool start_incr_transfer(XSelectionRequestEvent *req, XEvent *notify,
				unsigned char *data, size_t size)
{
	XWindowAttributes attr;
	unsigned long size32;

	if (incr.active) {
		/* single-slot sender, do not clobber the running transfer */
		if (WMIV_DEBUG)
			fprintf(stderr, "INCR: refusing concurrent transfer to 0x%lx (one already active)\n",
				(unsigned long)req->requestor);
		return False;
	}

	incr.active = True;
	incr.requestor = req->requestor;
	incr.property = req->property;
	incr.target = req->target;
	incr.total_size = size;
	incr.offset = 0;
	incr.chunk_size = CHUNK_SIZE;
	incr.data = data;
	incr.saved_event_mask = 0;

	/* Listen for property deletions before announcing INCR, otherwise a fast
	   requestor can delete the property before our SelectInput reaches the
	   server and the first chunk is never sent */
	if (XGetWindowAttributes(dpy, req->requestor, &attr))
		incr.saved_event_mask = attr.your_event_mask;
	XSelectInput(dpy, req->requestor, incr.saved_event_mask | PropertyChangeMask);

	size32 = (unsigned long)size;
	XChangeProperty(dpy, req->requestor, req->property, incr_atom, 32,
			PropModeReplace, (unsigned char *)&size32, 1);
	notify->xselection.property = req->property;
	XSendEvent(dpy, req->requestor, False, 0, notify);
	XFlush(dpy);

	if (WMIV_DEBUG)
		fprintf(stderr, "Starting INCR for %s %zu bytes\n",
			req->target == png_atom ? "PNG" : "JPEG", size);
	return True;
}

/*
	Send the next chunk for INCR transfer (called when requestor deletes the property)
*/
static void send_next_incr_chunk(void)
{
	size_t remaining, to_send;

	if (!incr.active)
		return;

	if (!incr.data || incr.total_size == 0) {
		/* Nothing to send: finish with zero-length property */
		XChangeProperty(dpy, incr.requestor, incr.property, incr.target, 8, PropModeReplace, NULL, 0);
		/* Restore the requestor's previous event mask */
		XSelectInput(dpy, incr.requestor, incr.saved_event_mask);
		incr.active = False;
		return;
	}

	remaining = incr.total_size - incr.offset;
	to_send = remaining;

	if (to_send > incr.chunk_size)
		to_send = incr.chunk_size;

	XChangeProperty(dpy, incr.requestor, incr.property, incr.target, 8, PropModeReplace,
				incr.data + incr.offset, (int)to_send);
	XFlush(dpy);

	incr.offset += to_send;

	if (incr.offset >= incr.total_size) {
		/* finished: next deletion by client -> we'll send zero-length and finish */
		if (WMIV_DEBUG)
			fprintf(stderr, "INCR: all data sent (%zu bytes), awaiting final property deletion\n", incr.total_size);
	} else {
		if (WMIV_DEBUG)
			fprintf(stderr, "INCR: sent chunk %zu/%zu\n", incr.offset, incr.total_size);
	}
}

/*
	Called when Ctrl+C pressed: encode current image to PNG and JPEG and claim CLIPBOARD
*/
static void copy_current_image_to_clipboard(void)
{
	if (!dpy || !win || !img) return;

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
	XSetSelectionOwner(dpy, clipboard_atom, win, CurrentTime);
	if (XGetSelectionOwner(dpy, clipboard_atom) != win) {
		fprintf(stderr, "Error: failed to own clipboard selection\n");
		free_clipboard_data();
		we_own_clipboard = False;
		return;
	}
	we_own_clipboard = True;
	XFlush(dpy);
	if (WMIV_DEBUG)
		fprintf(stderr, "Copied image to clipboard (PNG: %zu bytes, JPEG: %zu bytes)\n", clipboard_png_size, clipboard_jpeg_size);
}

/*
	Handle SelectionRequest events: serve TARGETS and image formats (with INCR when needed)
*/
static void handle_selection_request(XSelectionRequestEvent *req)
{
	if (WMIV_DEBUG) {
		char *target_name = XGetAtomName(req->display, req->target);
		char *selection_name = XGetAtomName(req->display, req->selection);

		fprintf(stderr, "SelectionRequest received: target=%s, selection=%s\n",
				target_name ? target_name : "unknown",
				selection_name ? selection_name : "unknown");
		if (target_name)
			XFree(target_name);
		if (selection_name)
			XFree(selection_name);
	}

	XEvent ev;
	memset(&ev, 0, sizeof(ev));
	ev.xselection.type = SelectionNotify;
	ev.xselection.display = req->display;
	ev.xselection.requestor = req->requestor;
	ev.xselection.selection = req->selection;
	ev.xselection.target = req->target;
	ev.xselection.time = req->time;
	ev.xselection.property = None;

	if (req->selection == atom_XdndSelection) {
		/* serve the file:// URI prepared by initiate_file_drag() */
		if (xdnd_drag_uri &&
		    (req->target == atom_uri_list || req->target == atom_text_plain)) {
			XChangeProperty(dpy, req->requestor, req->property, req->target, 8,
					PropModeReplace, (unsigned char *)xdnd_drag_uri,
					(int)strlen(xdnd_drag_uri));
			ev.xselection.property = req->property;
		}
		XSendEvent(dpy, req->requestor, False, 0, &ev);
		XFlush(dpy);
		return;
	}

	if (req->target == targets_atom) {
		/* advertise supported targets, ICCCM requires TARGETS to list itself */
		Atom types[3];
		int ntypes = 0;
		types[ntypes++] = targets_atom;
#ifdef USE_PNG
		types[ntypes++] = png_atom;
#endif
#ifdef USE_JPEG
		types[ntypes++] = jpeg_atom;
#endif
		XChangeProperty(dpy, req->requestor, req->property, XA_ATOM, 32, PropModeReplace,
				(unsigned char *)types, ntypes);
		ev.xselection.property = req->property;
	}
	else if (req->target == png_atom) {
		if (!clipboard_png_data || clipboard_png_size == 0) {
			ev.xselection.property = None;
		} else if (clipboard_png_size > CHUNK_SIZE) {
			if (start_incr_transfer(req, &ev, clipboard_png_data, clipboard_png_size))
				return; /* SelectionNotify already sent */
			ev.xselection.property = None;
		} else {
			XChangeProperty(dpy, req->requestor, req->property, png_atom, 8, PropModeReplace,
					clipboard_png_data, (int)clipboard_png_size);
			ev.xselection.property = req->property;
		}
	}
	else if (req->target == jpeg_atom) {
		if (!clipboard_jpeg_data || clipboard_jpeg_size == 0) {
			ev.xselection.property = None;
		} else if (clipboard_jpeg_size > CHUNK_SIZE) {
			if (start_incr_transfer(req, &ev, clipboard_jpeg_data, clipboard_jpeg_size))
				return; /* SelectionNotify already sent */
			ev.xselection.property = None;
		} else {
			XChangeProperty(dpy, req->requestor, req->property, jpeg_atom, 8, PropModeReplace,
					clipboard_jpeg_data, (int)clipboard_jpeg_size);
			ev.xselection.property = req->property;
		}
	} else {
		ev.xselection.property = None;
	}

	XSendEvent(dpy, req->requestor, False, 0, &ev);
	XFlush(dpy);
}

/*
	Called when the owner loses the selection
*/
static void handle_selection_clear(void)
{
	if (we_own_clipboard) {
		we_own_clipboard = False;
		free_clipboard_data();
	}
	if (incr.active) {
		/* Cancel any active INCR transfer and clean up */
		if (incr.requestor != None) {
			/* Restore the requestor's previous event mask */
			XSelectInput(dpy, incr.requestor, incr.saved_event_mask);
		}
		incr.active = False;
		incr.offset = 0;
		incr.total_size = 0;
		incr.requestor = None;
		incr.property = None;
		incr.target = None;
	}
}

static void reset_incr_transfer(void)
{
	incr.active = False;
	incr.offset = 0;
	incr.total_size = 0;
	incr.requestor = None;
	incr.property = None;
	incr.target = None;
	incr.data = NULL;
}

static void handle_outgoing_incr_property_notify(XPropertyEvent *pe)
{
	if (!incr.active ||
	    pe->window != incr.requestor ||
	    pe->atom != incr.property ||
	    pe->state != PropertyDelete) {
		return;
	}

	if (WMIV_DEBUG)
		fprintf(stderr, "INCR: property delete observed, sending next chunk\n");

	if (incr.offset >= incr.total_size) {
		XChangeProperty(dpy, incr.requestor, incr.property, incr.target, 8,
				PropModeReplace, NULL, 0);
		XFlush(dpy);
		XSelectInput(dpy, incr.requestor, incr.saved_event_mask);
		reset_incr_transfer();
		if (WMIV_DEBUG)
			fprintf(stderr, "INCR: transfer complete\n");
		return;
	}

	send_next_incr_chunk();
}

typedef struct {
	Atom selection;
	Atom target;
	Atom property;
} ClipboardSelectionWait;

typedef struct {
	Atom property;
} ClipboardPropertyWait;

static Bool clipboard_selection_notify_matches(Display *display, XEvent *event, XPointer arg)
{
	ClipboardSelectionWait *wait = (ClipboardSelectionWait *)arg;

	(void)display;
	return event->type == SelectionNotify &&
		event->xselection.selection == wait->selection &&
		event->xselection.requestor == win &&
		event->xselection.target == wait->target &&
		(event->xselection.property == wait->property ||
		 event->xselection.property == None);
}

static Bool clipboard_property_new_value_matches(Display *display, XEvent *event, XPointer arg)
{
	ClipboardPropertyWait *wait = (ClipboardPropertyWait *)arg;

	(void)display;
	return event->type == PropertyNotify &&
		event->xproperty.window == win &&
		event->xproperty.atom == wait->property &&
		event->xproperty.state == PropertyNewValue;
}

static Bool clipboard_service_event_matches(Display *display, XEvent *event, XPointer arg)
{
	(void)display;
	(void)arg;

	return event->type == SelectionRequest ||
		event->type == SelectionClear ||
		(event->type == PropertyNotify &&
		 incr.active &&
		 event->xproperty.window == incr.requestor &&
		 event->xproperty.atom == incr.property &&
		 event->xproperty.state == PropertyDelete);
}

static void dispatch_clipboard_service_event(XEvent *event)
{
	if (event->type == SelectionRequest) {
		handle_selection_request(&event->xselectionrequest);
	} else if (event->type == SelectionClear) {
		handle_selection_clear();
	} else if (event->type == PropertyNotify) {
		handle_outgoing_incr_property_notify(&event->xproperty);
	}
}

static int defer_clipboard_event(XEvent **events, size_t *count,
		size_t *capacity, XEvent *event)
{
	XEvent *new_events;
	size_t new_capacity;

	if (*count == *capacity) {
		new_capacity = *capacity ? *capacity * 2 : 8;
		new_events = realloc(*events, new_capacity * sizeof(**events));
		if (!new_events)
			return 0;
		*events = new_events;
		*capacity = new_capacity;
	}

	(*events)[(*count)++] = *event;
	return 1;
}

static void restore_clipboard_events(XEvent *events, size_t count)
{
	while (count > 0)
		XPutBackEvent(dpy, &events[--count]);
	free(events);
}

static Bool wait_for_clipboard_event(Bool (*predicate)(Display *, XEvent *, XPointer),
		XPointer arg, XEvent *out, int timeout_ms)
{
	struct timespec deadline;
	XEvent service;
	XEvent ignored;
	XEvent *deferred = NULL;
	size_t deferred_count = 0;
	size_t deferred_capacity = 0;
	int ready;

	wmiv_set_deadline(&deadline, timeout_ms);
	for (;;) {
		if (XCheckIfEvent(dpy, out, predicate, arg)) {
			restore_clipboard_events(deferred, deferred_count);
			return True;
		}

		while (XCheckIfEvent(dpy, &service, clipboard_service_event_matches, NULL))
			dispatch_clipboard_service_event(&service);

		if (XCheckIfEvent(dpy, out, predicate, arg)) {
			restore_clipboard_events(deferred, deferred_count);
			return True;
		}

		if (XPending(dpy)) {
			XNextEvent(dpy, &ignored);
			if (clipboard_service_event_matches(dpy, &ignored, NULL)) {
				dispatch_clipboard_service_event(&ignored);
			} else if (!defer_clipboard_event(&deferred, &deferred_count,
					&deferred_capacity, &ignored)) {
				XPutBackEvent(dpy, &ignored);
				restore_clipboard_events(deferred, deferred_count);
				return False;
			}
			continue;
		}

		ready = wmiv_wait_x_until(dpy, &deadline);
		if (ready <= 0) {
			restore_clipboard_events(deferred, deferred_count);
			return False;
		}
	}
}

static Bool request_clipboard_selection(Atom selection, Atom target, Atom property,
		Time timestamp, XSelectionEvent *out)
{
	ClipboardSelectionWait wait;
	XEvent event;

	wait.selection = selection;
	wait.target = target;
	wait.property = property;

	XDeleteProperty(dpy, win, property);
	XConvertSelection(dpy, selection, target, property, win, timestamp);
	XFlush(dpy);

	if (!wait_for_clipboard_event(clipboard_selection_notify_matches,
			(XPointer)&wait, &event, CLIPBOARD_PASTE_TIMEOUT_MS)) {
		return False;
	}

	*out = event.xselection;
	return out->property != None;
}

static int clipboard_candidate_targets(Atom *targets)
{
	int count = 0;

#ifdef USE_PNG
	targets[count++] = png_atom;
#endif
#ifdef USE_JPEG
	targets[count++] = jpeg_atom;
#endif
	return count;
}

static const char *clipboard_target_name(Atom target)
{
	if (target == png_atom)
		return "image/png";
	if (target == jpeg_atom)
		return "image/jpeg";
	return "unknown";
}

static Atom choose_clipboard_target(void)
{
	XSelectionEvent selection;
	Atom type;
	int format;
	unsigned long nitems;
	unsigned long bytes_after;
	unsigned char *prop = NULL;
	Atom candidates[2];
	Atom *targets;
	int candidate_count;
	int i;
	unsigned long j;
	Atom result = None;

	if (!request_clipboard_selection(clipboard_atom, targets_atom, property_atom,
			CurrentTime, &selection)) {
		return None;
	}

	if (XGetWindowProperty(dpy, win, property_atom, 0, 128, False,
			XA_ATOM, &type, &format, &nitems, &bytes_after,
			&prop) != Success) {
		return None;
	}
	XDeleteProperty(dpy, win, property_atom);

	if (type != XA_ATOM || format != 32 || nitems == 0 || !prop) {
		if (prop)
			XFree(prop);
		return None;
	}

	candidate_count = clipboard_candidate_targets(candidates);
	targets = (Atom *)prop;
	for (i = 0; i < candidate_count && result == None; i++) {
		for (j = 0; j < nitems; j++) {
			if (targets[j] == candidates[i]) {
				result = candidates[i];
				break;
			}
		}
	}

	XFree(prop);
	return result;
}

static int read_clipboard_property_chunk(Atom property, Atom *type_out,
		unsigned char **data_out, size_t *len_out)
{
	Atom type, type2;
	int format, format2;
	unsigned long nitems, nitems2;
	unsigned long bytes_after, bytes_after2;
	unsigned char *prop = NULL, *prop2 = NULL;
	long length;

	*type_out = None;
	*data_out = NULL;
	*len_out = 0;

	if (XGetWindowProperty(dpy, win, property, 0, 0, False,
			AnyPropertyType, &type, &format, &nitems,
			&bytes_after, &prop) != Success) {
		return -1;
	}
	if (prop)
		XFree(prop);
	if (type == None)
		return 0;
	if (format != 8 || bytes_after > CLIPBOARD_PASTE_MAX_BYTES)
		return -1;

	length = (long)((bytes_after + 3) / 4);
	if (length == 0)
		length = 1;

	if (XGetWindowProperty(dpy, win, property, 0, length, True,
			AnyPropertyType, &type2, &format2, &nitems2,
			&bytes_after2, &prop2) != Success) {
		return -1;
	}
	if (format2 != 8 || bytes_after2 != 0 || nitems2 > CLIPBOARD_PASTE_MAX_BYTES) {
		if (prop2)
			XFree(prop2);
		return -1;
	}

	*type_out = type2;
	*data_out = prop2;
	*len_out = (size_t)nitems2;
	return 1;
}

static int receive_clipboard_incr_data(Atom property, Atom target,
		Atom *type_out, unsigned char **data_out, size_t *len_out)
{
	ClipboardPropertyWait wait;
	XEvent event;
	unsigned char *data;
	size_t size = 0;
	Atom actual_type = None;

	data = malloc(1);
	if (!data)
		return 0;

	wait.property = property;
	XDeleteProperty(dpy, win, property);
	XFlush(dpy);

	for (;;) {
		Atom chunk_type;
		unsigned char *chunk = NULL;
		unsigned char *new_data;
		size_t chunk_len = 0;
		int chunk_status;

		if (!wait_for_clipboard_event(clipboard_property_new_value_matches,
				(XPointer)&wait, &event, CLIPBOARD_PASTE_TIMEOUT_MS)) {
			free(data);
			return 0;
		}

		chunk_status = read_clipboard_property_chunk(property, &chunk_type, &chunk, &chunk_len);
		if (chunk_status < 0) {
			free(data);
			return 0;
		}
		if (chunk_status == 0)
			continue;

		if (chunk_len == 0) {
			if (chunk)
				XFree(chunk);
			if (size == 0) {
				free(data);
				return 0;
			}
			*type_out = actual_type == None ? target : actual_type;
			*data_out = data;
			*len_out = size;
			return 1;
		}

		if (chunk_type != target ||
		    chunk_len > CLIPBOARD_PASTE_MAX_BYTES ||
		    size > CLIPBOARD_PASTE_MAX_BYTES - chunk_len) {
			XFree(chunk);
			free(data);
			return 0;
		}

		new_data = realloc(data, size + chunk_len);
		if (!new_data) {
			XFree(chunk);
			free(data);
			return 0;
		}
		data = new_data;
		memcpy(data + size, chunk, chunk_len);
		size += chunk_len;
		if (actual_type == None)
			actual_type = chunk_type;
		XFree(chunk);
	}
}

static int retrieve_clipboard_property(Atom property, Atom target,
		Atom *type_out, unsigned char **data_out, size_t *len_out)
{
	Atom type, type2;
	int format, format2;
	unsigned long nitems, nitems2;
	unsigned long bytes_after, bytes_after2;
	unsigned char *prop = NULL, *prop2 = NULL;
	unsigned char *copy;
	long length;

	*type_out = None;
	*data_out = NULL;
	*len_out = 0;

	if (XGetWindowProperty(dpy, win, property, 0, 0, False,
			AnyPropertyType, &type, &format, &nitems,
			&bytes_after, &prop) != Success) {
		return 0;
	}
	if (prop)
		XFree(prop);

	if (type == None)
		return 0;
	if (type == incr_atom)
		return receive_clipboard_incr_data(property, target, type_out, data_out, len_out);
	if (type != target || format != 8 || bytes_after == 0 ||
	    bytes_after > CLIPBOARD_PASTE_MAX_BYTES) {
		return 0;
	}

	length = (long)((bytes_after + 3) / 4);
	if (length == 0)
		length = 1;

	if (XGetWindowProperty(dpy, win, property, 0, length, True,
			AnyPropertyType, &type2, &format2, &nitems2,
			&bytes_after2, &prop2) != Success) {
		return 0;
	}
	if (type2 != target || format2 != 8 || bytes_after2 != 0 ||
	    nitems2 == 0 || nitems2 > CLIPBOARD_PASTE_MAX_BYTES) {
		if (prop2)
			XFree(prop2);
		return 0;
	}

	copy = malloc((size_t)nitems2);
	if (!copy) {
		XFree(prop2);
		return 0;
	}
	memcpy(copy, prop2, (size_t)nitems2);
	XFree(prop2);

	*type_out = type2;
	*data_out = copy;
	*len_out = (size_t)nitems2;
	return 1;
}

static int request_clipboard_data(Atom target, Atom *type_out,
		unsigned char **data_out, size_t *len_out)
{
	XSelectionEvent selection;

	if (!request_clipboard_selection(clipboard_atom, target, property_atom,
			CurrentTime, &selection)) {
		return 0;
	}

	return retrieve_clipboard_property(property_atom, target, type_out, data_out, len_out);
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
		if (exifEntry)
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
		if (!asprintf(&combined_title, "%s - %s", APPNAME, filename))
			return EXIT_FAILURE;
	}
	else {
		if (!asprintf(&combined_title, "%s - %u/%u - %s", APPNAME, current_index, max_index, filename))
			if (!asprintf(&combined_title, "%s - %u/%u", APPNAME, current_index, max_index))
				return EXIT_FAILURE;
	}
	WXSetWMName(dpy, win, combined_title);
	free(combined_title);
	return EXIT_SUCCESS;
}

/*
	Rescale the current image based on the screen size
	Returns EXIT_SUCCESS on success, EXIT_FAILURE on failure
*/
int rescale_image(void)
{
	long final_width = img->width;
	long final_height = img->height;
	int max_width_tmp = max_width;
	int max_height_tmp = max_height;

	if (!fullscreen_flag) {
		max_width_tmp -= extents.left + extents.right;
		max_height_tmp -= extents.top + extents.bottom;
	}

	/* check if there is already a zoom factor applied */
	if (fabsf(zoom_factor) <= 0.0f) {
		final_width = img->width + (int)(img->width * zoom_factor);
		final_height = img->height + (int)(img->height * zoom_factor);
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
	if (!RConvertImage(ctx, img, &pix)) {
		fprintf(stderr, "Error: %s\n", RMessageForError(RErrorCode));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
	Find the best image size for the current display
	Returns EXIT_SUCCESS on success
*/
int maximize_image(void)
{
	rescale_image();
	XCopyArea(dpy, pix, win, ctx->copy_gc, 0, 0,
		img->width, img->height, max_width/2-img->width/2, max_height/2-img->height/2);
	return EXIT_SUCCESS;
}

/*
	Merge the current image with a checkerboard background
	Returns EXIT_SUCCESS on success, EXIT_FAILURE on failure
*/
int merge_with_background(RImage *i)
{
	if (i) {
		RImage *back;
		back = RCreateImage(i->width, i->height, True);
		if (back) {
			int opaq = 255;
			int x = 0, y = 0;

			RFillImage(back, &lightGray);
			for (x = 0; x <= i->width; x += 8) {
				if (x/8 % 2)
					y = 8;
				else
					y = 0;
				for (; y <= i->height; y += 16)
					ROperateRectangle(back, RAddOperation, x, y, x+8, y+8, &darkGray);
			}

			RCombineImagesWithOpaqueness(i, back, opaq);
			RReleaseImage(back);
			return EXIT_SUCCESS;
		}
	}
	return EXIT_FAILURE;
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
			XResizeWindow(dpy, win, tmp->width, tmp->height);
	}

	RReleaseImage(img);
	img = tmp;

	rescale_image();
	if (!fullscreen_flag) {
		XCopyArea(dpy, pix, win, ctx->copy_gc, 0, 0, img->width, img->height, 0, 0);
	} else {
		XClearWindow(dpy, win);
		XCopyArea(dpy, pix, win, ctx->copy_gc, 0, 0,
			img->width, img->height, max_width/2-img->width/2, max_height/2-img->height/2);
	}

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
	XWindowAttributes attr;

	if (win && (XGetWindowAttributes(dpy, win, &attr) >= 0))
		failed_image = RCreateImage(attr.width, attr.height, False);
	else
		failed_image = RCreateImage(50, 50, False);
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
	Atom wm_state = XInternAtom(dpy, "_NET_WM_STATE", True);
	Atom fullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", True);

	if (fullscreen_flag) {
		fullscreen_flag = False;
		zoom_factor = 0;
		back_from_fullscreen = True;
	} else {
		fullscreen_flag = True;
		zoom_factor = 1000;
		RReleaseImage(img);
		img = load_oriented_image(ctx, current_link->data, 0);
		if (!img)
			img = draw_failed_image();
		else
			merge_with_background(img);
	}

	WXSendClientMessage(dpy, DefaultRootWindow(dpy), SubstructureNotifyMask,
			    win, wm_state, fullscreen_flag, fullscreen, 0, 0, 0);
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
		if (!fullscreen_flag) {
			XResizeWindow(dpy, win, img->width, img->height);
			XClearWindow(dpy, win);
			XCopyArea(dpy, pix, win, ctx->copy_gc, 0, 0,
				img->width, img->height, 0, 0);
		} else {
			XClearWindow(dpy, win);
			XCopyArea(dpy, pix, win, ctx->copy_gc, 0, 0,
				img->width, img->height,
				max_width / 2 - img->width / 2,
				max_height / 2 - img->height / 2);
		}
		XSync(dpy, False);
	}
	return EXIT_SUCCESS;
}

static int paste_clipboard_image(void)
{
	Atom candidates[2];
	Atom target;
	Atom actual_type = None;
	unsigned char *data = NULL;
	size_t size = 0;
	char *path;
	int candidate_count;
	int i;

	if (XGetSelectionOwner(dpy, clipboard_atom) == None) {
		fprintf(stderr, "Error: clipboard is empty\n");
		return EXIT_FAILURE;
	}

	candidate_count = clipboard_candidate_targets(candidates);
	if (candidate_count == 0) {
		fprintf(stderr, "Error: this wmiv build cannot load PNG or JPEG clipboard data\n");
		return EXIT_FAILURE;
	}

	target = choose_clipboard_target();
	if (target != None && request_clipboard_data(target, &actual_type, &data, &size)) {
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
	RImage *tmp = load_oriented_image(ctx, current_link->data, 0);
	if (!tmp)
		return EXIT_FAILURE;

	if (z) {
		zoom_factor += 0.2f;
		img = RScaleImage(tmp, tmp->width + (int)(tmp->width * zoom_factor),
				tmp->height + (int)(tmp->height * zoom_factor));
		if (!img) {
			img = old_img;
			RReleaseImage(tmp);
			return EXIT_FAILURE;
		}
	} else {
		zoom_factor -= 0.2f;
		int new_width = tmp->width + (int) (tmp->width * zoom_factor);
		int new_height = tmp->height + (int)(tmp->height * zoom_factor);
		if ((new_width <= 0) || (new_height <= 0)) {
			zoom_factor += 0.2f;
			RReleaseImage(tmp);
			return EXIT_FAILURE;
		}
		img = RScaleImage(tmp, new_width, new_height);
		if (!img) {
			img = old_img;
			RReleaseImage(tmp);
			return EXIT_FAILURE;
		}
	}
	RReleaseImage(old_img);
	RReleaseImage(tmp);
	XFreePixmap(dpy, pix);

	merge_with_background(img);
	if (!RConvertImage(ctx, img, &pix)) {
		fprintf(stderr, "Error: %s\n", RMessageForError(RErrorCode));
		return EXIT_FAILURE;
	}
	XResizeWindow(dpy, win, img->width, img->height);
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
		if (!img) {
			if (strlen((char *)current_link->data) == 0)
				fprintf(stderr, "Error: %s\n", RMessageForError(RErrorCode));
			else
				fprintf(stderr, "Error: %s %s\n", (char *)current_link->data, RMessageForError(RErrorCode));
			img = draw_failed_image();
			if (ignore_unknown_file_format) {
				link_t *tmp_link;
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
				linked_list_del(&list, current_link);
				current_link = tmp_link;
				max_index = list.count;
				return change_image(way);
			}
		} else {
			merge_with_background(img);
		}
		rescale_image();
		if (win) {
			if (!fullscreen_flag) {
				if ((old_img_width != img->width) || (old_img_height != img->height))
					XResizeWindow(dpy, win, img->width, img->height);
				else
					XCopyArea(dpy, pix, win, ctx->copy_gc, 0, 0, img->width, img->height, 0, 0);
				change_title((char *)current_link->data);
			} else {
				XClearWindow(dpy, win);
				XCopyArea(dpy, pix, win, ctx->copy_gc, 0, 0,
					img->width, img->height, max_width/2-img->width/2, max_height/2-img->height/2);
			}
		}
		return EXIT_SUCCESS;
	}
	return EXIT_FAILURE;
}

#ifdef HAVE_PTHREAD
/*
	Send an X event to display the next image at every delay set to slideshow_delay
*/
void *slideshow(void *arg)
{
	(void) arg;

	XKeyEvent event;
	event.display = dpy;
	event.window = win;
	event.root = DefaultRootWindow(dpy);
	event.subwindow = None;
	event.time = CurrentTime;
	event.x = 1;
	event.y = 1;
	event.x_root = 1;
	event.y_root = 1;
	event.same_screen = True;
	event.keycode = XKeysymToKeycode(dpy, XK_Right);
	event.state = 0;
	event.type = KeyPress;

	while (slideshow_flag) {
		int r;
		r = XSendEvent(event.display, event.window, True, KeyPressMask, (XEvent *)&event);
		if (!r)
			fprintf(stderr, "Error: can't send event\n");
		XFlush(dpy);
		/* default sleep time between moving to next image */
		sleep(slideshow_delay);
	}
	tid = 0;
	return arg;
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

	if (!dirpath)
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
			if (dirpath[strlen(dirpath)-1] == FILE_SEPARATOR)
				snprintf(path, PATH_MAX, "%s%s", dirpath, dir[idx]->d_name);
			else
				snprintf(path, PATH_MAX, "%s%c%s", dirpath, FILE_SEPARATOR, dir[idx]->d_name);

			free(dir[idx]);
			if ((lstat(path, &stDirInfo) == 0) && !S_ISDIR(stDirInfo.st_mode))
				linked_list_add(list, strdup(path));
	}
	free(dir);
	return list->first;
}

#ifdef USE_RANDR
/*
	Retrieve the monitor resolution where the window is located
*/
void get_monitor_dimensions(Display *dpy, int screen_num, Window win, int *width, int *height)
{
	int monitor_count, abs_x, abs_y;
	Window child;
	Window root_window = RootWindow(dpy, screen_num);
	XWindowAttributes attrs;
	XGetWindowAttributes(dpy, win, &attrs);
	XTranslateCoordinates(dpy, win, attrs.root, 0, 0, &abs_x, &abs_y, &child);

	XRRMonitorInfo *monitors = XRRGetMonitors(dpy, root_window, True, &monitor_count);
	if (monitors && monitor_count > 0) {
		for (int i = 0; i < monitor_count; i++) {
			if (abs_x >= monitors[i].x && abs_x < (monitors[i].x + monitors[i].width) &&
				abs_y >= monitors[i].y && abs_y < (monitors[i].y + monitors[i].height)) {
					*width = monitors[i].width;
					*height = monitors[i].height;
					break;
				}
		}
		XRRFreeMonitors(monitors);
	}
}
#endif

/*
	Transform the given URI to UTF-8 string
*/
void decode_uri(char *uri)
{
	char *last = uri + strlen(uri);

	while (uri < last-2) {
			if (*uri == '%') {
					int h;
					if (sscanf(uri+1, "%2X", &h) != 1)
							break;
					*uri = h;
					memmove(uri+1, uri+3, last - (uri+2));
					last -= 2;
			}
			uri++;
	}
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
	atom_XdndAware      = XInternAtom(dpy, "XdndAware", False);
	atom_XdndEnter      = XInternAtom(dpy, "XdndEnter", False);
	atom_XdndPosition   = XInternAtom(dpy, "XdndPosition", False);
	atom_XdndStatus     = XInternAtom(dpy, "XdndStatus", False);
	atom_XdndLeave      = XInternAtom(dpy, "XdndLeave", False);
	atom_XdndDrop       = XInternAtom(dpy, "XdndDrop", False);
	atom_XdndFinished   = XInternAtom(dpy, "XdndFinished", False);
	atom_XdndSelection  = XInternAtom(dpy, "XdndSelection", False);
	atom_XdndTypeList   = XInternAtom(dpy, "XdndTypeList", False);
	atom_XdndActionCopy = XInternAtom(dpy, "XdndActionCopy", False);
	atom_uri_list       = XInternAtom(dpy, "text/uri-list", False);
	atom_text_plain     = XInternAtom(dpy, "text/plain", False);
}

/* ----- XDND drop target ---------------------------------------------- */

static Atom wmiv_xdnd_pick_type(const Atom *types, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		if (types[i] == atom_uri_list)
			return atom_uri_list;
	for (i = 0; i < count; i++)
		if (types[i] == atom_text_plain)
			return atom_text_plain;
	return None;
}

static void wmiv_xdnd_send_status(Bool accept)
{
	WXSendClientMessage(dpy, xdnd_target.source, NoEventMask,
			    xdnd_target.source, atom_XdndStatus,
			    (long)win,
			    accept ? 1 : 0,
			    0, 0,
			    accept ? (long)atom_XdndActionCopy : 0);
	XFlush(dpy);
}

static void wmiv_xdnd_send_finished(Bool ok)
{
	WXSendClientMessage(dpy, xdnd_target.source, NoEventMask,
			    xdnd_target.source, atom_XdndFinished,
			    (long)win,
			    (xdnd_target.version >= 5 && ok) ? 1 : 0,
			    (xdnd_target.version >= 5 && ok) ? (long)atom_XdndActionCopy : 0,
			    0, 0);
	XFlush(dpy);
}

static Bool wmiv_xdnd_is_selnotify(Display *d, XEvent *ev, XPointer arg)
{
	(void)d; (void)arg;
	/* bind the reply to the conversion we issued, a stale or forged
	 * SelectionNotify naming a different requestor, target or
	 * property must not steer which window property we then read as
	 * the dropped URI list */
	return ev->type == SelectionNotify
		&& ev->xselection.selection == atom_XdndSelection
		&& ev->xselection.requestor == win
		&& ev->xselection.target == xdnd_target.want_type
		&& (ev->xselection.property == atom_XdndSelection
		    || ev->xselection.property == None);
}

/* bounded wait for the XdndSelection SelectionNotify so a hostile or
 * crashed source cannot wedge the main loop indefinitely */
static Bool wmiv_xdnd_wait_selnotify(XEvent *out)
{
	return wmiv_wait_for_matching_event(dpy, out, wmiv_xdnd_is_selnotify, NULL, 2000);
}

static Bool wmiv_xdnd_is_drag_reply(Display *display, XEvent *event, XPointer arg)
{
	(void)display;
	(void)arg;

	return event->type == SelectionRequest ||
		(event->type == ClientMessage &&
		 event->xclient.window == win &&
		 event->xclient.message_type == atom_XdndFinished);
}

/*
 * Called from the main loop for every ClientMessage. Advances the XDND
 * target state machine and returns 1 with a NUL-terminated wmalloc'd copy
 * of the dropped data only when an XdndDrop completes. Returns 0 for
 * non-XDND messages and for intermediate XDND messages so the caller can
 * continue dispatching.
 */
static int wmiv_xdnd_handle_message(XClientMessageEvent *ev, unsigned char **out, int *out_len)
{
	/* every XDND message is a format-32 ClientMessage, anything else
	 * has garbage in data.l[] */
	if (ev->format != 32)
		return 0;

	if (ev->message_type == atom_XdndEnter) {
		Atom inline_types[3];

		xdnd_target.source  = (Window)ev->data.l[0];
		xdnd_target.version = (int)((unsigned long)ev->data.l[1] >> 24);
		xdnd_target.want_type = None;

		if (ev->data.l[1] & 1) {
			/* more than three types, fetch the bounded list from the
			 * source window using the validating helper */
			size_t n;
			Atom *types = WXGetAtomList(dpy, xdnd_target.source,
						    atom_XdndTypeList, WMIV_XDND_MAX_TYPES, &n);
			if (types) {
				xdnd_target.want_type = wmiv_xdnd_pick_type(types, n);
				wfree(types);
			}
		} else {
			inline_types[0] = (Atom)ev->data.l[2];
			inline_types[1] = (Atom)ev->data.l[3];
			inline_types[2] = (Atom)ev->data.l[4];
			xdnd_target.want_type = wmiv_xdnd_pick_type(inline_types, 3);
		}
		return 0;
	}

	if (ev->message_type == atom_XdndPosition) {
		if ((Window)ev->data.l[0] == xdnd_target.source)
			wmiv_xdnd_send_status(xdnd_target.want_type != None);
		return 0;
	}

	if (ev->message_type == atom_XdndLeave) {
		xdnd_target.source = None;
		xdnd_target.want_type = None;
		return 0;
	}

	if (ev->message_type == atom_XdndDrop) {
		XEvent sn;
		size_t len = 0;
		char *data;

		if ((Window)ev->data.l[0] != xdnd_target.source ||
		    xdnd_target.want_type == None) {
			wmiv_xdnd_send_finished(False);
			xdnd_target.source = None;
			return 0;
		}

		XConvertSelection(dpy, atom_XdndSelection, xdnd_target.want_type,
				  atom_XdndSelection, win, (Time)ev->data.l[2]);
		XFlush(dpy);

		/* wait for the source to deliver the converted selection,
		 * bounded so an unresponsive source cannot block us */
		if (!wmiv_xdnd_wait_selnotify(&sn)) {
			wmiv_xdnd_send_finished(False);
			xdnd_target.source = None;
			xdnd_target.want_type = None;
			return 0;
		}

		if (sn.xselection.property == None) {
			wmiv_xdnd_send_finished(False);
			xdnd_target.source = None;
			return 0;
		}

		data = WXRcvString(dpy, WXReqString8(dpy, win, sn.xselection.property,
						     xdnd_target.want_type), &len);
		XDeleteProperty(dpy, win, sn.xselection.property);
		wmiv_xdnd_send_finished(data != NULL);
		xdnd_target.source = None;
		xdnd_target.want_type = None;

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
	uri = wmalloc(n);
	snprintf(uri, n, "file://%s", full_path);
	free(full_path);
	return uri;
}

/*
 * Walk down from the root to the deepest child under (rx,ry) that carries
 * the XdndAware property. Some toolkits set it on the toplevel only, so
 * remember the highest aware ancestor and keep descending while children
 * exist under the pointer.
 */
static Window wmiv_xdnd_find_target(Window root, int rx, int ry)
{
	Window cur = root, child, aware = None;
	int cx = rx, cy = ry;
	Atom version;

	for (;;) {
		if (cur != root && cur != win &&
		    WXGetAtom(dpy, cur, atom_XdndAware, &version))
			aware = cur;
		if (!XTranslateCoordinates(dpy, root, cur, rx, ry, &cx, &cy, &child) ||
		    child == None)
			break;
		cur = child;
	}
	return aware;
}

static void wmiv_xdnd_send_enter(Window target)
{
	WXSendClientMessage(dpy, target, NoEventMask, target, atom_XdndEnter,
			    (long)win,
			    (long)WMIV_XDND_VERSION << 24,
			    (long)atom_uri_list,
			    (long)atom_text_plain,
			    0);
}

static void wmiv_xdnd_send_position(Window target, int rx, int ry, Time t)
{
	WXSendClientMessage(dpy, target, NoEventMask, target, atom_XdndPosition,
			    (long)win, 0,
			    ((long)rx << 16) | (ry & 0xffff),
			    (long)t,
			    (long)atom_XdndActionCopy);
}

static void wmiv_xdnd_send_leave(Window target)
{
	WXSendClientMessage(dpy, target, NoEventMask, target, atom_XdndLeave,
			    (long)win, 0, 0, 0, 0);
}

static void wmiv_xdnd_send_drop(Window target, Time t)
{
	WXSendClientMessage(dpy, target, NoEventMask, target, atom_XdndDrop,
			    (long)win, 0, (long)t, 0, 0);
}

/*
 * Run the XDND source state machine until button release. The selection
 * data is precomputed into xdnd_drag_uri so the existing
 * handle_selection_request() can serve it when the target asks.
 */
static void initiate_file_drag(void)
{
	Window root = DefaultRootWindow(dpy);
	Window target = None;
	Bool target_accepts = False;
	XEvent ev;

	if (xdnd_drag_uri)
		wfree(xdnd_drag_uri);
	xdnd_drag_uri = wmiv_build_drag_uri();
	if (!xdnd_drag_uri)
		return;

	XSetSelectionOwner(dpy, atom_XdndSelection, win, CurrentTime);
	if (XGetSelectionOwner(dpy, atom_XdndSelection) != win) {
		wfree(xdnd_drag_uri);
		xdnd_drag_uri = NULL;
		return;
	}

	if (XGrabPointer(dpy, win, False,
			 ButtonReleaseMask | PointerMotionMask,
			 GrabModeAsync, GrabModeAsync, None, None,
			 CurrentTime) != GrabSuccess) {
		wfree(xdnd_drag_uri);
		xdnd_drag_uri = NULL;
		return;
	}

	if (WMIV_DEBUG)
		fprintf(stderr, "XDND drag started: %s\n", xdnd_drag_uri);

	for (;;) {
		XNextEvent(dpy, &ev);

		if (ev.type == MotionNotify) {
			Window over = wmiv_xdnd_find_target(root,
							    ev.xmotion.x_root,
							    ev.xmotion.y_root);
			if (over != target) {
				if (target != None)
					wmiv_xdnd_send_leave(target);
				target = over;
				target_accepts = False;
				if (target != None)
					wmiv_xdnd_send_enter(target);
			}
			if (target != None)
				wmiv_xdnd_send_position(target,
							ev.xmotion.x_root,
							ev.xmotion.y_root,
							ev.xmotion.time);
			XFlush(dpy);
			continue;
		}

		if (ev.type == ClientMessage &&
		    ev.xclient.message_type == atom_XdndStatus) {
			if ((Window)ev.xclient.data.l[0] == target)
				target_accepts = (ev.xclient.data.l[1] & 1) != 0;
			continue;
		}

		if (ev.type == SelectionRequest) {
			handle_selection_request(&ev.xselectionrequest);
			continue;
		}

		if (ev.type == ButtonRelease) {
			XUngrabPointer(dpy, ev.xbutton.time);
			if (target != None && target_accepts) {
				struct timespec deadline;

				wmiv_xdnd_send_drop(target, ev.xbutton.time);
				XFlush(dpy);
				/* serve the selection and wait briefly for
				 * XdndFinished, but do not block forever if
				 * the target never sends it */
				wmiv_set_deadline(&deadline, 1000);
				while (wmiv_wait_for_matching_event_until(dpy, &ev, wmiv_xdnd_is_drag_reply,
								       NULL, &deadline)) {
					if (ev.type == SelectionRequest) {
						handle_selection_request(&ev.xselectionrequest);
						continue;
					}
					break;
				}
			} else if (target != None) {
				wmiv_xdnd_send_leave(target);
			}
			XFlush(dpy);
			break;
		}
	}

	wfree(xdnd_drag_uri);
	xdnd_drag_uri = NULL;
}

/*
	main
*/
int main(int argc, char **argv)
{
	int option = -1;
	RContextAttributes attr = {};
	XEvent e;
	KeySym keysym;
	char *reading_filename = "";
	int screen_num, file_i;
	int quit = 0;
	WXWMHints *win_hints;
	Atom delWindow;

#ifdef USE_XPM
	Pixmap icon_pixmap, icon_shape;
#endif

	/* Init colors */
	lightGray.red = lightGray.green = lightGray.blue = 211;
	darkGray.red = darkGray.green = darkGray.blue = 169;
	lightGray.alpha = darkGray.alpha = 1;
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
			ignore_unknown_file_format = True;
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
	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "Error: can't open display\n");
		linked_list_free(&list);
		return EXIT_FAILURE;
	}

	/* Initialize clipboard atoms */
	clipboard_atom = XInternAtom(dpy, "CLIPBOARD", False);
	targets_atom = XInternAtom(dpy, "TARGETS", False);
	png_atom = XInternAtom(dpy, "image/png", False);
	jpeg_atom = XInternAtom(dpy, "image/jpeg", False);
	property_atom = XInternAtom(dpy, "WMIV_CLIPBOARD_PROP", False);
	incr_atom = XInternAtom(dpy, "INCR", False);

	screen_num = DefaultScreen(dpy);
	max_width = DisplayWidth(dpy, screen_num);
	max_height = DisplayHeight(dpy, screen_num);

	attr.flags = 0;
	ctx = RCreateContext(dpy, screen_num, &attr);

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

	merge_with_background(img);

	win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 10, 10, 0, 0, BlackPixel(dpy, screen_num));
	XSelectInput(dpy, win, KeyPressMask|StructureNotifyMask|ExposureMask|ButtonPressMask|ButtonReleaseMask|PointerMotionMask|FocusChangeMask|PropertyChangeMask);

	delWindow = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	WXSetWMProtocols(dpy, win, &delWindow, 1);
	change_title(reading_filename);

	wmiv_xdnd_init_atoms();
	WXSetAtom(dpy, win, atom_XdndAware, (Atom)WMIV_XDND_VERSION);
	win_hints = WXAllocWMHints();

	if (win_hints) {
		win_hints->flags = StateHint|InputHint|WindowGroupHint;

#ifdef USE_XPM
		if ((XpmCreatePixmapFromData(dpy, win, wmiv_xpm, &icon_pixmap, &icon_shape, NULL)) == 0) {
			win_hints->flags |= IconPixmapHint|IconMaskHint;
			win_hints->icon_pixmap = icon_pixmap;
			win_hints->icon_mask = icon_shape;

			Atom net_wm_icon = XInternAtom(dpy, "_NET_WM_ICON", False);
			if (net_wm_icon != None) {
				int icon_width, icon_height;
				if (sscanf(wmiv_xpm[0], "%d %d", &icon_width, &icon_height) == 2) {
					/* Convert XPM pixmap to ARGB32 format */
					XImage *icon_image = XGetImage(dpy, icon_pixmap, 0, 0, icon_width, icon_height, AllPlanes, ZPixmap);
					if (icon_image) {
						XImage *mask_image = NULL;
						if (icon_shape != None) {
							mask_image = XGetImage(dpy, icon_shape, 0, 0, icon_width, icon_height, AllPlanes, ZPixmap);
						}

						/* Create ARGB32 data: width, height, followed by ARGB pixel data */
						uint32_t *icon_data = malloc(sizeof(uint32_t) * (2 + icon_width * icon_height));
						if (icon_data) {
							icon_data[0] = icon_width;
							icon_data[1] = icon_height;

							for (int y = 0; y < icon_height; y++) {
								for (int x = 0; x < icon_width; x++) {
									unsigned long pixel = XGetPixel(icon_image, x, y);
									unsigned long alpha = 0xFF000000; /* default opaque */

									/* Check mask for transparency */
									if (mask_image) {
										unsigned long mask_pixel = XGetPixel(mask_image, x, y);
										if (mask_pixel == 0) {
											alpha = 0x00000000; /* transparent */
										}
									}

									icon_data[2 + y * icon_width + x] = alpha | (pixel & 0x00FFFFFF);
								}
							}

							WXSetCardinalN(dpy, win, net_wm_icon, icon_data, 2 + icon_width * icon_height);
							free(icon_data);
						}

						XDestroyImage(icon_image);
						if (mask_image) {
							XDestroyImage(mask_image);
						}
					}
				}
			}
		}
#endif
		win_hints->initial_state = NormalState;
		win_hints->input = True;
		win_hints->window_group = win;
		WXSetWMIconName(dpy, win, APPNAME);
		WXSetCommand(dpy, win, argv, argc);
		WXSetWMHints(dpy, win, win_hints);
		WXSetWMClass(dpy, win, APPNAME, "wmiv");
		wfree(win_hints);
	}
	XMapWindow(dpy, win);
	XFlush(dpy);

	if (wait_for_frame_extents(dpy, win, 1000)) { /* Wait up to 1 second */
		if (!get_window_decor_size(dpy, win, &extents) && WMIV_DEBUG)
			fprintf(stderr, "Warning: Property found but could not parse frame extents\n");
	} else {
		if (WMIV_DEBUG)
			fprintf(stderr, "Warning: Window manager did not set _NET_FRAME_EXTENTS property within timeout\n");
	}

#ifdef USE_RANDR
	get_monitor_dimensions(dpy, screen_num, win, &max_width, &max_height);
#endif

	if (WMIV_DEBUG)
		fprintf(stderr, "Display size: %dx%d\n", max_width, max_height);

	rescale_image();
	XCopyArea(dpy, pix, win, ctx->copy_gc, 0, 0, img->width, img->height, 0, 0);
	XResizeWindow(dpy, win, img->width, img->height);
	XSync(dpy, True);

	/* Main event loop */
	while (!quit) {
		XNextEvent(dpy, &e);
		if (e.type == ClientMessage) {
			unsigned char *dropBuffer;
			int length;

			if (wmiv_xdnd_handle_message(&e.xclient, &dropBuffer, &length)) {
				char *handled_uri_header = "file://";
				char *filename_separator = "\n";
				char *ptr = strtok((char *)dropBuffer, filename_separator);

				if (WMIV_DEBUG)
					fprintf(stderr, "Dropped data: %s\n", (char *)dropBuffer);

				linked_list_free(&list);
				linked_list_init(&list);

				Bool find_one_file = False;
				while (ptr != NULL) {
					if (strstr(ptr, handled_uri_header) == ptr) {
						char *tmp_file;
						if (ptr[strlen(ptr) - 1] == '\r')
							ptr[strlen(ptr) - 1] = '\0';
						tmp_file = ptr + strlen(handled_uri_header);

						/* drag-and-drop file name format as per the spec is encoded as an URI */
						decode_uri(tmp_file);
						if (connect_dir(tmp_file, &list))
							find_one_file = True;
					}
					ptr = strtok(NULL, filename_separator);
				}
				wfree(dropBuffer);

				if (find_one_file) {
					max_index = list.count;
					current_link = list.last;
					change_image(NEXT);
				} else {
					if (WMIV_DEBUG)
						fprintf(stderr, "Error: no valid file found in dropped data\n");
					XClearArea(dpy, win, 0, 0, 0, 0, True);
				}
			}
			if (e.xclient.data.l[0] == delWindow)
				quit = 1;
			continue;
		}
		if (e.type == SelectionRequest) {
			/* Serve clipboard requests */
			handle_selection_request(&e.xselectionrequest);
			continue;
		}
		if (e.type == SelectionClear) {
			/* we lost ownership */
			handle_selection_clear();
			continue;
		}
		if (e.type == PropertyNotify) {
			XPropertyEvent *pe = &e.xproperty;

			if (incr.active &&
			    pe->window == incr.requestor &&
			    pe->atom == incr.property &&
			    pe->state == PropertyDelete) {
				handle_outgoing_incr_property_notify(pe);
			} else if (pe->window == win) {
				if (WMIV_DEBUG && incr.active)
					fprintf(stderr, "Ignoring PropertyNotify on main window during INCR\n");
			}
			continue;
		}
		if (e.type == FocusIn) {
			focus = True;
			continue;
		}
		if (e.type == FocusOut) {
			focus = False;
			continue;
		}
		if (!fullscreen_flag && (e.type == Expose)) {
			XExposeEvent xev = e.xexpose;
			if (xev.count == 0)
				XCopyArea(dpy, pix, win, ctx->copy_gc, 0, 0, img->width, img->height, 0, 0);
			continue;
		}
		if (!fullscreen_flag && e.type == ConfigureNotify) {
			XConfigureEvent xce = e.xconfigure;

			/* there is no file loaded and the window is resized */
			if (!current_link) {
				XResizeWindow(dpy, win, img->width, img->height);
				continue;
			}

			if (xce.width != img->width || xce.height != img->height) {
				RImage *old_img = img;
				img = load_oriented_image(ctx, current_link->data, 0);
				if (!img) {
					/* keep the old img and window size */
					img = old_img;
					XResizeWindow(dpy, win, img->width, img->height);
				} else {
					RImage *tmp2;
					if (!back_from_fullscreen) {
						/* manually resized window */
						tmp2 = RScaleImage(img, xce.width, xce.height);
					}
					else {
						/* back from fullscreen mode, maybe img was rotated */
						tmp2 = img;
						back_from_fullscreen = False;
						XClearWindow(dpy, win);
					}
					merge_with_background(tmp2);
					if (RConvertImage(ctx, tmp2, &pix)) {
						RReleaseImage(old_img);
						img = RCloneImage(tmp2);
						RReleaseImage(tmp2);
						change_title((char *)current_link->data);
						XSync(dpy, True);
						rescale_image();
						XResizeWindow(dpy, win, img->width, img->height);
						XCopyArea(dpy, pix, win, ctx->copy_gc, 0, 0,
							img->width, img->height, 0, 0);

					}
				}
			}
			continue;
		}
		if (fullscreen_flag && e.type == ConfigureNotify) {
			int new_width = e.xconfigure.width;
			int new_height = e.xconfigure.height;
			if (new_width != img->width || new_height != img->height)
				maximize_image();
			continue;
		}
		if (e.type == ButtonPress) {
			switch (e.xbutton.button) {
			case Button1: {
				/* Store button press for potential drag operation */
				button1_pressed = True;
				drag_start_x = e.xbutton.x;
				drag_start_y = e.xbutton.y;
			}
			break;
			case Button4:
				zoom_in();
				break;
			case Button5:
				zoom_out();
				break;
			case 8:
				change_image(PREV);
				break;
			case 9:
				change_image(NEXT);
				break;
			}
			continue;
		}
		if (e.type == ButtonRelease) {
			if (e.xbutton.button == Button1 && button1_pressed) {
				/* Button released without dragging - treat as click for image navigation */
				button1_pressed = False;
				if (focus) {
					if (img && (drag_start_x > img->width/2))
						change_image(NEXT);
					else
						change_image(PREV);
				}
			}
			continue;
		}
		if (e.type == MotionNotify && button1_pressed) {
			/* Check if we've moved enough to start a drag */
			int dx = e.xmotion.x - drag_start_x;
			int dy = e.xmotion.y - drag_start_y;
			if (abs(dx) > drag_threshold || abs(dy) > drag_threshold) {
				/* Start drag operation */
				button1_pressed = False;
				if (current_link && current_link->data) {
					initiate_file_drag();
				}
			}
			continue;
		}
		if (e.type == KeyPress) {
			keysym = W_KeycodeToKeysym(dpy, e.xkey.keycode, e.xkey.state & ShiftMask?1:0);
#ifdef HAVE_PTHREAD
			if (keysym != XK_Right)
				slideshow_flag = False;
#endif
			/* Detect Ctrl+C: state contains ControlMask */
			if ((e.xkey.state & ControlMask) && (keysym == XK_c || keysym == XK_C)) {
				copy_current_image_to_clipboard();
				continue;
			}
			if ((e.xkey.state & ControlMask) && (keysym == XK_v || keysym == XK_V)) {
				paste_clipboard_image();
				continue;
			}

			switch (keysym) {
			case XK_Right:
				change_image(NEXT);
				break;
			case XK_Left:
				change_image(PREV);
				break;
			case XK_Up:
				if (current_link) {
				current_link = list.last;
				change_image(NEXT);
				}
				break;
			case XK_Down:
				if (current_link) {
				current_link = list.first;
				change_image(PREV);
				}
				break;
#ifdef HAVE_PTHREAD
			case XK_F5:
			case XK_d:
				if (!tid) {
					if (current_link && !slideshow_flag) {
						slideshow_flag = True;
						pthread_create(&tid, NULL, &slideshow, NULL);
					} else {
						fprintf(stderr, "Error: can't start slideshow\n");
					}
				}
				break;
#endif
			case XK_q:
				quit = 1;
				break;
			case XK_Escape:
				if (!fullscreen_flag) {
					zoom_factor = -0.2f;
					zoom_in();
				} else {
					full_screen();
				}
				break;
			case XK_plus:
				zoom_in();
				break;
			case XK_minus:
				zoom_out();
				break;
			case XK_F11:
			case XK_f:
				full_screen();
				break;
			case XK_r:
				rotate_image_right();
				break;
			case XK_l:
				rotate_image_left();
				break;
			}

		}
	}

	if (img)
		RReleaseImage(img);
	if (pix)
		XFreePixmap(dpy, pix);
#ifdef USE_XPM
	if (icon_pixmap)
		XFreePixmap(dpy, icon_pixmap);
	if (icon_shape)
		XFreePixmap(dpy, icon_shape);
#endif
	linked_list_free(&list);
	RDestroyContext(ctx);
	RShutdown();

	free_clipboard_data();
	cleanup_temp_dir();

	XCloseDisplay(dpy);
	return EXIT_SUCCESS;
}
