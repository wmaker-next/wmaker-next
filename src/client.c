/*
 *  Window Maker window manager
 *
 *  Copyright (c) 1997-2003 Alfredo K. Kojima
 *  Copyright (c) 2009-2026 Window Maker Team
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
#include "wconfig.h"

#ifdef USE_XSHAPE
#include <xcb/shape.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <WINGs/WXProp.h>
#include <WINGs/WXHints.h>

#include "WindowMaker.h"
#include "framewin.h"
#include "window.h"
#include "properties.h"
#include "actions.h"
#include "icon.h"
#include "client.h"
#include "colormap.h"
#include "stacking.h"
#include "appicon.h"
#include "appmenu.h"
#include "wmspec.h"
#include "misc.h"


/*
 *--------------------------------------------------------------------
 * wClientRestore--
 * 	Reparent the window back to the root window.
 *
 *--------------------------------------------------------------------
 */
void wClientRestore(WWindow * wwin)
{
	xcb_configure_window(wdisp->conn, wwin->client_win, XCB_CONFIG_WINDOW_BORDER_WIDTH, (const uint32_t[]){ wwin->old_border_width });
	xcb_reparent_window(wdisp->conn, wwin->client_win, wwin->screen_ptr->root_win, wwin->frame_x, wwin->frame_y);

	/* don't let the window get iconified after restart */
	/*
	   if (wwin->flags.shaded)
	   wClientSetState(wwin, NormalState, None);
	 */
}

/*
 *----------------------------------------------------------------------
 * wClientSetState--
 * 	Set the state of the client window to one of the window
 * states defined in ICCCM (Iconic, Withdrawn, Normal)
 *
 * Side effects:
 * 	The WM_STATE property of the window is updated as well as the
 * WWindow.state variable.
 *----------------------------------------------------------------------
 */
void wClientSetState(WWindow * wwin, int state, xcb_window_t icon_win)
{
	uint32_t data[2];

	wwin->state = state;

	data[0] = (uint32_t)state;
	data[1] = (uint32_t)icon_win;

	WXSetFormat32N(wdisp->conn, wwin->client_win, wdisp->atom.wm.state,
		       wdisp->atom.wm.state, data, 2);
}

void wClientGetGravityOffsets(WWindow * wwin, int *ofs_x, int *ofs_y)
{
	/* internal windows have no size hints, behave like
	 * NorthWestGravity instead of crashing */
	if (!wwin->normal_hints) {
		*ofs_x = -1;
		*ofs_y = -1;
		return;
	}

	switch (wwin->normal_hints->win_gravity) {
	default:
	case XCB_GRAVITY_BIT_FORGET:
	case XCB_GRAVITY_CENTER:
	case XCB_GRAVITY_STATIC:
		*ofs_x = 0;
		*ofs_y = 0;
		break;
	case XCB_GRAVITY_NORTH_WEST:
		*ofs_x = -1;
		*ofs_y = -1;
		break;
	case XCB_GRAVITY_NORTH:
		*ofs_x = 0;
		*ofs_y = -1;
		break;
	case XCB_GRAVITY_NORTH_EAST:
		*ofs_x = 1;
		*ofs_y = -1;
		break;
	case XCB_GRAVITY_WEST:
		*ofs_x = -1;
		*ofs_y = 0;
		break;
	case XCB_GRAVITY_EAST:
		*ofs_x = 1;
		*ofs_y = 0;
		break;
	case XCB_GRAVITY_SOUTH_WEST:
		*ofs_x = -1;
		*ofs_y = 1;
		break;
	case XCB_GRAVITY_SOUTH:
		*ofs_x = 0;
		*ofs_y = 1;
		break;
	case XCB_GRAVITY_SOUTH_EAST:
		*ofs_x = 1;
		*ofs_y = 1;
		break;
	}
}

