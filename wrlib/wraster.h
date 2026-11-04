/*
 * Raster graphics library
 *
 * Copyright (c) 1997-2003 Alfredo K. Kojima
 * Copyright (c) 2012-2026 Window Maker Team
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

/*
 * Environment variables:
 *
 * WRASTER_GAMMA <rgamma>/<ggamma>/<bgamma>
 * gamma correction value used by loaders that support file gamma metadata.
 * Must be greater than 0.
 *
 * Default:
 * WRASTER_GAMMA 1/1/1
 *
 *
 * If you want a specific value for a screen, append the screen number
 * preceded by a hash to the variable name as in
 * WRASTER_GAMMA#1
 * for screen number 1
 */

#ifndef RLRASTER_H_
#define RLRASTER_H_


/* version of the header for the library */
#define WRASTER_HEADER_VERSION	28


#include <xcb/xcb.h>

/* forward-declared so this header does not pull in <xcb/xcb_image.h> */
typedef struct xcb_image_t xcb_image_t;


/*
 * Define some macro to provide attributes for the compiler
 *
 * These attributes help producing better code and/or detecting bugs, however not all compiler support them
 * as they are not standard.
 * Because we're a public API header, we can't count on autoconf to detect them for us, so we use that #if
 * mechanism and define an internal macro appropriately. Please note that the macro are not considered being
 * part of the public API.
 */
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5)
#define __wrlib_deprecated(msg)  __attribute__ ((deprecated(msg)))
#elif __GNUC__ >= 3
#define __wrlib_deprecated(msg)  __attribute__ ((deprecated))
#else
#define __wrlib_deprecated(msg)
#endif

#if __GNUC__ >= 3
/* Apparently introduced in GCC 3.0.x */
/* Function returns newly allocated memory that does not alias any existing variable */
#define __wrlib_nonalias         __attribute__ ((malloc))
#else
#define __wrlib_nonalias
#endif

#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 3)
/* Apparently introduced in GCC 3.3.x */
/* The argument at index #i must not be NULL */
#define __wrlib_nonnull(...)     __attribute__ ((nonnull(__VA_ARGS__)))
#else
#define __wrlib_nonnull(argidx)
#endif

#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)
/* Apparently introduced in GCC 3.4.x */
/* The return value of this function *must* be checked/used */
#define __wrlib_useresult        __attribute__ ((warn_unused_result))
#else
#define __wrlib_useresult
#endif


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* do gamma correction */
#define RC_GammaCorrection	(1<<2)

/* visual id to use */
#define RC_VisualID		(1<<3)

/* shared memory usage */
#define RC_UseSharedMemory	(1<<4)

/* use default instead of best visual */
#define RC_DefaultVisual	(1<<5)

/* filter type for smoothed scaling */
#define RC_ScalingFilter	(1<<6)


/* smoothed scaling filter types */
typedef enum {
	RBoxFilter,
	RTriangleFilter,
	RBellFilter,
	RBSplineFilter,
	RLanczos3Filter,
	RMitchellFilter,
	RCatmullRomFilter
} RScalingFilter;


typedef struct RContextAttributes {
    int flags;
    float rgamma;		       /* gamma correction for red, */
    float ggamma;		       /* green, */
    float bgamma;		       /* and blue */
    xcb_visualid_t visualid;	       /* visual ID to use */
    int use_shared_memory;	       /* true or false */
    RScalingFilter scaling_filter;
} RContextAttributes;


/*
 * describes a screen in terms of depth and visual. Only TrueColor visuals
 * are supported, indexed-colour displays are no longer handled.
 *
 * cmap/drawable are xcb XIDs.
 */
