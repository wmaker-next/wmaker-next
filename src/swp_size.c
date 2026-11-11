/*
 *  swp_size.c
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

/*
 * Checked switch panel sizing arithmetic in wide signed intermediates,
 * display-free by design.
 */

#include "wconfig.h"

#include <limits.h>
#include <stddef.h>

#include "swp_size.h"

int wSwitchPanelTileSize(int icon_size)
{
	int tile;

	if (icon_size < WSP_ICON_SIZE_MIN || icon_size > WSP_ICON_SIZE_MAX)
		return 0;

	/* rounded icon_size * 1.2, the tile the panel has always derived */
	tile = (int)(((float)icon_size * 1.2F) + 0.5F);
	if (tile <= 0 || tile > WSP_IMAGE_MAX)
		return 0;

	return tile;
}

bool wSwitchPanelDevicePx(int logical, double ui_scale, int *dev)
{
	double v;

	if (logical <= 0 || !(ui_scale > 0.0))
		return false;

	v = (double)logical * ui_scale + 0.5;
	if (!(v >= 1.0) || v > (double)INT_MAX)
		return false;

	*dev = (int)v;
	return true;
}

bool wSwitchPanelIconPos(int index, int tile, int *x)
{
	long long v;

	if (index < 0 || tile <= 0)
		return false;

	v = (long long)index * tile;
	if (v < 0 || v > INT_MAX)
		return false;

	*x = (int)v;
	return true;
}

bool wSwitchPanelChildCoordOk(long long logical, double ui_scale)
{
	double v;

	if (!(ui_scale > 0.0) || ui_scale > WSP_UI_SCALE_MAX)
		return false;
	v = (double)logical * ui_scale + 0.5;
	return v > -32769.0 && v < 32768.0;
}

bool wSwitchPanelStripWidth(int count, int tile, double ui_scale, int *width)
{
	long long full, last;
	int dev;

	if (count <= 0 || tile <= 0)
		return false;

	full = (long long)count * tile;
	last = (long long)(count - 1) * tile;
	if (full <= 0 || full > WSP_XDIM_MAX)
		return false;
	if (!wSwitchPanelChildCoordOk(last, ui_scale) ||
	    !wSwitchPanelChildCoordOk(-last, ui_scale))
		return false;
	if (!wSwitchPanelDevicePx((int)full, ui_scale, &dev))
		return false;
	if (dev > WSP_XDIM_MAX)
		return false;

	*width = (int)full;
	return true;
}

bool wSwitchPanelDrillExtra(int border, int tile, int *extra)
{
	long long v;

	if (border < 0 || tile <= 0)
		return false;

	v = (long long)border + tile;
	if (v <= 0 || v > INT_MAX)
		return false;

	*extra = (int)v;
	return true;
}

bool wSwitchPanelImageSizeOk(int width, int height)
{
	return width >= 1 && height >= 1 &&
	       width <= WSP_IMAGE_MAX && height <= WSP_IMAGE_MAX;
}

bool wSwitchPanelWidgetSizeOk(int width, int height, double ui_scale)
{
	int dw, dh;

	if (!wSwitchPanelDevicePx(width, ui_scale, &dw))
		return false;
	if (!wSwitchPanelDevicePx(height, ui_scale, &dh))
		return false;

	return dw <= WSP_XDIM_MAX && dh <= WSP_XDIM_MAX;
}

bool wSwitchPanelRasterBytes(int width, int height, long long *bytes)
{
	long long row, total;

	if (width <= 0 || height <= 0 || !bytes)
		return false;

	/* scanlines padded to a 32 bit boundary */
	row = (long long)width * WSP_BYTES_PER_PIXEL;
	if (row <= 0 || row > LLONG_MAX - 3)
		return false;
	row = (row + 3) & ~3LL;

	if (row > LLONG_MAX / height)
		return false;
	total = row * height;
	if (total <= 0)
		return false;

	*bytes = total;
	return true;
}

void wSwitchPanelBudgetInit(WSwitchPanelBudget *b)
{
	if (!b)
		return;
	b->live = 0;
	b->peak = 0;
}

bool wSwitchPanelBudgetAdd(WSwitchPanelBudget *b, long long bytes)
{
	if (!b || bytes <= 0 || bytes > WSP_ALLOC_LIMIT)
		return false;
	if (b->live < 0 || b->live > WSP_PEAK_LIMIT - bytes)
		return false;

	b->live += bytes;
	if (b->live > b->peak)
		b->peak = b->live;
	return true;
}

void wSwitchPanelBudgetRemove(WSwitchPanelBudget *b, long long bytes)
{
	if (!b || bytes <= 0)
		return;

	if (b->live <= bytes)
		b->live = 0;
	else
		b->live -= bytes;
}

static bool budget_raster(WSwitchPanelBudget *b, int width, int height)
{
	long long bytes;

	return wSwitchPanelRasterBytes(width, height, &bytes) &&
	       wSwitchPanelBudgetAdd(b, bytes);
}

