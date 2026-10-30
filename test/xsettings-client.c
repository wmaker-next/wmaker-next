/*
 *  xsettings-client.c
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 2026 Window Maker Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/*
 * xsettings-client - independent XSETTINGS consumer for the drive test.
 * It re-implements the wire parse instead of linking the WM's codec, so
 * a pass means the property on the wire is right, not just self-consistent.
 *
 * usage: xsettings-client [--screen N] [--watch] [--serial]
 *
 * default: print "name<TAB>type<TAB>value" lines for every setting
 * published by the manager owning _XSETTINGS_S<n>, exit 1 when there is
 * no manager or the property cannot be read or parsed.
 * --watch: after the first dump, print "serial <n>" plus a fresh dump
 * whenever the settings property changes, until killed.
 * --serial: print the current serial number and exit.
 * --steal: claim the _XSETTINGS_S<n> selection on a private window,
 * forcing a SelectionClear on the current manager, and exit.
 * --owner: print the hex id of the selection owner window and exit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

static const char *atom_settings = "_XSETTINGS_SETTINGS";

static unsigned int rd16(const unsigned char *p, int be)
{
	if (be)
		return ((unsigned int)p[0] << 8) | p[1];
	return ((unsigned int)p[1] << 8) | p[0];
}

static unsigned int rd32(const unsigned char *p, int be)
{
	if (be)
		return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
		       ((unsigned int)p[2] << 8) | p[3];
	return ((unsigned int)p[3] << 24) | ((unsigned int)p[2] << 16) |
	       ((unsigned int)p[1] << 8) | p[0];
}

/* returns 0 on success, -1 on any wire-level problem */
static int parse_payload(const unsigned char *p, unsigned long len,
			 unsigned int *serial_out, int dump)
{
	int be;
	unsigned int count, i;
	unsigned long o;

	if (len < 12)
		return -1;
	/* the byte-order field carries the X protocol constants */
	if (p[0] == 0)
		be = 0;
	else if (p[0] == 1)
		be = 1;
	else
		return -1;

	*serial_out = rd32(p + 4, be);
	count = rd32(p + 8, be);
	if (count > 4096)
		return -1;
	if (dump)
		printf("serial\t%d\t%d\n", *serial_out, *serial_out);

	o = 12;
	for (i = 0; i < count; i++) {
		unsigned int type, nlen, vlen;
		const unsigned char *name;

		if (o + 4 > len)
			return -1;
		type = p[o];
		nlen = rd16(p + o + 2, be);
		o += 4;
		if (o + nlen > len)
			return -1;
		name = p + o;
		o += nlen;
		o += (4 - (o & 3)) & 3;
		if (o + 4 > len)
			return -1;
		if (rd32(p + o, be) > 0xffffff)
			return -1;	/* implausible last-change serial */
		o += 4;

		if (type == 0) {	/* integer */
			if (o + 4 > len)
				return -1;
			if (dump)
				printf("%.*s\tinteger\t%u\n",
				       (int)nlen, name, rd32(p + o, be));
			o += 4;
		} else if (type == 1) {	/* string */
			/* value-len on the wire is a CARD32 */
			if (o + 4 > len)
				return -1;
			vlen = rd32(p + o, be);
			o += 4;
			if (vlen > len - o)
				return -1;
			if (dump)
				printf("%.*s\tstring\t%.*s\n",
				       (int)nlen, name, (int)vlen, p + o);
			o += vlen;
			o += (4 - (o & 3)) & 3;
		} else if (type == 2) {	/* color */
			if (o + 8 > len)
				return -1;
			o += 8;
		} else {
			return -1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	Display *dpy;
	int screen = -1, watch = 0, dump = 1, serial_only = 0, steal = 0, owner_only = 0;
	char selname[32];
	Atom sel, prop, type;
	Window owner;
	int i, j;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--watch") == 0) {
			watch = 1;
		} else if (strcmp(argv[i], "--serial") == 0) {
			serial_only = 1;
		} else if (strcmp(argv[i], "--steal") == 0) {
			steal = 1;
		} else if (strcmp(argv[i], "--owner") == 0) {
			owner_only = 1;
		} else if (strcmp(argv[i], "--screen") == 0 && i + 1 < argc) {
			screen = atoi(argv[++i]);
		} else {
			fprintf(stderr,
				"usage: %s [--screen N] [--watch] [--serial] [--steal] [--owner]\n",
				argv[0]);
			return 2;
		}
	}

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "cannot open display\n");
		return 1;
	}
	if (screen < 0)
		screen = DefaultScreen(dpy);

	snprintf(selname, sizeof(selname), "_XSETTINGS_S%d", screen);
	sel = XInternAtom(dpy, selname, False);
	prop = XInternAtom(dpy, atom_settings, False);
	type = XInternAtom(dpy, atom_settings, False);

	if (owner_only) {
		Window w = XGetSelectionOwner(dpy, sel);

		if (w == None) {
			fprintf(stderr, "no owner\n");
			return 1;
		}
		printf("0x%lx\n", (unsigned long)w);
		return 0;
	}

	if (steal) {
		Window win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy),
						 0, 0, 4, 4, 0, 0, 0);

		XSetSelectionOwner(dpy, sel, win, CurrentTime);
		if (XGetSelectionOwner(dpy, sel) != win) {
			fprintf(stderr, "could not take %s\n", selname);
			return 1;
		}
		printf("stolen\n");
		fflush(stdout);
		return 0;
	}

	owner = XGetSelectionOwner(dpy, sel);
	if (owner == None) {
		fprintf(stderr, "no %s owner\n", selname);
		return 1;
	}

	for (j = 0;; j++) {
		unsigned char *data;
		Atom rtype;
		int rformat;
		unsigned long nitems, bytes;
		unsigned int serial;

		if (XGetWindowProperty(dpy, owner, prop, 0, 65536, False, type,
				       &rtype, &rformat, &nitems, &bytes, &data)
		    != Success) {
			fprintf(stderr, "GetProperty failed\n");
			return 1;
		}
		if (rtype != type || rformat != 8 || !data) {
			if (data)
				XFree(data);
			fprintf(stderr, "unexpected property type %lu format %d\n",
				(unsigned long)rtype, rformat);
			return 1;
		}
		serial = 0;
		if (parse_payload(data, nitems, &serial, dump && !serial_only) != 0) {
			XFree(data);
			fprintf(stderr, "malformed settings payload\n");
			return 1;
		}
		XFree(data);
		if (serial_only) {
			printf("%u\n", serial);
			fflush(stdout);
			return 0;
		}
		if (!watch)
			return 0;
		fflush(stdout);

		/* first iteration only: subscribe to republishes */
		if (j == 0)
			XSelectInput(dpy, owner, PropertyChangeMask);

		for (;;) {
			XEvent ev;

			XNextEvent(dpy, &ev);
			if (ev.type == PropertyNotify)
				break;
		}
	}
}
