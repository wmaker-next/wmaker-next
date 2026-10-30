/*
 *  wpanel.c
 *
 *  Window Maker WINGs library
 *
 *  Copyright (c) 1998 scottc
 *  Copyright (c) 1999-2004 Dan Pascu
 *  Copyright (c) 1999-2001 Alfredo K. Kojima
 *  Copyright (c) 2008-2026 Window Maker Team
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

#include "WINGsP.h"

#include <xkbcommon/xkbcommon-keysyms.h>
#include <stdint.h>

static void alertPanelOnClick(WMWidget * self, void *clientData)
{
	WMAlertPanel *panel = clientData;

	WMBreakModalLoop(WMWidgetScreen(self));
	if (self == panel->defBtn) {
		panel->result = WAPRDefault;
	} else if (self == panel->othBtn) {
		panel->result = WAPROther;
	} else if (self == panel->altBtn) {
		panel->result = WAPRAlternate;
	}
}

static void handleKeyPress(xcb_generic_event_t * event, void *clientData)
{
	WMAlertPanel *panel = (WMAlertPanel *) clientData;
	xkb_keysym_t ksym;

	ksym = W_EventKeysym(WMWidgetScreen(panel->win),
			     (const xcb_key_press_event_t *)event);

	if ((ksym == XKB_KEY_Return || ksym == XKB_KEY_KP_Enter) && panel->defBtn) {
		WMPerformButtonClick(panel->defBtn);
	} else if (ksym == XKB_KEY_Escape) {
		if (panel->altBtn || panel->othBtn) {
			WMPerformButtonClick(panel->othBtn ? panel->othBtn : panel->altBtn);
		} else {
			panel->result = WAPRDefault;
			WMBreakModalLoop(WMWidgetScreen(panel->win));
		}
	}
}

int
WMRunAlertPanel(WMScreen * scrPtr, WMWindow * owner,
		const char *title, const char *msg, const char *defaultButton, const char *alternateButton, const char *otherButton)
{
	WMAlertPanel *panel;
	WMView *view;
	int px, py;
	int tmp;

	panel = WMCreateAlertPanel(scrPtr, owner, title, msg, defaultButton, alternateButton, otherButton);

	view = WMWidgetView(panel->win);
	if (owner) {
		WMView *oview = WMWidgetView(owner);
		WMPoint pt = WMGetViewScreenPosition(oview);

		px = (W_VIEW_WIDTH(oview) - W_VIEW_WIDTH(view)) / 2;
		py = (W_VIEW_HEIGHT(oview) - W_VIEW_HEIGHT(view)) / 2;

		px += pt.x;
		py += pt.y;
	} else {
		px = (W_VIEW_WIDTH(scrPtr->rootView) - W_VIEW_WIDTH(view)) / 2;
		py = (W_VIEW_HEIGHT(scrPtr->rootView) - W_VIEW_HEIGHT(view)) / 2;
	}
	WMSetWindowInitialPosition(panel->win, px, py);

	WMMapWidget(panel->win);

	WMRunModalLoop(scrPtr, W_VIEW(panel->win));

	tmp = panel->result;

	WMDestroyAlertPanel(panel);

	return tmp;
}

void WMDestroyAlertPanel(WMAlertPanel * panel)
{
	WMUnmapWidget(panel->win);
	WMDestroyWidget(panel->win);
	wfree(panel);
}

WMAlertPanel *WMCreateAlertPanel(WMScreen * scrPtr, WMWindow * owner,
				 const char *title, const char *msg, const char *defaultButton,
				 const char *alternateButton, const char *otherButton)
{
	WMAlertPanel *panel;
	WMFont *defaultFont;
	int dw = 0, aw = 0, ow = 0, w;
	WMBox *hbox;
	WMBox *mbox;
	WMPixmap *icon;
	int pwidth, pheight;
	int msgw, msgh;

	defaultFont = WMSystemFontOfSize(scrPtr, 12);
	panel = wmalloc(sizeof(WMAlertPanel));

	if (owner) {
		panel->win = WMCreatePanelWithStyleForWindow(owner, "alertPanel", WMTitledWindowMask);
	} else {
		panel->win = WMCreateWindowWithStyle(scrPtr, "alertPanel", WMTitledWindowMask);
	}

	/* calculate and set the panel's size */
	pwidth = 400;

	/* widen so the non-wrapping title fits beside the icon */
	if (title) {
		WMFont *largeFont = WMBoldSystemFontOfSize(scrPtr, 24);

		if (largeFont) {
			w = WMWidthOfString(largeFont, title, strlen(title))
			    + 64			/* icon */
			    + 10			/* gap after icon */
			    + 2 * 5			/* row borders */
			    + 8;			/* label paint inset */
			if (w > pwidth)
				pwidth = w;
			WMReleaseFont(largeFont);
		}
	}
	if (pwidth > scrPtr->rootView->size.width - 40)
		pwidth = scrPtr->rootView->size.width - 40;

	pheight = 64 + 2 * 5		/* icon row */
	    + 5 + 2 + 5;		/* divider line */
	msgw = pwidth - 2 * 12 - 8;
	msgh = 0;
	if (msg) {
		msgh = W_GetTextHeight(defaultFont, msg, msgw, true);
		if (msgh < WMFontHeight(defaultFont) * 2)
			msgh = WMFontHeight(defaultFont) * 2;
		pheight += msgh + 2 * 12 + 5;
	}
	pheight += 44;			/* button row */

	if (pheight > scrPtr->rootView->size.height - 40)
		pheight = scrPtr->rootView->size.height - 40;

	WMResizeWidget(panel->win, pwidth, pheight);

	WMSetWindowInitialPosition(panel->win,
				   (scrPtr->rootView->size.width - pwidth) / 2,
				   (scrPtr->rootView->size.height - pheight) / 2);

	WMSetWindowTitle(panel->win, "");

	panel->vbox = WMCreateBox(panel->win);
	WMSetViewExpandsToParent(WMWidgetView(panel->vbox), 0, 0, 0, 0);
	WMSetBoxHorizontal(panel->vbox, false);
	WMMapWidget(panel->vbox);

	hbox = WMCreateBox(panel->vbox);
	WMSetBoxBorderWidth(hbox, 5);
	WMSetBoxHorizontal(hbox, true);
	WMMapWidget(hbox);
	WMAddBoxSubview(panel->vbox, WMWidgetView(hbox), false, true, 74, 0, 5);

	panel->iLbl = WMCreateLabel(hbox);
	WMSetLabelImagePosition(panel->iLbl, WIPImageOnly);
	WMMapWidget(panel->iLbl);
	WMAddBoxSubview(hbox, WMWidgetView(panel->iLbl), false, true, 64, 0, 10);
	icon = WMCreateApplicationIconBlendedPixmap(scrPtr, (RColor *) NULL);
	if (icon) {
		WMSetLabelImage(panel->iLbl, icon);
		WMReleasePixmap(icon);
	} else {
		WMSetLabelImage(panel->iLbl, scrPtr->applicationIconPixmap);
	}

	if (title) {
		WMFont *largeFont;

		largeFont = WMBoldSystemFontOfSize(scrPtr, 24);

		panel->tLbl = WMCreateLabel(hbox);
		WMMapWidget(panel->tLbl);
		WMAddBoxSubview(hbox, WMWidgetView(panel->tLbl), true, true, 64, 0, 0);
		WMSetLabelText(panel->tLbl, title);
		WMSetLabelTextAlignment(panel->tLbl, WALeft);
		WMSetLabelFont(panel->tLbl, largeFont);

		WMReleaseFont(largeFont);
	}

	/* create divider line */

	panel->line = WMCreateFrame(panel->win);
	WMMapWidget(panel->line);
	WMAddBoxSubview(panel->vbox, WMWidgetView(panel->line), false, true, 2, 2, 5);
	WMSetFrameRelief(panel->line, WRGroove);

	if (msg) {
		/* bordered row so the text keeps a gap from the frame */
		mbox = WMCreateBox(panel->vbox);
		WMSetBoxBorderWidth(mbox, 12);
		WMSetBoxHorizontal(mbox, true);
		WMMapWidget(mbox);
		WMAddBoxSubview(panel->vbox, WMWidgetView(mbox), true, true,
				msgh + 24, 0, 5);

		panel->mLbl = WMCreateLabel(mbox);
		WMSetLabelWraps(panel->mLbl, true);
		WMMapWidget(panel->mLbl);
		WMAddBoxSubview(mbox, WMWidgetView(panel->mLbl), false, true,
				pwidth - 24, 0, 0);
		WMSetLabelText(panel->mLbl, msg);
		WMSetLabelTextAlignment(panel->mLbl, WACenter);
		WMSetLabelFont(panel->mLbl, defaultFont);
	}

	panel->hbox = WMCreateBox(panel->vbox);
	WMSetBoxBorderWidth(panel->hbox, 10);
	WMSetBoxHorizontal(panel->hbox, true);
	WMMapWidget(panel->hbox);
	WMAddBoxSubview(panel->vbox, WMWidgetView(panel->hbox), false, true, 44, 0, 0);

	/* create buttons */
	if (otherButton)
		ow = WMWidthOfString(defaultFont, otherButton, strlen(otherButton));

	if (alternateButton)
		aw = WMWidthOfString(defaultFont, alternateButton, strlen(alternateButton));

	if (defaultButton)
		dw = WMWidthOfString(defaultFont, defaultButton, strlen(defaultButton));

	dw = dw + (scrPtr->buttonArrow ? scrPtr->buttonArrow->width : 0);

	aw += 30;
	ow += 30;
	dw += 30;

	w = WMAX(dw, WMAX(aw, ow));
	if ((w + 10) * 3 < 400) {
		aw = w;
		ow = w;
		dw = w;
	} else {
		int t;

		t = 400 - 40 - aw - ow - dw;
		aw += t / 3;
		ow += t / 3;
		dw += t / 3;
	}

	if (defaultButton) {
		panel->defBtn = WMCreateCommandButton(panel->hbox);
		WMSetButtonAction(panel->defBtn, alertPanelOnClick, panel);
		WMAddBoxSubviewAtEnd(panel->hbox, WMWidgetView(panel->defBtn), false, true, dw, 0, 0);
		WMSetButtonText(panel->defBtn, defaultButton);
		WMSetButtonImage(panel->defBtn, scrPtr->buttonArrow);
		WMSetButtonAltImage(panel->defBtn, scrPtr->pushedButtonArrow);
		WMSetButtonImagePosition(panel->defBtn, WIPRight);
		WMSetButtonFont(panel->defBtn, defaultFont);
	}
	if (alternateButton) {
		panel->altBtn = WMCreateCommandButton(panel->hbox);
		WMAddBoxSubviewAtEnd(panel->hbox, WMWidgetView(panel->altBtn), false, true, aw, 0, 5);
		WMSetButtonAction(panel->altBtn, alertPanelOnClick, panel);
		WMSetButtonText(panel->altBtn, alternateButton);
		WMSetButtonFont(panel->altBtn, defaultFont);
	}
	if (otherButton) {
		panel->othBtn = WMCreateCommandButton(panel->hbox);
		WMSetButtonAction(panel->othBtn, alertPanelOnClick, panel);
		WMAddBoxSubviewAtEnd(panel->hbox, WMWidgetView(panel->othBtn), false, true, ow, 0, 5);
		WMSetButtonText(panel->othBtn, otherButton);
		WMSetButtonFont(panel->othBtn, defaultFont);
	}

	WMMapSubwidgets(panel->hbox);

	WMCreateEventHandler(W_VIEW(panel->win), XCB_EVENT_MASK_KEY_PRESS, handleKeyPress, panel);

	WMRealizeWidget(panel->win);
	WMMapSubwidgets(panel->win);

	WMReleaseFont(defaultFont);

	return panel;
}

