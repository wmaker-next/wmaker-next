/* cursorhide-client.c - fake a pointer motion through XTest
 *
 *  Window Maker integration test helper
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

#include <stdlib.h>

#include <xcb/xcb.h>
#include <xcb/xtest.h>

int main(int argc, char **argv)
{
	xcb_connection_t *conn;
	xcb_screen_t *screen;
	int x, y;

	if (argc != 3)
		return 2;

	x = atoi(argv[1]);
	y = atoi(argv[2]);

	conn = xcb_connect(NULL, NULL);
	if (!conn || xcb_connection_has_error(conn))
		return 1;

	screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
	if (!screen)
		return 1;

	xcb_test_fake_input(conn, XCB_MOTION_NOTIFY, 0, XCB_CURRENT_TIME,
			    screen->root, x, y, 0);
	free(xcb_get_input_focus_reply(conn, xcb_get_input_focus(conn), NULL));
	xcb_disconnect(conn);

	return 0;
}
