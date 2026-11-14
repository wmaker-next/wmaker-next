/* load.c - load image from file
 *
 * Raster graphics library
 *
 * Copyright (c) 1997-2003 Alfredo K. Kojima
 * Copyright (c) 2009-2026 Window Maker Team
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

#include <config.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "wraster.h"
#include "imgformat.h"
#include "wr_i18n.h"

/* upper bound on a file slurped into memory by the in-memory decoders,
 * matches the most restrictive existing caller */
#define RREAD_FILE_MAX ((size_t)1 << 28)

_Thread_local uint64_t RImageLoadLimit = R_IMAGE_MAX_BYTES;

unsigned char *RReadWholeFile(const char *path, size_t *out_size)
{
	FILE *f;
	long len;
	unsigned char *buf;

	*out_size = 0;

	f = fopen(path, "rb");
	if (!f) {
		RErrorCode = RERR_OPEN;
		return NULL;
	}

	if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 ||
	    fseek(f, 0, SEEK_SET) != 0) {
		RErrorCode = RERR_READ;
		fclose(f);
		return NULL;
	}
	if (len == 0 || (size_t)len > RREAD_FILE_MAX ||
	    (uint64_t)len > RImageLoadLimit) {
		RErrorCode = RERR_BADIMAGEFILE;
		fclose(f);
		return NULL;
	}

	buf = malloc((size_t)len);
	if (!buf) {
		RErrorCode = RERR_NOMEMORY;
		fclose(f);
		return NULL;
	}
	if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
		RErrorCode = RERR_READ;
		free(buf);
		fclose(f);
		return NULL;
	}
	fclose(f);

	*out_size = (size_t)len;
	return buf;
}

typedef struct RCachedImage {
	RImage *image;
	char *file;
	struct timespec last_modif;	/* nanosecond mtime from st_mtim */
	time_t last_use;	/* last time image was used */
	/* cache key: render size for vector formats, 0 for raster */
	unsigned width, height;
	/* frame index for multi-frame formats, 0 elsewhere */
	int index;
} RCachedImage;

static char *copy_cache_filename(const char *file)
{
	size_t len;
	char *copy;

	len = strlen(file) + 1;
	copy = malloc(len);
	if (!copy)
		return NULL;
	memcpy(copy, file, len);
	return copy;
}

/*
 * Number of image to keep in the cache
 */
static int RImageCacheSize = -1;

#define IMAGE_CACHE_DEFAULT_NBENTRIES	  32
#define IMAGE_CACHE_MAXIMUM_NBENTRIES	256

/*
 * Max. size of image (in pixels) to store in the cache
 */
static int RImageCacheMaxImage = -1;	/* 0 = any size */

#define IMAGE_CACHE_DEFAULT_MAXPIXELS	(256 * 256)
#define IMAGE_CACHE_MAXIMUM_MAXPIXELS	(256 * 256)


static RCachedImage *RImageCache;


static WRImgFormat identFile(const char *path);

/*
 * Single source of truth for the format-name strings returned by
 * RSupportedFileFormats() and RGetImageFileFormat(). Entries with
 * fmt == IM_UNKNOWN are listed in RSupportedFileFormats() but never
 * returned by RGetImageFileFormat(): PNM is an alias for the PPM
 * loader kept for legacy callers, and TGA has no magic-byte signature
 * so identFile() cannot detect it (it is handled by the stb fallback
 * in RLoadImage's IM_UNKNOWN path).
 */
