
#include <WINGs/WINGsP.h>

#include "geomview.h"

WGeometryView *WCreateGeometryView(WMScreen *scr)
{
	WGeometryView *gview;

	gview = WCreateOSDView(scr);
	if (!gview)
		return NULL;

	/* Lock the box size so it never resizes inside the server-grabbed
	 * interactive move/resize loop. */
	WSetOSDViewFixedTemplate(gview, "+0000 ,  +0000    ");

	return gview;
}

void WSetGeometryViewShownPosition(WGeometryView *gview, int x, int y)
{
	char buffer[64];

	snprintf(buffer, sizeof(buffer), "%+5i , %+5i    ", x, y);
	WSetOSDViewText(gview, buffer);
}

void WSetGeometryViewShownSize(WGeometryView *gview, unsigned width, unsigned height)
{
	char buffer[64];

	snprintf(buffer, sizeof(buffer), "%+5i x %+5i    ", width, height);
	WSetOSDViewText(gview, buffer);
}