typedef struct RContext {
    int screen_number;
    xcb_colormap_t cmap;

    RContextAttributes *attribs;

    int depth;
    xcb_drawable_t drawable;               /* window to pass for XCreatePixmap().*/
    /* generally = root */
    int vclass;

    unsigned long black;
    unsigned long white;

    int red_offset;
    int green_offset;
    int blue_offset;

    xcb_connection_t *xc;
    xcb_screen_t *xscreen;
    xcb_visualtype_t *xvisual;
    xcb_gcontext_t copy_xgc;
    uint8_t xdepth;

    /* depth-32 TrueColor visual and a colormap created against it, for
     * callers that want per-pixel alpha (translucent OSD, wireframe fill,
     * switch panel). NULL/0 when the server offers no such visual, in
     * which case the caller stays on the opaque path. */
    xcb_visualtype_t *xvisual_argb;
    xcb_colormap_t cmap_argb;

    struct {
        unsigned int use_shared_pixmap:1;
        unsigned int optimize_for_speed:1
            __wrlib_deprecated("Flag optimize_for_speed in RContext is not used anymore "
                               "and will be removed in future version, please do not use");
    } flags;

    /* Private: persistent MIT-SHM segment reused across RCreateXImage
     * calls so each conversion does not pay an attach/sync/detach cycle.
     * shm_size is the segment byte size, shm_used is the next free
     * offset inside it for the bump allocator in RCreateXImage. */
    uint32_t shm_xseg;             /* xcb_shm_seg_t, 0 when unattached */
    int shm_id;
    uint8_t *shm_addr;
    size_t shm_size;
    size_t shm_used;
} RContext;


typedef struct RColor {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char alpha;
} RColor;


typedef struct RHSVColor {
    unsigned short hue;	       /* 0-359 */
    unsigned char saturation;      /* 0-255 */
    unsigned char value;	       /* 0-255 */
} RHSVColor;



typedef struct RPoint {
    int x, y;
} RPoint;


typedef struct RSegment {
    int x1, y1, x2, y2;
} RSegment;



/* image formats */
enum RImageFormat {
    RRGBFormat,
    RRGBAFormat
};


/*
 * internal 24bit+alpha image representation
 */
typedef struct RImage {
    unsigned char *data;       /* image data RGBA or RGB */
    int width, height;	   /* size of the image */
    enum RImageFormat format;
    RColor background;	   /* background color */
    int refCount;
    /* Private: premultiplied ARGB32 cairo_image_surface_t snapshot,
     * created lazily on the first RImageCairoSurface() call and freed
     * from RReleaseImage(). Not updated by the in-place mutators. */
    void *cairo_surface;
} RImage;


/*
 * internal wrapper for the wire image. Used for shm abstraction.
 */
typedef struct RXImage {
    xcb_image_t *ximg;

    /* Private data. Do not access */
    size_t shm_offset;
    char is_shared;
} RXImage;


/* note that not all operations are supported in all functions */
typedef enum {
    RClearOperation,	       /* clear with 0 */
    RCopyOperation,
    RNormalOperation,	       /* same as combine */
    RAddOperation,
    RSubtractOperation
} RPixelOperation;


typedef enum {
    RAbsoluteCoordinates = 0,
    RRelativeCoordinates = 1
} RCoordinatesMode;


enum {
    RSunkenBevel	= -1,
    RNoBevel	= 0,
    RRaisedBevel	= 1
};
/* bw compat */
#define RBEV_SUNKEN	RSunkenBevel
/* 1 pixel wide */
#define RBEV_RAISED	RRaisedBevel
/* 1 pixel wide on top/left 2 on bottom/right */
#define RBEV_RAISED2	2
/* 2 pixel width */
#define RBEV_RAISED3	3

typedef enum {
    RHorizontalGradient = 2,
    RVerticalGradient = 3,
    RDiagonalGradient = 4
} RGradientStyle;
/* for backwards compatibility */
#define RGRD_HORIZONTAL  RHorizontalGradient
#define RGRD_VERTICAL 	RVerticalGradient
#define RGRD_DIAGONAL	RDiagonalGradient


/*
 * Cairo bridge. Forward-declared so wraster.h does not force cairo.h
 * on every includer, callers that use the return value already have it.
 */
typedef struct _cairo_surface cairo_surface_t;
typedef struct _cairo_pattern cairo_pattern_t;

