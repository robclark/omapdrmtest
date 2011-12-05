/*
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob.clark@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "util.h"

#include <drm.h>

void disp_kms_usage(void);
struct display * disp_kms_open(int argc, char **argv);

#ifdef HAVE_X11
void disp_x11_usage(void);
struct display * disp_x11_open(int argc, char **argv);
#endif

void
disp_usage(void)
{
#ifdef HAVE_X11
	disp_x11_usage();
#endif
	disp_kms_usage();
}

struct display *
disp_open(int argc, char **argv)
{
	struct display *disp;

#ifdef HAVE_X11
	disp = disp_x11_open(argc, argv);
	if (disp)
		return disp;
#endif

	disp = disp_kms_open(argc, argv);

	if (!disp) {
		ERROR("unable to create display");
	}

	return disp;
}

int
check_args(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; i++) {
		if (argv[i]) {
			ERROR("invalid arg: %s", argv[i]);
			return -1;
		}
	}
	return 0;
}

/* stolen from modetest.c */
static void
fillRGB4(char *virtual, int n, int width, int height, int stride)
{
	int i, j;
	/* paint the buffer with colored tiles */
	for (j = 0; j < height; j++) {
		uint32_t *fb_ptr = (uint32_t*)((char*)virtual + j * stride);
		for (i = 0; i < width; i++) {
			div_t d = div(n+i+j, width);
			fb_ptr[i] =
					0x00130502 * (d.quot >> 6) +
					0x000a1120 * (d.rem >> 6);
		}
	}
}


/* swap these for big endian.. */
#define RED   2
#define GREEN 1
#define BLUE  0

static void
fill420(unsigned char *y, unsigned char *u, unsigned char *v,
		int cs /*chroma pixel stride */,
		int n, int width, int height, int stride)
{
	int i, j;

	/* paint the buffer with colored tiles, in blocks of 2x2 */
	for (j = 0; j < height; j+=2) {
		unsigned char *y1p = y + j * stride;
		unsigned char *y2p = y1p + stride;
		unsigned char *up = u + (j/2) * stride * cs / 2;
		unsigned char *vp = v + (j/2) * stride * cs / 2;

		for (i = 0; i < width; i+=2) {
			div_t d = div(n+i+j, width);
			uint32_t rgb = 0x00130502 * (d.quot >> 6) + 0x000a1120 * (d.rem >> 6);
			unsigned char *rgbp = (unsigned char *)&rgb;
			unsigned char y = (0.299 * rgbp[RED]) + (0.587 * rgbp[GREEN]) + (0.114 * rgbp[BLUE]);

			*(y2p++) = *(y1p++) = y;
			*(y2p++) = *(y1p++) = y;

			*up = (rgbp[BLUE] - y) * 0.565 + 128;
			*vp = (rgbp[RED] - y) * 0.713 + 128;
			up += cs;
			vp += cs;
		}
	}
}

static void
fill422(unsigned char *virtual, int n, int width, int height, int stride)
{
	int i, j;
	/* paint the buffer with colored tiles */
	for (j = 0; j < height; j++) {
		uint8_t *ptr = (uint8_t*)((char*)virtual + j * stride);
		for (i = 0; i < width; i++) {
			div_t d = div(n+i+j, width);
			uint32_t rgb = 0x00130502 * (d.quot >> 6) + 0x000a1120 * (d.rem >> 6);
			unsigned char *rgbp = (unsigned char *)&rgb;
			unsigned char y = (0.299 * rgbp[RED]) + (0.587 * rgbp[GREEN]) + (0.114 * rgbp[BLUE]);

			*(ptr++) = y;
			*(ptr++) = (rgbp[BLUE] - y) * 0.565 + 128;
			*(ptr++) = y;
			*(ptr++) = (rgbp[RED] - y) * 0.713 + 128;
		}
	}
}


void
fill(struct buffer *buf, int i)
{
	void *ptr;

	omap_bo_cpu_prep(buf->bo, OMAP_GEM_WRITE);
	ptr = omap_bo_map(buf->bo);

	switch(buf->fourcc) {
	case 0: {
		fillRGB4(ptr, i, buf->width, buf->height, buf->stride);
		break;
	}
	case FOURCC('Y','U','Y','V'): {
		fill422(ptr, i, buf->width, buf->height, buf->stride);
		break;
	}
	case FOURCC('N','V','1','2'): {
		unsigned char *y = ptr;
		unsigned char *u = y + (buf->width * buf->stride);
		unsigned char *v = u + 1;
		fill420(y, u, v, 2, i, buf->width, buf->height, buf->stride);
		break;
	}
	case FOURCC('I','4','2','0'): {
		unsigned char *y = ptr;
		unsigned char *u = y + (buf->width * buf->stride);
		unsigned char *v = u + (buf->width * buf->stride) / 4;
		fill420(y, u, v, 1, i, buf->width, buf->height, buf->stride);
		break;
	}
	default:
		ERROR("invalid format: 0x%08x", buf->fourcc);
		break;
	}

	omap_bo_cpu_fini(buf->bo, OMAP_GEM_WRITE);
}