WMAlertPanel *WMCreateScaledAlertPanel(WMScreen * scrPtr, WMWindow * owner,
				       const char *title, const char *msg, const char *defaultButton,
				       const char *alternateButton, const char *otherButton)
{
	WMAlertPanel *panel;
	int dw = 0, aw = 0, ow = 0, w;
	WMBox *hbox;
	WMBox *mbox;
	WMPixmap *icon;
	int wmScaleWidth, wmScaleHeight;
	int pwidth, pheight;
	int msgw, msgh;

	panel = wmalloc(sizeof(WMAlertPanel));

	if (owner) {
		panel->win = WMCreatePanelWithStyleForWindow(owner, "alertPanel", WMTitledWindowMask);
	} else {
		panel->win = WMCreateWindowWithStyle(scrPtr, "alertPanel", WMTitledWindowMask);
	}

	/* calculate and set the panel's size */
	WMGetScaleBaseFromSystemFont(scrPtr, &wmScaleWidth, &wmScaleHeight);
	pwidth = WMScaleX(400);

	/* widen so the non-wrapping title fits beside the icon */
	if (title) {
		WMFont *largeFont = WMBoldSystemFontOfSize(scrPtr, WMScaleY(24));

		if (largeFont) {
			w = WMWidthOfString(largeFont, title, strlen(title))
			    + 64			/* icon */
			    + WMScaleX(10)		/* gap after icon */
			    + 2 * WMScaleX(5)		/* row borders */
			    + 8;			/* label paint inset */
			if (w > pwidth)
				pwidth = w;
			WMReleaseFont(largeFont);
		}
	}
	w = scrPtr->rootView->size.width - 2 * WMScaleX(20);
	if (pwidth > w)
		pwidth = w;

	pheight = 64 + 2 * WMScaleY(5)	/* icon row */
	    + WMScaleY(5) + 2 + WMScaleY(5);	/* divider line */
	msgw = pwidth - 2 * WMScaleX(12) - 8;
	msgh = 0;
	if (msg) {
		msgh = W_GetTextHeight(scrPtr->normalFont, msg, msgw, true);
		if (msgh < WMFontHeight(scrPtr->normalFont) * 2)
			msgh = WMFontHeight(scrPtr->normalFont) * 2;
		pheight += msgh + 2 * WMScaleY(12) + WMScaleY(5);
	}
	pheight += WMScaleY(44);	/* button row */

	w = scrPtr->rootView->size.height - 2 * WMScaleY(20);
	if (pheight > w)
		pheight = w;

	WMResizeWidget(panel->win, pwidth, pheight);

	WMSetWindowInitialPosition(panel->win,
				   (scrPtr->rootView->size.width - pwidth) / 2,
				   (scrPtr->rootView->size.height - pheight) / 2);

	WMSetWindowTitle(panel->win, "");

	panel->vbox = WMCreateBox(panel->win);
	WMSetViewExpandsToParent(WMWidgetView(panel->vbox), 0, 0, 0, 0);
	WMSetBoxHorizontal(panel->vbox, false);
	WMMapWidget(panel->vbox);

	hbox = WMCreateBox(panel->vbox);
	WMSetBoxBorderWidth(hbox, WMScaleX(5));
	WMSetBoxHorizontal(hbox, true);
	WMMapWidget(hbox);
	WMAddBoxSubview(panel->vbox, WMWidgetView(hbox), false, true, 64 + 2 * WMScaleY(5), 0, WMScaleY(5));

	panel->iLbl = WMCreateLabel(hbox);
	WMSetLabelImagePosition(panel->iLbl, WIPImageOnly);
	WMMapWidget(panel->iLbl);
	WMAddBoxSubview(hbox, WMWidgetView(panel->iLbl), false, true, 64, 0, 10);
	icon = WMCreateApplicationIconBlendedPixmap(scrPtr, (RColor *) NULL);
	if (icon) {
		WMSetLabelImage(panel->iLbl, icon);
		WMReleasePixmap(icon);
	} else {
		WMSetLabelImage(panel->iLbl, scrPtr->applicationIconPixmap);
	}

	if (title) {
		WMFont *largeFont;

		largeFont = WMBoldSystemFontOfSize(scrPtr, 24);

		panel->tLbl = WMCreateLabel(hbox);
		WMMapWidget(panel->tLbl);
		WMAddBoxSubview(hbox, WMWidgetView(panel->tLbl), true, true, 64, 0, 0);
		WMSetLabelText(panel->tLbl, title);
		WMSetLabelTextAlignment(panel->tLbl, WALeft);
		WMSetLabelFont(panel->tLbl, largeFont);

		WMReleaseFont(largeFont);
	}

	/* create divider line */

	panel->line = WMCreateFrame(panel->win);
	WMMapWidget(panel->line);
	WMAddBoxSubview(panel->vbox, WMWidgetView(panel->line), false, true, 2, 2, WMScaleY(5));
	WMSetFrameRelief(panel->line, WRGroove);

	if (msg) {
		/* bordered row so the text keeps a gap from the frame */
		mbox = WMCreateBox(panel->vbox);
		WMSetBoxBorderWidth(mbox, WMScaleX(12));
		WMSetBoxHorizontal(mbox, true);
		WMMapWidget(mbox);
		WMAddBoxSubview(panel->vbox, WMWidgetView(mbox), true, true,
				msgh + 2 * WMScaleY(12), 0, WMScaleY(5));

		panel->mLbl = WMCreateLabel(mbox);
		WMSetLabelWraps(panel->mLbl, true);
		WMMapWidget(panel->mLbl);
		WMAddBoxSubview(mbox, WMWidgetView(panel->mLbl), false, true,
				pwidth - 2 * WMScaleX(12), 0, 0);
		WMSetLabelText(panel->mLbl, msg);
		WMSetLabelTextAlignment(panel->mLbl, WACenter);
	}

	panel->hbox = WMCreateBox(panel->vbox);
	WMSetBoxBorderWidth(panel->hbox, WMScaleX(10));
	WMSetBoxHorizontal(panel->hbox, true);
	WMMapWidget(panel->hbox);
	WMAddBoxSubview(panel->vbox, WMWidgetView(panel->hbox), false, true, WMScaleY(44), 0, 0);

	/* create buttons */
	if (otherButton)
		ow = WMWidthOfString(scrPtr->normalFont, otherButton, strlen(otherButton));

	if (alternateButton)
		aw = WMWidthOfString(scrPtr->normalFont, alternateButton, strlen(alternateButton));

	if (defaultButton)
		dw = WMWidthOfString(scrPtr->normalFont, defaultButton, strlen(defaultButton));

	dw = dw + (scrPtr->buttonArrow ? scrPtr->buttonArrow->width : 0);

	aw += WMScaleX(30);
	ow += WMScaleX(30);
	dw += WMScaleX(30);

	w = WMAX(dw, WMAX(aw, ow));
	if ((w + WMScaleX(10)) * 3 < pwidth) {
		aw = w;
		ow = w;
		dw = w;
	} else {
		int t;

		t = pwidth - 4 * WMScaleX(10) - aw - ow - dw;
		aw += t / 3;
		ow += t / 3;
		dw += t / 3;
	}

	if (defaultButton) {
		panel->defBtn = WMCreateCommandButton(panel->hbox);
		WMResizeWidget(panel->defBtn, dw, WMScaleY(24));
		WMSetButtonAction(panel->defBtn, alertPanelOnClick, panel);
		WMAddBoxSubviewAtEnd(panel->hbox, WMWidgetView(panel->defBtn), false, true, dw, 0, 0);
		WMSetButtonText(panel->defBtn, defaultButton);
		WMSetButtonImage(panel->defBtn, scrPtr->buttonArrow);
		WMSetButtonAltImage(panel->defBtn, scrPtr->pushedButtonArrow);
		WMSetButtonImagePosition(panel->defBtn, WIPRight);
	}
	if (alternateButton) {
		panel->altBtn = WMCreateCommandButton(panel->hbox);
		WMResizeWidget(panel->altBtn, aw, WMScaleY(24));
		WMAddBoxSubviewAtEnd(panel->hbox, WMWidgetView(panel->altBtn), false, true, aw, 0, WMScaleX(5));
		WMSetButtonAction(panel->altBtn, alertPanelOnClick, panel);
		WMSetButtonText(panel->altBtn, alternateButton);
	}
	if (otherButton) {
		panel->othBtn = WMCreateCommandButton(panel->hbox);
		WMResizeWidget(panel->othBtn, ow, WMScaleY(24));
		WMSetButtonAction(panel->othBtn, alertPanelOnClick, panel);
		WMAddBoxSubviewAtEnd(panel->hbox, WMWidgetView(panel->othBtn), false, true, ow, 0, WMScaleX(5));
		WMSetButtonText(panel->othBtn, otherButton);
	}

	WMMapSubwidgets(panel->hbox);

	WMCreateEventHandler(W_VIEW(panel->win), XCB_EVENT_MASK_KEY_PRESS, handleKeyPress, panel);

	WMRealizeWidget(panel->win);
	WMMapSubwidgets(panel->win);

	return panel;
}