/* CAIRO_FORMAT_ARGB32 image surface holding a premultiplied copy of
 * the RImage pixels. The surface is created on the first call and
 * cached on the RImage until RReleaseImage() (or an explicit
 * RImageDropCairoSurface()), so repeat calls are free. It is a
 * snapshot: RCombineImages/RClearImage/etc do not update it. The
 * caller receives an added reference and releases it with
 * cairo_surface_destroy(). */
cairo_surface_t *RImageCairoSurface(RImage *image);

/* Drop the cached surface so the next RImageCairoSurface() re-reads
 * image->data, for callers that mutate the image in place between
 * paints. */
void RImageDropCairoSurface(RImage *image);

/* cairo linear pattern with the same geometry as RRenderGradient(),
 * width/height give the diagonal slope, caller owns the reference. */
cairo_pattern_t *RGradientCairoPattern(RGradientStyle style,
                                       unsigned width, unsigned height,
                                       const RColor *from, const RColor *to);

/* Multi-stop variant matching RRenderMultiGradient(), colours is a
 * NULL-terminated array spread evenly from 0 to 1. */
cairo_pattern_t *RMultiGradientCairoPattern(RGradientStyle style,
                                            unsigned width, unsigned height,
                                            RColor **colors);


/*
 * How an image can be flipped, for RFlipImage
 *
 * Values are actually bit-mask which can be OR'd
 */
#define RHorizontalFlip	0x0001
#define RVerticalFlip	0x0002


/* error codes */
#define RERR_NONE		0
#define RERR_OPEN	 	1      /* can't open file */
#define RERR_READ		2      /* error reading from file */
#define RERR_WRITE		3      /* error writing to file */
#define RERR_NOMEMORY		4      /* out of memory */
#define RERR_BADIMAGEFILE	6      /* image file is corrupted or invalid */
#define RERR_BADFORMAT		7      /* image file format is unknown */
#define RERR_BADINDEX		8      /* no such image index in file */

#define RERR_BADVISUALID	16     /* invalid visual ID requested for context */
#define RERR_BADVISUALCLASS	17     /* visual is not TrueColor */

#define RERR_XERROR		127    /* internal X error */
#define RERR_INTERNAL		128    /* should not happen */


/*
 * Cleaning before application exit
 */
void RShutdown(void);

/*
 * Drop every cached decoded image (see RLoadImage) so a later load
 * re-reads and re-decodes the files. Callers use it when the set of
 * icon/theme files changes underneath, e.g. on a config reload.
 */
void RReleaseCache(void);

/*
 * Returns a NULL terminated array of strings containing the
 * supported formats, such as: TIFF, XPM, PNG, JPEG, PPM, GIF
 * Do not free the returned data.
 */
char **RSupportedFileFormats(void)
	__wrlib_useresult;


char *RGetImageFileFormat(const char *file)
	__wrlib_useresult __wrlib_nonnull(1);

/*
 * Context. The connection comes from xcb_connect().
 */
RContext *RCreateContextXCB(xcb_connection_t *xc, int screen_number,
                            const RContextAttributes *attribs)
	__wrlib_useresult __wrlib_nonalias __wrlib_nonnull(1);

void RDestroyContext(RContext *context);

int RGetClosestXColor(RContext *context, const RColor *color, uint32_t *pixel)
	__wrlib_nonnull(1, 2, 3);

/*
 * RImage creation
 */
RImage *RCreateImage(unsigned width, unsigned height, int alpha)
	__wrlib_useresult __wrlib_nonalias;

RImage *RCreateImageFromRXImage(RContext *context, RXImage *image, RXImage *mask)
	__wrlib_useresult __wrlib_nonalias __wrlib_nonnull(1, 2);

RImage *RCreateImageFromDrawable(RContext *context, xcb_drawable_t drawable,
                                 xcb_pixmap_t mask)
	__wrlib_useresult __wrlib_nonalias __wrlib_nonnull(1);

RImage *RLoadImage(RContext *context, const char *file, int index)
	__wrlib_useresult __wrlib_nonalias __wrlib_nonnull(1, 2);

/* As RLoadImage but with a target render size for vector formats
 * (SVG). Raster loaders ignore width/height. 0,0 asks for the file's
 * intrinsic size (clamped to a sane default when the file declares
 * none), and RLoadImage() is a (..., 0, 0) shim. */