void wClientConfigure(WWindow *wwin, const xcb_configure_request_event_t *xcre)
{
	WXWindowChanges xwc;
	int nx, ny, nwidth, nheight;
	int ofs_x, ofs_y;
	uint32_t mask;

	if (wwin == NULL) {
		/*
		 * configure a window that was not mapped by us
		 */
		xwc.x = xcre->x;
		xwc.y = xcre->y;
		xwc.width = xcre->width;
		xwc.height = xcre->height;
		xwc.border_width = xcre->border_width;
		xwc.stack_mode = xcre->stack_mode;
		xwc.sibling = xcre->sibling;
		wXConfigureWindow(wdisp->conn, xcre->window, xcre->value_mask, &xwc);
		return;
	}
#ifdef USE_XSHAPE
	if (wdisp->xext.shape.supported) {
		xcb_shape_query_extents_reply_t *sr;

		xcb_shape_select_input(wdisp->conn, wwin->client_win, 1);
		sr = xcb_shape_query_extents_reply(wdisp->conn,
				xcb_shape_query_extents(wdisp->conn, wwin->client_win), NULL);
		wwin->flags.shaped = sr ? sr->bounding_shaped : 0;
		free(sr);
	}
#endif
	if (xcre->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) {
		WObjDescriptor *desc;
		WWindow *sibling;

		if ((xcre->value_mask & XCB_CONFIG_WINDOW_SIBLING) &&
		    (desc = WMHashGet(wdisp->context.client_win, (void *)(uintptr_t) xcre->sibling)) != NULL
		    && (desc->parent_type == WCLASS_WINDOW)) {
			sibling = desc->parent;
			xwc.sibling = sibling->frame->core->window;
		} else {
			xwc.sibling = xcre->sibling;
		}
		xwc.stack_mode = xcre->stack_mode;

		/* Apply a deferred raise/lower from this event batch first:
		 * the request below must stack against the order the user
		 * asked for, and the RemakeStackList() rebuild after the
		 * XConfigureWindow must not overwrite the still-unflushed
		 * internal mutation */
		if (wwin->screen_ptr->flags.need_restack)
			CommitStacking(wwin->screen_ptr);

		wXConfigureWindow(wdisp->conn, wwin->frame->core->window,
				  xcre->value_mask & (XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE), &xwc);
		/* fix stacking order */
		/* exemption: this is the client-requested configure relay,
		 * not a wm-initiated restack, RemakeStackList() below
		 * reconciles the list with what the server did */
		RemakeStackList(wwin->screen_ptr);
	}

	if (wwin->flags.tiled) {
		/* the layout owns the cell geometry, so reply with the
		 * real geometry instead of honoring the request */
		wWindowSynthConfigureNotify(wwin);
		return;
	}

	wClientGetGravityOffsets(wwin, &ofs_x, &ofs_y);

	if (xcre->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) {
		wwin->old_border_width = xcre->border_width;
	}

	if (!wwin->flags.shaded) {
		/* locked against the user is locked against the client:
		 * drop the geometry presence bits, before nx/ny so the
		 * early return and jitter-suppression see the same mask */
		mask = xcre->value_mask;
		if (!IS_MOVABLE(wwin))
			mask &= ~(XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y);
		if (!IS_RESIZABLE(wwin))
			mask &= ~(XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT);

		/* If the window is shaded, wrong height will be set for the window */
		if (mask & XCB_CONFIG_WINDOW_X) {
			nx = xcre->x;
			/* Subtracting the border makes the window shift by 1 pixel -Dan */
			/*if (HAS_BORDER(wwin)) {
			   nx -= wwin->screen_ptr->frame_border_width;
			   } */
		} else {
			nx = wwin->frame_x;
		}

		if (mask & XCB_CONFIG_WINDOW_Y) {
			ny = xcre->y - ((ofs_y < 0) ? 0 : wwin->frame->top_width);
			/* Subtracting the border makes the window shift by 1 pixel -Dan */
			/*if (HAS_BORDER(wwin)) {
			   ny -= wwin->screen_ptr->frame_border_width;
			   } */
		} else {
			ny = wwin->frame_y;
		}

		if (mask & XCB_CONFIG_WINDOW_WIDTH)
			nwidth = xcre->width;
		else
			nwidth = wwin->frame->core->width;

		if (mask & XCB_CONFIG_WINDOW_HEIGHT)
			nheight = xcre->height;
		else
			nheight = wwin->frame->core->height - wwin->frame->top_width - wwin->frame->bottom_width;

		/* Don't overwrite the saved geometry unnecessarily. */
		if (!(mask & (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT)))
			return;

		if (nwidth != wwin->old_geometry.width)
			wwin->flags.maximized &= ~(MAX_HORIZONTAL | MAX_TOPHALF | MAX_BOTTOMHALF | MAX_MAXIMUS | MAX_CENTRAL);
		if (nheight != wwin->old_geometry.height)
			wwin->flags.maximized &= ~(MAX_VERTICAL | MAX_LEFTHALF | MAX_RIGHTHALF | MAX_MAXIMUS | MAX_CENTRAL);

		wWindowConstrainSize(wwin, (unsigned int *)&nwidth, (unsigned int *)&nheight);

		/* Ignore tiny position adjustments from client ConfigureRequests to prevent
		 * window jitters (e.g., VirtualBox sending incremental 1-2px corrections). */
		if (wwin->flags.mapped && !wwin->moveresize.active && (mask & (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y))) {
			int dx = abs(nx - wwin->frame_x);
			int dy = abs(ny - wwin->frame_y);

			if (dx < 3 && dy < 3) {
				/* Keep current position */
				nx = wwin->frame_x;
				ny = wwin->frame_y;
			}
		}

		wWindowConfigure(wwin, nx, ny, nwidth, nheight);
		wwin->old_geometry.x = nx;
		wwin->old_geometry.y = ny;
		wwin->old_geometry.width = nwidth;
		wwin->old_geometry.height = nheight;
	}
}

