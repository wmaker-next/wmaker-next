
#include <stdlib.h>
#include <stdio.h>
#include <xcb/xcb.h>
#include <WINGs/WINGs.h>

void showSelectedColor(void *self, void *cdata)
{
	WMColorPanel *panel = (WMColorPanel *) self;

	(void) cdata;
	printf("Selected Color: %s\n", WMGetColorRGBDescription(WMGetColorPanelColor(panel)));
}

int main(int argc, char **argv)
{
	xcb_connection_t *xc;
	int sn;
	static xcb_connection_t *xc;
	WMScreen *scr;
	WMColorPanel *panel;

	WMInitializeApplication("wmcolorpick", &argc, argv);

	/* xcb_connect(NULL, ...) reads $DISPLAY exactly like libX11's
	 * XOpenDisplay("") did */
	xc = xcb_connect(NULL, &sn);
	if (xcb_connection_has_error(xc)) {
		printf("could not open display\n");
		exit(1);
	}

	scr = WMCreateScreen(xc, sn);

	panel = WMGetColorPanel(scr);
	WMSetColorPanelAction(panel, showSelectedColor, NULL);
	WMShowColorPanel(panel);

	WMScreenMainLoop(scr);

	return 0;
}
