#include "wconfig.h"
#include "WindowMaker.h"
#include "wsound.h"
#include "ipc.h"

void wSoundPlay(WScreen *scr, const char *event)
{
	if (w_global.config->no_sound)
		return;
	if (!scr || !event || !*event)
		return;
	wIPCEmitSound(scr, event);
}