bool wSwitchPanelBudgetCreate(WSwitchPanelBudget *b, const WSwitchPanelSize *size,
			      int count, int icon_size, bool has_background)
{
	WSwitchPanelBudget next;
	long long logical_bg, device_bg;
	int bound, i;

	if (!b || !size || count < 0 ||
	    icon_size < WSP_ICON_SIZE_MIN || icon_size > WSP_ICON_SIZE_MAX)
		return false;
	bound = icon_size + WSP_ICON_IMAGE_SLACK;
	if (bound > size->tile)
		bound = size->tile;
	wSwitchPanelBudgetInit(&next);
	if (!budget_raster(&next, size->tile, size->tile) ||
	    !budget_raster(&next, size->tile, size->tile) ||
	    !budget_raster(&next, size->dev_tile, size->dev_tile))
		return false;
	for (i = 0; i < count; i++) {
		if (!budget_raster(&next, bound, bound) ||
		    !budget_raster(&next, size->dev_tile, size->dev_tile))
			return false;
	}
	if (has_background) {
		if (!wSwitchPanelRasterBytes(size->panel_width, size->panel_height, &logical_bg) ||
		    !wSwitchPanelRasterBytes(size->dev_panel_width, size->dev_panel_height, &device_bg) ||
		    !wSwitchPanelBudgetAdd(&next, logical_bg) ||
		    !wSwitchPanelBudgetAdd(&next, logical_bg) ||
		    !wSwitchPanelBudgetAdd(&next, device_bg) ||
		    !wSwitchPanelBudgetAdd(&next, device_bg) ||
		    !wSwitchPanelBudgetAdd(&next, device_bg))
			return false;
		wSwitchPanelBudgetRemove(&next, logical_bg);
		wSwitchPanelBudgetRemove(&next, device_bg);
		wSwitchPanelBudgetRemove(&next, device_bg);
	}
	*b = next;
	return true;
}

bool wSwitchPanelBudgetRebuild(WSwitchPanelBudget *b, int old_w, int old_h,
			       int new_w, int new_h, double ui_scale)
{
	WSwitchPanelBudget next;
	long long old_bytes, old_dev, new_bytes, new_dev;
	int ow, oh, nw, nh;
	int i;

	if (!b || !wSwitchPanelRasterBytes(old_w, old_h, &old_bytes) ||
	    !wSwitchPanelRasterBytes(new_w, new_h, &new_bytes) ||
	    !wSwitchPanelDevicePx(old_w, ui_scale, &ow) ||
	    !wSwitchPanelDevicePx(old_h, ui_scale, &oh) ||
	    !wSwitchPanelDevicePx(new_w, ui_scale, &nw) ||
	    !wSwitchPanelDevicePx(new_h, ui_scale, &nh) ||
	    !wSwitchPanelRasterBytes(ow, oh, &old_dev) ||
	    !wSwitchPanelRasterBytes(nw, nh, &new_dev))
		return false;
	next = *b;
	for (i = 0; i < 2; i++)
		if (!wSwitchPanelBudgetAdd(&next, new_bytes))
			return false;
	for (i = 0; i < 3; i++)
		if (!wSwitchPanelBudgetAdd(&next, new_dev))
			return false;
	wSwitchPanelBudgetRemove(&next, old_bytes);
	wSwitchPanelBudgetRemove(&next, old_dev);
	wSwitchPanelBudgetRemove(&next, new_bytes);
	wSwitchPanelBudgetRemove(&next, new_dev);
	wSwitchPanelBudgetRemove(&next, new_dev);
	*b = next;
	return true;
}

bool wSwitchPanelIconDims(int src_w, int src_h, int tile, int *out_w, int *out_h)
{
	long long w, h;

	if (src_w <= 0 || src_h <= 0 || tile <= 0 || !out_w || !out_h)
		return false;

	if (src_w <= tile && src_h <= tile) {
		*out_w = src_w;
		*out_h = src_h;
		return true;
	}

	/* preserve the aspect ratio, then clamp to a positive box */
	if (src_w >= src_h) {
		w = tile;
		h = (long long)src_h * tile / src_w;
	} else {
		h = tile;
		w = (long long)src_w * tile / src_h;
	}
	if (w < 1)
		w = 1;
	if (h < 1)
		h = 1;
	if (w > tile)
		w = tile;
	if (h > tile)
		h = tile;

	*out_w = (int)w;
	*out_h = (int)h;
	return true;
}

static void set_err(WSwitchPanelError *err, WSwitchPanelError code)
{
	if (err)
		*err = code;
}