static void inputBoxOnClick(WMWidget * self, void *clientData)
{
	WMInputPanel *panel = clientData;

	WMBreakModalLoop(WMWidgetScreen(self));
	if (self == panel->defBtn) {
		panel->result = WAPRDefault;
	} else if (self == panel->altBtn) {
		panel->result = WAPRAlternate;
	}
}

static void handleKeyPress2(xcb_generic_event_t * event, void *clientData)
{
	WMInputPanel *panel = (WMInputPanel *) clientData;
	xkb_keysym_t ksym;

	ksym = W_EventKeysym(WMWidgetScreen(panel->win),
			     (const xcb_key_press_event_t *)event);

	if ((ksym == XKB_KEY_Return || ksym == XKB_KEY_KP_Enter) && panel->defBtn) {
		WMPerformButtonClick(panel->defBtn);
	} else if (ksym == XKB_KEY_Escape) {
		if (panel->altBtn) {
			WMPerformButtonClick(panel->altBtn);
		} else {
			/*           printf("got esc\n"); */
			WMBreakModalLoop(WMWidgetScreen(panel->win));
			panel->result = WAPRDefault;
		}
	}
}

char *WMRunInputPanel(WMScreen * scrPtr, WMWindow * owner, const char *title,
		      const char *msg, const char *defaultText, const char *okButton, const char *cancelButton)
{
	WMInputPanel *panel;
	WMView *view;
	int px, py;
	char *tmp;

	panel = WMCreateInputPanel(scrPtr, owner, title, msg, defaultText, okButton, cancelButton);

	view = WMWidgetView(panel->win);
	if (owner) {
		WMView *oview = WMWidgetView(owner);
		WMPoint pt = WMGetViewScreenPosition(oview);

		px = (W_VIEW_WIDTH(oview) - W_VIEW_WIDTH(view)) / 2;
		py = (W_VIEW_HEIGHT(oview) - W_VIEW_HEIGHT(view)) / 2;

		px += pt.x;
		py += pt.y;
	} else {
		px = (W_VIEW_WIDTH(scrPtr->rootView) - W_VIEW_WIDTH(view)) / 2;
		py = (W_VIEW_HEIGHT(scrPtr->rootView) - W_VIEW_HEIGHT(view)) / 2;
	}
	WMSetWindowInitialPosition(panel->win, px, py);

	WMMapWidget(panel->win);

	WMRunModalLoop(scrPtr, W_VIEW(panel->win));

	if (panel->result == WAPRDefault)
		tmp = WMGetTextFieldText(panel->text);
	else
		tmp = NULL;

	WMDestroyInputPanel(panel);

	return tmp;
}

