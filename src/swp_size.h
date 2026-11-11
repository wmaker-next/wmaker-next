/*
 *  swp_size.h
 *
 *  Window Maker window manager
 *
 *  Copyright (c) 2026 Window Maker Team
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef WMSWP_SIZE_H
#define WMSWP_SIZE_H

#include <stdbool.h>

/*
 * Checked switch panel sizing arithmetic, display-free so the unit
 * tests can link it without X.
 */

/* legacy fit margin taken off the head width, in device pixels */
#define WSP_FIT_SPACING (2 * 20)

/* wrlib image dimension cap */
#define WSP_IMAGE_MAX 20000

/* X window dimension, 16 bit in the protocol */
#define WSP_XDIM_MAX 65535

/* schema bounds of SwitchPanelIconSize */
#define WSP_ICON_SIZE_MIN 1
#define WSP_ICON_SIZE_MAX 16667
#define WSP_ICON_IMAGE_SLACK 3

/* caps on the panel-controlled rasters and pixmaps, not on total
 * process memory or pre-existing shared images */
#define WSP_ALLOC_LIMIT (16LL * 1024 * 1024)
#define WSP_PEAK_LIMIT  (64LL * 1024 * 1024)

/* RGBA rasters and 24 bit X pixmaps, scanlines padded to 32 bits */
#define WSP_BYTES_PER_PIXEL 4

/* a larger UI scale is invalid input, not a device conversion failure */
#define WSP_UI_SCALE_MAX 64.0

typedef struct {
	int icon_size;      /* SwitchPanelIconSize value */
	int count;          /* windows on the strip, >= 0 */
	int border;         /* logical edge pad, >= 0 */
	int label_height;   /* logical label row, >= 1 */
	int extra_h;        /* logical extra height (label gap, drill growth) */
	long long head_w;   /* target head width, device pixels, >= 0 */
	long long head_h;   /* target head height, device pixels, >= 0 */
	double ui_scale;    /* WMScreenUIScale() */
	bool opt_in_fit;    /* false: legacy fit, true: border-aware fit */
} WSwitchPanelSizing;

typedef struct {
	int tile;             /* derived icon tile size, logical */
	int dev_tile;         /* tile in device pixels */
	int visible;          /* icons shown at once */
	int full_width;       /* count * tile, logical */
	int strip_width;      /* visible * tile, logical */
	int content_height;   /* label_height + tile + extra_h, logical */
	int panel_width;      /* strip_width + 2 * border, logical */
	int panel_height;     /* content_height + 2 * border, logical */
	int dev_panel_width;  /* panel_width in device pixels */
	int dev_panel_height; /* panel_height in device pixels */
} WSwitchPanelSize;

/* which check failed, for diagnostics that report the real reason */
typedef enum {
	WSP_ERR_NONE = 0,
	WSP_ERR_INPUT,   /* icon size, tile, counts or scale invalid */
	WSP_ERR_STRIP,   /* full strip leaves the X window dimension range */
	WSP_ERR_IMAGE,   /* a raster leaves the wrlib image dimension range */
	WSP_ERR_WIDGET   /* widget or device conversion leaves the int range */
} WSwitchPanelError;

/* rounded icon_size * 1.2 tile, 0 outside the schema or image range */
int wSwitchPanelTileSize(int icon_size);

/* WINGs VDEV conversion, logical to device pixels */
bool wSwitchPanelDevicePx(int logical, double ui_scale, int *dev);

bool wSwitchPanelIconPos(int index, int tile, int *x);

bool wSwitchPanelChildCoordOk(long long logical, double ui_scale);

/* full scrolling strip width, fails past the X dimension range */
bool wSwitchPanelStripWidth(int count, int tile, double ui_scale, int *width);

bool wSwitchPanelDrillExtra(int border, int tile, int *extra);

bool wSwitchPanelImageSizeOk(int width, int height);

bool wSwitchPanelWidgetSizeOk(int width, int height, double ui_scale);

/* visible count for the fit mode, 0 for an empty list, -1 on invalid
 * input, never 0 for a nonempty list */
int wSwitchPanelVisibleCount(const WSwitchPanelSizing *in);

/* panel dimensions for a fixed visible count, 'err' gets the failing
 * check when it is not NULL */
int wSwitchPanelCalcDims(const WSwitchPanelSizing *in, int visible,
			 WSwitchPanelSize *out, WSwitchPanelError *err);

int wSwitchPanelCalcSize(const WSwitchPanelSizing *in, WSwitchPanelSize *out,
			 WSwitchPanelError *err);

/* padded byte count of a raster, false when the product is not
 * positive or leaves the range of long long */
bool wSwitchPanelRasterBytes(int width, int height, long long *bytes);

/* running account of the panel-controlled pixel allocations, the peak
 * being the largest live total reached */
typedef struct {
	long long live;
	long long peak;
} WSwitchPanelBudget;

void wSwitchPanelBudgetInit(WSwitchPanelBudget *b);

/* fails unchanged when the count is not positive, exceeds
 * WSP_ALLOC_LIMIT on its own, or would push live over WSP_PEAK_LIMIT */
bool wSwitchPanelBudgetAdd(WSwitchPanelBudget *b, long long bytes);

/* over-releases saturate the live total at zero */
void wSwitchPanelBudgetRemove(WSwitchPanelBudget *b, long long bytes);

bool wSwitchPanelBudgetCreate(WSwitchPanelBudget *b, const WSwitchPanelSize *size,
			      int count, int icon_size, bool has_background);

bool wSwitchPanelBudgetRebuild(WSwitchPanelBudget *b, int old_w, int old_h,
			       int new_w, int new_h, double ui_scale);

/* positive aspect-preserving dimensions that fit inside tile, or the
 * unchanged source size when it already fits */
bool wSwitchPanelIconDims(int src_w, int src_h, int tile, int *out_w, int *out_h);

#endif /* WMSWP_SIZE_H */