void wClientSendProtocol(WWindow * wwin, xcb_atom_t protocol, xcb_timestamp_t time)
{
	WXSendClientMessage(wdisp->conn, wwin->client_win, XCB_EVENT_MASK_NO_EVENT,
			    wwin->client_win, wdisp->atom.wm.protocols,
			    (long)protocol, (long)time, 0, 0, 0);
	wXFlush(wdisp->conn);
}

void wClientKill(WWindow * wwin)
{
	xcb_kill_client(wdisp->conn, wwin->client_win);

	wXFlush(wdisp->conn);
}

/*
 *----------------------------------------------------------------------
 * wClientCheckProperty--
 * 	Handles PropertyNotify'es, verifying which property was
 * changed and updating internal state according to that, like redrawing
 * the icon title when it is changed.
 *
 * Side effects:
 * 	Depends on the changed property.
 *----------------------------------------------------------------------
 */
void wClientCheckProperty(WWindow *wwin, xcb_atom_t atom)
{
	WXWindowAttributes attribs;
	WXWMHints *new_hints;
	int i, g1, g2;
	char *tmp = NULL;

	switch (atom) {
	case XCB_ATOM_WM_NAME:
		if (!wwin->flags.net_has_title) {
			/* window title was changed */
			if (!wFetchName(wdisp->conn, wwin->client_win, &tmp)) {
				wWindowUpdateName(wwin, NULL);
			} else {
				wWindowUpdateName(wwin, tmp);
			}
			if (tmp)
				wfree(tmp);
		}
		break;

	case XCB_ATOM_WM_ICON_NAME:
		/* Title has changed, update the icon title */
		if (wwin->icon) {
			wIconChangeTitle(wwin->icon, wwin);
			wIconPaint(wwin->icon);
		}
		break;

	case XCB_ATOM_WM_COMMAND:
		if (wwin->wm_command)
			wfree(wwin->wm_command);
		wwin->wm_command = GetCommandForWindow(wwin->client_win);
		if (wwin->main_window != XCB_NONE) {
			WApplication *wapp = wApplicationOf(wwin->main_window);
			char *command;

			if (!wapp || !wapp->app_icon || wapp->app_icon->docked)
				break;

			command = GetCommandForWindow(wwin->main_window);
			if (command) {
				if (wapp->app_icon->command)
					wfree(wapp->app_icon->command);
				wapp->app_icon->command = command;
			}
		}
		break;

	case XCB_ATOM_WM_HINTS:
		/* WM_HINTS */

		new_hints = WXGetWMHints(wdisp->conn, wwin->client_win);

		/* group leader update
		 *
		 * This means that the window is setting the leader after
		 * it was mapped, changing leaders or removing the leader.
		 *
		 * Valid state transitions are:
		 *
		 *          _1            __2
		 *         / \           /  \
		 *         v |           v  |
		 *         (GC)         (GC')
		 *        /  ^          /   ^
		 *       3|  |4        5|   |6
		 *        |  |          |   |
		 *        v  /          v   /
		 *        (G'C)        (G'C')
		 *
		 * Where G is the window_group hint, C is CLIENT_LEADER property
		 * and ' indicates the hint is unset.
		 *
		 * 1,2 - change group leader to new value of window_group
		 * 3 - change leader to value of CLIENT_LEADER
		 * 4 - change leader to value of window_group
		 * 5 - destroy application
		 * 6 - create application
		 */
		if (new_hints && (new_hints->flags & XCB_ICCCM_WM_HINT_WINDOW_GROUP)
		    && new_hints->window_group != XCB_NONE) {
			g2 = 1;
		} else {
			g2 = 0;
		}
		if (wwin->wm_hints && (wwin->wm_hints->flags & XCB_ICCCM_WM_HINT_WINDOW_GROUP)
		    && wwin->wm_hints->window_group != XCB_NONE) {
			g1 = 1;
		} else {
			g1 = 0;
		}

		if (wwin->client_leader) {
			if (g1 && g2 && wwin->wm_hints->window_group != new_hints->window_group) {
				i = 1;
			} else if (g1 && !g2) {
				i = 3;
			} else if (!g1 && g2) {
				i = 4;
			} else {
				i = 0;
			}
		} else {
			if (g1 && g2 && wwin->wm_hints->window_group != new_hints->window_group) {
				i = 2;
			} else if (g1 && !g2) {
				i = 5;
			} else if (!g1 && g2) {
				i = 6;
			} else {
				i = 0;
			}
		}

		/* Handling this may require more work. -Dan */
		if (wwin->fake_group != NULL) {
			i = 7;
		}

		if (wwin->wm_hints)
			wfree(wwin->wm_hints);

		wwin->wm_hints = new_hints;

		/* drop the decoded icon_pixmap cache so the wIconUpdate
		 * and titlebar refresh below re-read the new pixmap */
		if (wwin->wm_hints_icon_image) {
			RReleaseImage(wwin->wm_hints_icon_image);
			wwin->wm_hints_icon_image = NULL;
		}

		if (wwin->frame)
			wWindowRefreshTitlebarIcon(wwin);

		/* do action according to state transition */
		switch (i) {
			/* 3 - change leader to value of CLIENT_LEADER */
		case 3:
			wApplicationDestroy(wApplicationOf(wwin->main_window));
			wwin->main_window = wwin->client_leader;
			wwin->group_id = XCB_NONE;
			wApplicationCreate(wwin);
			break;

			/* 1,2,4 - change leader to new value of window_group */
		case 1:
		case 2:
		case 4:
			wApplicationDestroy(wApplicationOf(wwin->main_window));
			wwin->main_window = new_hints->window_group;
			wwin->group_id = wwin->main_window;
			wApplicationCreate(wwin);
			break;

			/* 5 - destroy application */
		case 5:
			wApplicationDestroy(wApplicationOf(wwin->main_window));
			wwin->main_window = XCB_NONE;
			wwin->group_id = XCB_NONE;
			break;

			/* 6 - create application */
		case 6:
			wwin->main_window = new_hints->window_group;
			wwin->group_id = wwin->main_window;
			wApplicationCreate(wwin);
			break;
			/* 7 - we have a fake window group id, so just ignore anything else */
		case 7:
			break;
		}

		if (wwin->wm_hints) {
			/* update icon */
			if ((wwin->wm_hints->flags & XCB_ICCCM_WM_HINT_ICON_PIXMAP)
			    || (wwin->wm_hints->flags & XCB_ICCCM_WM_HINT_ICON_WINDOW)) {
				if (!WFLAGP(wwin, always_user_icon)) {
					WApplication *wapp;
					if (wwin->flags.miniaturized && wwin->icon)
						wIconUpdate(wwin->icon);

					wapp = wApplicationOf(wwin->main_window);
					if (wapp && wapp->app_icon) {
						wIconUpdate(wapp->app_icon->icon);
						wAppIconPaint(wapp->app_icon);
					}
				}
			}

			if (wwin->wm_hints->flags & XCB_ICCCM_WM_HINT_X_URGENCY)
				wwin->flags.urgent = 1;
			else
				wwin->flags.urgent = 0;
			wAppBounceWhileUrgent(wApplicationOf(wwin->main_window));
			wWindowUrgentBlink(wwin);
		/*} else if (wwin->fake_group!=NULL) {
			wwin->group_id = wwin->fake_group->leader; */
		} else {
			wwin->group_id = XCB_NONE;
		}
		break;

	case XCB_ATOM_WM_NORMAL_HINTS:
		/* normal (geometry) hints */
		{
			int foo;
			unsigned bar;

			if (wXGetWindowAttributes(wdisp->conn, wwin->client_win, &attribs, NULL))
				wClientGetNormalHints(wwin, &attribs, false,
						      WXReqNormalHints(wdisp->conn, wwin->client_win),
						      &foo, &foo, &bar, &bar);
			/* TODO: should we check for consistency of the current
			 * size against the new geometry hints? */
		}
		break;

	case XCB_ATOM_WM_TRANSIENT_FOR:
		{
			xcb_window_t new_owner;
			WWindow *owner;

			if (!WXGetTransientFor(wdisp->conn, wwin->client_win, &new_owner)) {
				new_owner = XCB_NONE;
			} else {
				if (new_owner == 0 || new_owner == wwin->client_win) {
					new_owner = wwin->screen_ptr->root_win;
				}
			}
			if (new_owner != wwin->transient_for) {
				owner = wWindowFor(wwin->transient_for);
				if (owner) {
					if (owner->flags.semi_focused) {
						owner->flags.semi_focused = 0;
						if ((owner->flags.mapped || owner->flags.shaded)
						    && owner->frame)
							wFrameWindowPaint(owner->frame);
					}
				}
				owner = wWindowFor(new_owner);
				if (owner) {
					if (!owner->flags.semi_focused) {
						owner->flags.semi_focused = 1;
						if ((owner->flags.mapped || owner->flags.shaded)
						    && owner->frame)
							wFrameWindowPaint(owner->frame);
					}
				}
				wwin->transient_for = new_owner;
				if (new_owner == XCB_NONE) {
					if (WFLAGP(wwin, no_miniaturizable)) {
						wwin->client_flags.no_miniaturizable = 0;
						wwin->client_flags.no_miniaturize_button = 0;
						if (wwin->frame)
							wWindowConfigureBorders(wwin);
					}
				} else if (!WFLAGP(wwin, no_miniaturizable)) {
					wwin->client_flags.no_miniaturizable = 1;
					wwin->client_flags.no_miniaturize_button = 1;
					if (wwin->frame)
						wWindowConfigureBorders(wwin);
				}
			}
		}
		break;

	default:
		if (atom == wdisp->atom.wm.protocols) {

			PropGetProtocols(wwin->client_win, &wwin->protocols);

			wwin->client_flags.kill_close = !wwin->protocols.DELETE_WINDOW;

			if (wwin->frame)
				wWindowUpdateButtonImages(wwin);

		} else if (atom == wdisp->atom.wm.colormap_windows) {

			GetColormapWindows(wwin);
			wColormapInstallForWindow(wwin->screen_ptr, wwin);

		} else if (atom == wdisp->atom.wmaker.menu) {
			WApplication *wapp;

			wapp = wApplicationOf(wwin->main_window);
			if (wapp) {
				if (wapp->menu) {
					/* update menu */
					/* TODO: remake appmenu update */
					wAppMenuDestroy(wapp->menu);
				}
				if (wwin->fake_group) {
					WScreen *scr = wwin->screen_ptr;
					WWindow *foo = scr->focused_window;
					WFakeGroupLeader *fPtr = wwin->fake_group;

					wApplicationDestroy(wapp);
					while (foo) {
						if (foo->fake_group && foo->fake_group == fPtr) {
							foo->client_flags.shared_appicon = 0;
							foo->fake_group = NULL;

							if (foo->group_id != XCB_NONE)
								foo->main_window = foo->group_id;
							else if (foo->client_leader != XCB_NONE)
								foo->main_window = foo->client_leader;
							else if (WFLAGP(foo, emulate_appicon))
								foo->main_window = foo->client_win;
							else
								foo->main_window = XCB_NONE;

							if (foo->main_window)
								wApplicationCreate(foo);
						}
						foo = foo->prev;
					}

					if (fPtr->leader != XCB_NONE)
						xcb_destroy_window(wdisp->conn, fPtr->leader);
					fPtr->retainCount = 0;
					fPtr->leader = XCB_NONE;
					fPtr->origLeader = XCB_NONE;

					wapp = wApplicationOf(wwin->main_window);
					if (wapp) {
						wapp->menu = wAppMenuGet(scr, wwin->main_window);
					}
					if (w_global.config->auto_arrange_icons) {
						wArrangeIcons(wwin->screen_ptr, true);
					}
				} else {
					wapp->menu = wAppMenuGet(wwin->screen_ptr, wwin->main_window);
				}
				/* make the appmenu be mapped */
				wSetFocusTo(wwin->screen_ptr, NULL);
				wSetFocusTo(wwin->screen_ptr, wwin->screen_ptr->focused_window);
			}
		} else if (atom == wdisp->atom.gnustep.wm_attr) {
			GNUstepWMAttributes *attr;

			if (PropGetGNUstepWMAttr(wwin->client_win, &attr)) {
				wWindowUpdateGNUstepAttr(wwin, attr);
				wfree(attr);
			}
		} else {
			wNETWMCheckClientHintChange(wwin, atom);
		}
	}
}