RImage *RLoadImageAtSize(RContext *context, const char *file, int index,
                         unsigned width, unsigned height)
	__wrlib_useresult __wrlib_nonalias __wrlib_nonnull(1, 2);

#ifdef USE_SVG
RImage *RLoadSVGFromData(const char *data, size_t size,
			 unsigned int width, unsigned int height)
	__wrlib_useresult __wrlib_nonalias __wrlib_nonnull(1);
#endif

RImage* RRetainImage(RImage *image);

void RReleaseImage(RImage *image)
	__wrlib_nonnull(1);

RImage *RGetImageFromXPMData(RContext *context, char **xpmData)
	__wrlib_useresult __wrlib_nonalias __wrlib_nonnull(1, 2);

/*
 * RImage storing
 */
int RSaveImage(RImage *image, const char *filename, const char *format)
	__wrlib_nonnull(1, 2, 3);

int RSaveRawImage(RImage *image, const char *format, unsigned char **out_buf, size_t *out_size)
	__wrlib_nonnull(1, 2, 3, 4);

int RSaveTitledImage(RImage *image, const char *filename, const char *format, char *title)
	__wrlib_nonnull(1, 2, 3);

/*
 * Area manipulation
 */
RImage *RCloneImage(RImage *image)
	__wrlib_useresult __wrlib_nonalias __wrlib_nonnull(1);

RImage *RGetSubImage(RImage *image, int x, int y, unsigned width,
                     unsigned height)
	__wrlib_useresult __wrlib_nonalias __wrlib_nonnull(1);

void RCombineImageWithColor(RImage *image, const RColor *color)
	__wrlib_nonnull(1, 2);

void RCombineImages(RImage *image, RImage *src)
	__wrlib_nonnull(1, 2);

void RCombineArea(RImage *image, RImage *src, int sx, int sy, unsigned width,
                  unsigned height, int dx, int dy)
	__wrlib_nonnull(1, 2);

void RCopyArea(RImage *image, RImage *src, int sx, int sy, unsigned width,
               unsigned height, int dx, int dy)
	__wrlib_nonnull(1, 2);

void RCombineImagesWithOpaqueness(RImage *image, RImage *src, int opaqueness)
	__wrlib_nonnull(1, 2);

void RCombineAreaWithOpaqueness(RImage *image, RImage *src, int sx, int sy,
                                unsigned width, unsigned height, int dx, int dy,
                                int opaqueness)
	__wrlib_nonnull(1, 2);

void RCombineAlpha(unsigned char *d, unsigned char *s, int s_has_alpha,
		   int width, int height, int dwi, int swi, int opacity)
	__wrlib_nonnull(1, 2);

RImage *RScaleImage(RImage *image, unsigned new_width, unsigned new_height)
	__wrlib_useresult __wrlib_nonalias;

RImage *RSmoothScaleImage(RImage *src, unsigned new_width,
                          unsigned new_height)
	__wrlib_useresult __wrlib_nonalias __wrlib_nonnull(1);

RImage *RRotateImage(RImage *image, float angle)
	__wrlib_useresult __wrlib_nonalias __wrlib_nonnull(1);

RImage *RFlipImage(RImage *image, int mode)
	__wrlib_useresult __wrlib_nonalias;

RImage *RMakeTiledImage(RImage *tile, unsigned width, unsigned height)
	__wrlib_useresult __wrlib_nonalias __wrlib_nonnull(1);

RImage* RMakeCenteredImage(RImage *image, unsigned width, unsigned height,
                           const RColor *color)
	__wrlib_useresult __wrlib_nonalias __wrlib_nonnull(1, 4);

/*
 * Drawing
 */
int RGetPixel(RImage *image, int x, int y, RColor *color)
	__wrlib_nonnull(1, 4);

void RPutPixel(RImage *image, int x, int y, const RColor *color)
	__wrlib_nonnull(1, 4);

void ROperatePixel(RImage *image, RPixelOperation operation, int x, int y, const RColor *color)
	__wrlib_nonnull(1, 5);