void WMDestroyInputPanel(WMInputPanel * panel)
{
	WMRemoveNotificationObserver(panel);
	WMUnmapWidget(panel->win);
	WMDestroyWidget(panel->win);
	wfree(panel);
}

static void endedEditingObserver(void *observerData, WMNotification * notification)
{
	WMInputPanel *panel = (WMInputPanel *) observerData;

	switch ((uintptr_t)WMGetNotificationClientData(notification)) {
	case WMReturnTextMovement:
		if (panel->defBtn)
			WMPerformButtonClick(panel->defBtn);
		break;
	case WMEscapeTextMovement:
		if (panel->altBtn)
			WMPerformButtonClick(panel->altBtn);
		else {
			WMBreakModalLoop(WMWidgetScreen(panel->win));
			panel->result = WAPRDefault;
		}
		break;
	default:
		break;
	}
}

WMInputPanel *WMCreateInputPanel(WMScreen * scrPtr, WMWindow * owner, const char *title, const char *msg,
				 const char *defaultText, const char *okButton, const char *cancelButton)
{
	WMInputPanel *panel;
	WMFont *defaultFont;
	WMFont *largeFont = NULL;
	int x, dw = 0, aw = 0, w, panel_w;

	defaultFont = WMSystemFontOfSize(scrPtr, 12);
	panel = wmalloc(sizeof(WMInputPanel));

	if (owner)
		panel->win = WMCreatePanelWithStyleForWindow(owner, "inputPanel", WMTitledWindowMask);
	else
		panel->win = WMCreateWindowWithStyle(scrPtr, "inputPanel", WMTitledWindowMask);
	WMSetWindowTitle(panel->win, "");

	/* widen the panel so the non-wrapping title fits, capped at 400 */
	panel_w = 320;
	if (title) {
		int tw;

		largeFont = WMBoldSystemFontOfSize(scrPtr, 24);
		if (largeFont) {
			tw = WMWidthOfString(largeFont, title, strlen(title));
			if (tw + 48 > panel_w)
				panel_w = tw + 48 > 400 ? 400 : tw + 48;
		}
	}
	WMResizeWidget(panel->win, panel_w, msg ? 160 : 110);

	if (title) {
		int th;

		th = WMFontHeight(largeFont) + 4;

		panel->tLbl = WMCreateLabel(panel->win);
		WMMoveWidget(panel->tLbl, 20, msg ? 16 : (54 - th) / 2);
		WMResizeWidget(panel->tLbl, panel_w - 40, th);
		WMSetLabelText(panel->tLbl, title);
		WMSetLabelTextAlignment(panel->tLbl, WALeft);
		WMSetLabelFont(panel->tLbl, largeFont);

		WMReleaseFont(largeFont);
	}

	if (msg) {
		panel->mLbl = WMCreateLabel(panel->win);
		WMMoveWidget(panel->mLbl, 20, 50);
		WMResizeWidget(panel->mLbl, panel_w - 40, WMFontHeight(scrPtr->normalFont) * 2);
		WMSetLabelText(panel->mLbl, msg);
		WMSetLabelTextAlignment(panel->mLbl, WALeft);
		WMSetLabelFont(panel->mLbl, defaultFont);
	}

	panel->text = WMCreateTextField(panel->win);
	WMMoveWidget(panel->text, 20, msg ? 85 : 54);
	WMResizeWidget(panel->text, panel_w - 40, WMWidgetHeight(panel->text));
	WMSetTextFieldText(panel->text, defaultText);
	WMSetTextFieldFont(panel->text, defaultFont);

	WMAddNotificationObserver(endedEditingObserver, panel, WMTextDidEndEditingNotification, panel->text);

	/* create buttons */
	if (cancelButton)
		aw = WMWidthOfString(defaultFont, cancelButton, strlen(cancelButton));

	if (okButton)
		dw = WMWidthOfString(defaultFont, okButton, strlen(okButton));

	w = dw + (scrPtr->buttonArrow ? scrPtr->buttonArrow->width : 0);
	if (aw > w)
		w = aw;

	w += 30;
	x = panel_w - 10;

	if (okButton) {
		x -= w + 10;

		panel->defBtn = WMCreateCustomButton(panel->win, WBBPushInMask
						     | WBBPushChangeMask | WBBPushLightMask);
		WMSetButtonAction(panel->defBtn, inputBoxOnClick, panel);
		WMMoveWidget(panel->defBtn, x, msg ? 124 : 80);
		WMResizeWidget(panel->defBtn, w, 24);
		WMSetButtonText(panel->defBtn, okButton);
		WMSetButtonImage(panel->defBtn, scrPtr->buttonArrow);
		WMSetButtonAltImage(panel->defBtn, scrPtr->pushedButtonArrow);
		WMSetButtonImagePosition(panel->defBtn, WIPRight);
		WMSetButtonFont(panel->defBtn, defaultFont);
	}
	if (cancelButton) {
		x -= w + 10;

		panel->altBtn = WMCreateCommandButton(panel->win);
		WMSetButtonAction(panel->altBtn, inputBoxOnClick, panel);
		WMMoveWidget(panel->altBtn, x, msg ? 124 : 80);
		WMResizeWidget(panel->altBtn, w, 24);
		WMSetButtonText(panel->altBtn, cancelButton);
		WMSetButtonFont(panel->altBtn, defaultFont);
	}

	WMCreateEventHandler(W_VIEW(panel->win), XCB_EVENT_MASK_KEY_PRESS, handleKeyPress2, panel);

	WMRealizeWidget(panel->win);
	WMMapSubwidgets(panel->win);

	WMSetFocusToWidget(panel->text);

	WMReleaseFont(defaultFont);

	return panel;
}

