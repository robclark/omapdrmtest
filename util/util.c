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

struct buffer *
disp_get_fb(struct display *disp)
{
	struct buffer **bufs = disp_get_buffers(disp, 1);
	if (!bufs)
		return NULL;
	fill(bufs[0], 42);
	disp_post_buffer(disp, bufs[0]);
	return bufs[0];
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
fill(struct buffer *buf, int n)
{
	int i;

	for (i = 0; i < buf->nbo; i++)
		omap_bo_cpu_prep(buf->bo[i], OMAP_GEM_WRITE);

	switch(buf->fourcc) {
	case 0: {
		assert(buf->nbo == 1);
		fillRGB4(omap_bo_map(buf->bo[0]), n,
				buf->width, buf->height, buf->pitches[0]);
		break;
	}
	case FOURCC('Y','U','Y','V'): {
		assert(buf->nbo == 1);
		fill422(omap_bo_map(buf->bo[0]), n,
				buf->width, buf->height, buf->pitches[0]);
		break;
	}
	case FOURCC('N','V','1','2'): {
		unsigned char *y, *u, *v;
		assert(buf->nbo == 2);
		y = omap_bo_map(buf->bo[0]);
		u = omap_bo_map(buf->bo[1]);
		v = u + 1;
		fill420(y, u, v, 2, n, buf->width, buf->height, buf->pitches[0]);
		break;
	}
	case FOURCC('I','4','2','0'): {
		unsigned char *y, *u, *v;
		assert(buf->nbo == 3);
		y = omap_bo_map(buf->bo[0]);
		u = omap_bo_map(buf->bo[1]);
		v = omap_bo_map(buf->bo[2]);
		fill420(y, u, v, 1, n, buf->width, buf->height, buf->pitches[0]);
		break;
	}
	default:
		ERROR("invalid format: 0x%08x", buf->fourcc);
		break;
	}

	for (i = 0; i < buf->nbo; i++)
		omap_bo_cpu_fini(buf->bo[i], OMAP_GEM_WRITE);
}
