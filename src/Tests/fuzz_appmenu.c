/* fuzz_appmenu - libFuzzer harness for the WMMenu parse core
 *
 * Feeds arbitrary bytes as the command lines of a _WINDOWMAKER_MENU
 * property (the "WMMenu 0" header stripped, exactly what wAppMenuGet
 * hands to wAppMenuParse) and drives the standalone parse/validate
 * core, freeing every produced node tree under ASan/LSan.
 */

#include "appmenuparse.h"

#include <WINGs/WUtil.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static volatile unsigned sink;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	const uint8_t *p = data, *end = data + size;
	char *lines[256];
	size_t nlines = 0, i;
	const char *bad = NULL;
	WAppMenuNode *root = NULL;

	/* the property decode yields one NUL-terminated line per
	 * newline; strndup copies keep the same contract over the
	 * read-only fuzzer buffer. 256 lines so the depth-64 nesting
	 * cap and the entry-count paths are exercisable. */
	while (p < end && nlines < sizeof(lines) / sizeof(lines[0])) {
		const uint8_t *nl = memchr(p, '\n', (size_t)(end - p));
		size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
		char *s = strndup((const char *)p, len);

		if (!s)
			break;
		lines[nlines++] = s;
		p += len + (nl ? 1 : 0);
	}

	if (nlines == 0) {
		for (i = 0; i < nlines; i++)
			free(lines[i]);
		return 0;
	}

	switch (wAppMenuParse((const char *const *)lines, (int)nlines, &bad, &root)) {
	case WAPPMENU_OK: {
		const WAppMenuNode *n;

		for (n = root->first_child; n; n = n->next) {
			sink += (unsigned)strlen(n->title);
			if (n->rtext)
				sink += (unsigned)strlen(n->rtext);
			sink += (unsigned)n->tag;
		}
		wAppMenuFreeNode(root);
		break;
	}
	case WAPPMENU_BAD_ENTRY:
		sink += bad ? (unsigned)strlen(bad) : 1;
		break;
	default:
		break;
	}

	for (i = 0; i < nlines; i++)
		free(lines[i]);

	return 0;
}
