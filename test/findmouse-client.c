/* findmouse-client.c - print the FindMouse overlay state */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/shape.h>

static int print_window(xcb_connection_t *c, xcb_window_t win)
{
	xcb_get_window_attributes_cookie_t ac;
	xcb_get_window_attributes_reply_t *ar;
	xcb_get_geometry_cookie_t gc;
	xcb_get_geometry_reply_t *gr;
	int bounding = -1;
	int input = -1;
	const xcb_query_extension_reply_t *shape;

	ac = xcb_get_window_attributes(c, win);
	ar = xcb_get_window_attributes_reply(c, ac, NULL);
	if (!ar)
		return 1;

	gc = xcb_get_geometry(c, win);
	gr = xcb_get_geometry_reply(c, gc, NULL);
	if (!gr) {
		free(ar);
		return 1;
	}

	shape = xcb_get_extension_data(c, &xcb_shape_id);
	if (shape && shape->present) {
		xcb_shape_get_rectangles_reply_t *reply;

		reply = xcb_shape_get_rectangles_reply(c,
			xcb_shape_get_rectangles(c, win, XCB_SHAPE_SK_BOUNDING),
			NULL);
		if (reply) {
			bounding = xcb_shape_get_rectangles_rectangles_length(reply);
			free(reply);
		}
		reply = xcb_shape_get_rectangles_reply(c,
			xcb_shape_get_rectangles(c, win, XCB_SHAPE_SK_INPUT),
			NULL);
		if (reply) {
			input = xcb_shape_get_rectangles_rectangles_length(reply);
			free(reply);
		}
	}

	printf("found=1 override_redirect=%d map_state=%d x=%d y=%d "
	       "width=%u height=%u bounding_rects=%d input_rects=%d\n",
	       ar->override_redirect, ar->map_state,
	       gr->x, gr->y, gr->width, gr->height, bounding, input);

	free(ar);
	free(gr);
	return 0;
}

int main(void)
{
	xcb_connection_t *c;
	xcb_screen_t *screen;
	xcb_intern_atom_cookie_t atom_ck;
	xcb_intern_atom_reply_t *atom_reply;
	xcb_atom_t class_atom;
	xcb_query_tree_cookie_t tree_ck;
	xcb_query_tree_reply_t *tree;
	xcb_window_t *children;
	int count;
	int i;
	int found = 0;

	c = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(c)) {
		fprintf(stderr, "findmouse-client: cannot connect to the X server\n");
		xcb_disconnect(c);
		return 1;
	}

	screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
	atom_ck = xcb_intern_atom(c, 0, 8, "WM_CLASS");
	atom_reply = xcb_intern_atom_reply(c, atom_ck, NULL);
	if (!atom_reply) {
		xcb_disconnect(c);
		return 1;
	}
	class_atom = atom_reply->atom;
	free(atom_reply);

	tree_ck = xcb_query_tree(c, screen->root);
	tree = xcb_query_tree_reply(c, tree_ck, NULL);
	if (!tree) {
		xcb_disconnect(c);
		return 1;
	}

	children = xcb_query_tree_children(tree);
	count = xcb_query_tree_children_length(tree);
	for (i = 0; i < count; i++) {
		xcb_get_property_cookie_t pck;
		xcb_get_property_reply_t *prep;
		int length;
		int ok = 0;

		pck = xcb_get_property(c, 0, children[i], class_atom,
				       XCB_ATOM_STRING, 0, 64);
		prep = xcb_get_property_reply(c, pck, NULL);
		if (prep) {
			length = xcb_get_property_value_length(prep);
			if (prep->format == 8 && length > 0) {
				const char *data = xcb_get_property_value(prep);

				ok = strcmp(data, "findmouse") == 0;
			}
			free(prep);
		}
		if (ok) {
			found = 1;
			print_window(c, children[i]);
			break;
		}
	}

	free(tree);
	xcb_disconnect(c);

	if (!found)
		printf("found=0 override_redirect=0 map_state=0 x=0 y=0 "
		       "width=0 height=0 bounding_rects=-1 input_rects=-1\n");

	return 0;
}