static const struct {
	WRImgFormat fmt;
	const char *name;
} wr_format_names[] = {
	{ IM_XPM,     "XPM"  },
	{ IM_UNKNOWN, "PNM"  },
	{ IM_PPM,     "PPM"  },
	{ IM_BMP,     "BMP"  },
	{ IM_UNKNOWN, "TGA"  },
	{ IM_GIF,     "GIF"  },
#ifdef USE_TIFF
	{ IM_TIFF,    "TIFF" },
#endif
#ifdef USE_PNG
	{ IM_PNG,     "PNG"  },
#endif
#ifdef USE_JPEG
	{ IM_JPEG,    "JPEG" },
#endif
#ifdef USE_JXL
	{ IM_JXL,     "JXL"  },
#endif
#ifdef USE_WEBP
	{ IM_WEBP,    "WEBP" },
#endif
#ifdef USE_SVG
	{ IM_SVG,     "SVG"  },
#endif
#ifdef USE_HEIF
	{ IM_HEIF,    "HEIF" },
#endif
};
#define WR_FORMAT_NAME_COUNT (sizeof(wr_format_names) / sizeof(wr_format_names[0]))

char **RSupportedFileFormats(void)
{
	static char *tmp[WR_FORMAT_NAME_COUNT + 1];
	size_t i;

	for (i = 0; i < WR_FORMAT_NAME_COUNT; i++)
		tmp[i] = (char *)wr_format_names[i].name;
	tmp[i] = NULL;

	return tmp;
}

static void init_cache(void)
{
	char *tmp;

	tmp = getenv("RIMAGE_CACHE");
	if (!tmp || sscanf(tmp, "%i", &RImageCacheSize) != 1)
		RImageCacheSize = IMAGE_CACHE_DEFAULT_NBENTRIES;
	if (RImageCacheSize < 0)
		RImageCacheSize = 0;
	if (RImageCacheSize > IMAGE_CACHE_MAXIMUM_NBENTRIES)
		RImageCacheSize = IMAGE_CACHE_MAXIMUM_NBENTRIES;

	tmp = getenv("RIMAGE_CACHE_SIZE");
	if (!tmp || sscanf(tmp, "%i", &RImageCacheMaxImage) != 1)
		RImageCacheMaxImage = IMAGE_CACHE_DEFAULT_MAXPIXELS;
	if (RImageCacheMaxImage < 0)
		RImageCacheMaxImage = 0;
	if (RImageCacheMaxImage > IMAGE_CACHE_MAXIMUM_MAXPIXELS)
		RImageCacheMaxImage = IMAGE_CACHE_MAXIMUM_MAXPIXELS;

	if (RImageCacheSize > 0) {
		RImageCache = malloc(sizeof(RCachedImage) * RImageCacheSize);
		if (RImageCache == NULL) {
			fprintf(stderr, _("wrlib: out of memory for image cache\n"));
			RImageCacheSize = 0;
			return;
		}
		memset(RImageCache, 0, sizeof(RCachedImage) * RImageCacheSize);
	}
}

void RReleaseCache(void)
{
	int i;

	if (RImageCacheSize > 0) {
		for (i = 0; i < RImageCacheSize; i++) {
			if (RImageCache[i].file) {
				RReleaseImage(RImageCache[i].image);
				free(RImageCache[i].file);
			}
		}
		free(RImageCache);
		RImageCache = NULL;
		RImageCacheSize = -1;
	}
}

RImage *RLoadImage(RContext *context, const char *file, int index)
{
	return RLoadImageAtSize(context, file, index, 0, 0);
}

RImage *RLoadImageAtSizeLimited(RContext *context, const char *file, int index,
				unsigned int width, unsigned int height, size_t max_bytes)
{
	uint64_t saved = RImageLoadLimit;
	RImage *image;

	if (max_bytes < RImageLoadLimit)
		RImageLoadLimit = max_bytes;
	image = RLoadImageAtSize(context, file, index, width, height);
	RImageLoadLimit = saved;
	return image;
}