void RPutPixels(RImage *image, const RPoint *points, int npoints, RCoordinatesMode mode,
                const RColor *color)
	__wrlib_nonnull(1, 2, 5);

void ROperatePixels(RImage *image, RPixelOperation operation, const RPoint *points,
                    int npoints, RCoordinatesMode mode, const RColor *color)
	__wrlib_nonnull(1, 3, 6);

int RDrawLine(RImage *image, int x0, int y0, int x1, int y1, const RColor *color)
	__wrlib_nonnull(1, 6);

int ROperateLine(RImage *image, RPixelOperation operation, int x0, int y0, int x1, int y1,
                 const RColor *color)
	__wrlib_nonnull(1, 7);

void RDrawLines(RImage *image, const RPoint *points, int npoints, RCoordinatesMode mode,
                const RColor *color)
	__wrlib_nonnull(1, 2, 5);

void ROperateLines(RImage *image, RPixelOperation operation, const RPoint *points, int npoints,
                   RCoordinatesMode mode, const RColor *color)
	__wrlib_nonnull(1, 3, 6);

void ROperateRectangle(RImage *image, RPixelOperation operation, int x0, int y0, int x1, int y1, const RColor *color)
	__wrlib_nonnull(1, 7);

void RDrawSegments(RImage *image, const RSegment *segs, int nsegs, const RColor *color)
	__wrlib_nonnull(1, 2, 4);

void ROperateSegments(RImage *image, RPixelOperation operation, const RSegment *segs, int nsegs,
                      const RColor *color)
	__wrlib_nonnull(1, 3, 5);

/*
 * Color convertion
 */
void RRGBtoHSV(const RColor *color, RHSVColor *hsv)
	__wrlib_nonnull(1, 2);

void RHSVtoRGB(const RHSVColor *hsv, RColor *rgb)
	__wrlib_nonnull(1, 2);

/*
 * Painting
 */
void RClearImage(RImage *image, const RColor *color)
	__wrlib_nonnull(1, 2);

void RLightImage(RImage *image, const RColor *color)
	__wrlib_nonnull(1, 2);

void RFillImage(RImage *image, const RColor *color)
	__wrlib_nonnull(1, 2);

void RBevelImage(RImage *image, int bevel_type)
	__wrlib_nonnull(1);

RImage *RRenderGradient(unsigned width, unsigned height, const RColor *from,
                        const RColor *to, RGradientStyle style)
	__wrlib_nonalias __wrlib_useresult __wrlib_nonnull(3, 4);


RImage *RRenderMultiGradient(unsigned width, unsigned height, RColor **colors,
                             RGradientStyle style)
	__wrlib_nonalias __wrlib_useresult __wrlib_nonnull(3);


RImage *RRenderInterwovenGradient(unsigned width, unsigned height,
                                  RColor colors1[2], int thickness1,
                                  RColor colors2[2], int thickness2)
	__wrlib_nonalias __wrlib_useresult;


/*
 * Convertion into X Pixmaps
 */
int RConvertImage(RContext *context, RImage *image, xcb_pixmap_t *pixmap)
	__wrlib_nonnull(1, 2, 3);

int RConvertImageMask(RContext *context, RImage *image, xcb_pixmap_t *pixmap,
                      xcb_pixmap_t *mask, int threshold)
	__wrlib_nonnull(1, 2, 3, 4);


/*
 * misc. utilities
 */
RXImage *RCreateXImage(RContext *context, int depth,
                       unsigned width, unsigned height)
	__wrlib_nonalias __wrlib_useresult __wrlib_nonnull(1);

RXImage *RGetXImage(RContext *context, xcb_drawable_t d, int x, int y,
                    unsigned width, unsigned height)
	__wrlib_nonalias __wrlib_useresult __wrlib_nonnull(1);

void RDestroyXImage(RContext *context, RXImage *ximage)
	__wrlib_nonnull(1, 2);

void RPutXImage(RContext *context, xcb_drawable_t d, xcb_gcontext_t gc, RXImage *ximage,
                int src_x, int src_y, int dest_x, int dest_y,
                unsigned width, unsigned height)
	__wrlib_nonnull(1);