WMInputPanel *WMCreateScaledInputPanel(WMScreen * scrPtr, WMWindow * owner, const char *title, const char *msg,
				       const char *defaultText, const char *okButton, const char *cancelButton)
{
	WMInputPanel *panel;
	WMFont *largeFont = NULL;
	int x, dw = 0, aw = 0, w, panel_w;
	int wmScaleWidth, wmScaleHeight;

	panel = wmalloc(sizeof(WMInputPanel));

	if (owner)
		panel->win = WMCreatePanelWithStyleForWindow(owner, "inputPanel", WMTitledWindowMask);
	else
		panel->win = WMCreateWindowWithStyle(scrPtr, "inputPanel", WMTitledWindowMask);
	WMSetWindowTitle(panel->win, "");

	WMGetScaleBaseFromSystemFont(scrPtr, &wmScaleWidth, &wmScaleHeight);
	/* widen the panel so the non-wrapping title fits, capped at 400 */
	panel_w = WMScaleX(320);
	if (title) {
		int tw;

		largeFont = WMBoldSystemFontOfSize(scrPtr, WMScaleY(24));
		if (largeFont) {
			tw = WMWidthOfString(largeFont, title, strlen(title));
			if (tw + 2 * WMScaleX(20) + 8 > panel_w)
				panel_w = tw + 2 * WMScaleX(20) + 8 > WMScaleX(400)
					? WMScaleX(400) : tw + 2 * WMScaleX(20) + 8;
		}
	}
	WMResizeWidget(panel->win, panel_w, WMScaleY((msg ? 160 : 110)));

	if (title) {
		int th;

		th = WMFontHeight(largeFont) + WMScaleY(4);

		panel->tLbl = WMCreateLabel(panel->win);
		WMMoveWidget(panel->tLbl, WMScaleX(20), msg ? WMScaleY(16) : (WMScaleY(54) - th) / 2);
		WMResizeWidget(panel->tLbl, panel_w - 2 * WMScaleX(20), th);
		WMSetLabelText(panel->tLbl, title);
		WMSetLabelTextAlignment(panel->tLbl, WALeft);
		WMSetLabelFont(panel->tLbl, largeFont);

		WMReleaseFont(largeFont);
	}

	if (msg) {
		panel->mLbl = WMCreateLabel(panel->win);
		WMMoveWidget(panel->mLbl, WMScaleX(20), WMScaleY(50));
		WMResizeWidget(panel->mLbl, panel_w - 2 * WMScaleX(20), WMFontHeight(scrPtr->normalFont) * 2);
		WMSetLabelText(panel->mLbl, msg);
		WMSetLabelTextAlignment(panel->mLbl, WALeft);
	}

	panel->text = WMCreateTextField(panel->win);
	WMMoveWidget(panel->text, WMScaleX(20), WMScaleY((msg ? 85 : 54)));
	WMResizeWidget(panel->text, panel_w - 2 * WMScaleX(20), WMScaleY(20));
	WMSetTextFieldText(panel->text, defaultText);

	WMAddNotificationObserver(endedEditingObserver, panel, WMTextDidEndEditingNotification, panel->text);

	/* create buttons */
	if (cancelButton)
		aw = WMWidthOfString(scrPtr->normalFont, cancelButton, strlen(cancelButton));

	if (okButton)
		dw = WMWidthOfString(scrPtr->normalFont, okButton, strlen(okButton));

	w = dw + (scrPtr->buttonArrow ? scrPtr->buttonArrow->width : 0);
	if (aw > w)
		w = aw;

	w += WMScaleX(30);
	x = panel_w - WMScaleX(10);

	if (okButton) {
		x -= w + WMScaleX(10);

		panel->defBtn = WMCreateCustomButton(panel->win, WBBPushInMask
						     | WBBPushChangeMask | WBBPushLightMask);
		WMSetButtonAction(panel->defBtn, inputBoxOnClick, panel);
		WMMoveWidget(panel->defBtn, x, WMScaleY((msg ? 124 : 80)));
		WMResizeWidget(panel->defBtn, w, WMScaleY(24));
		WMSetButtonText(panel->defBtn, okButton);
		WMSetButtonImage(panel->defBtn, scrPtr->buttonArrow);
		WMSetButtonAltImage(panel->defBtn, scrPtr->pushedButtonArrow);
		WMSetButtonImagePosition(panel->defBtn, WIPRight);
	}
	if (cancelButton) {
		x -= w + WMScaleX(10);

		panel->altBtn = WMCreateCommandButton(panel->win);
		WMSetButtonAction(panel->altBtn, inputBoxOnClick, panel);
		WMMoveWidget(panel->altBtn, x, WMScaleY((msg ? 124 : 80)));
		WMResizeWidget(panel->altBtn, w, WMScaleY(24));
		WMSetButtonText(panel->altBtn, cancelButton);
	}

	WMCreateEventHandler(W_VIEW(panel->win), XCB_EVENT_MASK_KEY_PRESS, handleKeyPress2, panel);

	WMRealizeWidget(panel->win);
	WMMapSubwidgets(panel->win);

	WMSetFocusToWidget(panel->text);

	return panel;
}

