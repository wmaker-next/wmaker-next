
#include <stdlib.h>
#include <stdio.h>
#include <WINGs/WINGs.h>

void showSelectedColor(void *self, void *cdata)
{
	WMColorPanel *panel = (WMColorPanel *) self;

	(void) cdata;
	printf("Selected Color: %s\n", WMGetColorRGBDescription(WMGetColorPanelColor(panel)));
}

int main(int argc, char **argv)
{
	Display *dpy;
	WMScreen *scr;
	WMColorPanel *panel;

	WMInitializeApplication("wmcolorpick", &argc, argv);

	dpy = XOpenDisplay("");
	if (!dpy) {
		printf("could not open display\n");
		exit(1);
	}

	scr = WMCreateScreen(dpy, DefaultScreen(dpy));

	panel = WMGetColorPanel(scr);
	WMSetColorPanelAction(panel, showSelectedColor, NULL);
	WMShowColorPanel(panel);

	WMScreenMainLoop(scr);

	return 0;
}
