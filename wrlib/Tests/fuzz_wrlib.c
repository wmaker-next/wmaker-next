/* fuzz_wrlib - libFuzzer harness for the libwraster decoders
 *
 * Pure public-API driver: the first input byte selects a magic prefix
 * (or none) that is prepended to the payload before it is written to a
 * temp file, so each format is reachable regardless of what the payload
 * itself sniffs as, and RLoadImageAtSize() runs the shipped dispatch.
 * Bytes 1-2 carry a small format parameter (multi-frame GIF/TIFF frame
 * index, render-size hint). A successful decode is also pushed through
 * RSmoothScaleImage so the resize pipeline is exercised on attacker
 * shaped images. The zeroed fake context is a complete stand-in for a
 * real connection: the decoders only read attribs->flags (PNG gamma)
 * and treat xc == NULL as "no server" (XPM colour names).
 */

#include <config.h>

#include <wraster.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

enum {
	MAGIC_NONE = 0,		/* stb probes: BMP, TGA, GIF, PSD, PIC */
	MAGIC_PNG,
	MAGIC_GIF,
	MAGIC_BMP,
	MAGIC_JPEG,
	MAGIC_TIFF,
	MAGIC_WEBP,
	MAGIC_JXL,
	MAGIC_XPM,
	MAGIC_PPM,
	MAGIC_HEIF,		/* ISO-BMFF ftyp box, AVIF primary brand */
	MAGIC_XPMDATA		/* RGetImageFromXPMData on in-memory lines */
};

static const char *const magics[] = {
	[MAGIC_NONE] = NULL,
	[MAGIC_PNG] = "\x89PNG\r\n\x1a\n",
	[MAGIC_GIF] = "GIF89a",
	[MAGIC_BMP] = "BM",
	[MAGIC_JPEG] = "\xff\xd8\xff",
	[MAGIC_TIFF] = "II*\0",
	[MAGIC_WEBP] = "RIFF\0\0\0\0WEBP",
	[MAGIC_JXL] = "\xff\x0a",
	[MAGIC_XPM] = "/* XPM */\nstatic char *x[] = {\n",
	[MAGIC_PPM] = "P6\n8 8\n255\n",
	[MAGIC_HEIF] = "\0\0\0\x18ftypavif",
	[MAGIC_XPMDATA] = NULL
};

static char tmp_path[64];

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void) argc;
	(void) argv;
	snprintf(tmp_path, sizeof(tmp_path), "/tmp/wrlib-fuzz-%d.bin", (int)getpid());
	return 0;
}

/* RLoadImageAtSize and RGetImageFromXPMData are annotated
 * nonnull(context) but run headless with NULL (see check_load.c);
 * suppress the compile-time warning for these wrappers only. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull"
#if defined(__GNUC__) || defined(__clang__)
__attribute__((no_sanitize("nonnull-attribute")))
#endif
static RImage *load_headless(const char *path, int index, unsigned w, unsigned h)
{
	return RLoadImageAtSize(NULL, path, index, w, h);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((no_sanitize("nonnull-attribute")))
#endif
static RImage *decode_xpmdata(const uint8_t *data, size_t size)
{
	char *copy;
	char **lines;
	size_t i, nlines = 1;
	RImage *img;

	copy = malloc(size + 1);
	if (!copy)
		return NULL;
	memcpy(copy, data, size);
	copy[size] = '\0';

	for (i = 0; i < size; i++)
		if (data[i] == '\n')
			nlines++;

	if (nlines > 4096)
		nlines = 4096;
	lines = malloc((nlines + 1) * sizeof(char *));
	if (!lines) {
		free(copy);
		return NULL;
	}

	{
		char *p = copy;
		size_t n = 0;

		while (n < nlines) {
			lines[n++] = p;
			p = strchr(p, '\n');
			if (!p)
				break;
			*p = '\0';
			p++;
		}
		lines[n] = NULL;
	}

	img = RGetImageFromXPMData(NULL, lines);
	free(lines);
	free(copy);
	return img;
}
#pragma GCC diagnostic pop

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	int sel, index;
	unsigned w, h;
	RImage *img;
	size_t payload;
	uint8_t *buf;

	if (size < 4)
		return 0;

	sel = data[0] % (MAGIC_XPMDATA + 1);
	index = (int)(data[1] & 0x03);
	w = 1u << (data[2] & 0x0f);	/* 1..32768 render-size hint */
	h = 1u << (data[3] & 0x0f);
	payload = size - 4;

	if (sel == MAGIC_XPMDATA) {
		img = decode_xpmdata(data + 4, payload);
	} else {
		const char *magic = magics[sel];
		size_t mlen = magic ? strlen(magic) : 0;
		FILE *f;

		buf = malloc(mlen + payload);
		if (!buf)
			return 0;
		if (mlen)
			memcpy(buf, magic, mlen);
		memcpy(buf + mlen, data + 4, payload);

		f = fopen(tmp_path, "wb");
		if (!f) {
			free(buf);
			return 0;
		}
		if (mlen + payload && fwrite(buf, 1, mlen + payload, f) != mlen + payload) {
			fclose(f);
			free(buf);
			return 0;
		}
		fclose(f);
		free(buf);

		img = load_headless(tmp_path, index, w, h);
	}

	if (img) {
		/* every decoded image is a scaling input somewhere in the
		 * callers; run the resize path on the result too */
		RImage *scaled = RSmoothScaleImage(img, 17, 9);

		if (scaled)
			RReleaseImage(scaled);
		RReleaseImage(img);
	}
	return 0;
}