static void handleKeyPress3(xcb_generic_event_t * event, void *clientData)
{
	WMGenericPanel *panel = (WMGenericPanel *) clientData;
	xkb_keysym_t ksym;

	ksym = W_EventKeysym(WMWidgetScreen(panel->win),
			     (const xcb_key_press_event_t *)event);

	if ((ksym == XKB_KEY_Return || ksym == XKB_KEY_KP_Enter) && panel->defBtn) {
		WMPerformButtonClick(panel->defBtn);
	} else if (ksym == XKB_KEY_Escape) {
		if (panel->altBtn) {
			WMPerformButtonClick(panel->altBtn);
		} else {
			panel->result = WAPRDefault;
			WMBreakModalLoop(WMWidgetScreen(panel->win));
		}
	}
}

void WMDestroyGenericPanel(WMGenericPanel * panel)
{
	WMUnmapWidget(panel->win);
	WMDestroyWidget(panel->win);
	wfree(panel);
}

WMGenericPanel *WMCreateGenericPanel(WMScreen * scrPtr, WMWindow * owner,
				     const char *title, const char *defaultButton, const char *alternateButton)
{
	WMGenericPanel *panel;
	WMFont *defaultFont;
	int dw = 0, aw = 0, w;
	WMBox *hbox;
	WMPixmap *icon;

	defaultFont = WMSystemFontOfSize(scrPtr, 12);
	panel = wmalloc(sizeof(WMGenericPanel));

	if (owner) {
		panel->win = WMCreatePanelWithStyleForWindow(owner, "genericPanel", WMTitledWindowMask);
	} else {
		panel->win = WMCreateWindowWithStyle(scrPtr, "genericPanel", WMTitledWindowMask);
	}

	WMSetWindowInitialPosition(panel->win,
				   (scrPtr->rootView->size.width - WMWidgetWidth(panel->win)) / 2,
				   (scrPtr->rootView->size.height - WMWidgetHeight(panel->win)) / 2);

	WMSetWindowTitle(panel->win, "");

	panel->vbox = WMCreateBox(panel->win);
	WMSetViewExpandsToParent(WMWidgetView(panel->vbox), 0, 0, 0, 0);
	WMSetBoxHorizontal(panel->vbox, false);
	WMMapWidget(panel->vbox);

	hbox = WMCreateBox(panel->vbox);
	WMSetBoxBorderWidth(hbox, 5);
	WMSetBoxHorizontal(hbox, true);
	WMMapWidget(hbox);
	WMAddBoxSubview(panel->vbox, WMWidgetView(hbox), false, true, 74, 0, 5);

	panel->iLbl = WMCreateLabel(hbox);
	WMSetLabelImagePosition(panel->iLbl, WIPImageOnly);
	WMMapWidget(panel->iLbl);
	WMAddBoxSubview(hbox, WMWidgetView(panel->iLbl), false, true, 64, 0, 10);
	icon = WMCreateApplicationIconBlendedPixmap(scrPtr, (RColor *) NULL);
	if (icon) {
		WMSetLabelImage(panel->iLbl, icon);
		WMReleasePixmap(icon);
	} else {
		WMSetLabelImage(panel->iLbl, scrPtr->applicationIconPixmap);
	}

	if (title) {
		WMFont *largeFont;

		largeFont = WMBoldSystemFontOfSize(scrPtr, 24);

		panel->tLbl = WMCreateLabel(hbox);
		WMMapWidget(panel->tLbl);
		WMAddBoxSubview(hbox, WMWidgetView(panel->tLbl), true, true, 64, 0, 0);
		WMSetLabelText(panel->tLbl, title);
		WMSetLabelTextAlignment(panel->tLbl, WALeft);
		WMSetLabelFont(panel->tLbl, largeFont);

		WMReleaseFont(largeFont);
	}

	/* create divider line */

	panel->line = WMCreateFrame(panel->vbox);
	WMMapWidget(panel->line);
	WMAddBoxSubview(panel->vbox, WMWidgetView(panel->line), false, true, 2, 2, 5);
	WMSetFrameRelief(panel->line, WRGroove);

	panel->content = WMCreateFrame(panel->vbox);
	WMMapWidget(panel->content);
	WMAddBoxSubview(panel->vbox, WMWidgetView(panel->content), true, true, 50, 0, 5);
	WMSetFrameRelief(panel->content, WRFlat);

	hbox = WMCreateBox(panel->vbox);
	WMSetBoxBorderWidth(hbox, 10);
	WMSetBoxHorizontal(hbox, true);
	WMMapWidget(hbox);
	WMAddBoxSubview(panel->vbox, WMWidgetView(hbox), false, true, 44, 0, 0);

	/* create buttons */
	if (defaultButton)
		dw = WMWidthOfString(defaultFont, defaultButton, strlen(defaultButton));

	if (alternateButton)
		aw = WMWidthOfString(defaultFont, alternateButton, strlen(alternateButton));

	dw = dw + (scrPtr->buttonArrow ? scrPtr->buttonArrow->width : 0);

	aw += 30;
	dw += 30;

	w = WMAX(dw, aw);
	if ((w + 10) * 2 < 400) {
		dw = w;
	} else {
		int t;

		t = 400 - 40 - aw - dw;
		dw += t / 2;
	}

	if (defaultButton) {
		panel->defBtn = WMCreateCommandButton(hbox);
		WMSetButtonAction(panel->defBtn, alertPanelOnClick, panel);
		WMAddBoxSubviewAtEnd(hbox, WMWidgetView(panel->defBtn), false, true, dw, 0, 0);
		WMSetButtonText(panel->defBtn, defaultButton);
		WMSetButtonImage(panel->defBtn, scrPtr->buttonArrow);
		WMSetButtonAltImage(panel->defBtn, scrPtr->pushedButtonArrow);
		WMSetButtonImagePosition(panel->defBtn, WIPRight);
		WMSetButtonFont(panel->defBtn, defaultFont);
	}

	WMMapSubwidgets(hbox);

	WMCreateEventHandler(W_VIEW(panel->win), XCB_EVENT_MASK_KEY_PRESS, handleKeyPress3, panel);

	WMRealizeWidget(panel->win);
	WMMapSubwidgets(panel->win);

	WMReleaseFont(defaultFont);

	return panel;
}
