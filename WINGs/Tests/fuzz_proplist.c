/* fuzz_proplist - libFuzzer harness for the WINGs property-list parser
 *
 * Exercises the two production entry points that own the parse core:
 * WMCreatePropListFromDescription() (string description, as setstyle and
 * the IPC path feed it) and WMReadPropListFromFile() (the domain-file
 * path, including the fstat size cap). A successful parse is then run
 * through WMGetPropListDescription() and re-parsed to drive the
 * description writer with attacker-shaped trees.
 */

#include <WINGs/WUtil.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

static char tmp_path[64];

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	snprintf(tmp_path, sizeof(tmp_path), "/tmp/proplist-fuzz-%d.plist", (int)getpid());
	return 0;
}

static int write_tmp(const uint8_t *data, size_t size)
{
	FILE *f = fopen(tmp_path, "wb");

	if (!f)
		return -1;
	if (size && fwrite(data, 1, size, f) != size) {
		fclose(f);
		return -1;
	}
	if (fclose(f) != 0)
		return -1;
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	WMPropList *pl, *pl2;
	char *desc, *desc2;
	char *copy;

	if (size == 0)
		return 0;

	/* WMCreatePropListFromDescription takes a NUL-terminated string */
	copy = malloc(size + 1);
	if (!copy)
		return 0;
	memcpy(copy, data, size);
	copy[size] = '\0';

	pl = WMCreatePropListFromDescription(copy);
	if (pl) {
		desc = WMGetPropListDescription(pl, false);
		if (desc) {
			/* re-parse the written form to drive the reader with
			 * the writer's output */
			pl2 = WMCreatePropListFromDescription(desc);
			if (pl2) {
				desc2 = WMGetPropListDescription(pl2, false);
				if (desc2)
					free(desc2);
				WMReleasePropList(pl2);
			}
			free(desc);
		}
		WMReleasePropList(pl);
	}

	if (write_tmp(data, size) == 0) {
		pl = WMReadPropListFromFile(tmp_path);
		if (pl)
			WMReleasePropList(pl);
	}

	free(copy);
	return 0;
}