/* do not free the returned string! */
const char *RMessageForError(int errorCode)
	__wrlib_useresult;

int RBlurImage(RImage *image)
	__wrlib_nonnull(1);

int RStackBlurImage(RImage *image, unsigned radius)
	__wrlib_nonnull(1);

void RGrayscaleImage(RImage *image)
	__wrlib_nonnull(1);

/*
 * Wallpaper-derived UI palette.
 */

#define DYNAMIC_PALETTE_SWATCHES 8

typedef struct RPaletteSwatch {
	unsigned char r, g, b;
	unsigned long weight;
} RPaletteSwatch;

typedef struct RPalette {
	RPaletteSwatch swatches[DYNAMIC_PALETTE_SWATCHES];
	int count;
} RPalette;

enum RDynamicColorMode {
	RDYNAMIC_MODE_AUTO = 0,
	RDYNAMIC_MODE_LIGHT,
	RDYNAMIC_MODE_DARK
};

enum RDynamicColorLadder {
	RDYNAMIC_LADDER_LIGHT = 0,
	RDYNAMIC_LADDER_DARK
};

enum {
	RDYNAMIC_ROLE_DOMINANT_BACK,	/* FTitleBack, MenuTitleBack */
	RDYNAMIC_ROLE_SECONDARY_BACK,	/* UTitleBack, MenuTextBack, ResizebarBack,
					 * WidgetColor, IconTitleBack, FrameBorderColor */
	RDYNAMIC_ROLE_TERTIARY_BACK,	/* PTitleBack */
	RDYNAMIC_ROLE_ICON_BACK_NEAR,	/* IconBack endpoint 1 */
	RDYNAMIC_ROLE_ICON_BACK_FAR,	/* IconBack endpoint 2 */
	RDYNAMIC_ROLE_HIGHLIGHT_BACK,	/* HighlightColor */
	RDYNAMIC_ROLE_DOMINANT_TEXT,	/* FTitleColor, MenuTitleColor, ClipTitleColor,
					 * CClipTitleColor, FrameSelectedBorderColor */
	RDYNAMIC_ROLE_SECONDARY_TEXT,	/* UTitleColor, MenuTextColor, IconTitleColor,
					 * TabInactiveTitleColor */
	RDYNAMIC_ROLE_TERTIARY_TEXT,	/* PTitleColor */
	RDYNAMIC_ROLE_MENU_DISABLED_TEXT,/* MenuDisabledColor */
	RDYNAMIC_ROLE_HIGHLIGHT_TEXT,	/* HighlightTextColor */
	RDYNAMIC_ROLE_FOCUSED_BORDER,	/* FrameFocusedBorderColor */
	RDYNAMIC_ROLE_COUNT
};

typedef struct RDynamicTheme {
	RColor roles[RDYNAMIC_ROLE_COUNT];
	int ladder;
} RDynamicTheme;

RPalette *RExtractPalette(const RImage *image)
	__wrlib_useresult __wrlib_nonalias __wrlib_nonnull(1);
RDynamicTheme *RDeriveDynamicTheme(const RImage *image, int mode)
	__wrlib_useresult __wrlib_nonalias __wrlib_nonnull(1);

/* WCAG 2.x relative luminance of an sRGB triple, in [0,1]. */
double RRelativeLuminance(unsigned char r, unsigned char g, unsigned char b);
/* WCAG 2.x contrast ratio of two relative luminances, >= 1.0. */
double RWCAGContrastRatio(double l1, double l2);
/* Black or white, whichever has the higher WCAG ratio against (r,g,b). */
RColor RPickWCAGText(unsigned char r, unsigned char g, unsigned char b);

/****** Global Variables *******/

extern int RErrorCode;

#ifdef __cplusplus
}
#endif /* __cplusplus */

/*
 * The definitions below are done for internal use only
 * We undef them so users of the library may not misuse them
 */
#undef __wrlib_deprecated
#undef __wrlib_nonalias
#undef __wrlib_nonnull
#undef __wrlib_useresult

#endif