RImage *RLoadImageAtSize(RContext *context, const char *file, int index,
			 unsigned width, unsigned height)
{
	RImage *image = NULL;
	int i;
	struct stat st;
	WRImgFormat fmt;
	unsigned key_w = 0, key_h = 0;

#ifndef USE_SVG
	(void)width;
	(void)height;
#endif

	assert(file != NULL);

	if (RImageCacheSize < 0)
		init_cache();

	fmt = identFile(file);
	if (fmt == IM_ERROR) {
		/* the file is gone, drop anything cached for it */
		if (RImageCacheSize > 0 && RImageCache) {
			for (i = 0; i < RImageCacheSize; i++) {
				if (RImageCache[i].file
				    && strcmp(file, RImageCache[i].file) == 0) {
					free(RImageCache[i].file);
					RImageCache[i].file = NULL;
					RReleaseImage(RImageCache[i].image);
				}
			}
		}
		return NULL;
	}

#ifdef USE_SVG
	/* only SVG consumes the render size */
	if (fmt == IM_SVG) {
		key_w = width;
		key_h = height;
	}
#endif

	if (RImageCacheSize > 0 && RImageCache) {

		for (i = 0; i < RImageCacheSize; i++) {
			if (RImageCache[i].file && strcmp(file, RImageCache[i].file) == 0
			    && RImageCache[i].width == key_w && RImageCache[i].height == key_h
			    && RImageCache[i].index == index) {

				if (stat(file, &st) == 0
				    && st.st_mtim.tv_sec == RImageCache[i].last_modif.tv_sec
				    && st.st_mtim.tv_nsec == RImageCache[i].last_modif.tv_nsec) {
					RImageCache[i].last_use = time(NULL);

					return RCloneImage(RImageCache[i].image);

				} else {
					free(RImageCache[i].file);
					RImageCache[i].file = NULL;
					RReleaseImage(RImageCache[i].image);
				}
			}
		}
	}

	switch (fmt) {

	case IM_UNKNOWN:
		/* TGA, PSD and PIC have no usable magic for identFile(),
		 * stb_image probes the structure itself. ImageMagick stays
		 * as the last resort for PCX, PICT etc. */
		image = RLoadStb(file);
#ifdef USE_MAGICK
		if (!image)
			image = RLoadMagick(file);
#endif
		break;

	case IM_BMP:
		image = RLoadStb(file);
		break;

	case IM_GIF:
		image = RLoadStbGIF(file, index);
		break;

	case IM_XPM:
		image = RLoadXPM(context, file);
		break;

#ifdef USE_TIFF
	case IM_TIFF:
		image = RLoadTIFF(file, index);
		break;
#endif				/* USE_TIFF */

#ifdef USE_PNG
	case IM_PNG:
		image = RLoadPNG(context, file);
		break;
#endif				/* USE_PNG */

#ifdef USE_JPEG
	case IM_JPEG:
		image = RLoadJPEG(file);
		break;
#endif				/* USE_JPEG */

#ifdef USE_JXL
	case IM_JXL:
		image = RLoadJXL(file);
		break;
#endif				/* USE_JXL */

#ifdef USE_WEBP
	case IM_WEBP:
		image = RLoadWEBP(file);
		break;
#endif				/* USE_WEBP */

#ifdef USE_SVG
	case IM_SVG:
		image = RLoadSVG(file, width, height);
		break;
#endif				/* USE_SVG */

#ifdef USE_HEIF
	case IM_HEIF:
		image = RLoadHEIF(file, index);
		break;
#endif				/* USE_HEIF */

	case IM_PPM:
		image = RLoadPPM(file);
		break;

	default:
		RErrorCode = RERR_BADFORMAT;
		return NULL;
	}

	/* store image in cache */
	if (RImageCacheSize > 0 && RImageCache && image &&
	    (RImageCacheMaxImage == 0 || RImageCacheMaxImage >= image->width * image->height)) {
		time_t oldest = time(NULL);
		int oldest_idx = 0;
		int done = 0;

		if (stat(file, &st) != 0) {
			/* If we can't get the info, at least use a valid time to reduce risk of problems */
			st.st_mtim.tv_sec = oldest;
			st.st_mtim.tv_nsec = 0;
		}

		for (i = 0; i < RImageCacheSize; i++) {
			if (!RImageCache[i].file) {
				RImage *cached = RCloneImage(image);
				char *cache_file;

				if (!cached)
					return image;
				cache_file = copy_cache_filename(file);
				if (!cache_file) {
					RReleaseImage(cached);
					return image;
				}
				RImageCache[i].file = cache_file;
				RImageCache[i].image = cached;
				RImageCache[i].last_modif = st.st_mtim;
				RImageCache[i].last_use = time(NULL);
				RImageCache[i].width = key_w;
				RImageCache[i].height = key_h;
				RImageCache[i].index = index;
				done = 1;
				break;
			} else {
				if (oldest > RImageCache[i].last_use) {
					oldest = RImageCache[i].last_use;
					oldest_idx = i;
				}
			}
		}

		/* if no slot available, dump least recently used one */
		if (!done) {
			RImage *cached = RCloneImage(image);
			char *cache_file;

			if (!cached)
				return image;
			cache_file = copy_cache_filename(file);
			if (!cache_file) {
				RReleaseImage(cached);
				return image;
			}
			free(RImageCache[oldest_idx].file);
			RReleaseImage(RImageCache[oldest_idx].image);
			RImageCache[oldest_idx].file = cache_file;
			RImageCache[oldest_idx].image = cached;
			RImageCache[oldest_idx].last_modif = st.st_mtim;
			RImageCache[oldest_idx].last_use = time(NULL);
			RImageCache[oldest_idx].width = key_w;
			RImageCache[oldest_idx].height = key_h;
			RImageCache[oldest_idx].index = index;
		}
	}

	return image;
}