/*
 *----------------------------------------------------------------------
 * wClientGetNormalHints--
 * 	Get size (normal) hints and a default geometry for the client
 * window. The hints are also checked for inconsistency. If geometry is
 * True, the returned data will account for client specified initial
 * geometry.
 *
 * Side effects:
 * 	normal_hints is filled with valid data.
 *----------------------------------------------------------------------
 */
void
wClientGetNormalHints(WWindow * wwin, WXWindowAttributes * wattribs, bool geometry,
		      WXCookie normal_ck,
		      int *x, int *y, unsigned *width, unsigned *height)
{
	int pre_icccm = 0;	/* not used */

	/* find a position for the window */
	if (!wwin->normal_hints)
		wwin->normal_hints = WXAllocSizeHints();

	if (!PropRcvNormalHints(normal_ck, wwin->normal_hints, &pre_icccm)) {
		wwin->normal_hints->flags = 0;
	}
	*x = wattribs->x;
	*y = wattribs->y;

	*width = wattribs->width;
	*height = wattribs->height;

	if (!(wwin->normal_hints->flags & XCB_ICCCM_SIZE_HINT_P_WIN_GRAVITY)) {
		wwin->normal_hints->win_gravity = XCB_GRAVITY_NORTH_WEST;
	}
	if (!(wwin->normal_hints->flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE)) {
		wwin->normal_hints->min_width = MIN_WINDOW_SIZE;
		wwin->normal_hints->min_height = MIN_WINDOW_SIZE;
	}
	if (!(wwin->normal_hints->flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE)) {
		wwin->normal_hints->base_width = 0;
		wwin->normal_hints->base_height = 0;
	}
	if (!(wwin->normal_hints->flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE)) {
		wwin->normal_hints->max_width = wwin->screen_ptr->scr_width * 2;
		wwin->normal_hints->max_height = wwin->screen_ptr->scr_height * 2;
	}

	/* some buggy apps set weird hints.. */
	if (wwin->normal_hints->min_width <= 0)
		wwin->normal_hints->min_width = MIN_WINDOW_SIZE;

	if (wwin->normal_hints->min_height <= 0)
		wwin->normal_hints->min_height = MIN_WINDOW_SIZE;

	if (wwin->normal_hints->max_width < wwin->normal_hints->min_width)
		wwin->normal_hints->max_width = wwin->normal_hints->min_width;

	if (wwin->normal_hints->max_height < wwin->normal_hints->min_height)
		wwin->normal_hints->max_height = wwin->normal_hints->min_height;

	if (!(wwin->normal_hints->flags & XCB_ICCCM_SIZE_HINT_P_RESIZE_INC)) {
		wwin->normal_hints->width_inc = 1;
		wwin->normal_hints->height_inc = 1;
	} else {
		if (wwin->normal_hints->width_inc <= 0)
			wwin->normal_hints->width_inc = 1;
		if (wwin->normal_hints->height_inc <= 0)
			wwin->normal_hints->height_inc = 1;
	}

	if (wwin->normal_hints->flags & XCB_ICCCM_SIZE_HINT_P_ASPECT) {
		if (wwin->normal_hints->min_aspect_num < 1)
			wwin->normal_hints->min_aspect_num = 1;
		if (wwin->normal_hints->min_aspect_den < 1)
			wwin->normal_hints->min_aspect_den = 1;

		if (wwin->normal_hints->max_aspect_num < 1)
			wwin->normal_hints->max_aspect_num = 1;
		if (wwin->normal_hints->max_aspect_den < 1)
			wwin->normal_hints->max_aspect_den = 1;
	}

	if (wwin->normal_hints->min_height > wwin->normal_hints->max_height) {
		wwin->normal_hints->min_height = wwin->normal_hints->max_height;
	}
	if (wwin->normal_hints->min_width > wwin->normal_hints->max_width) {
		wwin->normal_hints->min_width = wwin->normal_hints->max_width;
	}
#ifdef IGNORE_PPOSITION
	wwin->normal_hints->flags &= ~XCB_ICCCM_SIZE_HINT_P_POSITION;
#endif

	if (pre_icccm && !wwin->screen_ptr->flags.startup && geometry) {
		if (wwin->normal_hints->flags & (XCB_ICCCM_SIZE_HINT_US_POSITION | XCB_ICCCM_SIZE_HINT_P_POSITION)) {
			*x = wwin->normal_hints->x;
			*y = wwin->normal_hints->y;
		}
		if (wwin->normal_hints->flags & (XCB_ICCCM_SIZE_HINT_US_SIZE | XCB_ICCCM_SIZE_HINT_P_SIZE)) {
			*width = wwin->normal_hints->width;
			*height = wwin->normal_hints->height;
		}
	}
}

WXCookie wClientReqColormapWindows(xcb_window_t window)
{
	/* Xlib's XGetWMColormapWindows() interns WM_COLORMAP_WINDOWS on
	 * every call, the atom is in the cold-boot batch so read the
	 * property directly and let wManagePrefetch() pipeline it */
	return WXReqFormat32(wdisp->conn, window, wdisp->atom.wm.colormap_windows,
			     XCB_ATOM_WINDOW, 64);
}

void wClientRcvColormapWindows(WWindow *wwin, WXCookie ck)
{
	size_t count = 0;

	if (wwin->cmap_windows)
		wfree(wwin->cmap_windows);

	wwin->cmap_windows = WXRcvWindowList(wdisp->conn, ck, &count);
	wwin->cmap_window_no = wwin->cmap_windows ? (int)count : 0;
}

void GetColormapWindows(WWindow *wwin)
{
	wClientRcvColormapWindows(wwin, wClientReqColormapWindows(wwin->client_win));
}
