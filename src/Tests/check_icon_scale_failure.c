#include <wraster.h>

#include "iconscale.h"
#include "check_common.h"

static int releases;

RImage *RScaleImage(RImage *image, unsigned int width, unsigned int height)
{
	(void)image;
	(void)width;
	(void)height;
	return NULL;
}

void RReleaseImage(RImage *image)
{
	(void)image;
	releases++;
}

int main(void)
{
	RImage image = { 0 };

	image.width = 100;
	image.height = 50;
	CHECK(wIconValidateIconSizeStrict(&image, 64) == NULL);
	CHECK(image.width == 100 && image.height == 50);
	CHECK(releases == 0);
	CHECK(wIconValidateIconSize(&image, 64) == &image);
	CHECK(releases == 0);
	return check_report("check_icon_scale_failure");
}