int wSwitchPanelCalcDims(const WSwitchPanelSizing *in, int visible,
			 WSwitchPanelSize *out, WSwitchPanelError *err)
{
	long long full, strip, content, pw, ph;
	int tile, dev_pw, dev_ph, dev_tile, width;

	set_err(err, WSP_ERR_INPUT);
	if (!in || !out)
		return -1;

	tile = wSwitchPanelTileSize(in->icon_size);
	if (tile <= 0)
		return -1;
	if (in->count < 0 || visible < 0 || visible > in->count)
		return -1;
	if (in->border < 0 || in->label_height < 1 || in->extra_h < 0)
		return -1;
	if (in->head_w < 0 || in->head_h < 0)
		return -1;
	if (!(in->ui_scale > 0.0) || in->ui_scale > WSP_UI_SCALE_MAX)
		return -1;

	full = (long long)tile * in->count;
	strip = (long long)tile * visible;
	content = (long long)in->label_height + tile + in->extra_h;
	pw = strip + 2LL * in->border;
	ph = content + 2LL * in->border;

	if (full < 0 || full > INT_MAX)
		return -1;
	if (strip < 0 || strip > INT_MAX)
		return -1;
	if (content < 1 || content > INT_MAX)
		return -1;
	if (pw < 1 || pw > INT_MAX || ph < 1 || ph > INT_MAX)
		return -1;

	set_err(err, WSP_ERR_STRIP);
	if (!wSwitchPanelStripWidth(in->count, tile, in->ui_scale, &width))
		return -1;

	set_err(err, WSP_ERR_WIDGET);
	if (!wSwitchPanelChildCoordOk(in->border, in->ui_scale) ||
	    !wSwitchPanelChildCoordOk(2LL * in->border + 2LL * tile + 5,
				      in->ui_scale))
		return -1;
	if (!wSwitchPanelWidgetSizeOk((int)strip, tile, in->ui_scale))
		return -1;
	if (!wSwitchPanelWidgetSizeOk((int)pw, (int)ph, in->ui_scale))
		return -1;

	/* the background is allocated at the logical panel size */
	set_err(err, WSP_ERR_IMAGE);
	if (!wSwitchPanelImageSizeOk((int)pw, (int)ph))
		return -1;
	if (!wSwitchPanelDevicePx((int)pw, in->ui_scale, &dev_pw))
		return -1;
	if (!wSwitchPanelDevicePx((int)ph, in->ui_scale, &dev_ph))
		return -1;
	if (!wSwitchPanelImageSizeOk(dev_pw, dev_ph))
		return -1;

	if (!wSwitchPanelDevicePx(tile, in->ui_scale, &dev_tile))
		return -1;
	if (dev_tile > WSP_IMAGE_MAX)
		return -1;

	out->tile = tile;
	out->dev_tile = dev_tile;
	out->visible = visible;
	out->full_width = (int)full;
	out->strip_width = (int)strip;
	out->content_height = (int)content;
	out->panel_width = (int)pw;
	out->panel_height = (int)ph;
	out->dev_panel_width = dev_pw;
	out->dev_panel_height = dev_ph;

	set_err(err, WSP_ERR_NONE);
	return 0;
}

int wSwitchPanelVisibleCount(const WSwitchPanelSizing *in)
{
	long long avail, fit, full, dev_full;
	WSwitchPanelSize sz;
	int tile, dev_tile, visible, dev;

	if (!in)
		return -1;
	if (in->count < 0 || in->head_w < 0 || in->head_h < 0)
		return -1;
	if (!(in->ui_scale > 0.0) || in->ui_scale > WSP_UI_SCALE_MAX)
		return -1;

	tile = wSwitchPanelTileSize(in->icon_size);
	if (tile <= 0)
		return -1;
	if (in->count == 0)
		return 0;

	if (!in->opt_in_fit) {
		/* legacy head-width-margin fit in device pixels, the
		 * strip converted once so a scaled UI selects what
		 * really fits */
		if (!wSwitchPanelDevicePx(tile, in->ui_scale, &dev_tile))
			return -1;
		full = (long long)tile * in->count;
		dev_full = (full <= INT_MAX && wSwitchPanelDevicePx((int)full, in->ui_scale, &dev))
			? dev : (long long)INT_MAX + 1;
		if (dev_full <= in->head_w)
			return in->count;

		avail = in->head_w - WSP_FIT_SPACING;
		fit = avail / dev_tile;
		if (fit < 1)
			fit = 1;
		if (fit > in->count)
			fit = in->count;
		return (int)fit;
	}

	/* opt-in fit: the largest count whose complete panel fits,
	 * one icon when nothing fits */
	for (visible = in->count; visible >= 1; visible--) {
		if (wSwitchPanelCalcDims(in, visible, &sz, NULL) != 0)
			continue;
		if (sz.dev_panel_width <= in->head_w &&
		    sz.dev_panel_height <= in->head_h)
			return visible;
	}

	return 1;
}

int wSwitchPanelCalcSize(const WSwitchPanelSizing *in, WSwitchPanelSize *out,
			 WSwitchPanelError *err)
{
	int visible;

	set_err(err, WSP_ERR_INPUT);
	if (!in || !out)
		return -1;

	visible = wSwitchPanelVisibleCount(in);
	if (visible < 0)
		return -1;

	return wSwitchPanelCalcDims(in, visible, out, err);
}