char *RGetImageFileFormat(const char *file)
{
	WRImgFormat fmt;
	size_t i;

	fmt = identFile(file);
	if (fmt == IM_ERROR || fmt == IM_UNKNOWN)
		return NULL;

	for (i = 0; i < WR_FORMAT_NAME_COUNT; i++)
		if (wr_format_names[i].fmt == fmt)
			return (char *)wr_format_names[i].name;

	return NULL;
}

static WRImgFormat identFile(const char *path)
{
	FILE *file;
	unsigned char buffer[32];
	const unsigned char *p;
	size_t nread, rem;

	assert(path != NULL);

	for (;;) {
		file = fopen(path, "rb");
		if (file != NULL)
			break;
		if (errno != EINTR) {
			RErrorCode = RERR_OPEN;
			return IM_ERROR;
		}
	}

	/* zero so the magic checks below see a definite mismatch past
	 * end-of-file instead of stack garbage, then accept any non-empty
	 * read so small but valid files (a tiny PNM is well under 32 bytes)
	 * reach the per-format checks instead of being rejected here */
	memset(buffer, 0, sizeof(buffer));
	nread = fread(buffer, 1, sizeof(buffer), file);
	if (nread < 2 || ferror(file)) {
		fclose(file);
		RErrorCode = RERR_READ;
		return IM_ERROR;
	}
	fclose(file);

	/* check for XPM */
	if (strncmp((char *)buffer, "/* XPM */", 9) == 0)
		return IM_XPM;

	/* check for TIFF */
	if ((buffer[0] == 'I' && buffer[1] == 'I' && buffer[2] == '*' && buffer[3] == 0)
	    || (buffer[0] == 'M' && buffer[1] == 'M' && buffer[2] == 0 && buffer[3] == '*'))
		return IM_TIFF;

	/*
	 * check for PNG
	 *
	 * The signature is defined in the PNG specifiation:
	 * http://www.libpng.org/pub/png/spec/1.2/PNG-Structure.html
	 * it is valid for v1.0, v1.1, v1.2 and ISO version
	 */
	if (buffer[0] == 137 && buffer[1] == 80 && buffer[2] == 78 && buffer[3] == 71 &&
	    buffer[4] ==  13 && buffer[5] == 10 && buffer[6] == 26 && buffer[7] == 10)
		return IM_PNG;

	/* check for PBM or PGM or PPM */
	if (buffer[0] == 'P' && (buffer[1] > '0' && buffer[1] < '7') && (buffer[2] == 0x0a || buffer[2] == 0x20 || buffer[2] == 0x09 || buffer[2] == 0x0d))
		return IM_PPM;

	/* check for JPEG */
	if (buffer[0] == 0xff && buffer[1] == 0xd8)
		return IM_JPEG;

	/* check for JXL */
	if ((buffer[0] == 0xff && buffer[1] == 0x0a) ||  /* naked codestream */
	    (buffer[0] == 0x00 && buffer[1] == 0x00 && buffer[2] == 0x00 && buffer[3] == 0x0c &&  /* container format */
	     buffer[4] == 0x4a && buffer[5] == 0x58 && buffer[6] == 0x4c && buffer[7] == 0x20 &&
	     buffer[8] == 0x0d && buffer[9] == 0x0a && buffer[10] == 0x87 && buffer[11] == 0x0a))
		return IM_JXL;

	/* check for GIF */
	if (buffer[0] == 'G' && buffer[1] == 'I' && buffer[2] == 'F' && buffer[3] == '8' &&
	    (buffer[4] == '7' ||  buffer[4] == '9') && buffer[5] == 'a')
		return IM_GIF;

	/* check for BMP (DIB, OS/2 v1/v2, Windows v3+) */
	if (buffer[0] == 'B' && buffer[1] == 'M')
		return IM_BMP;

	/* check for SVG: after an optional UTF-8 BOM, either an XML
	 * prolog (the <svg root may sit past this 32-byte prefix, so
	 * accept on the prolog alone and let the loader reject non-SVG
	 * XML) or a bare <svg root */
	p = buffer;
	rem = nread;
	if (rem >= 3 && p[0] == 0xef && p[1] == 0xbb && p[2] == 0xbf) {
		p += 3;
		rem -= 3;
	}
	while (rem > 0 && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
		p++;
		rem--;
	}
	if ((rem >= 5 && memcmp(p, "<?xml", 5) == 0) ||
	    (rem >= 4 && memcmp(p, "<svg", 4) == 0))
		return IM_SVG;

	/* check for WEBP */
	if (buffer[0]  == 'R' && buffer[1]  == 'I' && buffer[2]  == 'F' && buffer[3]  == 'F' &&
	    buffer[8]  == 'W' && buffer[9]  == 'E' && buffer[10] == 'B' && buffer[11] == 'P' &&
	    buffer[12] == 'V' && buffer[13] == 'P' && buffer[14] == '8' &&
	    (buffer[15] == ' '       /* Simple File Format (Lossy) */
	     || buffer[15] == 'L'    /* Simple File Format (Lossless) */
	     || buffer[15] == 'X'))  /* Extended File Format */
		return IM_WEBP;

	/* check for HEIF (HEIC/AVIF): both are ISO-BMFF containers, the
	 * "ftyp" box carries the primary brand at offset 8-11. Only the
	 * HEIF image brands are accepted, so MP4 and other ISOBMFF files
	 * (e.g. brand "isom") stay on the IM_UNKNOWN path. The brands
	 * cover HEVC images ("heic", "heix", "hevc", "hevx", "heim",
	 * "heis", "hevm", "hevs"), AV1 images ("avif", "avis") and the
	 * generic multi-image brands ("mif1", "msf1", "mif2") that both
	 * families may carry as primary brand. */
	if (buffer[4] == 'f' && buffer[5] == 't' && buffer[6] == 'y' && buffer[7] == 'p') {
		static const char *const heif_brands[] = {
			"avif", "avis", "heic", "heix", "heim", "heis",
			"hevc", "hevx", "hevm", "hevs", "mif1", "mif2", "msf1"
		};
		size_t i;

		for (i = 0; i < sizeof(heif_brands) / sizeof(heif_brands[0]); i++) {
			if (buffer[8]  == heif_brands[i][0] &&
			    buffer[9]  == heif_brands[i][1] &&
			    buffer[10] == heif_brands[i][2] &&
			    buffer[11] == heif_brands[i][3])
				return IM_HEIF;
		}
	}

	return IM_UNKNOWN;
}
