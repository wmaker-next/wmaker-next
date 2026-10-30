/*
 *  WindowMaker miscellaneous functions
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>

#include "wconfig.h"

#include "xutil.h"

#include <WINGs/WUtil.h>

static const char *requestCodes[] = {
	"DUMMY",
	"X_CreateWindow",
	"X_ChangeWindowAttributes",
	"X_GetWindowAttributes",
	"X_DestroyWindow",
	"X_DestroySubwindows",
	"X_ChangeSaveSet",
	"X_ReparentWindow",
	"X_MapWindow",
	"X_MapSubwindows",
	"X_UnmapWindow",
	"X_UnmapSubwindows",
	"X_ConfigureWindow",
	"X_CirculateWindow",
	"X_GetGeometry",
	"X_QueryTree",
	"X_InternAtom",
	"X_GetAtomName",
	"X_ChangeProperty",
	"X_DeleteProperty",
	"X_GetProperty",
	"X_ListProperties",
	"X_SetSelectionOwner",
	"X_GetSelectionOwner",
	"X_ConvertSelection",
	"X_SendEvent",
	"X_GrabPointer",
	"X_UngrabPointer",
	"X_GrabButton",
	"X_UngrabButton",
	"X_ChangeActivePointerGrab",
	"X_GrabKeyboard",
	"X_UngrabKeyboard",
	"X_GrabKey",
	"X_UngrabKey",
	"X_AllowEvents",
	"X_GrabServer",
	"X_UngrabServer",
	"X_QueryPointer",
	"X_GetMotionEvents",
	"X_TranslateCoords",
	"X_WarpPointer",
	"X_SetInputFocus",
	"X_GetInputFocus",
	"X_QueryKeymap",
	"X_OpenFont",
	"X_CloseFont",
	"X_QueryFont",
	"X_QueryTextExtents",
	"X_ListFonts",
	"X_ListFontsWithInfo",
	"X_SetFontPath",
	"X_GetFontPath",
	"X_CreatePixmap",
	"X_FreePixmap",
	"X_CreateGC",
	"X_ChangeGC",
	"X_CopyGC",
	"X_SetDashes",
	"X_SetClipRectangles",
	"X_FreeGC",
	"X_ClearArea",
	"X_CopyArea",
	"X_CopyPlane",
	"X_PolyPoint",
	"X_PolyLine",
	"X_PolySegment",
	"X_PolyRectangle",
	"X_PolyArc",
	"X_FillPoly",
	"X_PolyFillRectangle",
	"X_PolyFillArc",
	"X_PutImage",
	"X_GetImage",
	"X_PolyText8",
	"X_PolyText16",
	"X_ImageText8",
	"X_ImageText16",
	"X_CreateColormap",
	"X_FreeColormap",
	"X_CopyColormapAndFree",
	"X_InstallColormap",
	"X_UninstallColormap",
	"X_ListInstalledColormaps",
	"X_AllocColor",
	"X_AllocNamedColor",
	"X_AllocColorCells",
	"X_AllocColorPlanes",
	"X_FreeColors",
	"X_StoreColors",
	"X_StoreNamedColor",
	"X_QueryColors",
	"X_LookupColor",
	"X_CreateCursor",
	"X_CreateGlyphCursor",
	"X_FreeCursor",
	"X_RecolorCursor",
	"X_QueryBestSize",
	"X_QueryExtension",
	"X_ListExtensions",
	"X_ChangeKeyboardMapping",
	"X_GetKeyboardMapping",
	"X_ChangeKeyboardControl",
	"X_GetKeyboardControl",
	"X_Bell",
	"X_ChangePointerControl",
	"X_GetPointerControl",
	"X_SetScreenSaver",
	"X_GetScreenSaver",
	"X_ChangeHosts",
	"X_ListHosts",
	"X_SetAccessControl",
	"X_SetCloseDownMode",
	"X_KillClient",
	"X_RotateProperties",
	"X_ForceScreenSaver",
	"X_SetPointerMapping",
	"X_GetPointerMapping",
	"X_SetModifierMapping",
	"X_GetModifierMapping",
	"X_NoOperation"
};

/* the core error descriptions, the XErrorDB stand-in for XGetErrorText()
 * which needed a Display; extension errors beyond the core set have no
 * static table and print by number */
static const char *const coreErrorNames[] = {
	"BadRequest (invalid request code or no such operation)",
	"BadValue (integer parameter out of range for operation)",
	"BadWindow (invalid Window parameter)",
	"BadPixmap (invalid Pixmap parameter)",
	"BadAtom (invalid Atom parameter)",
	"BadCursor (invalid Cursor parameter)",
	"BadFont (invalid Font parameter)",
	"BadMatch (invalid parameter attributes)",
	"BadDrawable (invalid Pixmap or Window parameter)",
	"BadAccess (attempt to use private resource invalid)",
	"BadAlloc (insufficient resources for operation)",
	"BadColor (invalid Colormap parameter)",
	"BadGC (invalid GContext parameter)",
	"BadIDChoice (invalid resource ID chosen for this connection)",
	"BadName (invalid or unknown Font or Color name)",
	"BadLength (request too long or internal length error)",
	"BadImplementation (server does not implement operation)"
};

void FormatXError(unsigned int error_code, unsigned int major,
		  unsigned int minor, unsigned long resource_id,
		  unsigned long serial, char *buffer, int size)
{
	int i, p;

	if (error_code >= 1 && error_code <= wlengthof(coreErrorNames))
		p = snprintf(buffer, size, "%s", coreErrorNames[error_code - 1]);
	else
		p = snprintf(buffer, size, "error code %u", error_code);
	if (p < 0)
		return;
	i = p;
	if (i > size - 100)
		return;
	buffer += i;
	if (major >= wlengthof(requestCodes)) {
		p = snprintf(buffer, size - i, "\n	Request code: %u\n", major);
	} else {
		p = snprintf(buffer, size - i, "\n	Request code: %u %s\n",
			     major, requestCodes[major]);
	}
	if (p < 0 || p >= size - i)
		return;
	i += p;
	if (i > size - 40)
		return;
	buffer += p;
	p = snprintf(buffer, size - i, "	Request minor code: %u\n", minor);
	if (p < 0 || p >= size - i)
		return;
	i += p;
	if (i > size - 30)
		return;
	buffer += p;
	p = snprintf(buffer, size - i, "	Resource ID: 0x%x\n", (unsigned int)resource_id);
	if (p < 0 || p >= size - i)
		return;
	i += p;
	if (i > size - 30)
		return;
	buffer += p;
	snprintf(buffer, size - i, "	Error serial: %lu\n", serial);
}
